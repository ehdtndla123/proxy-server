#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <time.h>
#include "types.h"
#include "logger.h"
#include "config.h"
#include "filter.h"
#include "proxy.h"

static volatile int keep_running = 1;

void signal_handler(int signum) {
    if (signum == SIGINT || signum == SIGTERM) {
        LOG_INFO("\n프록시 서버 종료 중...");
        keep_running = 0;
        
        // 모든 자식 프로세스 종료
        signal(SIGCHLD, SIG_IGN);  // 좀비 방지
        kill(0, SIGTERM);  // 프로세스 그룹 전체 종료
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
            case 'p':
                config.listen_port = atoi(optarg);
                break;
            case 't': {
                char *colon = strchr(optarg, ':');
                if (colon) {
                    *colon = '\0';
                    strncpy(config.target_host, optarg, sizeof(config.target_host) - 1);
                    config.target_port = atoi(colon + 1);
                } else {
                    fprintf(stderr, "잘못된 대상 서버 형식: %s\n", optarg);
                    return 1;
                }
                break;
            }
            case 'c':
                strncpy(config_file, optarg, sizeof(config_file) - 1);
                break;
            case 'l':
                strncpy(config.log_file, optarg, sizeof(config.log_file) - 1);
                break;
            case 'd':
                filter_chain_add_delay(&filter_chain, atoi(optarg));
                config.enable_filters = true;
                break;
            case 'r':
                filter_chain_add_drop(&filter_chain, atof(optarg));
                config.enable_filters = true;
                break;
            case 'b':
                filter_chain_add_throttle(&filter_chain, atoi(optarg));
                config.enable_filters = true;
                break;
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
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    signal(SIGCHLD, SIG_IGN);  // 좀비 프로세스 방지
    
    // 프록시 시작
    int result = proxy_start(&config, 
                             config.enable_filters ? &filter_chain : NULL);
    
    // 정리
    logger_cleanup();
    
    return result;
}
