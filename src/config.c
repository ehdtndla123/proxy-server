#include "../include/config.h"
#include "../include/logger.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void config_init(ProxyConfig *config) {
    memset(config, 0, sizeof(ProxyConfig));

    // 기본값 설정
    config->listen_port = 9999;
    strncpy(config->target_host, "127.0.0.1", MAX_HOST_LEN - 1);
    config->target_host[MAX_HOST_LEN - 1] = '\0';
    config->target_port = 8080;
    config->enable_logging = true;
    strncpy(config->log_file, "logs/proxy.log", MAX_PATH_LEN - 1);
    config->log_file[MAX_PATH_LEN - 1] = '\0';
    strncpy(config->control_socket, "/tmp/tcp_proxy_control.sock", MAX_PATH_LEN - 1);
    config->control_socket[MAX_PATH_LEN - 1] = '\0';
    config->enable_filters = false;
}

bool config_load(ProxyConfig *config, const char *config_file) {
    FILE *fp = fopen(config_file, "r");
    if (fp == NULL) {
        LOG_WARN("설정 파일을 열 수 없습니다: %s (기본값 사용)", config_file);
        return false;
    }
    
    char line[256];
    int line_num = 0;
    
    while (fgets(line, sizeof(line), fp)) {
        line_num++;
        
        // 주석 및 빈 줄 무시
        if (line[0] == '#' || line[0] == '\n') {
            continue;
        }
        
        // 개행 제거
        line[strcspn(line, "\n")] = 0;
        
        // Key=Value 파싱
        char *key = strtok(line, "=");
        char *value = strtok(NULL, "=");
        
        if (key == NULL || value == NULL) {
            continue;
        }
        
        // 공백 제거
        while (*key == ' ') key++;
        while (*value == ' ') value++;
        
        // 설정 적용
        if (strcmp(key, "listen_port") == 0) {
            config->listen_port = atoi(value);
        } else if (strcmp(key, "target_host") == 0) {
            strncpy(config->target_host, value, sizeof(config->target_host) - 1);
        } else if (strcmp(key, "target_port") == 0) {
            config->target_port = atoi(value);
        } else if (strcmp(key, "enable_logging") == 0) {
            config->enable_logging = (strcmp(value, "true") == 0 || strcmp(value, "1") == 0);
        } else if (strcmp(key, "log_file") == 0) {
            strncpy(config->log_file, value, sizeof(config->log_file) - 1);
        } else if (strcmp(key, "enable_filters") == 0) {
            config->enable_filters = (strcmp(value, "true") == 0 || strcmp(value, "1") == 0);
        }
    }
    
    fclose(fp);
    LOG_INFO("설정 파일 로드 완료: %s", config_file);
    return true;
}

void config_print(const ProxyConfig *config) {
    LOG_INFO("=== 프록시 설정 ===");
    LOG_INFO("  리스닝 포트: %d", config->listen_port);
    LOG_INFO("  대상 서버: %s:%d", config->target_host, config->target_port);
    LOG_INFO("  로깅: %s", config->enable_logging ? "활성화" : "비활성화");
    if (config->enable_logging) {
        LOG_INFO("  로그 파일: %s", config->log_file);
    }
    LOG_INFO("  필터: %s", config->enable_filters ? "활성화" : "비활성화");
}
