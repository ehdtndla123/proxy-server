#ifndef LOGGER_H
#define LOGGER_H

#include <stdio.h>
#include <stdbool.h>

// 로그 레벨
typedef enum {
    LOG_DEBUG = 0,
    LOG_INFO,
    LOG_WARN,
    LOG_ERROR
} LogLevel;

// 로거 초기화
bool logger_init(const char *log_file, LogLevel level);

// 로그 출력
void log_message(LogLevel level, const char *format, ...);

// 로그 매크로
#define LOG_DEBUG(...) log_message(LOG_DEBUG, __VA_ARGS__)
#define LOG_INFO(...)  log_message(LOG_INFO, __VA_ARGS__)
#define LOG_WARN(...)  log_message(LOG_WARN, __VA_ARGS__)
#define LOG_ERROR(...) log_message(LOG_ERROR, __VA_ARGS__)

// 로거 종료
void logger_cleanup(void);

#endif // LOGGER_H
