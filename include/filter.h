#ifndef FILTER_H
#define FILTER_H

#include "types.h"
#include <stdbool.h>

// 필터 체인 초기화
void filter_chain_init(FilterChain *chain);

// 필터 추가
bool filter_chain_add_delay(FilterChain *chain, int delay_ms);
bool filter_chain_add_drop(FilterChain *chain, float drop_rate);
bool filter_chain_add_throttle(FilterChain *chain, int bytes_per_sec);

// 필터 적용
bool filter_apply(FilterChain *chain, const char *data, int length, ConnectionStats *stats);

// 필터 정보 출력
void filter_chain_print(const FilterChain *chain);

#endif // FILTER_H
