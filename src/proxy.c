#include "../include/proxy.h"
#include "../include/logger.h"
#include "../include/filter.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netdb.h>
#include <errno.h>

#define BUFFER_SIZE 8192

static void stats_init(ConnectionStats *stats) {
    memset(stats, 0, sizeof(ConnectionStats));
    stats->start_time = time(NULL);
    stats->last_activity = stats->start_time;
}

static void stats_print(const ConnectionStats *stats) {
    time_t duration = time(NULL) - stats->start_time;
    
    LOG_INFO("=== 연결 통계 ===");
    LOG_INFO("  전송: %lu bytes (%d packets)", 
             stats->bytes_sent, stats->packets_sent);
    LOG_INFO("  수신: %lu bytes (%d packets)", 
             stats->bytes_received, stats->packets_received);
    LOG_INFO("  드롭: %d packets", stats->packets_dropped);
    LOG_INFO("  연결 시간: %ld 초", duration);
    
    if (duration > 0) {
        LOG_INFO("  평균 전송률: %.2f KB/s", 
                 (float)stats->bytes_sent / duration / 1024);
        LOG_INFO("  평균 수신률: %.2f KB/s", 
                 (float)stats->bytes_received / duration / 1024);
    }
}

int proxy_connect_target(const char *host, int port) {
    struct hostent *server = gethostbyname(host);
    if (server == NULL) {
        LOG_ERROR("호스트를 찾을 수 없습니다: %s", host);
        return -1;
    }
    
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        LOG_ERROR("소켓 생성 실패: %s", strerror(errno));
        return -1;
    }
    
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    memcpy(&server_addr.sin_addr.s_addr, server->h_addr, server->h_length);
    
    if (connect(sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        LOG_ERROR("서버 연결 실패: %s:%d - %s", host, port, strerror(errno));
        close(sock);
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
    
    LOG_INFO("프록시 시작: 클라이언트[%s:%d] <-> 서버[fd:%d]", 
             conn->client_addr, conn->client_port, conn->server_fd);
    
    stats_init(&conn->stats);
    
    while (1) {
        FD_ZERO(&read_fds);
        FD_SET(conn->client_fd, &read_fds);
        FD_SET(conn->server_fd, &read_fds);
        
        struct timeval timeout = {60, 0};  // 60초 타임아웃
        int activity = select(max_fd + 1, &read_fds, NULL, NULL, &timeout);
        
        if (activity < 0) {
            LOG_ERROR("select 에러: %s", strerror(errno));
            break;
        }
        
        if (activity == 0) {
            LOG_WARN("타임아웃 (60초 동안 활동 없음)");
            break;
        }
        
        // 클라이언트 → 서버
        if (FD_ISSET(conn->client_fd, &read_fds)) {
            int bytes = recv(conn->client_fd, buffer, BUFFER_SIZE, 0);
            
            if (bytes <= 0) {
                if (bytes == 0) {
                    LOG_INFO("클라이언트 연결 종료");
                } else {
                    LOG_ERROR("클라이언트 수신 실패: %s", strerror(errno));
                }
                break;
            }
            
            conn->stats.bytes_received += bytes;
            conn->stats.packets_received++;
            conn->stats.last_activity = time(NULL);
            
            LOG_DEBUG("클라이언트 → 서버: %d bytes", bytes);
            
            // 필터 적용
            if (!filter_apply(&conn->filter_chain, buffer, bytes, &conn->stats)) {
                LOG_WARN("패킷 필터링됨 (드롭)");
                continue;
            }
            
            int sent = send(conn->server_fd, buffer, bytes, 0);
            if (sent <= 0) {
                LOG_ERROR("서버 전송 실패: %s", strerror(errno));
                break;
            }
            
            conn->stats.bytes_sent += sent;
            conn->stats.packets_sent++;
        }
        
        // 서버 → 클라이언트
        if (FD_ISSET(conn->server_fd, &read_fds)) {
            int bytes = recv(conn->server_fd, buffer, BUFFER_SIZE, 0);
            
            if (bytes <= 0) {
                if (bytes == 0) {
                    LOG_INFO("서버 연결 종료");
                } else {
                    LOG_ERROR("서버 수신 실패: %s", strerror(errno));
                }
                break;
            }
            
            conn->stats.bytes_received += bytes;
            conn->stats.packets_received++;
            conn->stats.last_activity = time(NULL);
            
            LOG_DEBUG("서버 → 클라이언트: %d bytes", bytes);
            
            // 필터 적용
            if (!filter_apply(&conn->filter_chain, buffer, bytes, &conn->stats)) {
                LOG_WARN("패킷 필터링됨 (드롭)");
                continue;
            }
            
            int sent = send(conn->client_fd, buffer, bytes, 0);
            if (sent <= 0) {
                LOG_ERROR("클라이언트 전송 실패: %s", strerror(errno));
                break;
            }
            
            conn->stats.bytes_sent += sent;
            conn->stats.packets_sent++;
        }
    }
    
    stats_print(&conn->stats);
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
    
    if (listen(proxy_sock, 10) < 0) {
        LOG_ERROR("리스닝 실패: %s", strerror(errno));
        close(proxy_sock);
        return -1;
    }
    
    LOG_INFO("======================================");
    LOG_INFO("프록시 서버 시작");
    LOG_INFO("리스닝: 0.0.0.0:%d", config->listen_port);
    LOG_INFO("대상: %s:%d", config->target_host, config->target_port);
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
            conn.client_fd = client_sock;
            conn.server_fd = server_sock;
            strcpy(conn.client_addr, client_ip);
            conn.client_port = client_port;
            
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
    
    close(proxy_sock);
    LOG_INFO("프록시 서버 종료 완료");
    return 0;
}
