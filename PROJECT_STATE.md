# PROJECT STATE

기준 명세: [C_TRADING_MASTER_PLAN.md](C_TRADING_MASTER_PLAN.md)
갱신일: 2026-09-28

## 현재 단계

단계 6 (LS 인증·과거/실시간 읽기) — **완료** (인증·과거 분봉·실시간 구독 전부 실제 검증)

## 완료 항목 (누적)

- **core** (외부 의존성 없음): model(단위·시간·역법·종목·이벤트), market(봉·틱·세션·링·봉빌더·집계),
  indicators(미래곡선 전 함수 포팅 — 원본 13종 + 메인 독립 계산부 전부, docs/yeslanguage_mapping.md)
- **adapters/history**: 논리 시간 replay
- **adapters/storage** (SQLite 3.46.1): 스키마 13테이블 마이그레이션, 체결 트랜잭션, 명령 중복 검사
- **adapters/ipc** (libzmq + yyjson 벤더): ROUTER/DEALER 명령 + PUB/SUB 상태, JSON 계약, 순번 추적
- **traderctl**: 11개 명령, 종료 코드 계약
- **runtime/engine**: 틱→봉→지표→상태 스트림, `trading-engine --replay FILE [--replay-delay MS]`
- **web/** (Node 24, ES Modules): WS 브리지, 차트(lightweight-charts), 화면틀 CRUD·인증, npm test 5개
- **adapters/ls** (libcurl): OAuth 토큰 발급·자동 갱신, t8412/t8465 1분봉 조회·정규화
- **adapters/ls** (libwebsockets 4.3.5 정적): 실시간 구독 S3_/H1_/FC9/FH9, 틱·호가 정규화,
  재연결 백오프·재인증·재구독, 입력 큐 상한·포화 카운트
- **종목 레지스트리** (t8436+t8467 마스터): 유형 판별·검색(ls_master_search), market.instruments 명령(q/limit)
- **대시보드 종목 검색**: GET /api/market 프록시 + 헤더 검색 드롭다운(코드 접두사/종목명 부분 일치, 클릭 전환)
- 실제 API 검증 명세: [docs/ls_api_mapping.md](docs/ls_api_mapping.md)

## 검증 결과

- `ctest --preset default` — **35/35 통과** (ls_live, ls_rt_live는 키 없으면 자동 skip)
- 라이브 검증 (`.env` 로딩 후, 2026-09-28 장중): 토큰 발급, 005930 1분봉, A016C000(코스피200선물) 1분봉,
  S3_ 실시간 틱(가격×100·개별 체결량) 수신 — 전부 성공
- live 명령 응답 지연: 수정 전 3~15초 → 수정 후 **17~27ms** (lws_service timeout 무시 결함 sul wake으로 해결)
- 대시보드 종목 검색 E2E: /api/market?q=0059 → 5종, q=삼성 → 5종, q=A016 → 선물 F 2612 (4314종 레지스트리)
- `cd web && npm test` — 7/7 통과
- Windows 크로스 컴파일 — 경고 0, exe 생성. 네이티브 ctest 미검증
- 미래곡선 완료 수준: '부분 구현+수식 검증' (계획서 §10.4). HTS 출력 비교 자료는 미확보

## 바로 다음 작업

1. 계좌 읽기(CSPAQ12300/12200)·대사 (REST만으로 가능)
2. ~~runtime을 실시간 입력과 연결~~ — **완료**: `--live`/`--live-fut` 모드 (백필 워밍업 + 실시간 틱 + status/engine.stop 명령)
3. ~~호가 구독 연결~~ — **완료**: H1_/FH9 실측 필드(totbidrem/totofferrem) → obd2 실입력, 대시보드 호가 표시
4. ~~다중 종목 지원~~ — **완료**: `market select` 종목 전환 + 종목 레지스트리(마스터 기반 유형 판별) + 대시보드 검색 UI
5. 단계 8: 전략·포트폴리오·리스크·주문·시뮬레이터 (백테스트 체결 모델)

## 차단·미결 사항

- HTS 기준 출력 묶음 없음 — 원본과의 봉 확정 출력 비교는 미검증 상태 유지
- Git 원격 저장소 미연결
- live 실거래 실행 여부: 사용자 결정 사항 (구현 에이전트가 임의 실행하지 않음)
- ~~정적 libwebsockets 간헐 정지~~ — **해결(2026-09-28)**: 근본 원인은 lws 4.3.5가 lws_service의
  timeout 인자를 무시하는 upstream 동작(양수이면 23일로 강제, lib/plat/unix/unix-service.c).
  tr_ls_rt_service가 호출마다 wake sul을 걸어 poll 대기 상한을 보장하도록 수정
- ATR 산식은 YLHelp.pdf 공식 매뉴얼로 확정(SMA of TR). Bids/Asks 단계는 LS 필드로 매핑 예정
