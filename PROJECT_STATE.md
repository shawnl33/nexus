# PROJECT STATE

기준 명세: [C_TRADING_MASTER_PLAN.md](C_TRADING_MASTER_PLAN.md)
갱신일: 2026-09-28

## 현재 단계

단계 0 (저장소 구성·문서·CMake 골격) — 완료

## 완료 항목

- CMake 3.24+ 프로젝트 골격 (`CMakeLists.txt`), C17 / extensions 비활성 / GCC·MSVC 경고 분리
- `trading-engine` 실행 파일: `--help`, `--version`, `--data-dir`(예약) — `src/app/main.c`
- `.gitignore` (`build/`, `.env`, `.env:Zone.Identifier` 제외)
- `README.md` (빌드·실행 절차, 비밀정보 원칙)

## 검증 결과

- 환경: Linux, CMake 3.28.1, GCC 15.3.0
- `cmake -S . -B build && cmake --build build --config Debug` 통과
- `./build/trading-engine --help` / `--version` 정상 동작
- 테스트: 없음 (계획서 §25에 따라 '테스트 없음'으로 보고, 0개 통과를 성공으로 기록하지 않음)
- Windows(MSVC) 빌드: 미검증

## 바로 다음 작업

- 단계 1: 공통 모델·종목·시간·이벤트·Ring Buffer (`src/core/model/`, `src/core/market/` 시작) + 경계 테스트
- 병행 가능: 단계 6의 LS 읽기 전용 사전 확인 (토큰 발급, TR 매핑 문서화) — `.env`의 키 사용

## 차단·미결 사항

- 미제공 원본 함수 6개 (`WSF_1m_DailyTrendLinkV1` 등, 계획서 §10.2) — 원본 추가 전까지 `MISSING_DEPENDENCY`
- HTS 기준 출력 묶음 없음 — 전체 원본 일치 검증은 별도 상태로 기록 예정
- Git 원격 저장소 미연결
- live 실거래 실행 여부: 사용자 결정 사항 (구현 에이전트가 임의 실행하지 않음)
