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

실측 확인된 연속 조회(페이지네이션) 규칙 (2026-09-28, t8465):
- 첫 페이지: `edate="99999999"`, etime/cts 공백 → 최신 qrycnt봉 + OutBlock에 `cts_date`/`cts_time`.
- 다음 페이지: **edate=이전 cts_date, etime=이전 cts_time** (cts 필드는 공백). InBlock cts 필드에 넣는
  방식은 이 서버에서 "해당자료가 없습니다" 또는 첫 페이지 반복으로 동작하지 않는다.
- 페이지끼리 세션 경계에서 최대 ~100봉 겹침 — 주입 층에서 시각 기준으로 중복 거부한다.
- 비압축(comp_yn=N)은 1회 최대 500건, 압축(Y)은 2000건 (명세). 현재 비압축 500건 페이지 사용.
- **차트 TR은 1 TPS**: 간격 없이 연속 호출하면 HTTP 500. 어댑터가 최소 1.1초 간격을 강제한다.

| 용도 | TR | 경로 | TPS | 검증 |
|---|---|---|---|---|
| 주식 차트 N분 | `t8412` | `/stock/chart` | 1 | ✅ 005930 1분봉 |
| 주식 차트 일주월년 | `t8410` | `/stock/chart` | 1 | ✅ 005930 일봉 (2026-09-30) |
| 선물 차트 N분 | `t8465` | `/futureoption/chart` | 1 | ✅ A016C000 1분봉 |
| 선물 차트 일주월 | `t8466` | `/futureoption/chart` | 1 | ✅ A016C000 일봉 (2026-09-29) |
| 코스피200선물 마스터 | `t8467` | `/futureoption/market-data` | 2 | ✅ (gubun 공백 → 13종) |
| 주식 현재가호가 | `t1101` | `/stock/market-data` | 10 | ✅ (구조) |
| 주식 현재가시세 | `t1102` | `/stock/market-data` | 10 | ✅ (구조) |
| 주식 종목조회 | `t8436` | `/stock/etc` | - | ✅ |

**t8412/t8465 InBlock 공통**: `shcode`(String), `ncnt`(Number, 분 단위), `qrycnt`(Number, 비압축 최대 500), `nday`, `sdate`, `stime`, `edate`(필수, `"99999999"`=당일), `etime`, `cts_date`, `cts_time`, `comp_yn`("N").
**OutBlock1 필드(주식)**: `date`, `time`, `open`, `high`, `low`, `close`(Number), `jdiff_vol`, `value`, `jongchk`, `rate`, `sign`.
**OutBlock1 필드(선물)**: 동일하나 가격은 **문자열**(소수점 포함, 예: `"1094.00"`) + `openyak`(미결제약정) 추가.
- 가격 스케일: 주식은 1(정수), 코스피200선물은 0.01틱 → raw = round(v×100).
- 세션: OutBlock의 `s_time`/`e_time`(예: 선물 084500~154500)이 해당 상품의 장 시간을 알려준다.
- 세션 커버리지 실측 (2026-09-28): t8412는 **NXT 시간외까지 포함**, t8465는 **주간 세션만**
  (야간 18:00~05:00 봉 미포함, 일 411봉 = 08:46 프리장~15:35; 15:35~45 마감 단일가 구간 봉 없음).
  야간선물 과거 봉은 별도 TR(t8461)을 쓴다 (아래 참조, 2026-09-28 구현 완료).
- **NXT 프리마켓(08:00~09:00)은 t8412에 없다** (2026-10-01 08:1x~08:2x 실측): t8412가 전일 20:00
  봉 다음에 당일 09:01 스텁 봉(거래량 0, 가격은 당시 참조가)으로 점프한다. 프리마켓 중에는 과거봉도
  실시간 봉도 얻을 수 없다 — 09:00 정규장부터 정상. NXT **애프터마켓(15:40~20:00)은 포함**되므로
  비대칭에 주의. 선물은 프리장(08:45~) 봉이 t8465에 있어 08:45부터 표시된다.
- **t8412 1분봉 히스토리 상한 ≈ 최근 890봉** (2026-09-28 실측): 당일 500 + 전일 391 = 891봉.
  sdate/stime 구간 지정·edate/etime 앵커·InBlock cts 모두 그 이전으로 못 간다 (반복 또는 무시).
  즉 주식 1분 백필은 API상 최근 약 1.2일치(NXT 720봉/일 기준)가 한계다.

**t8410/t8466 (일주월 차트) 실측 규칙** (2026-09-29 t8466 A016C000 일봉, 2026-09-30 t8410 005930 일봉):
- InBlock: `shcode`, `gubun`("2"=일봉), `qrycnt`, `sdate`, `edate`("99999999"=당일), `cts_date`, **`comp_yn`("N") 필수** — 빠뜨리면 `rsp_msg "압축여부 구분코드 오류"` + 빈 OutBlock1. 주식 t8410은 `sujung`("Y"=수정주가) 추가.
- 응답 OutBlock1은 **오름차순(과거→최신)** — 분봉 TR과 행 순서가 반대. **당일 진행 중 일봉도 포함**된다(close는 현재가 수준). `date`는 "YYYYMMDD" 문자열. 가격 스케일은 분봉과 같다: t8410은 **Number 정수**, t8466은 문자열 소수.
- t8410 OutBlock1 필드 실측: `date`, `open`, `high`, `low`, `close`, `jdiff_vol`, `value`, `jongchk`, `pricechk`, `ratevalue`, `rate`, `sign`.
- 페이지네이션: OutBlock `cts_date`는 분봉과 같은 규칙(다음 페이지 edate로 사용).

**t8461 (KRX야간파생 틱분별, 야간 1분봉) 실측 규칙**:
- InBlock: `focode`(String), `cgubun:"B"`(분차트), **`bgubun`은 명세와 달리 String `"1"`**(Number는 IGW40011),
  `cnt`는 Number **최대 999** (1000 이상 IGW40011, 연속 조회 키 없음 — 최대 약 1.4 세션만 조회 가능).
- 응답은 `t8461OutBlock1` **최신→과거 내림차순**, 요약 OutBlock 없음.
- 행에 **날짜 필드가 없고 `chetime`(HHMMSS)만** 있다. 야간 세션은 자정을 넘으므로 날짜는 추론이 필요:
  최신 행의 세션 기준일에서 뒤로 걸으며 저녁(18:00~)→아침 전이마다 기준일을 직전 **거래일**로 옮긴다.
  평일 추정은 추석 같은 연휴에서 어긋난다 (2026-09-28: 09-24/25 추석인데 09-23 세션 봉을 09-25/26으로
  오표기한 사건 — 가격 연속성 비교로 규명). **반드시 t8465 주간 봉의 실제 거래일을 골격으로 쓴다.**
- 가격 필드: `price`(종가)/`open`/`high`/`low` 문자열, 봉 거래량은 `cvolume`(Number) — 누적은 `volume`.

## 4. 실시간 (WebSocket)

구독 등록: `{"header":{"token":TOKEN,"tr_type":"3"},"body":{"tr_cd":"...","tr_key":"..."}}` → ACK(`rsp_cd 00000`) 후 푸시.

| 채널 | 내용 | 검증 |
|---|---|---|
| `S3_` | KOSPI 체결 (tr_key=종목코드) — **주식 기본 채널. NXT 시간외 체결도 이 채널로 수신됨** | ✅ 정규장 틱, ✅ NXT 애프터(19:45~19:59 실측) |
| `H1_` / `HA_` | KOSPI/KOSDAQ 호가잔량 — **주식 기본 채널** | 명세 + 정규장 실측 |
| `US3`/`UH1` | 통합(KRX+NXT) 체결/호가잔량 | ⚠ 구독 ACK는 정상이나 실측 구간(16:00, 19:45)에서 데이터 무수신 — 사용 보류 |
| `NS3`/`NH1` | NXT 전용 체결/호가잔량 | ⚠ US3와 동일하게 무수신 — 사용 보류 |
| `K3_` | KOSDAQ 체결 | 명세 |
| `FC9` | KOSPI200선물 체결 — **주간 세션 채널** | ✅ ACK (당시 틱 미수신) |
| `FH9` | KOSPI200선물 호가 — **주간 세션 채널** | 명세 |
| `DC0` | **KRX야간파생 체결 — 야간선물(18:00~익일05:00) 채널** | ✅ 2026-09-28 20:1x 실측 (A016C000, price/cvolume/chetime) |
| `DH0` | **KRX야간파생 호가 — 야간선물 채널** | ✅ 실측 (totbidrem/totofferrem 총잔량 존재) |
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
- 2026-10-01 프리마켓(08:16~08:21) 실측: S3_/NS3/US3 모두 구독 ACK(00000) 후 틱 무수신, 같은 시각
  t1102는 volume 0·호가 없음(KRX 미개장이라 NXT 활동 판별 불가). 프리마켓 중에는 어느 RT 채널로도
  주식 체결을 받을 수 없다는 결론 — 09:00 정규장부터 S3_ 정상 수신.

S3_ 실측 body 필드: `price`, `cvolume`(개별 체결량), `mdchecnt`/`mschecnt`(매도/매수 체결건수), `mdvolume`(누적거래량), `offerho`/`bidho`, `sign`, `cpower`(체결강도) 등.
- `cvolume`은 **개별 체결량**, `mdvolume`은 누적 — Tick 모델의 `volume_meaning`과 매핑 시 구분한다.
- 선물 가격은 소수점 문자열.

H1_ 실측 body 필드 (2026-09-28 005930 확인):
- **총매수잔량 `totbidrem`, 총매도잔량 `totofferrem`** — 예스랭귀지 Bids/Asks에 각각 대응 (obd2 입력).
- 호가 시각 `hotime` (HHMMSS).
- 단계별: `bidho1..10`/`bidrem1..10` (매수 호가/잔량), `offerho1..10`/`offerrem1..10` (매도 호가/잔량). 값은 문자열.
- `volume`(누적거래량), `midsumremgubun`, `donsigubun` 등 부가 필드.

해외선물 실시간 (2026-10-01 ESZ26 실측, OVC 초당 ~12건 수신 확인):
- **tr_key는 8자리 고정 — 심볼 우측 공백 패딩** (`"ESZ26   "`). 미패딩 시 `rsp_cd 10009` 거절.
  OVC/OVH/WOC/WOH 모두 같은 규칙 (어댑터가 구독 시 자동 패딩, 수신 tr_key 비교는 후행 공백 무시).
- OVC body 필드 (전부 문자열): `symbol`, `curpr`(현재가), `trdq`(**개별 체결량** — 누적은 `totq`),
  `cgubun`(체결구분 "+"/"-"), `trdtm`/`ovsdate`(**거래소 현지** 시각/날짜),
  **`kortm`/`kordate`(한국 시각/날짜 — 이벤트 시각은 이걸 쓴다)**, `ovsmkend`(세션 일자), `lSeq`.
- OVH body 필드: `hotime`(**거래소 현지 시각** — KST 22:33 수신 메시지에 "083301"(시카고)로 온다.
  한국 날짜 필드가 없어 epoch 변환이 불가하므로 호가 이벤트 시각은 수신 시각을 쓴다),
  5단계 `offerho1..5`/`offerrem1..5`/`offerno1..5`, `bidho1..5`/`bidrem1..5`/`bidno1..5`,
  총잔량 `totofferrem`/`totbidrem`, 건수 `totoffercnt`/`totbidcnt`.

해외선물 REST (2026-10-01 실측 — **이 계정은 CME 차단**):
- `o3101`(해외선물마스터조회, `/overseas-futureoption/market-data`, InBlock gubun 공백):
  이 계정은 HKEX(42)+LME(33) 75행만 온다 (CME 없음). 행 필드: `Symbol`, `SymbolNm`, `BscGdsCd`,
  `ExchCd`, `UntPrc`(최소가격변동 = 틱, 실제 가격 단위 문자열), `DlStrtTm`/`DlEndTm`,
  `DotGb`(가격 소수 자리, Number — 실측 분포: 0→26종, 1→3종, 2→39종, 4→7종. CUS=4, HSI=0).
  **`DotGb > 2`(소수 3자리 이상)는 ×100 raw 스케일에서 가격이 절단되므로 레지스트리에 등록하지 않는다**
  (예: CUS "6.7124" → 671, 24틱 오차 — 정적 표의 배제 원칙과 동일. 기동 시 "정밀도 배제 N종" 요약 1줄).
  CME 계열은 내장 정적 표(ls_ovsfut)로 해결한다.
- `o3103`(해외선물차트 분봉, `/overseas-futureoption/chart`): InBlock `shcode`/`ncnt`/`readcnt`(500 성공)/
  `cts_date`/`cts_time`. 응답 OutBlock1은 **최신→과거 내림차순**, `date`/`time`은 **거래소 현지**,
  OHLC는 문자열, `volume`은 Number. OutBlock `timediff`가 시차(현지 = KST + timediff 시간, HKEX는 -1).
  **연속 조회는 동작하지 않는다**: cts_date/cts_time 입력은 무시(첫 페이지 반복), tr_cont_key는
  "해당자료가 없습니다" — v1은 1페이지만 쓴다. **CME 종목(ESZ26)은 rsp_cd 00000 + "해당자료가 없습니다."**
  (OutBlock 자체가 없음) — 백필 불가 계정이라 실시간 전용(RT-only)으로 억제한다.
  일시적 빈 페이지로 RT-only가 오탐 고정될 수 있으므로, 권한 해소·오탐 의심 시 회복은
  **수동 unwatch/watch** (watch 때마다 o3103을 다시 시도한다).
- `o3108`(해외선물차트 일주월): HKEX는 응답 옴. 해외선물의 ⑤ 체인 일봉 프라임은 v1에서 스킵.

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
