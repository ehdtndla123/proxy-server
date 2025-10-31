#!/bin/bash

echo "======================================"
echo "TCP 프록시 서버 테스트"
echo "======================================"
echo ""

# 색상 정의
GREEN='\033[0;32m'
BLUE='\033[0;34m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

echo -e "${BLUE}1. 기본 프록시 테스트${NC}"
echo "   ./bin/tcp_proxy -p 9999 -t 127.0.0.1:8080"
echo ""

echo -e "${BLUE}2. DB 프록시 테스트${NC}"
echo "   ./bin/tcp_proxy -p 10000 -t 127.0.0.1:3306 -c config/db_proxy.conf"
echo ""

echo -e "${BLUE}3. 지연 필터 테스트 (100ms)${NC}"
echo "   ./bin/tcp_proxy -p 9999 -t 127.0.0.1:8080 -d 100"
echo ""

echo -e "${BLUE}4. 드롭 필터 테스트 (10%)${NC}"
echo "   ./bin/tcp_proxy -p 9999 -t 127.0.0.1:8080 -r 0.1"
echo ""

echo -e "${BLUE}5. 복합 필터 테스트 (지연 50ms + 드롭 5% + 쓰로틀 10KB/s)${NC}"
echo "   ./bin/tcp_proxy -p 9999 -t 127.0.0.1:8080 -d 50 -r 0.05 -b 10240"
echo ""

echo -e "${BLUE}6. 디버그 모드${NC}"
echo "   ./bin/tcp_proxy -p 9999 -t 127.0.0.1:8080 -v"
echo ""

echo -e "${YELLOW}테스트 순서:${NC}"
echo "1. 터미널1: ../test_server2 실행 (포트 8080)"
echo "2. 터미널2: 위 명령어 중 하나 실행"
echo "3. 터미널3: ../test_client 127.0.0.1 9999 실행"
echo ""
echo "======================================"
