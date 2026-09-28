# LS OPEN API 매핑 문서

작성일: 2026-09-28 (월요일, 장중)
상태: **실제 API 호출로 검증 완료** (사용자 키, `.env`의 `LS_APP_KEY`/`LS_SECRET_KEY`)
공식 기계가독 명세: https://openapi.ls-sec.co.kr/api/apis/public/tr-guides.json (버전 `20260923083222998`, 41개 API 그룹)

> 계획서 §15의 요구사항: 구현 전 실제 API 명세 확인. 이 문서의 모든 항목은 당일 실제 호출 응답으로 검증했다.

## 1. 공통

| 항목 | 값 | 검증 |
|---|---|---|
| REST 도메인 | `https://openapi.ls-sec.co.kr:8080` | ✅ |
| WebSocket | `wss://openapi.ls-sec.co.kr:9443/websocket` | ✅ |
| 호출 방식 | **POST + JSON body** (`tXXXXInBlock` 래퍼). GET+query는 "해당자료가 없습니다"만 반환 | ✅ |
| 필수 헤더 | `Authorization: Bearer <token>`, `tr_cd: <TR코드>`, `tr_cont: N`, `Content-Type: application/json; charset=utf-8` | ✅ |
| 숫자 필드 | `Number` 타입 필드는 JSON 숫자로 전송 (문자열이면 `...data type을 확인하세요` 오류) | ✅ |
| 연속 조회 | OutBlock의 `cts_date`/`cts_time`을 다음 호출의 InBlock에 설정, 헤더 `tr_cont: Y` | ✅ |
| 성공 응답 | `rsp_cd: "00000"`. **주의**: "해당자료가 없습니다"도 `00000` — OutBlock 배열 유무로 빈 결과를 구분 | ✅ |
| 오류 구분 | HTTP 상태 / `rsp_cd`(IGW...: 게이트웨이 오류) / 전송 오류 / 파싱 오류를 분리해 기록 | ✅ |

## 2. 인증

| 항목 | 값 | 검증 |
|---|---|---|
| 토큰 발급 | `POST /oauth2/token`, form-urlencoded: `grant_type=client_credentials`, `appkey`, `appsecretkey`, `scope=oob` | ✅ HTTP 200 |
| 응답 | `access_token`(380자), `token_type=Bearer`, `expires_in`(당일 실측 66031초 ≈ 18시간) | ✅ |
| 폐기 | `/oauth2/revoke` 존재 (명세상) | 미검증 |
| 키 로딩 | 환경변수 `LS_APP_KEY`, `LS_SECRET_KEY` (`.env`, Git 제외). 코드·로그에 기록 금지 | ✅ |

## 3. 과거 데이터 (읽기)

| 용도 | TR | 경로 | TPS | 검증 |
|---|---|---|---|---|
| 주식 차트 N분 | `t8412` | `/stock/chart` | 1 | ✅ 005930 1분봉 |
| 주식 차트 일주월년 | `t8410` | `/stock/chart` | 1 | 미검증(명세 확보) |
| 선물 차트 N분 | `t8465` | `/futureoption/chart` | 1 | ✅ A016C000 1분봉 |
| 선물 차트 일주월 | `t8466` | `/futureoption/chart` | 1 | 미검증 |
| 코스피200선물 마스터 | `t8467` | `/futureoption/market-data` | 2 | ✅ (gubun 공백 → 13종) |
| 주식 현재가호가 | `t1101` | `/stock/market-data` | 10 | ✅ (구조) |
| 주식 현재가시세 | `t1102` | `/stock/market-data` | 10 | ✅ (구조) |
| 주식 종목조회 | `t8436` | `/stock/etc` | - | ✅ |

**t8412/t8465 InBlock 공통**: `shcode`(String), `ncnt`(Number, 분 단위), `qrycnt`(Number, 비압축 최대 500), `nday`, `sdate`, `stime`, `edate`(필수, `"99999999"`=당일), `etime`, `cts_date`, `cts_time`, `comp_yn`("N").
**OutBlock1 필드(주식)**: `date`, `time`, `open`, `high`, `low`, `close`(Number), `jdiff_vol`, `value`, `jongchk`, `rate`, `sign`.
**OutBlock1 필드(선물)**: 동일하나 가격은 **문자열**(소수점 포함, 예: `"1094.00"`) + `openyak`(미결제약정) 추가.
- 가격 스케일: 주식은 1(정수), 코스피200선물은 0.01틱 → raw = round(v×100).
- 세션: OutBlock의 `s_time`/`e_time`(예: 선물 084500~154500)이 해당 상품의 장 시간을 알려준다.

## 4. 실시간 (WebSocket)

구독 등록: `{"header":{"token":TOKEN,"tr_type":"3"},"body":{"tr_cd":"...","tr_key":"..."}}` → ACK(`rsp_cd 00000`) 후 푸시.

| 채널 | 내용 | 검증 |
|---|---|---|
| `S3_` | KOSPI 체결 (tr_key=종목코드) — **주식 기본 채널. NXT 시간외 체결도 이 채널로 수신됨** | ✅ 정규장 틱, ✅ NXT 애프터(19:45~19:59 실측) |
| `H1_` / `HA_` | KOSPI/KOSDAQ 호가잔량 — **주식 기본 채널** | 명세 + 정규장 실측 |
| `US3`/`UH1` | 통합(KRX+NXT) 체결/호가잔량 | ⚠ 구독 ACK는 정상이나 실측 구간(16:00, 19:45)에서 데이터 무수신 — 사용 보류 |
| `NS3`/`NH1` | NXT 전용 체결/호가잔량 | ⚠ US3와 동일하게 무수신 — 사용 보류 |
| `K3_` | KOSDAQ 체결 | 명세 |
| `FC9` | KOSPI200선물 체결 | ✅ ACK (당시 틱 미수신) |
| `FH9` | KOSPI200선물 호가 | 명세 |
| `C01`/`O01`/`H01` | 선물 주문체결/접수/정정취소 | 단계 9에서 |
| `SC0`~`SC4` | 주식 주문 접수/체결/정정/취소/거부 | 단계 9에서 |
| `JIF` | 장운영정보 | 명세 |

통합/NXT 채널(US3/UH1/NS3/NH1, 공식 명세 tr-guides.json 확인):
- **tr_key는 10자리 고정** — "단축코드 7자리 + 공백 3자리". 6자리 종목코드는 공백으로 우측 패딩한다
  (어댑터가 구독 시 자동 패딩, 수신 tr_key 비교는 후행 공백 무시).
- US3 body: S3_와 필드명 동일, 수치 필드는 Number 타입. UH1 body: 잔량은 시장별 접두사
  (`krx_*`/`nxt_*`/`unt_*`), 통합 총잔량은 `unt_totbidrem`/`unt_totofferrem`, 가격 단계는 무접두사.
- 2026-09-28 실측: 패딩한 구독은 ACK(00000)되나 두 시간대 모두 데이터가 오지 않았다.
  반면 같은 시각 S3_에는 NXT 애프터마켓 체결이 유입됐다. 통합 채널 활성 조건은 미확인 — 파서는 유지하되 구독은 S3_/H1_.

S3_ 실측 body 필드: `price`, `cvolume`(개별 체결량), `mdchecnt`/`mschecnt`(매도/매수 체결건수), `mdvolume`(누적거래량), `offerho`/`bidho`, `sign`, `cpower`(체결강도) 등.
- `cvolume`은 **개별 체결량**, `mdvolume`은 누적 — Tick 모델의 `volume_meaning`과 매핑 시 구분한다.
- 선물 가격은 소수점 문자열.

H1_ 실측 body 필드 (2026-09-28 005930 확인):
- **총매수잔량 `totbidrem`, 총매도잔량 `totofferrem`** — 예스랭귀지 Bids/Asks에 각각 대응 (obd2 입력).
- 호가 시각 `hotime` (HHMMSS).
- 단계별: `bidho1..10`/`bidrem1..10` (매수 호가/잔량), `offerho1..10`/`offerrem1..10` (매도 호가/잔량). 값은 문자열.
- `volume`(누적거래량), `midsumremgubun`, `donsigubun` 등 부가 필드.

## 5. 계좌 (읽기, 단계 6 후반)

| TR | 내용 | 경로 |
|---|---|---|
| `CSPAQ12200` | 예수금·주문가능금액 | `/stock/accno` |
| `CSPAQ12300` | 잔고(BEP) 조회 | `/stock/accno` |
| `t0424` | 주식잔고2 | `/stock/accno` |
| `t0425` | 주식체결/미체결 | `/stock/accno` |
| `CSPAQ13700` | 주문체결내역 | `/stock/accno` |

## 6. 주문 (단계 9, 지금은 구현 금지)

| TR | 내용 | TPS |
|---|---|---|
| `CSPAT00601` | 현물 주문 | 10 |
| `CSPAT00701` | 현물 정정주문 | 3 |
| `CSPAT00801` | 현물 취소주문 | 3 |

- 실전/모의는 토큰 종류로 구분 (모의투자 도메인 별도 — 명세상).
- 중복 방지 필드·조회 방법은 주문 구현 전에 별도 확인 (계획서 §14, §15).

## 7. 확인된 특이 사항 (어댑터가 처리해야 함)

1. **POST + `tXXXXInBlock` 래퍼 필수**. GET은 조용히 빈 결과를 준다.
2. `Number` 타입 필드에 문자열을 넣으면 `IGW40011` 오류.
3. `tr_id` 헤더는 구 문서의 잔재 — 현재 게이트웨이는 `tr_cd` 필수 (공식 howto-use 문서와 다름, 이 문서가 정답).
4. 성공(`00000`)인데 데이터 없음(주말·비거래·초기 구간)은 OutBlock 유무로 판단한다.
5. 주식 가격 필드는 Number, 선물 가격 필드는 String(소수점) — 파서가 둘 다 받는다.
6. 차트 TR은 전부 1 TPS — 어댑터에 최소 간격 제한을 둔다 (계획서 §15 제한 관리).
7. 토큰 유효기간은 고정 24시간이 아니라 발급 시각 기준 가변(실측 ~18시간) → `expires_in`을 저장해 갱신 시점을 계산한다.
