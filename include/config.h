#ifndef CONFIG_H
#define CONFIG_H

#include "types.h"
#include <stdbool.h>

// 설정 초기화 (기본값)
void config_init(ProxyConfig *config);

// 설정 파일 로드
bool config_load(ProxyConfig *config, const char *config_file);

// 설정 출력
void config_print(const ProxyConfig *config);

#endif // CONFIG_H
