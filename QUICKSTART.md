# 빠른 시작 가이드

## 1. 빌드

```bash
cd tcp_proxy
make
```

## 2. 기본 사용

### 서버2 프록시
```bash
# 터미널 1: 서버2 실행
./test_server2

# 터미널 2: 프록시 실행
cd tcp_proxy
./bin/tcp_proxy -p 9999 -t 127.0.0.1:8080

# 터미널 3: 클라이언트 테스트
./test_client 127.0.0.1 9999
```

### DB 프록시 (MySQL 예시)
```bash
# MySQL이 3306에서 실행 중이라면
./bin/tcp_proxy -p 10000 -t 127.0.0.1:3306

# 또는 설정 파일 사용
./bin/tcp_proxy -c config/db_proxy.conf

# MySQL 클라이언트로 접속
mysql -h 127.0.0.1 -P 10000 -u root -p
```

## 3. 필터 사용

### 네트워크 지연 시뮬레이션
```bash
# 100ms 지연
./bin/tcp_proxy -p 9999 -t 127.0.0.1:8080 -d 100
```

### 패킷 손실 시뮬레이션
```bash
# 10% 패킷 드롭
./bin/tcp_proxy -p 9999 -t 127.0.0.1:8080 -r 0.1
```

### 대역폭 제한
```bash
# 10KB/s로 제한
./bin/tcp_proxy -p 9999 -t 127.0.0.1:8080 -b 10240
```

### 복합 필터
```bash
# 느린 네트워크 시뮬레이션
./bin/tcp_proxy -p 9999 -t 127.0.0.1:8080 -d 50 -r 0.05 -b 10240
```

## 4. 서버1 코드 수정

### 기존 코드
```c
// 서버2 직접 연결
connect(sock, "서버2주소", 8080);

// DB 직접 연결
mysql_connect(db, "DB주소", "user", "pass", "dbname", 3306, ...);
```

### 변경 코드 (프록시 경유)
```c
// 서버2는 프록시로
connect(sock, "프록시주소", 9999);

// DB도 프록시로
mysql_connect(db, "프록시주소", "user", "pass", "dbname", 10000, ...);
```

## 5. 로그 확인

```bash
# 실시간 로그 보기
tail -f tcp_proxy/logs/proxy.log

# 전체 로그 보기
cat tcp_proxy/logs/proxy.log
```

## 6. 여러 프록시 동시 실행

```bash
# 터미널 1: 서버2 프록시
./bin/tcp_proxy -p 9999 -t server2.com:8080 -l logs/server2.log

# 터미널 2: DB 프록시
./bin/tcp_proxy -p 10000 -t db.com:3306 -l logs/db.log

# 터미널 3: 다른 서비스 프록시
./bin/tcp_proxy -p 10001 -t redis.com:6379 -l logs/redis.log
```

## 디버그 모드

```bash
# 상세한 로그 출력
./bin/tcp_proxy -p 9999 -t 127.0.0.1:8080 -v
```

## 문제 해결

### 포트 사용 중
```bash
lsof -i :9999
kill -9 <PID>
```

### 재빌드
```bash
make clean
make
```

## 주요 파일

- `bin/tcp_proxy` - 실행 파일
- `config/proxy.conf` - 기본 설정
- `config/db_proxy.conf` - DB 프록시 설정
- `logs/proxy.log` - 로그 파일
- `README.md` - 상세 문서
