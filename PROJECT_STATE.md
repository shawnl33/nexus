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
- adapters/history: replay — 논리 시간 재생
- CTest 13개 전부 통과: ring, units, model, civil_time, session, bar_builder, aggregator, replay,
  linreg, htf_curve, lp4, obd2, lr3

## 검증 결과

- `ctest --preset default` — 13/13 통과 (Linux, GCC 15.3.0, 경고 0)
- Windows 크로스 컴파일 — 경고 0, exe 생성. Windows exe 실행 확인(사용자, 2026-09-28). 네이티브 ctest 미검증
- 완료 수준 표기(계획서 §10.4): 제공 원본 포팅은 '부분 구현+수식 검증' 단계.
  원본 런타임 비교 자료(HTS 출력)가 없어 '봉 확정 출력 비교' 이상은 미검증

## 완료된 분석

- YesLanguage 원본 6개 전수 분석: docs/yeslanguage_mapping.md (호출 그래프, P01~P06 갱신, 미제공 함수 영향)

## 바로 다음 작업

- 단계 3 잔여: 메인 지표의 독립 계산부(과거예측 검증 가변 룩백, 고정 기억선/지속선 상태 기계, 마켓 VWAP, 통합 점수) — 필요 시
- 단계 4: 저장소·기록·IPC·CLI
- 병행 가능: 단계 6 LS 읽기 사전 확인 (ATR 산식·Bids/Asks 범위 등 '불명' 해소에도 필요)

## 차단·미결 사항

- 미제공 원본 함수 6개 (`WSF_1m_DailyTrendLinkV1` 등, 계획서 §10.2) — 원본 추가 전까지 `MISSING_DEPENDENCY`
- HTS 기준 출력 묶음 없음 — 전체 원본 일치 검증은 별도 상태로 기록 예정
- Git 원격 저장소 미연결
- live 실거래 실행 여부: 사용자 결정 사항 (구현 에이전트가 임의 실행하지 않음)
