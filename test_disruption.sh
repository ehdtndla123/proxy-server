#!/bin/bash

# 네트워크 단절 테스트 스크립트
# 프록시를 끊는 다양한 방법

show_menu() {
    echo "======================================"
    echo "프록시 종료 테스트 메뉴"
    echo "======================================"
    echo "1. 정상 종료 (SIGTERM) - Ctrl+C 시뮬레이션"
    echo "2. 강제 종료 (SIGKILL) - 크래시 시뮬레이션"
    echo "3. 일시 정지 (SIGSTOP) - 프로세스 멈춤"
    echo "4. 재개 (SIGCONT) - 프로세스 재개"
    echo "5. 프로세스 목록 보기"
    echo "6. 연결 상태 보기"
    echo "7. 특정 자식 프로세스만 종료"
    echo "0. 종료"
    echo "======================================"
}

find_proxy_pids() {
    ps aux | grep '[b]in/tcp_proxy' | awk '{print $2}'
}

find_main_pid() {
    # 부모 프로세스 찾기 (PPID가 1이 아닌 것)
    ps -eo pid,ppid,cmd | grep '[b]in/tcp_proxy' | awk '$2 == 1 {print $1}' | head -1
}

case_1() {
    echo "정상 종료 (SIGTERM)..."
    PIDS=$(find_proxy_pids)
    if [ -z "$PIDS" ]; then
        echo "실행 중인 프록시가 없습니다."
        return
    fi
    for PID in $PIDS; do
        echo "  SIGTERM → PID $PID"
        kill -TERM $PID
    done
    echo "완료! 프록시가 정상적으로 종료됩니다."
    echo "→ 클라이언트/서버: FIN 패킷 수신"
}

case_2() {
    echo "강제 종료 (SIGKILL)..."
    PIDS=$(find_proxy_pids)
    if [ -z "$PIDS" ]; then
        echo "실행 중인 프록시가 없습니다."
        return
    fi
    for PID in $PIDS; do
        echo "  SIGKILL → PID $PID"
        kill -9 $PID
    done
    echo "완료! 프록시가 즉시 종료되었습니다."
    echo "→ 클라이언트/서버: 연결이 갑자기 끊김 (RST)"
}

case_3() {
    echo "프로세스 일시 정지 (SIGSTOP)..."
    PIDS=$(find_proxy_pids)
    if [ -z "$PIDS" ]; then
        echo "실행 중인 프록시가 없습니다."
        return
    fi
    for PID in $PIDS; do
        echo "  SIGSTOP → PID $PID"
        kill -STOP $PID
    done
    echo "완료! 프록시가 멈췄습니다 (프로세스는 살아있음)"
    echo "→ 클라이언트/서버: 응답 없음 (타임아웃 발생)"
    echo "→ 재개하려면 옵션 4 선택"
}

case_4() {
    echo "프로세스 재개 (SIGCONT)..."
    PIDS=$(find_proxy_pids)
    if [ -z "$PIDS" ]; then
        echo "실행 중인 프록시가 없습니다."
        return
    fi
    for PID in $PIDS; do
        echo "  SIGCONT → PID $PID"
        kill -CONT $PID
    done
    echo "완료! 프록시가 다시 동작합니다."
}

case_5() {
    echo "프록시 프로세스 목록:"
    echo ""
    ps aux | grep '[b]in/tcp_proxy' | head -20
    echo ""
    echo "총 프로세스 수: $(find_proxy_pids | wc -l)"
}

case_6() {
    echo "연결 상태 확인:"
    echo ""
    
    # 프록시 포트 찾기
    PORTS=$(ps aux | grep '[b]in/tcp_proxy.*-p' | grep -oP '\-p\s+\K\d+' | sort -u)
    
    if [ -z "$PORTS" ]; then
        echo "프록시 포트를 찾을 수 없습니다."
        echo "기본 포트 9999로 확인합니다..."
        PORTS="9999"
    fi
    
    for PORT in $PORTS; do
        echo "포트 $PORT 연결 상태:"
        ss -tnp | grep ":$PORT" || netstat -tnp 2>/dev/null | grep ":$PORT" || echo "  연결 없음"
        echo ""
    done
}

case_7() {
    echo "자식 프로세스 목록:"
    ps -eo pid,ppid,cmd | grep '[b]in/tcp_proxy'
    echo ""
    read -p "종료할 PID 입력: " TARGET_PID
    
    if [ -z "$TARGET_PID" ]; then
        echo "취소되었습니다."
        return
    fi
    
    echo "PID $TARGET_PID 종료 중..."
    kill -TERM $TARGET_PID
    echo "완료! 해당 연결이 종료됩니다."
}

# 메인 루프
while true; do
    echo ""
    show_menu
    read -p "선택: " choice
    echo ""
    
    case $choice in
        1) case_1 ;;
        2) case_2 ;;
        3) case_3 ;;
        4) case_4 ;;
        5) case_5 ;;
        6) case_6 ;;
        7) case_7 ;;
        0) echo "종료합니다."; exit 0 ;;
        *) echo "잘못된 선택입니다." ;;
    esac
    
    read -p "계속하려면 Enter..."
done
