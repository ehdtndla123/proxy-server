#include "../include/filter.h"
#include "../include/logger.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>

void filter_chain_init(FilterChain *chain) {
    memset(chain, 0, sizeof(FilterChain));
    chain->count = 0;
}

bool filter_chain_add_delay(FilterChain *chain, int delay_ms) {
    if (chain->count >= MAX_FILTERS) {
        LOG_ERROR("필터 체인이 가득 찼습니다");
        return false;
    }
    
    Filter *filter = &chain->filters[chain->count++];
    filter->type = FILTER_DELAY;
    filter->enabled = true;
    filter->params.delay.delay_ms = delay_ms;
    
    LOG_INFO("지연 필터 추가: %d ms", delay_ms);
    return true;
}

bool filter_chain_add_drop(FilterChain *chain, float drop_rate) {
    if (chain->count >= MAX_FILTERS) {
        LOG_ERROR("필터 체인이 가득 찼습니다");
        return false;
    }
    
    if (drop_rate < 0.0 || drop_rate > 1.0) {
        LOG_ERROR("드롭 확률은 0.0 ~ 1.0 사이여야 합니다");
        return false;
    }
    
    Filter *filter = &chain->filters[chain->count++];
    filter->type = FILTER_DROP;
    filter->enabled = true;
    filter->params.drop.drop_rate = drop_rate;
    
    LOG_INFO("드롭 필터 추가: %.2f%%", drop_rate * 100);
    return true;
}

bool filter_chain_add_throttle(FilterChain *chain, int bytes_per_sec) {
    if (chain->count >= MAX_FILTERS) {
        LOG_ERROR("필터 체인이 가득 찼습니다");
        return false;
    }
    
    Filter *filter = &chain->filters[chain->count++];
    filter->type = FILTER_THROTTLE;
    filter->enabled = true;
    filter->params.throttle.bytes_per_sec = bytes_per_sec;
    
    LOG_INFO("쓰로틀 필터 추가: %d bytes/sec", bytes_per_sec);
    return true;
}

bool filter_apply(FilterChain *chain, const char *data, int length, ConnectionStats *stats) {
    (void)data;   // 미사용 매개변수 경고 방지
    (void)stats;  // 미사용 매개변수 경고 방지

    if (chain->count == 0) {
        return true;  // 필터 없음, 통과
    }

    for (int i = 0; i < chain->count; i++) {
        Filter *filter = &chain->filters[i];

        if (!filter->enabled) {
            continue;
        }

        switch (filter->type) {
            case FILTER_DELAY: {
                int delay_ms = filter->params.delay.delay_ms;
                LOG_DEBUG("지연 적용: %d ms", delay_ms);
                usleep(delay_ms * 1000);  // ms to microseconds
                break;
            }

            case FILTER_DROP: {
                float drop_rate = filter->params.drop.drop_rate;
                float random = (float)rand() / RAND_MAX;

                if (random < drop_rate) {
                    LOG_WARN("패킷 드롭 (확률: %.2f%%, 랜덤: %.2f)",
                             drop_rate * 100, random * 100);
                    // 드롭 카운팅은 proxy.c에서 수행
                    return false;  // 패킷 드롭
                }
                break;
            }

            case FILTER_THROTTLE: {
                int bytes_per_sec = filter->params.throttle.bytes_per_sec;
                int delay_us = (length * 1000000) / bytes_per_sec;
                LOG_DEBUG("쓰로틀링: %d bytes -> %d us 지연", length, delay_us);
                usleep(delay_us);
                break;
            }

            default:
                break;
        }
    }

    return true;  // 통과
}

void filter_chain_print(const FilterChain *chain) {
    if (chain->count == 0) {
        LOG_INFO("활성 필터 없음");
        return;
    }
    
    LOG_INFO("=== 필터 체인 (%d개) ===", chain->count);
    for (int i = 0; i < chain->count; i++) {
        const Filter *filter = &chain->filters[i];
        
        switch (filter->type) {
            case FILTER_DELAY:
                LOG_INFO("  [%d] 지연: %d ms", i, filter->params.delay.delay_ms);
                break;
            case FILTER_DROP:
                LOG_INFO("  [%d] 드롭: %.2f%%", i, filter->params.drop.drop_rate * 100);
                break;
            case FILTER_THROTTLE:
                LOG_INFO("  [%d] 쓰로틀: %d bytes/sec", i, filter->params.throttle.bytes_per_sec);
                break;
            default:
                break;
        }
    }
}
