#!/bin/bash

# TCP 프록시 서버 종료 스크립트

echo "TCP 프록시 프로세스 검색 중..."

# tcp_proxy 프로세스 찾기
PIDS=$(ps aux | grep '[b]in/tcp_proxy' | awk '{print $2}')

if [ -z "$PIDS" ]; then
    echo "실행 중인 프록시 서버가 없습니다."
    exit 0
fi

echo "발견된 프로세스:"
ps aux | grep '[b]in/tcp_proxy'

echo ""
echo "프로세스 종료 중..."

# 모든 프로세스 종료
for PID in $PIDS; do
    echo "  PID $PID 종료 중..."
    kill -TERM $PID 2>/dev/null
done

# 1초 대기
sleep 1

# 아직 살아있는 프로세스 강제 종료
REMAINING=$(ps aux | grep '[b]in/tcp_proxy' | awk '{print $2}')
if [ ! -z "$REMAINING" ]; then
    echo "강제 종료 중..."
    for PID in $REMAINING; do
        echo "  PID $PID 강제 종료..."
        kill -9 $PID 2>/dev/null
    done
fi

echo "완료!"
