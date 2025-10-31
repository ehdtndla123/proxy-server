#ifndef TYPES_H
#define TYPES_H

#include <stdint.h>
#include <stdbool.h>
#include <time.h>

// 프록시 설정
typedef struct {
    int listen_port;              // 프록시 리스닝 포트
    char target_host[256];        // 대상 서버 주소
    int target_port;              // 대상 서버 포트
    bool enable_logging;          // 로깅 활성화
    char log_file[256];           // 로그 파일 경로
    bool enable_filters;          // 필터 활성화
} ProxyConfig;

// 필터 타입
typedef enum {
    FILTER_NONE = 0,
    FILTER_DELAY,                 // 지연 추가
    FILTER_DROP,                  // 패킷 드롭
    FILTER_THROTTLE,              // 대역폭 제한
    FILTER_MODIFY                 // 데이터 수정
} FilterType;

// 필터 규칙
typedef struct {
    FilterType type;
    bool enabled;
    union {
        struct {
            int delay_ms;         // 지연 시간 (밀리초)
        } delay;
        struct {
            float drop_rate;      // 드롭 확률 (0.0 ~ 1.0)
        } drop;
        struct {
            int bytes_per_sec;    // 초당 바이트 수
        } throttle;
    } params;
} Filter;

// 필터 체인
#define MAX_FILTERS 10
typedef struct {
    Filter filters[MAX_FILTERS];
    int count;
} FilterChain;

// 연결 통계
typedef struct {
    uint64_t bytes_sent;          // 전송 바이트
    uint64_t bytes_received;      // 수신 바이트
    time_t start_time;            // 연결 시작 시간
    time_t last_activity;         // 마지막 활동 시간
    int packets_sent;             // 전송 패킷 수
    int packets_received;         // 수신 패킷 수
    int packets_dropped;          // 드롭된 패킷 수
} ConnectionStats;

// 연결 정보
typedef struct {
    int client_fd;                // 클라이언트 소켓
    int server_fd;                // 서버 소켓
    char client_addr[64];         // 클라이언트 주소
    int client_port;              // 클라이언트 포트
    ConnectionStats stats;        // 통계
    FilterChain filter_chain;     // 필터 체인
} Connection;

#endif // TYPES_H
