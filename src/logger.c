#include "../include/logger.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <time.h>
#include <string.h>
#include <pthread.h>

static FILE *log_fp = NULL;
static LogLevel current_level = LOG_INFO;
static pthread_mutex_t log_mutex = PTHREAD_MUTEX_INITIALIZER;

static const char *level_strings[] = {
    "DEBUG",
    "INFO",
    "WARN",
    "ERROR"
};

static const char *level_colors[] = {
    "\033[36m",  // Cyan for DEBUG
    "\033[32m",  // Green for INFO
    "\033[33m",  // Yellow for WARN
    "\033[31m"   // Red for ERROR
};

#define COLOR_RESET "\033[0m"

bool logger_init(const char *log_file, LogLevel level) {
    current_level = level;
    
    if (log_file != NULL) {
        log_fp = fopen(log_file, "a");
        if (log_fp == NULL) {
            fprintf(stderr, "로그 파일 열기 실패: %s\n", log_file);
            return false;
        }
    }
    
    return true;
}

void log_message(LogLevel level, const char *format, ...) {
    if (level < current_level) {
        return;
    }
    
    pthread_mutex_lock(&log_mutex);
    
    // 시간 정보
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    char time_buf[64];
    strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S", tm_info);
    
    // 콘솔 출력 (컬러)
    printf("%s[%s]%s [%s] ", 
           level_colors[level], 
           level_strings[level], 
           COLOR_RESET, 
           time_buf);
    
    va_list args;
    va_start(args, format);
    vprintf(format, args);
    va_end(args);
    printf("\n");
    fflush(stdout);
    
    // 파일 출력 (컬러 없이)
    if (log_fp != NULL) {
        fprintf(log_fp, "[%s] [%s] ", level_strings[level], time_buf);
        va_start(args, format);
        vfprintf(log_fp, format, args);
        va_end(args);
        fprintf(log_fp, "\n");
        fflush(log_fp);
    }
    
    pthread_mutex_unlock(&log_mutex);
}

void logger_cleanup(void) {
    if (log_fp != NULL) {
        fclose(log_fp);
        log_fp = NULL;
    }
}
