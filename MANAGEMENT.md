# 프록시 서버 관리 가이드

## 관리 도구 사용법

`proxyctl`은 실행 중인 프록시 서버를 관리하기 위한 명령줄 도구입니다.

### 기본 사용법

```bash
./bin/proxyctl [명령] [옵션]
```

### 사용 가능한 명령어

#### 1. 활성 연결 목록 조회

```bash
./bin/proxyctl list
# 또는
./bin/proxyctl ls
```

**출력 예시:**
```
총 3개의 활성 연결:

PID      클라이언트            대상 서버            업로드       다운로드     연결 시간    마지막 활동
========================================================================================================================
12345    192.168.1.100:54321   127.0.0.1:8080      15.32 KB    102.45 KB   2분 30초    5초 전
12346    192.168.1.101:54322   127.0.0.1:8080      8.91 KB     45.67 KB    1분 15초    2초 전
12347    192.168.1.102:54323   127.0.0.1:8080      25.43 KB    198.32 KB   5분 12초    1초 전
```

#### 2. 특정 연결 종료

특정 PID의 연결을 종료합니다 (SIGTERM 전송).

```bash
./bin/proxyctl kill <PID>
```

**예시:**
```bash
./bin/proxyctl kill 12345
# 출력: 성공: PID 12345 종료 시그널 전송 성공
```

#### 3. 특정 연결에 시그널 전송

특정 PID의 프로세스에 원하는 시그널을 전송합니다.

```bash
./bin/proxyctl signal <PID> <시그널>
```

**사용 가능한 시그널:**
- `TERM`, `SIGTERM` - 정상 종료 요청
- `KILL`, `SIGKILL` - 강제 종료
- `STOP`, `SIGSTOP` - 프로세스 일시 정지
- `CONT`, `SIGCONT` - 일시 정지된 프로세스 재개
- `HUP`, `SIGHUP` - Hang up
- `USR1`, `SIGUSR1` - 사용자 정의 시그널 1
- `USR2`, `SIGUSR2` - 사용자 정의 시그널 2

**예시:**
```bash
# 연결 일시 정지
./bin/proxyctl signal 12345 STOP

# 연결 재개
./bin/proxyctl signal 12345 CONT

# 강제 종료
./bin/proxyctl signal 12345 KILL
```

#### 4. 통계 정보 조회

모든 활성 연결의 통계를 요약하여 표시합니다.

```bash
./bin/proxyctl stats
```

**출력 예시:**
```
=== 프록시 서버 통계 ===

활성 연결 수: 3
총 업로드 (클라이언트→서버): 49.66 KB
총 다운로드 (서버→클라이언트): 346.44 KB
총 데이터 전송량: 396.10 KB
```

#### 5. 프록시 서버 종료

전체 프록시 서버를 종료합니다 (확인 필요).

```bash
./bin/proxyctl shutdown
```

**출력:**
```
프록시 서버를 종료하시겠습니까? (yes/no): yes
성공: 프록시 서버 종료 명령 수신
```

### 커스텀 제어 소켓 경로

기본 제어 소켓 경로는 `/tmp/tcp_proxy_control.sock`입니다.
다른 경로를 사용하려면 `-s` 옵션을 사용하세요.

```bash
./bin/proxyctl -s /custom/path/control.sock list
```

## 사용 시나리오

### 시나리오 1: 특정 클라이언트 연결 차단

```bash
# 1. 활성 연결 목록 확인
./bin/proxyctl list

# 2. 차단할 클라이언트의 PID 확인
# 예: PID 12345가 악의적인 클라이언트

# 3. 해당 연결 종료
./bin/proxyctl kill 12345
```

### 시나리오 2: 프록시 서버 모니터링

```bash
# watch 명령과 함께 사용하여 실시간 모니터링
watch -n 2 './bin/proxyctl list'

# 또는 통계 정보 실시간 모니터링
watch -n 5 './bin/proxyctl stats'
```

### 시나리오 3: 디버깅을 위한 연결 일시 정지

```bash
# 1. 문제가 있는 연결 식별
./bin/proxyctl list

# 2. 해당 연결 일시 정지
./bin/proxyctl signal 12345 STOP

# 3. 디버깅 수행...

# 4. 연결 재개
./bin/proxyctl signal 12345 CONT
```

### 시나리오 4: 스크립트를 통한 자동 관리

```bash
#!/bin/bash
# 5분 이상 비활성 연결을 자동으로 종료하는 스크립트

while true; do
    ./bin/proxyctl list | grep -E '5분|시간' | awk '{print $1}' | while read pid; do
        echo "종료: PID $pid (5분 이상 비활성)"
        ./bin/proxyctl kill $pid
    done
    sleep 300  # 5분마다 실행
done
```

## 문제 해결

### 제어 소켓을 찾을 수 없음

**오류:**
```
오류: 제어 소켓을 찾을 수 없습니다: /tmp/tcp_proxy_control.sock
프록시 서버가 실행 중인지 확인하세요.
```

**해결 방법:**
1. 프록시 서버가 실행 중인지 확인
   ```bash
   ps aux | grep tcp_proxy
   ```

2. 제어 소켓 파일이 존재하는지 확인
   ```bash
   ls -la /tmp/tcp_proxy_control.sock
   ```

3. 프록시 서버를 재시작하면 제어 소켓이 자동으로 생성됩니다

### 권한 오류

제어 소켓에 접근 권한이 없는 경우, 프록시 서버를 실행한 사용자와 동일한 권한으로 proxyctl을 실행해야 합니다.

```bash
# root로 프록시 서버를 실행한 경우
sudo ./bin/proxyctl list
```

## 고급 사용법

### Python 스크립트로 제어

`proxyctl`은 Python 스크립트이므로, 직접 import하여 사용할 수도 있습니다.

```python
#!/usr/bin/env python3
import sys
sys.path.insert(0, '/path/to/proxy-server/bin')
from proxyctl import send_request, CMD_LIST_CONNECTIONS

# 연결 목록 가져오기
response = send_request('/tmp/tcp_proxy_control.sock', CMD_LIST_CONNECTIONS)

if response and response['success']:
    for conn in response['connections']:
        print(f"PID: {conn['pid']}, Client: {conn['client_addr']}:{conn['client_port']}")
```

### 로그와 함께 사용

```bash
# 프록시 서버 로그를 tail하면서 연결 상태 모니터링
tail -f logs/proxy.log &
watch -n 3 './bin/proxyctl stats'
```
