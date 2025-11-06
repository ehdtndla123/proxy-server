#include "../include/proxy.h"
#include "../include/logger.h"
#include "../include/filter.h"
#include "../include/control.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netdb.h>
#include <errno.h>

// 모든 데이터를 전송할 때까지 반복
static ssize_t send_all(int sockfd, const char *buf, size_t len) {
    size_t total_sent = 0;

    while (total_sent < len) {
        ssize_t sent = send(sockfd, buf + total_sent, len - total_sent, 0);
        if (sent <= 0) {
            if (sent == 0 || (errno != EINTR && errno != EAGAIN)) {
                return -1;
            }
            // EINTR or EAGAIN - 재시도
            continue;
        }
        total_sent += sent;
    }

    return total_sent;
}

static void stats_init(ConnectionStats *stats) {
    memset(stats, 0, sizeof(ConnectionStats));
    stats->start_time = time(NULL);
    stats->last_activity = stats->start_time;
}

static void stats_print(const ConnectionStats *stats) {
    time_t duration = time(NULL) - stats->start_time;

    LOG_INFO("=== 연결 통계 ===");
    LOG_INFO("  클라이언트 -> 서버:");
    LOG_INFO("    전송: %lu bytes (%d packets, %d dropped)",
             stats->client_to_server_bytes,
             stats->client_to_server_packets,
             stats->client_to_server_dropped);
    LOG_INFO("  서버 -> 클라이언트:");
    LOG_INFO("    전송: %lu bytes (%d packets, %d dropped)",
             stats->server_to_client_bytes,
             stats->server_to_client_packets,
             stats->server_to_client_dropped);
    LOG_INFO("  연결 시간: %ld 초", duration);

    if (duration > 0) {
        uint64_t total_bytes = stats->client_to_server_bytes + stats->server_to_client_bytes;
        LOG_INFO("  평균 전송률: %.2f KB/s",
                 (float)total_bytes / duration / 1024);
    }
}

int proxy_connect_target(const char *host, int port) {
    struct addrinfo hints, *result, *rp;
    int sock = -1;
    char port_str[16];

    snprintf(port_str, sizeof(port_str), "%d", port);

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;     // IPv4 또는 IPv6
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = 0;
    hints.ai_protocol = 0;

    int ret = getaddrinfo(host, port_str, &hints, &result);
    if (ret != 0) {
        LOG_ERROR("호스트를 찾을 수 없습니다: %s (%s)", host, gai_strerror(ret));
        return -1;
    }

    // 결과 목록을 순회하며 연결 시도
    for (rp = result; rp != NULL; rp = rp->ai_next) {
        sock = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (sock < 0) {
            continue;
        }

        if (connect(sock, rp->ai_addr, rp->ai_addrlen) == 0) {
            break;  // 성공
        }

        close(sock);
        sock = -1;
    }

    freeaddrinfo(result);

    if (sock < 0) {
        LOG_ERROR("서버 연결 실패: %s:%d - %s", host, port, strerror(errno));
        return -1;
    }

    LOG_INFO("대상 서버 연결 성공: %s:%d", host, port);
    return sock;
}

void proxy_handle_connection(Connection *conn) {
    fd_set read_fds;
    char buffer[BUFFER_SIZE];
    int max_fd = (conn->client_fd > conn->server_fd) ?
                  conn->client_fd : conn->server_fd;

    LOG_INFO("프록시 시작: 클라이언트[%s:%d] <-> 서버[%s:%d]",
             conn->client_addr, conn->client_port,
             conn->target_addr, conn->target_port);

    stats_init(&conn->stats);

    // 연결 정보 등록
    control_register_connection(conn);

    while (1) {
        FD_ZERO(&read_fds);
        FD_SET(conn->client_fd, &read_fds);
        FD_SET(conn->server_fd, &read_fds);

        struct timeval timeout = {SELECT_TIMEOUT_SEC, 0};
        int activity = select(max_fd + 1, &read_fds, NULL, NULL, &timeout);

        if (activity < 0) {
            if (errno == EINTR) {
                continue;  // 시그널 인터럽트, 재시도
            }
            LOG_ERROR("select 에러: %s", strerror(errno));
            break;
        }

        if (activity == 0) {
            LOG_WARN("타임아웃 (%d초 동안 활동 없음)", SELECT_TIMEOUT_SEC);
            break;
        }

        // 클라이언트 → 서버
        if (FD_ISSET(conn->client_fd, &read_fds)) {
            ssize_t bytes = recv(conn->client_fd, buffer, BUFFER_SIZE, 0);

            if (bytes <= 0) {
                if (bytes == 0) {
                    LOG_INFO("클라이언트 연결 종료");
                } else {
                    LOG_ERROR("클라이언트 수신 실패: %s", strerror(errno));
                }
                break;
            }

            conn->stats.last_activity = time(NULL);
            LOG_DEBUG("클라이언트 → 서버: %zd bytes", bytes);

            // 필터 적용
            if (!filter_apply(&conn->filter_chain, buffer, bytes, &conn->stats)) {
                LOG_WARN("패킷 필터링됨 (드롭)");
                conn->stats.client_to_server_dropped++;
                continue;
            }

            // 모든 데이터 전송
            ssize_t sent = send_all(conn->server_fd, buffer, bytes);
            if (sent < 0) {
                LOG_ERROR("서버 전송 실패: %s", strerror(errno));
                break;
            }

            conn->stats.client_to_server_bytes += sent;
            conn->stats.client_to_server_packets++;

            // 통계 업데이트
            control_update_stats(conn->pid, &conn->stats);
        }

        // 서버 → 클라이언트
        if (FD_ISSET(conn->server_fd, &read_fds)) {
            ssize_t bytes = recv(conn->server_fd, buffer, BUFFER_SIZE, 0);

            if (bytes <= 0) {
                if (bytes == 0) {
                    LOG_INFO("서버 연결 종료");
                } else {
                    LOG_ERROR("서버 수신 실패: %s", strerror(errno));
                }
                break;
            }

            conn->stats.last_activity = time(NULL);
            LOG_DEBUG("서버 → 클라이언트: %zd bytes", bytes);

            // 필터 적용
            if (!filter_apply(&conn->filter_chain, buffer, bytes, &conn->stats)) {
                LOG_WARN("패킷 필터링됨 (드롭)");
                conn->stats.server_to_client_dropped++;
                continue;
            }

            // 모든 데이터 전송
            ssize_t sent = send_all(conn->client_fd, buffer, bytes);
            if (sent < 0) {
                LOG_ERROR("클라이언트 전송 실패: %s", strerror(errno));
                break;
            }

            conn->stats.server_to_client_bytes += sent;
            conn->stats.server_to_client_packets++;

            // 통계 업데이트
            control_update_stats(conn->pid, &conn->stats);
        }
    }

    stats_print(&conn->stats);

    // 연결 정보 해제
    control_unregister_connection(conn->pid);
}

int proxy_start(const ProxyConfig *config, FilterChain *filter_chain) {
    int proxy_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (proxy_sock < 0) {
        LOG_ERROR("소켓 생성 실패: %s", strerror(errno));
        return -1;
    }

    // 주소 재사용
    int opt = 1;
    if (setsockopt(proxy_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        LOG_WARN("SO_REUSEADDR 설정 실패: %s", strerror(errno));
    }

    struct sockaddr_in proxy_addr;
    memset(&proxy_addr, 0, sizeof(proxy_addr));
    proxy_addr.sin_family = AF_INET;
    proxy_addr.sin_addr.s_addr = INADDR_ANY;
    proxy_addr.sin_port = htons(config->listen_port);

    if (bind(proxy_sock, (struct sockaddr *)&proxy_addr, sizeof(proxy_addr)) < 0) {
        LOG_ERROR("바인드 실패: %s", strerror(errno));
        close(proxy_sock);
        return -1;
    }

    if (listen(proxy_sock, MAX_LISTEN_BACKLOG) < 0) {
        LOG_ERROR("리스닝 실패: %s", strerror(errno));
        close(proxy_sock);
        return -1;
    }

    // 제어 서버 시작
    if (control_server_start(config->control_socket) < 0) {
        LOG_WARN("제어 서버 시작 실패 (관리 기능 비활성화)");
    }

    LOG_INFO("======================================");
    LOG_INFO("프록시 서버 시작");
    LOG_INFO("리스닝: 0.0.0.0:%d", config->listen_port);
    LOG_INFO("대상: %s:%d", config->target_host, config->target_port);
    LOG_INFO("제어 소켓: %s", config->control_socket);
    LOG_INFO("======================================");

    if (filter_chain && filter_chain->count > 0) {
        filter_chain_print(filter_chain);
    }
    
    while (1) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);

        int client_sock = accept(proxy_sock, (struct sockaddr *)&client_addr, &client_len);
        if (client_sock < 0) {
            if (errno == EINTR) {
                // 시그널에 의한 중단 (Ctrl+C)
                LOG_INFO("시그널 수신, 종료합니다");
                break;
            }
            LOG_ERROR("연결 수락 실패: %s", strerror(errno));
            continue;
        }

        char client_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, sizeof(client_ip));
        int client_port = ntohs(client_addr.sin_port);

        LOG_INFO("새 클라이언트 연결: %s:%d", client_ip, client_port);

        // 대상 서버 연결
        int server_sock = proxy_connect_target(config->target_host, config->target_port);
        if (server_sock < 0) {
            LOG_ERROR("대상 서버 연결 실패");
            close(client_sock);
            continue;
        }

        // 포크로 멀티 클라이언트 처리
        pid_t pid = fork();
        if (pid == 0) {
            // 자식 프로세스
            close(proxy_sock);

            Connection conn;
            memset(&conn, 0, sizeof(conn));
            conn.pid = getpid();
            conn.client_fd = client_sock;
            conn.server_fd = server_sock;

            // 안전한 문자열 복사
            strncpy(conn.client_addr, client_ip, MAX_ADDR_LEN - 1);
            conn.client_addr[MAX_ADDR_LEN - 1] = '\0';
            conn.client_port = client_port;

            strncpy(conn.target_addr, config->target_host, MAX_ADDR_LEN - 1);
            conn.target_addr[MAX_ADDR_LEN - 1] = '\0';
            conn.target_port = config->target_port;

            // 필터 체인 복사
            if (filter_chain) {
                memcpy(&conn.filter_chain, filter_chain, sizeof(FilterChain));
            } else {
                filter_chain_init(&conn.filter_chain);
            }

            proxy_handle_connection(&conn);

            close(client_sock);
            close(server_sock);
            LOG_INFO("연결 종료: %s:%d", client_ip, client_port);
            exit(0);
        } else if (pid > 0) {
            // 부모 프로세스
            close(client_sock);
            close(server_sock);
        } else {
            LOG_ERROR("fork 실패: %s", strerror(errno));
            close(client_sock);
            close(server_sock);
        }
    }

    // 정리
    control_server_stop();
    close(proxy_sock);
    LOG_INFO("프록시 서버 종료 완료");
    return 0;
}
