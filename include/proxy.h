#ifndef PROXY_H
#define PROXY_H

#include "types.h"
#include <stdbool.h>

// 프록시 서버 시작
int proxy_start(const ProxyConfig *config, FilterChain *filter_chain);

// 클라이언트 연결 처리
void proxy_handle_connection(Connection *conn);

// 대상 서버 연결
int proxy_connect_target(const char *host, int port);

#endif // PROXY_H
