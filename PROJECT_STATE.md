# PROJECT STATE

기준 명세: [C_TRADING_MASTER_PLAN.md](C_TRADING_MASTER_PLAN.md)
갱신일: 2026-09-28

## 현재 단계

단계 3 (기본 지표와 미래곡선의 독립 계산부) — 1차 완료 (제공된 원본 전부 포팅)

## 완료 항목

- CMake/Ninja 프리셋(Linux `default`, Windows `windows`, 크로스 `windows-cross`), `trading-engine --help/--version`
- core 정적 라이브러리 (외부 의존성 없음, 표준 라이브러리+libm만):
  - model: units(스케일 정수·유효성), time_us(UTC µs), civil_time(역법 변환), instrument, envelope(품질 플래그)
  - market: candle, tick, session(야간장·트레이딩 데이), ring(제자리 갱신·가변 접근),
    bar_builder(중복 제거·타이머/세션 확정·늦은 입력 정정·무거래 채움), bar_aggregator(세션 정렬·일봉)
  - indicators (계획서 §9 계약 + docs/yeslanguage_mapping.md §5 순서):
    - linreg — OLS 유틸 (V3·Htf 공유, 이력 비공유)
    - atr — Wilder 평활 (원본 내장 ATR(14) 산식 불명 → Wilder 채택, 버전 기록 대상)
    - htf_curve_predict — WSF_Htf_CurvePredict 포팅 (19표본, 원시 외삽, 방향=가격 수치, 워밍업 19/20)
    - linreg_predict_v4 — WSF_Mtf_LinRegPredictV4 포팅 (R² 계수, ATR 기울기/가속도/변위 클램프, 같은 방향 가속 억제)
    - orderbook_dir_v2 — WSF_OrderBookDirectionV2 포팅 (선물/주식 부호, 일자 리셋, 무효 시 미갱신)
    - linreg_v3 — WSF_Mtf_LinRegV3(제공 신형) 포팅 (주기별 n 자동 선택, 세션 리셋, 낶부 V4 연계)
    - past_prediction — 과거예측 검증 (가변 룩백 [예측봉수k], 세션 교차 무효화)
    - memory_lines — 회귀기억선(방향 전환 고정·호가 관성·틱 양자화) + 지속선(연속 봉수 == 조건 저장)
    - market_profile — 분봉 마켓 (세션 봉수, VWAP, 가중표준편차, 중심단계)
    - score_1m — 통합 점수 (방향 0 → −2 분기 보존, 세션 첫 봉 비교 항 페널티)
    - daily_linreg_trend_v1 — WSF_Daily_LinRegTrendV1 포팅 (무상태 추세 판정)
    - daily_align_v2 — WSF_1m_DailyAlignV2 포팅 (합의·일반장/갭장 가감점·큰 갭 비중 복원)
    - gap_regime_v1 — WSF_GapRegimeV1 포팅 (완성 세션 TR 평균·갭 등급/비중·자정 경과분)
    - daily_trend_link_v1 — WSF_1m_DailyTrendLinkV1 포팅 (세션 집계 완성 일봉·1봉 투영 회귀)
    - daily_market_profile_v1 — WSF_DailyMarketProfileV1 포팅 (기간 VWAP·세션 앵커 없음)
    - auto_session_adx_v1 — WSF_AutoSessionADXV1 포팅 (Wilder ADX, 원본에서 죽은 체인)
  - 구형 V3 확인: 신형과 회귀 코어 동일 → linreg_v3가 구형과 동치 (P01 해소)
  - 호가 V1 = V2와 동일 로직 → obd2 재사용 (P05 해소)
- adapters/history: replay — 논리 시간 재생
- CTest 23개 전부 통과

## 검증 결과

- `ctest --preset default` — 23/23 통과 (Linux, GCC 15.3.0, 경고 0)
- Windows 크로스 컴파일 — 경고 0, exe 생성. Windows exe 실행 확인(사용자, 2026-09-28). 네이티브 ctest 미검증
- 완료 수준 표기(계획서 §10.4): 제공 원본 포팅은 '부분 구현+수식 검증' 단계.
  원본 런타임 비교 자료(HTS 출력)가 없어 '봉 확정 출력 비교' 이상은 미검증

## 완료된 분석

- YesLanguage 원본 6개 전수 분석: docs/yeslanguage_mapping.md (호출 그래프, P01~P06 갱신, 미제공 함수 영향)

## 바로 다음 작업

- 단계 5 (Node 대시보드) — 1차 완료: WS 브리지·차트·화면틀 CRUD·인증. 잔여: 다중 패널/연결 그룹 UI, 종목 전환(generation) 실제 다중 종목 적용, 전략·계좌·로그 화면
- 단계 6 LS 읽기 사전 확인 가능
- 참고: replay는 완료 후 프로세스 종료. 대시보드와 지속 연결은 live/paper 모드(단계 6 이후)에서 의미 있음

## 완료 항목 (단계 4 + 런타임 + 대시보드 1차)

- adapters/storage (SQLite): 스키마 13테이블, 체결 트랜잭션, 명령 중복 검사
- adapters/ipc (libzmq + yyjson): ROUTER/DEALER + PUB/SUB, JSON 계약, 순번 추적
- traderctl: 11개 명령, 종료 코드 계약
- runtime/engine: 틱→봉→지표→상태 스트림, `--replay` 모드
- web/ (Node 24, ES Modules): HTTP 정적·REST(/api/status 프록시, /api/workspaces CRUD),
  WebSocket 브리지(엔진 상태 → 브라우저, restart/gap 감지 시 초기화), 인증 토큰+Origin 검사,
  화면틀 원자 저장(schema_version 1), lightweight-charts 4.2.3 차트(캔들·회귀선·예측선, 예측은 생성 시점 표기)
- CTest 30개 + web 테스트 5개 전부 통과

## 차단·미결 사항

- 미제공 원본 함수 6개 (`WSF_1m_DailyTrendLinkV1` 등, 계획서 §10.2) — 원본 추가 전까지 `MISSING_DEPENDENCY`
- HTS 기준 출력 묶음 없음 — 전체 원본 일치 검증은 별도 상태로 기록 예정
- Git 원격 저장소 미연결
- live 실거래 실행 여부: 사용자 결정 사항 (구현 에이전트가 임의 실행하지 않음)
