#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <signal.h>
#include <time.h>
#include <errno.h>
#include "../include/control.h"

#define DEFAULT_SOCKET_PATH "/tmp/tcp_proxy_control.sock"

// 시그널 이름을 번호로 변환
static int parse_signal(const char *sig_str) {
    if (strcasecmp(sig_str, "TERM") == 0 || strcasecmp(sig_str, "SIGTERM") == 0) return SIGTERM;
    if (strcasecmp(sig_str, "KILL") == 0 || strcasecmp(sig_str, "SIGKILL") == 0) return SIGKILL;
    if (strcasecmp(sig_str, "STOP") == 0 || strcasecmp(sig_str, "SIGSTOP") == 0) return SIGSTOP;
    if (strcasecmp(sig_str, "CONT") == 0 || strcasecmp(sig_str, "SIGCONT") == 0) return SIGCONT;
    if (strcasecmp(sig_str, "HUP") == 0 || strcasecmp(sig_str, "SIGHUP") == 0) return SIGHUP;
    if (strcasecmp(sig_str, "USR1") == 0 || strcasecmp(sig_str, "SIGUSR1") == 0) return SIGUSR1;
    if (strcasecmp(sig_str, "USR2") == 0 || strcasecmp(sig_str, "SIGUSR2") == 0) return SIGUSR2;

    // 숫자인 경우
    char *endptr;
    long sig = strtol(sig_str, &endptr, 10);
    if (*endptr == '\0' && sig > 0 && sig < 32) {
        return (int)sig;
    }

    return -1;
}

// 바이트를 읽기 쉬운 형식으로 변환
static void format_bytes(uint64_t bytes, char *buf, size_t size) {
    if (bytes < 1024) {
        snprintf(buf, size, "%lu B", bytes);
    } else if (bytes < 1024 * 1024) {
        snprintf(buf, size, "%.2f KB", bytes / 1024.0);
    } else if (bytes < 1024 * 1024 * 1024) {
        snprintf(buf, size, "%.2f MB", bytes / (1024.0 * 1024.0));
    } else {
        snprintf(buf, size, "%.2f GB", bytes / (1024.0 * 1024.0 * 1024.0));
    }
}

// 초를 읽기 쉬운 형식으로 변환
static void format_duration(time_t seconds, char *buf, size_t size) {
    if (seconds < 60) {
        snprintf(buf, size, "%ld초", seconds);
    } else if (seconds < 3600) {
        snprintf(buf, size, "%ld분 %ld초", seconds / 60, seconds % 60);
    } else {
        snprintf(buf, size, "%ld시간 %ld분", seconds / 3600, (seconds % 3600) / 60);
    }
}

// 제어 요청 전송 및 응답 수신
static int send_control_request(const char *socket_path, ControlRequest *req, ControlResponse *resp) {
    int sock = socket(AF_UNIX, SOCK_STREAM, 0);
    if (sock < 0) {
        fprintf(stderr, "소켓 생성 실패: %s\n", strerror(errno));
        return -1;
    }

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, socket_path, sizeof(addr.sun_path) - 1);

    if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        fprintf(stderr, "제어 소켓 연결 실패: %s\n", socket_path);
        fprintf(stderr, "프록시 서버가 실행 중인지 확인하세요.\n");
        close(sock);
        return -1;
    }

    // 요청 전송
    if (send(sock, req, sizeof(ControlRequest), 0) != sizeof(ControlRequest)) {
        fprintf(stderr, "요청 전송 실패: %s\n", strerror(errno));
        close(sock);
        return -1;
    }

    // 응답 수신
    if (recv(sock, resp, sizeof(ControlResponse), 0) != sizeof(ControlResponse)) {
        fprintf(stderr, "응답 수신 실패: %s\n", strerror(errno));
        close(sock);
        return -1;
    }

    close(sock);
    return 0;
}

// list 명령
static int cmd_list(const char *socket_path) {
    ControlRequest req = {0};
    ControlResponse resp = {0};

    req.cmd = CMD_LIST_CONNECTIONS;

    if (send_control_request(socket_path, &req, &resp) < 0) {
        return 1;
    }

    if (!resp.success) {
        fprintf(stderr, "실패: %s\n", resp.message);
        return 1;
    }

    if (resp.connection_count == 0) {
        printf("활성 연결이 없습니다.\n");
        return 0;
    }

    printf("\n총 %d개의 활성 연결:\n\n", resp.connection_count);
    printf("%-8s %-22s %-22s %-12s %-12s %-12s %s\n",
           "PID", "클라이언트", "대상 서버", "업로드", "다운로드", "연결 시간", "마지막 활동");
    printf("========================================================================================================================\n");

    time_t now = time(NULL);

    for (int i = 0; i < resp.connection_count; i++) {
        ConnectionInfo *conn = &resp.connections[i];

        char client_str[64];
        snprintf(client_str, sizeof(client_str), "%s:%d", conn->client_addr, conn->client_port);

        char target_str[64];
        snprintf(target_str, sizeof(target_str), "%s:%d", conn->target_addr, conn->target_port);

        char upload_str[16], download_str[16];
        format_bytes(conn->client_to_server_bytes, upload_str, sizeof(upload_str));
        format_bytes(conn->server_to_client_bytes, download_str, sizeof(download_str));

        char duration_str[32], activity_str[32];
        format_duration(now - conn->start_time, duration_str, sizeof(duration_str));

        time_t last_activity = now - conn->last_activity;
        if (last_activity < 60) {
            snprintf(activity_str, sizeof(activity_str), "%ld초 전", last_activity);
        } else {
            format_duration(last_activity, activity_str, sizeof(activity_str));
        }

        printf("%-8d %-22s %-22s %-12s %-12s %-12s %s\n",
               conn->pid, client_str, target_str, upload_str, download_str,
               duration_str, activity_str);
    }

    return 0;
}

// kill 명령
static int cmd_kill(const char *socket_path, pid_t pid) {
    ControlRequest req = {0};
    ControlResponse resp = {0};

    req.cmd = CMD_KILL_CONNECTION;
    req.target_pid = pid;

    if (send_control_request(socket_path, &req, &resp) < 0) {
        return 1;
    }

    if (resp.success) {
        printf("성공: %s\n", resp.message);
        return 0;
    } else {
        fprintf(stderr, "실패: %s\n", resp.message);
        return 1;
    }
}

// signal 명령
static int cmd_signal(const char *socket_path, pid_t pid, const char *sig_str) {
    int sig_num = parse_signal(sig_str);
    if (sig_num < 0) {
        fprintf(stderr, "오류: 알 수 없는 시그널: %s\n", sig_str);
        fprintf(stderr, "사용 가능한 시그널: TERM, KILL, STOP, CONT, HUP, USR1, USR2\n");
        return 1;
    }

    ControlRequest req = {0};
    ControlResponse resp = {0};

    req.cmd = CMD_SEND_SIGNAL;
    req.target_pid = pid;
    req.signal_num = sig_num;

    if (send_control_request(socket_path, &req, &resp) < 0) {
        return 1;
    }

    if (resp.success) {
        printf("성공: %s\n", resp.message);
        return 0;
    } else {
        fprintf(stderr, "실패: %s\n", resp.message);
        return 1;
    }
}

// stats 명령
static int cmd_stats(const char *socket_path) {
    ControlRequest req = {0};
    ControlResponse resp = {0};

    req.cmd = CMD_GET_STATS;

    if (send_control_request(socket_path, &req, &resp) < 0) {
        return 1;
    }

    if (!resp.success) {
        fprintf(stderr, "실패: %s\n", resp.message);
        return 1;
    }

    if (resp.connection_count == 0) {
        printf("활성 연결이 없습니다.\n");
        return 0;
    }

    uint64_t total_c2s = 0;
    uint64_t total_s2c = 0;

    for (int i = 0; i < resp.connection_count; i++) {
        total_c2s += resp.connections[i].client_to_server_bytes;
        total_s2c += resp.connections[i].server_to_client_bytes;
    }

    char upload_str[32], download_str[32], total_str[32];
    format_bytes(total_c2s, upload_str, sizeof(upload_str));
    format_bytes(total_s2c, download_str, sizeof(download_str));
    format_bytes(total_c2s + total_s2c, total_str, sizeof(total_str));

    printf("\n=== 프록시 서버 통계 ===\n\n");
    printf("활성 연결 수: %d\n", resp.connection_count);
    printf("총 업로드 (클라이언트→서버): %s\n", upload_str);
    printf("총 다운로드 (서버→클라이언트): %s\n", download_str);
    printf("총 데이터 전송량: %s\n", total_str);

    return 0;
}

// shutdown 명령
static int cmd_shutdown(const char *socket_path) {
    printf("프록시 서버를 종료하시겠습니까? (yes/no): ");
    char confirm[10];
    if (fgets(confirm, sizeof(confirm), stdin) == NULL) {
        return 1;
    }

    // 개행 문자 제거
    confirm[strcspn(confirm, "\n")] = 0;

    if (strcasecmp(confirm, "yes") != 0 && strcasecmp(confirm, "y") != 0) {
        printf("취소되었습니다.\n");
        return 0;
    }

    ControlRequest req = {0};
    ControlResponse resp = {0};

    req.cmd = CMD_SHUTDOWN;

    if (send_control_request(socket_path, &req, &resp) < 0) {
        return 1;
    }

    if (resp.success) {
        printf("성공: %s\n", resp.message);
        return 0;
    } else {
        fprintf(stderr, "실패: %s\n", resp.message);
        return 1;
    }
}

// 사용법 출력
static void print_usage(const char *program_name) {
    printf("사용법: %s [옵션] <명령> [인자...]\n\n", program_name);
    printf("옵션:\n");
    printf("  -s <socket>    제어 소켓 경로 (기본값: %s)\n\n", DEFAULT_SOCKET_PATH);
    printf("명령어:\n");
    printf("  list, ls                      활성 연결 목록 조회\n");
    printf("  kill <PID>                    특정 연결 종료\n");
    printf("  signal <PID> <SIGNAL>         특정 연결에 시그널 전송\n");
    printf("  stats                         통계 정보 조회\n");
    printf("  shutdown                      프록시 서버 종료\n\n");
    printf("시그널:\n");
    printf("  TERM, KILL, STOP, CONT, HUP, USR1, USR2\n\n");
    printf("예시:\n");
    printf("  %s list\n", program_name);
    printf("  %s kill 12345\n", program_name);
    printf("  %s signal 12345 STOP\n", program_name);
    printf("  %s stats\n", program_name);
}

int main(int argc, char *argv[]) {
    const char *socket_path = DEFAULT_SOCKET_PATH;
    int opt;

    // 옵션 파싱
    while ((opt = getopt(argc, argv, "s:h")) != -1) {
        switch (opt) {
            case 's':
                socket_path = optarg;
                break;
            case 'h':
                print_usage(argv[0]);
                return 0;
            default:
                print_usage(argv[0]);
                return 1;
        }
    }

    // 명령어 파싱
    if (optind >= argc) {
        fprintf(stderr, "오류: 명령어가 필요합니다.\n\n");
        print_usage(argv[0]);
        return 1;
    }

    const char *command = argv[optind];

    if (strcmp(command, "list") == 0 || strcmp(command, "ls") == 0) {
        return cmd_list(socket_path);
    } else if (strcmp(command, "kill") == 0) {
        if (optind + 1 >= argc) {
            fprintf(stderr, "오류: PID가 필요합니다.\n");
            fprintf(stderr, "사용법: %s kill <PID>\n", argv[0]);
            return 1;
        }
        char *endptr;
        long pid = strtol(argv[optind + 1], &endptr, 10);
        if (*endptr != '\0' || pid <= 0) {
            fprintf(stderr, "오류: 잘못된 PID: %s\n", argv[optind + 1]);
            return 1;
        }
        return cmd_kill(socket_path, (pid_t)pid);
    } else if (strcmp(command, "signal") == 0 || strcmp(command, "sig") == 0) {
        if (optind + 2 >= argc) {
            fprintf(stderr, "오류: PID와 시그널이 필요합니다.\n");
            fprintf(stderr, "사용법: %s signal <PID> <SIGNAL>\n", argv[0]);
            return 1;
        }
        char *endptr;
        long pid = strtol(argv[optind + 1], &endptr, 10);
        if (*endptr != '\0' || pid <= 0) {
            fprintf(stderr, "오류: 잘못된 PID: %s\n", argv[optind + 1]);
            return 1;
        }
        return cmd_signal(socket_path, (pid_t)pid, argv[optind + 2]);
    } else if (strcmp(command, "stats") == 0) {
        return cmd_stats(socket_path);
    } else if (strcmp(command, "shutdown") == 0) {
        return cmd_shutdown(socket_path);
    } else {
        fprintf(stderr, "오류: 알 수 없는 명령어: %s\n\n", command);
        print_usage(argv[0]);
        return 1;
    }

    return 0;
}
