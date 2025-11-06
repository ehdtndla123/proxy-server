#include "../include/control.h"
#include "../include/logger.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/mman.h>
#include <pthread.h>
#include <signal.h>
#include <errno.h>

// 공유 메모리 구조체
typedef struct {
    ConnectionInfo connections[100];
    int connection_count;
    pthread_mutex_t mutex;
} SharedConnectionData;

// 공유 메모리로 관리되는 연결 정보
static SharedConnectionData *g_shared_data = NULL;
static int g_control_sock = -1;
static pthread_t g_control_thread;
static volatile bool g_control_running = false;

// 공유 메모리 초기화
static int init_shared_memory(void) {
    // 익명 공유 메모리 생성
    g_shared_data = mmap(NULL, sizeof(SharedConnectionData),
                         PROT_READ | PROT_WRITE,
                         MAP_SHARED | MAP_ANONYMOUS, -1, 0);

    if (g_shared_data == MAP_FAILED) {
        LOG_ERROR("공유 메모리 생성 실패: %s", strerror(errno));
        return -1;
    }

    // 초기화
    memset(g_shared_data, 0, sizeof(SharedConnectionData));
    g_shared_data->connection_count = 0;

    // 프로세스 간 공유 가능한 mutex 초기화
    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_setpshared(&attr, PTHREAD_PROCESS_SHARED);
    pthread_mutex_init(&g_shared_data->mutex, &attr);
    pthread_mutexattr_destroy(&attr);

    return 0;
}

// 공유 메모리 정리
static void cleanup_shared_memory(void) {
    if (g_shared_data != NULL) {
        pthread_mutex_destroy(&g_shared_data->mutex);
        munmap(g_shared_data, sizeof(SharedConnectionData));
        g_shared_data = NULL;
    }
}

// 제어 서버 스레드
static void* control_server_thread(void *arg) {
    const char *socket_path = (const char*)arg;

    while (g_control_running) {
        struct sockaddr_un client_addr;
        socklen_t client_len = sizeof(client_addr);

        int client_fd = accept(g_control_sock, (struct sockaddr*)&client_addr, &client_len);
        if (client_fd < 0) {
            if (errno == EINTR || !g_control_running) {
                break;
            }
            LOG_ERROR("제어 클라이언트 수락 실패: %s", strerror(errno));
            continue;
        }

        control_handle_request(client_fd);
        close(client_fd);
    }

    return NULL;
}

int control_server_start(const char *socket_path) {
    // 공유 메모리 초기화
    if (init_shared_memory() < 0) {
        return -1;
    }

    // 기존 소켓 파일 삭제
    unlink(socket_path);

    g_control_sock = socket(AF_UNIX, SOCK_STREAM, 0);
    if (g_control_sock < 0) {
        LOG_ERROR("제어 소켓 생성 실패: %s", strerror(errno));
        cleanup_shared_memory();
        return -1;
    }

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, socket_path, sizeof(addr.sun_path) - 1);

    if (bind(g_control_sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        LOG_ERROR("제어 소켓 바인드 실패: %s", strerror(errno));
        close(g_control_sock);
        cleanup_shared_memory();
        return -1;
    }

    if (listen(g_control_sock, 5) < 0) {
        LOG_ERROR("제어 소켓 리스닝 실패: %s", strerror(errno));
        close(g_control_sock);
        unlink(socket_path);
        cleanup_shared_memory();
        return -1;
    }

    g_control_running = true;

    if (pthread_create(&g_control_thread, NULL, control_server_thread, (void*)socket_path) != 0) {
        LOG_ERROR("제어 스레드 생성 실패: %s", strerror(errno));
        close(g_control_sock);
        unlink(socket_path);
        cleanup_shared_memory();
        return -1;
    }

    LOG_INFO("제어 서버 시작: %s", socket_path);
    return 0;
}

void control_server_stop(void) {
    g_control_running = false;

    if (g_control_sock >= 0) {
        shutdown(g_control_sock, SHUT_RDWR);
        close(g_control_sock);
        g_control_sock = -1;
    }

    pthread_join(g_control_thread, NULL);

    // 공유 메모리 정리
    cleanup_shared_memory();
}

void control_register_connection(const Connection *conn) {
    if (g_shared_data == NULL) return;

    pthread_mutex_lock(&g_shared_data->mutex);

    if (g_shared_data->connection_count >= 100) {
        LOG_WARN("최대 연결 수 초과, 등록 실패");
        pthread_mutex_unlock(&g_shared_data->mutex);
        return;
    }

    ConnectionInfo *info = &g_shared_data->connections[g_shared_data->connection_count++];
    info->pid = conn->pid;
    strncpy(info->client_addr, conn->client_addr, MAX_ADDR_LEN - 1);
    info->client_addr[MAX_ADDR_LEN - 1] = '\0';
    info->client_port = conn->client_port;
    strncpy(info->target_addr, conn->target_addr, MAX_ADDR_LEN - 1);
    info->target_addr[MAX_ADDR_LEN - 1] = '\0';
    info->target_port = conn->target_port;
    info->client_to_server_bytes = 0;
    info->server_to_client_bytes = 0;
    info->start_time = conn->stats.start_time;
    info->last_activity = conn->stats.last_activity;

    pthread_mutex_unlock(&g_shared_data->mutex);

    LOG_DEBUG("연결 등록: PID=%d, %s:%d -> %s:%d",
              conn->pid, conn->client_addr, conn->client_port,
              conn->target_addr, conn->target_port);
}

void control_unregister_connection(pid_t pid) {
    if (g_shared_data == NULL) return;

    pthread_mutex_lock(&g_shared_data->mutex);

    for (int i = 0; i < g_shared_data->connection_count; i++) {
        if (g_shared_data->connections[i].pid == pid) {
            // 배열에서 제거 (뒤의 요소들을 앞으로 이동)
            memmove(&g_shared_data->connections[i], &g_shared_data->connections[i + 1],
                    (g_shared_data->connection_count - i - 1) * sizeof(ConnectionInfo));
            g_shared_data->connection_count--;
            LOG_DEBUG("연결 해제: PID=%d", pid);
            break;
        }
    }

    pthread_mutex_unlock(&g_shared_data->mutex);
}

void control_update_stats(pid_t pid, const ConnectionStats *stats) {
    if (g_shared_data == NULL) return;

    pthread_mutex_lock(&g_shared_data->mutex);

    for (int i = 0; i < g_shared_data->connection_count; i++) {
        if (g_shared_data->connections[i].pid == pid) {
            g_shared_data->connections[i].client_to_server_bytes = stats->client_to_server_bytes;
            g_shared_data->connections[i].server_to_client_bytes = stats->server_to_client_bytes;
            g_shared_data->connections[i].last_activity = stats->last_activity;
            break;
        }
    }

    pthread_mutex_unlock(&g_shared_data->mutex);
}

void control_handle_request(int client_fd) {
    ControlRequest req;
    ControlResponse resp;

    memset(&resp, 0, sizeof(resp));

    // 요청 수신
    ssize_t n = recv(client_fd, &req, sizeof(req), 0);
    if (n != sizeof(req)) {
        LOG_ERROR("제어 요청 수신 실패");
        resp.success = false;
        snprintf(resp.message, sizeof(resp.message), "요청 수신 실패");
        send(client_fd, &resp, sizeof(resp), 0);
        return;
    }

    if (g_shared_data == NULL) {
        resp.success = false;
        snprintf(resp.message, sizeof(resp.message), "공유 메모리 미초기화");
        send(client_fd, &resp, sizeof(resp), 0);
        return;
    }

    pthread_mutex_lock(&g_shared_data->mutex);

    switch (req.cmd) {
        case CMD_LIST_CONNECTIONS:
            resp.success = true;
            resp.connection_count = g_shared_data->connection_count;
            memcpy(resp.connections, g_shared_data->connections,
                   g_shared_data->connection_count * sizeof(ConnectionInfo));
            snprintf(resp.message, sizeof(resp.message),
                     "총 %d개 연결", g_shared_data->connection_count);
            break;

        case CMD_KILL_CONNECTION:
            resp.success = false;
            for (int i = 0; i < g_shared_data->connection_count; i++) {
                if (g_shared_data->connections[i].pid == req.target_pid) {
                    if (kill(req.target_pid, SIGTERM) == 0) {
                        resp.success = true;
                        snprintf(resp.message, sizeof(resp.message),
                                "PID %d 종료 시그널 전송 성공", req.target_pid);
                    } else {
                        snprintf(resp.message, sizeof(resp.message),
                                "PID %d 종료 실패: %s", req.target_pid, strerror(errno));
                    }
                    break;
                }
            }
            if (!resp.success && strlen(resp.message) == 0) {
                snprintf(resp.message, sizeof(resp.message),
                        "PID %d를 찾을 수 없음", req.target_pid);
            }
            break;

        case CMD_SEND_SIGNAL:
            resp.success = false;
            for (int i = 0; i < g_shared_data->connection_count; i++) {
                if (g_shared_data->connections[i].pid == req.target_pid) {
                    if (kill(req.target_pid, req.signal_num) == 0) {
                        resp.success = true;
                        snprintf(resp.message, sizeof(resp.message),
                                "PID %d에 시그널 %d 전송 성공",
                                req.target_pid, req.signal_num);
                    } else {
                        snprintf(resp.message, sizeof(resp.message),
                                "시그널 전송 실패: %s", strerror(errno));
                    }
                    break;
                }
            }
            if (!resp.success && strlen(resp.message) == 0) {
                snprintf(resp.message, sizeof(resp.message),
                        "PID %d를 찾을 수 없음", req.target_pid);
            }
            break;

        case CMD_GET_STATS:
            resp.success = true;
            resp.connection_count = g_shared_data->connection_count;
            memcpy(resp.connections, g_shared_data->connections,
                   g_shared_data->connection_count * sizeof(ConnectionInfo));
            snprintf(resp.message, sizeof(resp.message),
                     "통계 조회 성공");
            break;

        case CMD_SHUTDOWN:
            resp.success = true;
            snprintf(resp.message, sizeof(resp.message),
                     "프록시 서버 종료 명령 수신");
            // 부모 프로세스에 종료 시그널 전송
            kill(getppid(), SIGTERM);
            break;

        default:
            resp.success = false;
            snprintf(resp.message, sizeof(resp.message),
                     "알 수 없는 명령: %d", req.cmd);
            break;
    }

    pthread_mutex_unlock(&g_shared_data->mutex);

    // 응답 전송
    send(client_fd, &resp, sizeof(resp), 0);

    LOG_DEBUG("제어 요청 처리: cmd=%d, success=%d, msg=%s",
              req.cmd, resp.success, resp.message);
}
