#ifndef TYPES_H
#define TYPES_H

#include <stdint.h>
#include <stdbool.h>
#include <time.h>
#include <sys/types.h>

// 상수 정의
#define MAX_HOST_LEN 256
#define MAX_PATH_LEN 256
#define MAX_ADDR_LEN 64
#define BUFFER_SIZE 8192
#define SELECT_TIMEOUT_SEC 60
#define MAX_LISTEN_BACKLOG 10

// 프록시 설정
typedef struct {
    int listen_port;              // 프록시 리스닝 포트
    char target_host[MAX_HOST_LEN];  // 대상 서버 주소
    int target_port;              // 대상 서버 포트
    bool enable_logging;          // 로깅 활성화
    char log_file[MAX_PATH_LEN];  // 로그 파일 경로
    bool enable_filters;          // 필터 활성화
    char control_socket[MAX_PATH_LEN]; // 제어 소켓 경로
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

// 연결 통계 (방향별로 구분)
typedef struct {
    // 클라이언트 -> 서버 방향
    uint64_t client_to_server_bytes;
    int client_to_server_packets;
    int client_to_server_dropped;

    // 서버 -> 클라이언트 방향
    uint64_t server_to_client_bytes;
    int server_to_client_packets;
    int server_to_client_dropped;

    // 시간 정보
    time_t start_time;            // 연결 시작 시간
    time_t last_activity;         // 마지막 활동 시간
} ConnectionStats;

// 연결 정보
typedef struct {
    pid_t pid;                    // 프로세스 ID
    int client_fd;                // 클라이언트 소켓
    int server_fd;                // 서버 소켓
    char client_addr[MAX_ADDR_LEN]; // 클라이언트 주소
    int client_port;              // 클라이언트 포트
    char target_addr[MAX_ADDR_LEN]; // 대상 서버 주소
    int target_port;              // 대상 서버 포트
    ConnectionStats stats;        // 통계
    FilterChain filter_chain;     // 필터 체인
} Connection;

#endif // TYPES_H
