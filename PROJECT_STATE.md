# PROJECT STATE

기준 명세: [C_TRADING_MASTER_PLAN.md](C_TRADING_MASTER_PLAN.md)
갱신일: 2026-09-28

## 현재 단계

단계 6 (LS 인증·과거/실시간 읽기) 진행 중 — 인증·과거 분봉 완료, 실시간 WebSocket 어댑터 다음

## 완료 항목 (누적)

- **core** (외부 의존성 없음): model(단위·시간·역법·종목·이벤트), market(봉·틱·세션·링·봉빌더·집계),
  indicators(미래곡선 전 함수 포팅 — 원본 13종 + 메인 독립 계산부 전부, docs/yeslanguage_mapping.md)
- **adapters/history**: 논리 시간 replay
- **adapters/storage** (SQLite 3.46.1): 스키마 13테이블 마이그레이션, 체결 트랜잭션, 명령 중복 검사
- **adapters/ipc** (libzmq + yyjson 벤더): ROUTER/DEALER 명령 + PUB/SUB 상태, JSON 계약, 순번 추적
- **traderctl**: 11개 명령, 종료 코드 계약
- **runtime/engine**: 틱→봉→지표→상태 스트림, `trading-engine --replay FILE [--replay-delay MS]`
- **web/** (Node 24, ES Modules): WS 브리지, 차트(lightweight-charts), 화면틀 CRUD·인증, npm test 5개
- **adapters/ls** (libcurl): OAuth 토큰 발급·자동 갱신, t8412/t8465 1분봉 조회·정규화.
  실제 API 검증 명세: [docs/ls_api_mapping.md](docs/ls_api_mapping.md)

## 검증 결과

- `ctest --preset default` — **32/32 통과** (ls_live는 키 없으면 자동 skip)
- 라이브 검증 (`.env` 로딩 후): 토큰 발급, 005930 1분봉 3건, A016C000(코스피200선물 2612) 1분봉 3건, 실시간 S3_ 틱 수신 — 전부 성공 (2026-09-28)
- `cd web && npm test` — 5/5 통과
- Windows 크로스 컴파일 — 경고 0, exe 생성. 네이티브 ctest 미검증
- 미래곡선 완료 수준: '부분 구현+수식 검증' (계획서 §10.4). HTS 출력 비교 자료는 미확보

## 바로 다음 작업

1. **실시간 WebSocket 어댑터** (단계 6 잔여): S3_/H1_/FC9/FH9 구독·정규화·재접속.
   libwebsockets 미설치 — 시스템 패키지(libwebsockets-dev) 또는 소스 빌드 결정 필요
2. 계좌 읽기(CSPAQ12300/12200)·대사 (단계 6 잔여)
3. 이후: 단계 8 (전략·포트폴리오·리스크·주문·시뮬레이터)

## 차단·미결 사항

- HTS 기준 출력 묶음 없음 — 원본과의 봉 확정 출력 비교는 미검증 상태 유지
- libwebsockets 미설치 (실시간 어댑터 의존성)
- Git 원격 저장소 미연결
- live 실거래 실행 여부: 사용자 결정 사항 (구현 에이전트가 임의 실행하지 않음)
- ATR 산식은 YLHelp.pdf 공식 매뉴얼로 확정(SMA of TR). Bids/Asks 단계는 LS 필드로 매핑 예정
