#include "../include/control.h"
#include "../include/logger.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <pthread.h>
#include <signal.h>
#include <errno.h>

// 연결 정보를 저장하는 전역 배열 (공유 메모리로 관리)
#define MAX_CONNECTIONS 100
static ConnectionInfo g_connections[MAX_CONNECTIONS];
static int g_connection_count = 0;
static pthread_mutex_t g_conn_mutex = PTHREAD_MUTEX_INITIALIZER;
static int g_control_sock = -1;
static pthread_t g_control_thread;
static volatile bool g_control_running = false;

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
    // 기존 소켓 파일 삭제
    unlink(socket_path);

    g_control_sock = socket(AF_UNIX, SOCK_STREAM, 0);
    if (g_control_sock < 0) {
        LOG_ERROR("제어 소켓 생성 실패: %s", strerror(errno));
        return -1;
    }

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, socket_path, sizeof(addr.sun_path) - 1);

    if (bind(g_control_sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        LOG_ERROR("제어 소켓 바인드 실패: %s", strerror(errno));
        close(g_control_sock);
        return -1;
    }

    if (listen(g_control_sock, 5) < 0) {
        LOG_ERROR("제어 소켓 리스닝 실패: %s", strerror(errno));
        close(g_control_sock);
        unlink(socket_path);
        return -1;
    }

    g_control_running = true;

    if (pthread_create(&g_control_thread, NULL, control_server_thread, (void*)socket_path) != 0) {
        LOG_ERROR("제어 스레드 생성 실패: %s", strerror(errno));
        close(g_control_sock);
        unlink(socket_path);
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
}

void control_register_connection(const Connection *conn) {
    pthread_mutex_lock(&g_conn_mutex);

    if (g_connection_count >= MAX_CONNECTIONS) {
        LOG_WARN("최대 연결 수 초과, 등록 실패");
        pthread_mutex_unlock(&g_conn_mutex);
        return;
    }

    ConnectionInfo *info = &g_connections[g_connection_count++];
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

    pthread_mutex_unlock(&g_conn_mutex);

    LOG_DEBUG("연결 등록: PID=%d, %s:%d -> %s:%d",
              conn->pid, conn->client_addr, conn->client_port,
              conn->target_addr, conn->target_port);
}

void control_unregister_connection(pid_t pid) {
    pthread_mutex_lock(&g_conn_mutex);

    for (int i = 0; i < g_connection_count; i++) {
        if (g_connections[i].pid == pid) {
            // 배열에서 제거 (뒤의 요소들을 앞으로 이동)
            memmove(&g_connections[i], &g_connections[i + 1],
                    (g_connection_count - i - 1) * sizeof(ConnectionInfo));
            g_connection_count--;
            LOG_DEBUG("연결 해제: PID=%d", pid);
            break;
        }
    }

    pthread_mutex_unlock(&g_conn_mutex);
}

void control_update_stats(pid_t pid, const ConnectionStats *stats) {
    pthread_mutex_lock(&g_conn_mutex);

    for (int i = 0; i < g_connection_count; i++) {
        if (g_connections[i].pid == pid) {
            g_connections[i].client_to_server_bytes = stats->client_to_server_bytes;
            g_connections[i].server_to_client_bytes = stats->server_to_client_bytes;
            g_connections[i].last_activity = stats->last_activity;
            break;
        }
    }

    pthread_mutex_unlock(&g_conn_mutex);
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

    pthread_mutex_lock(&g_conn_mutex);

    switch (req.cmd) {
        case CMD_LIST_CONNECTIONS:
            resp.success = true;
            resp.connection_count = g_connection_count;
            memcpy(resp.connections, g_connections,
                   g_connection_count * sizeof(ConnectionInfo));
            snprintf(resp.message, sizeof(resp.message),
                     "총 %d개 연결", g_connection_count);
            break;

        case CMD_KILL_CONNECTION:
            resp.success = false;
            for (int i = 0; i < g_connection_count; i++) {
                if (g_connections[i].pid == req.target_pid) {
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
            for (int i = 0; i < g_connection_count; i++) {
                if (g_connections[i].pid == req.target_pid) {
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
            resp.connection_count = g_connection_count;
            memcpy(resp.connections, g_connections,
                   g_connection_count * sizeof(ConnectionInfo));
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

    pthread_mutex_unlock(&g_conn_mutex);

    // 응답 전송
    send(client_fd, &resp, sizeof(resp), 0);

    LOG_DEBUG("제어 요청 처리: cmd=%d, success=%d, msg=%s",
              req.cmd, resp.success, resp.message);
}
