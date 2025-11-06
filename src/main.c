#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <time.h>
#include <sys/wait.h>
#include <errno.h>
#include <limits.h>
#include "types.h"
#include "logger.h"
#include "config.h"
#include "filter.h"
#include "proxy.h"
#include "control.h"

static volatile sig_atomic_t keep_running = 1;

// SIGCHLD 핸들러 - 좀비 프로세스 방지
void sigchld_handler(int signum) {
    (void)signum;  // 미사용 경고 방지
    int saved_errno = errno;

    // 모든 종료된 자식 프로세스 정리
    while (waitpid(-1, NULL, WNOHANG) > 0) {
        // 자식 프로세스 정리
    }

    errno = saved_errno;
}

void signal_handler(int signum) {
    if (signum == SIGINT || signum == SIGTERM) {
        LOG_INFO("\n프록시 서버 종료 중...");
        keep_running = 0;

        // 모든 자식 프로세스 종료
        kill(0, SIGTERM);  // 프로세스 그룹 전체 종료

        // 제어 소켓 정리
        control_server_stop();

        exit(0);
    }
}

void print_usage(const char *program_name) {
    printf("사용법: %s [옵션]\n", program_name);
    printf("\n옵션:\n");
    printf("  -p <port>       리스닝 포트 (기본값: 9999)\n");
    printf("  -t <host:port>  대상 서버 (기본값: 127.0.0.1:8080)\n");
    printf("  -c <file>       설정 파일 경로\n");
    printf("  -l <file>       로그 파일 경로 (기본값: logs/proxy.log)\n");
    printf("  -d <ms>         지연 필터 추가 (밀리초)\n");
    printf("  -r <rate>       드롭 필터 추가 (0.0~1.0)\n");
    printf("  -b <bytes/s>    쓰로틀 필터 추가 (bytes per second)\n");
    printf("  -v              디버그 모드\n");
    printf("  -h              도움말\n");
    printf("\n예시:\n");
    printf("  %s -p 9999 -t 127.0.0.1:8080\n", program_name);
    printf("  %s -p 10000 -t db.example.com:3306 -d 100 -r 0.1\n", program_name);
    printf("  %s -c config/proxy.conf\n", program_name);
}

int main(int argc, char *argv[]) {
    ProxyConfig config;
    FilterChain filter_chain;
    LogLevel log_level = LOG_INFO;
    char config_file[256] = {0};
    
    // 설정 초기화
    config_init(&config);
    filter_chain_init(&filter_chain);
    
    // 난수 초기화 (드롭 필터용)
    srand(time(NULL));
    
    // 명령행 인자 파싱
    int opt;
    while ((opt = getopt(argc, argv, "p:t:c:l:d:r:b:vh")) != -1) {
        switch (opt) {
            case 'p': {
                char *endptr;
                long port = strtol(optarg, &endptr, 10);
                if (*endptr != '\0' || port <= 0 || port > 65535) {
                    fprintf(stderr, "잘못된 포트 번호: %s (1-65535 사이여야 함)\n", optarg);
                    return 1;
                }
                config.listen_port = (int)port;
                break;
            }
            case 't': {
                char *colon = strchr(optarg, ':');
                if (colon) {
                    *colon = '\0';
                    strncpy(config.target_host, optarg, MAX_HOST_LEN - 1);
                    config.target_host[MAX_HOST_LEN - 1] = '\0';

                    char *endptr;
                    long port = strtol(colon + 1, &endptr, 10);
                    if (*endptr != '\0' || port <= 0 || port > 65535) {
                        fprintf(stderr, "잘못된 대상 포트 번호: %s\n", colon + 1);
                        return 1;
                    }
                    config.target_port = (int)port;
                } else {
                    fprintf(stderr, "잘못된 대상 서버 형식: %s (host:port 형식이어야 함)\n", optarg);
                    return 1;
                }
                break;
            }
            case 'c':
                strncpy(config_file, optarg, sizeof(config_file) - 1);
                config_file[sizeof(config_file) - 1] = '\0';
                break;
            case 'l':
                strncpy(config.log_file, optarg, MAX_PATH_LEN - 1);
                config.log_file[MAX_PATH_LEN - 1] = '\0';
                break;
            case 'd': {
                char *endptr;
                long delay = strtol(optarg, &endptr, 10);
                if (*endptr != '\0' || delay < 0 || delay > 10000) {
                    fprintf(stderr, "잘못된 지연 시간: %s (0-10000ms)\n", optarg);
                    return 1;
                }
                filter_chain_add_delay(&filter_chain, (int)delay);
                config.enable_filters = true;
                break;
            }
            case 'r': {
                char *endptr;
                double rate = strtod(optarg, &endptr);
                if (*endptr != '\0' || rate < 0.0 || rate > 1.0) {
                    fprintf(stderr, "잘못된 드롭 확률: %s (0.0-1.0)\n", optarg);
                    return 1;
                }
                filter_chain_add_drop(&filter_chain, (float)rate);
                config.enable_filters = true;
                break;
            }
            case 'b': {
                char *endptr;
                long bps = strtol(optarg, &endptr, 10);
                if (*endptr != '\0' || bps <= 0) {
                    fprintf(stderr, "잘못된 대역폭: %s (양수여야 함)\n", optarg);
                    return 1;
                }
                filter_chain_add_throttle(&filter_chain, (int)bps);
                config.enable_filters = true;
                break;
            }
            case 'v':
                log_level = LOG_DEBUG;
                break;
            case 'h':
                print_usage(argv[0]);
                return 0;
            default:
                print_usage(argv[0]);
                return 1;
        }
    }
    
    // 설정 파일 로드 (옵션)
    if (config_file[0] != '\0') {
        config_load(&config, config_file);
    }
    
    // 로거 초기화
    if (config.enable_logging) {
        if (!logger_init(config.log_file, log_level)) {
            fprintf(stderr, "로거 초기화 실패\n");
            return 1;
        }
    } else {
        logger_init(NULL, log_level);  // 콘솔만
    }
    
    LOG_INFO("TCP 프록시 서버 v1.0");
    
    // 설정 출력
    config_print(&config);
    
    // 시그널 핸들러 등록
    struct sigaction sa;

    // SIGCHLD 핸들러 (좀비 프로세스 방지)
    sa.sa_handler = sigchld_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART | SA_NOCLDSTOP;
    if (sigaction(SIGCHLD, &sa, NULL) < 0) {
        LOG_ERROR("SIGCHLD 핸들러 등록 실패: %s", strerror(errno));
        return 1;
    }

    // SIGINT, SIGTERM 핸들러
    sa.sa_handler = signal_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    if (sigaction(SIGINT, &sa, NULL) < 0 || sigaction(SIGTERM, &sa, NULL) < 0) {
        LOG_ERROR("시그널 핸들러 등록 실패: %s", strerror(errno));
        return 1;
    }
    
    // 프록시 시작
    int result = proxy_start(&config, 
                             config.enable_filters ? &filter_chain : NULL);
    
    // 정리
    logger_cleanup();
    
    return result;
}
