#ifndef CONTROL_H
#define CONTROL_H

#include "types.h"
#include <stdbool.h>

// 제어 명령 타입
typedef enum {
    CMD_LIST_CONNECTIONS,    // 모든 연결 목록
    CMD_KILL_CONNECTION,     // 특정 연결 종료
    CMD_SEND_SIGNAL,         // 특정 프로세스에 시그널 전송
    CMD_GET_STATS,           // 통계 정보 조회
    CMD_SHUTDOWN             // 프록시 서버 종료
} ControlCommand;

// 제어 요청 구조체
typedef struct {
    ControlCommand cmd;
    pid_t target_pid;        // 대상 프로세스 ID
    int signal_num;          // 전송할 시그널 번호
} ControlRequest;

// 연결 정보 요약 (관리용)
typedef struct {
    pid_t pid;
    char client_addr[MAX_ADDR_LEN];
    int client_port;
    char target_addr[MAX_ADDR_LEN];
    int target_port;
    uint64_t client_to_server_bytes;
    uint64_t server_to_client_bytes;
    time_t start_time;
    time_t last_activity;
} ConnectionInfo;

// 제어 응답 구조체
typedef struct {
    bool success;
    int connection_count;
    ConnectionInfo connections[100];  // 최대 100개 연결
    char message[256];
} ControlResponse;

// 제어 서버 시작
int control_server_start(const char *socket_path);

// 제어 서버 종료
void control_server_stop(void);

// 연결 정보 등록 (자식 프로세스가 호출)
void control_register_connection(const Connection *conn);

// 연결 정보 제거 (자식 프로세스가 종료될 때 호출)
void control_unregister_connection(pid_t pid);

// 연결 통계 업데이트
void control_update_stats(pid_t pid, const ConnectionStats *stats);

// 제어 요청 처리
void control_handle_request(int client_fd);

#endif // CONTROL_H
