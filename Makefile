# TCP 프록시 서버 Makefile

CC = gcc
CFLAGS = -Wall -Wextra -O2 -pthread
LDFLAGS = -pthread

# 디렉토리
SRC_DIR = src
INC_DIR = include
BUILD_DIR = build
BIN_DIR = bin

# tcp_proxy 소스 파일 (proxyctl.c 제외)
PROXY_SOURCES = $(filter-out $(SRC_DIR)/proxyctl.c, $(wildcard $(SRC_DIR)/*.c))
PROXY_OBJECTS = $(PROXY_SOURCES:$(SRC_DIR)/%.c=$(BUILD_DIR)/%.o)
PROXY_TARGET = $(BIN_DIR)/tcp_proxy

# proxyctl 소스 파일
PROXYCTL_SOURCES = $(SRC_DIR)/proxyctl.c
PROXYCTL_OBJECTS = $(BUILD_DIR)/proxyctl.o
PROXYCTL_TARGET = $(BIN_DIR)/proxyctl

# 기본 타겟
all: directories $(PROXY_TARGET) $(PROXYCTL_TARGET)

# 디렉토리 생성
directories:
	@mkdir -p $(BUILD_DIR)
	@mkdir -p $(BIN_DIR)
	@mkdir -p logs

# tcp_proxy 실행 파일 생성
$(PROXY_TARGET): $(PROXY_OBJECTS)
	@echo "링킹: $@"
	@$(CC) $(PROXY_OBJECTS) -o $@ $(LDFLAGS)
	@echo "빌드 완료: $@"

# proxyctl 실행 파일 생성
$(PROXYCTL_TARGET): $(PROXYCTL_OBJECTS)
	@echo "링킹: $@"
	@$(CC) $(PROXYCTL_OBJECTS) -o $@ $(LDFLAGS)
	@echo "빌드 완료: $@"

# 오브젝트 파일 생성
$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c
	@echo "컴파일: $<"
	@$(CC) $(CFLAGS) -I$(INC_DIR) -c $< -o $@

# 정리
clean:
	@echo "정리 중..."
	@rm -rf $(BUILD_DIR) $(BIN_DIR)
	@rm -f logs/*.log
	@echo "정리 완료"

# 재빌드
rebuild: clean all

# 설치
install: all
	@echo "설치 중..."
	@cp $(PROXY_TARGET) /usr/local/bin/
	@cp $(PROXYCTL_TARGET) /usr/local/bin/
	@echo "설치 완료: /usr/local/bin/tcp_proxy, /usr/local/bin/proxyctl"

# 제거
uninstall:
	@echo "제거 중..."
	@rm -f /usr/local/bin/tcp_proxy
	@rm -f /usr/local/bin/proxyctl
	@echo "제거 완료"

# 실행
run: all
	@$(PROXY_TARGET)

# 도움말
help:
	@echo "사용 가능한 타겟:"
	@echo "  make         - 프로젝트 빌드"
	@echo "  make clean   - 빌드 파일 정리"
	@echo "  make rebuild - 정리 후 재빌드"
	@echo "  make install - 시스템에 설치"
	@echo "  make uninstall - 시스템에서 제거"
	@echo "  make run     - 빌드 후 실행"
	@echo "  make help    - 도움말 표시"

.PHONY: all directories clean rebuild install uninstall run help
