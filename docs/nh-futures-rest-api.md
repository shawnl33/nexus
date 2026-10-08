# NH선물 REST API 확인 사항

2026-10-08에 실전 앱키로 조회만 호출해서 확인한 내용이다. 같은 날 포털 종목 마스터 페이지와 `opra_opt.mst.gz`, 해외 분봉 `chart-minute`을 추가로 확인했다. 주문 전송, 정정, 취소는 실행하지 않았다. 다른 프로젝트로 옮길 때 이 문서의 검증 여부를 구분해서 쓴다.

대상은 NH선물(`futures.co.kr`) Open API다. NH투자증권 나무 PLUG(`api.nhplug.com`)와는 다른 시스템이다.

개발자 포털은 `https://apidev.futures.co.kr/#/api/info/response-overview` 이다. 명세 JSON은 `https://apidev.futures.co.kr/dev-center-api/api/guide/detail?apiId={id}&ver=1&apiType=H` 로 받는다.

## 환경

| 구분 | 주소 |
| --- | --- |
| 실전 REST | `https://api.futures.co.kr` |
| 모의 REST | `https://apidemo.futures.co.kr` |
| 실전 웹소켓 | `wss://api.futures.co.kr/trade/ws-stream` |
| 모의 웹소켓 | `wss://apidemo.futures.co.kr/trade/ws-stream` |

이번 앱키는 실전 전용이다. 모의 서버 토큰 발급은 HTTP 400, `EAU024` 이었다. 토큰의 `scopes` 값 `R`은 운영, `S`는 모의투자다.

REST는 모두 POST다. 토큰 발급만 `application/x-www-form-urlencoded` 이고, 나머지 JSON 요청은 `application/json` 이다.

포털 LV1 호출 제한은 `GET /dev-center-api/api/guide/policy-limits` 기준이다. `apiPolicyCode` `O`는 주문 초당 5건, `A`는 계좌 초당 10건, `Q`는 시세 초당 10건이다. SDK 문서의 실시간 구독 한도는 계좌당 20개다.

## 인증

### 접근토큰 발급

`POST /auth-service/v1/token`

```
grant_type=client_credentials&appkey={APP_KEY}&appsecret={SECRET_KEY}
```

헤더는 `Accept: application/json` 과 `Content-Type: application/x-www-form-urlencoded` 이다.

응답에서 확인한 필드:

- `access_token`
- `token_type`: `Bearer`
- `scopes`: `R` 또는 `S`
- `expires_in`: 초. 이번 발급은 `86400`
- `expires_at`: `YYYY-MM-DD HH:MM:SS` 문자열. 시간대 표기는 없었다.

만료 전에 다시 발급하면 새 토큰이 생기지 않고 기존 토큰이 반환된다. 폐기된 뒤에만 새 토큰이 나온다.

이후 호출의 `Authorization` 값은 `Bearer {access_token}` 이다. `Bearer`와 토큰 사이에 공백이 있다.

### 접근토큰 폐기

`POST /auth-service/v1/revoke`

헤더는 `Content-Type: application/json` 과 `Authorization: Bearer {token}` 이다. 본문은 JSON이다.

```json
{ "appkey": "{APP_KEY}", "appsecret": "{SECRET_KEY}" }
```

`application/x-www-form-urlencoded` 로 보내면 HTTP 415 이다.

성공 본문:

```json
{ "timestamp": "2026-10-08 16:29:25", "status": 200, "message": "토큰 폐기 성공" }
```

폐기된 토큰으로 조회하면 HTTP 403, `EG049`, `현재 토큰은 폐기 되었습니다. 토큰을 다시 발급해주세요.`

시세 신청으로 이용 가능 종목이 바뀌어도 이미 발급된 토큰에는 반영되지 않는다. 폐기 후 재발급해야 한다. 이번에도 시세 신청 직후에는 기존 토큰이 `ETR010` 이었고, 폐기 후 새 토큰부터 호가가 열렸다.

## 확인한 오류

| 코드 | HTTP | 의미 |
| --- | --- | --- |
| `EAU024` | 400 | 이 앱키로 모의 서버 토큰 발급 불가 |
| `ETR010` | 400 | 종목 시세 미신청. 메시지에 `미신청거래소::O_SPW` |
| `EG049` | 403 | 폐기된 토큰 |
| `EGW016` | 403 | 이 앱키에 국내 계좌조회 권한 없음 |
| `5033` | 400 | `[주문불가계좌] 계좌입니다. 계좌번호를 확인하세요.` |
| `RT_FAIL` | 웹소켓 | 구독 거절. 시세 미신청과 같은 문구 |
| `RT_INIT` | 웹소켓 | 구독 접수. 현재 호가 스냅샷은 포함하지 않음 |

포털 오류표에서 증거금 부족은 `00040`, `00052`, `00053` 이다. 주문 사고 계좌는 `90016` 이다. `5033`은 그 코드들과 다른, 주문 시스템의 계좌 상태 거절이다. 잔고 0만으로 이 코드가 나온다고 확인하지는 못했다.

## SPXW 종목

NH 품목 코드는 `O_SPW` 이다. 거래소는 CBOE이고, 옵션 시세의 `exch_cd`는 `OCBO` 이다. 영문 이름은 `CBOE S&P 500 Weekly Option`, 한글 이름은 `CBOE S&P 500 위클리 옵션` 이다. 시장에서 말하는 SPXW 위클리이고, 표준 월물 `O_SPX`, CME E-mini 위클리 `O_EW`와 다른 품목이다.

상장 거래소는 Cboe다. 호가 권한 신청 화면의 항목 이름은 OPRA다. API가 거절할 때 찍는 미신청 코드는 `O_SPW` 이다. OPRA나 CBOE 행만 사용중이어도 `O_SPW`가 미신청이면 `ETR010` 이 유지됐다.

월물 코드 예시는 `O_SPW2RV26` 이다. 마스터의 요일 표기는 `2주 목요일`, 만기일은 `20261008` 이다. 같은 주의 금요일 코드 `O_SPW2FV26` 만기일은 `20261009` 이다. API 문서가 이 코드를 월물품목코드라고 부르지만 상품은 위클리다.

개별 종목 코드는 월물 코드에 권리 종류와 행사가를 붙인다.

- 콜 `O_SPW2RV26-C7775.0`
- 풋 `O_SPW2RV26-P7775.0`

체인 조회가 표시한 ATM 플래그는 행사가 7855였고, 그때 콜 호가는 0.45/0.50, 풋 호가는 79.90/80.60 이었다. 같은 시각 콜과 풋 호가가 맞닿은 행사는 7775였다. 행사가 선택은 ATM 플래그가 아니라 콜·풋 호가로 한다.

2026-10-08 17:23 재조회에서도 ATM 플래그는 맞닿은 행사가에서 `0` 이었다. 금요일 월물 `O_SPW2FV26`은 `rem_dy` `2`, 맞닿은 행사가 7785, 콜 `O_SPW2FV26-C7785.0` 매수 23.90 / 매도 24.20, 풋 `O_SPW2FV26-P7785.0` 매수 25.40 / 매도 25.70 이었다. 당일 만기 `O_SPW2RV26`은 `rem_dy` `1`, 맞닿은 행사가 7780, 콜 17.30 / 17.50, 풋 16.20 / 16.40 이었다.

가격 단위는 지수 포인트다. 마스터의 호가 단위는 `0.050000000` 이다. 주문 서버가 그 틱을 받아들이는지는 주문 미실행이라 확인하지 않았다.

### 종목마스터

오늘 만기 코드 목록을 주는 REST API는 확인되지 않았다. 목록은 포털 종목 마스터 페이지의 파일에서 읽는다.

페이지는 `https://apidev.futures.co.kr/#/api/info/market-master` 이다. 다운로드는 `https://apidev.futures.co.kr/guide/master/{파일}` 이다. 받은 파일은 gzip이다. 풀면 UTF-8, 세미콜론 구분, 한 줄이 한 종목이다. 2026-10-08에 페이지의 다운로드를 눌러 확인한 파일은 다음과 같다.

| 화면 이름 | 파일 |
| --- | --- |
| 국내 지수 선물 스프레드 옵션 | `fut_opt.mst.gz` |
| 국내 주식 선물 스프레드 옵션 | `stk_fut_opt.mst.gz` |
| 국내 상품 선물 스프레드 옵션 | `idx_fut_opt.mst.gz` |
| CME 선물 스프레드 | `cme_fut.mst.gz` |
| CME 옵션 | `cme_opt.mst.gz` |
| OPRA 옵션 | `opra_opt.mst.gz` |
| 기타 거래소 옵션 | `etc_opt.mst.gz` |
| 기타 선물 | `etc_fut.mst.gz` |

SPXW는 `opra_opt.mst.gz`에 있다. 같은 날 파일을 풀어 `O_SPW` 행 4520개, 월물 9개를 읽었다. 열 수는 19개이고 포털 OPRA 헤더 순서와 맞다. CME 헤더에는 `lmon`, `lmon_my`가 더 있다. CME 파일의 열 수는 세지 않았다.

| 열 | 헤더 | `O_SPW`에서 확인한 값 |
| --- | --- | --- |
| 0 | `code` | `O_SPW2RV26-C10000.0` 같은 종목코드 |
| 1 | `VenderCode` | 종목코드와 같았다. 실시간 요청에 쓰는 코드 |
| 2 | `ExchName` | `OCBO` |
| 3 | `enam` | 영문명 |
| 4 | `name` | 한글명 |
| 5 | `exch` | `15` (CBOE) |
| 6 | `type` | `O` |
| 7 | `sect` | 지수 계열 `30` |
| 8 | `pind` | `3` (소수점 2자리) |
| 9 | `trdf` | `0` (거래 가능) |
| 10 | `tsiz` | `0.050000000` |
| 11 | `adjv` | `1.000000` |
| 12 | `comd` | `O_SPW` |
| 13 | `yymm` | `202610` |
| 14 | `prev` | 전일종가 |
| 15 | `tval` | 가격변동폭 |
| 16 | `subExch` | 비어 있는 행이 있었다 |
| 17 | `weekDay` | `2주 목요일` |
| 18 | `lastDt` | `20261008` |

2026-10-08 마스터의 `O_SPW` 월물이다. `trdf`는 모두 `0` 이었다.

| 월물 | `lastDt` | `weekDay` | 행 수 |
| --- | --- | --- | --- |
| `O_SPW2RV26` | 20261008 | 2주 목요일 | 496 |
| `O_SPW2FV26` | 20261009 | 2주 금요일 | 596 |
| `O_SPW3MV26` | 20261012 | 3주 월요일 | 466 |
| `O_SPW3TV26` | 20261013 | 3주 화요일 | 422 |
| `O_SPW3WV26` | 20261014 | 3주 수요일 | 422 |
| `O_SPW3RV26` | 20261015 | 3주 목요일 | 422 |
| `O_SPW3FV26` | 20261016 | 3주 금요일 | 938 |
| `O_SPW4MV26` | 20261019 | 4주 월요일 | 392 |
| `O_SPW4TV26` | 20261020 | 4주 화요일 | 366 |

## 시세

개별 호가, 현재가, 주문가능수량, 웹소켓 호가는 `O_SPW` 시세 신청이 된 토큰에서만 된다. 옵션 체인 전체시세는 그 신청 없이 됐다.

시세 신청 화면은 HTS `9726`, WTS `실시간 시세신청` 이다. 신청 후에도 기존 토큰을 폐기하고 다시 발급해야 반영된다. 시세는 월 단위이고, 당월 거래가 없으면 자동연장을 켜 두어도 다음 달에 다시 신청해야 할 수 있다. NH 공개 안내의 시세 비용은 국내선물 무료, CME 월 비용, CME가 아닌 거래소 무료다. OPRA 이용료는 그 문장과 별도이며 9726 화면에 표시된다.

### 해외 옵션 전체시세

`POST /trade/v1/overseas/price-all-option`

```json
{ "exch_cd": "OCBO", "sym": "O_SPW2RV26" }
```

`sym`은 개별 행사가가 아니라 월물 코드다. 응답 `Occurs1` 에서 확인한 필드:

- `occurs1_strk_pric`
- `occurs1_c_sym`, `occurs1_c_last_pric`, `occurs1_c_bid_pric`, `occurs1_c_ask_pric`, `occurs1_c_trd_qty`, `occurs1_c_atm_flag`
- `occurs1_p_sym`, `occurs1_p_last_pric`, `occurs1_p_bid_pric`, `occurs1_p_ask_pric`, `occurs1_p_trd_qty`, `occurs1_p_atm_flag`

상위 필드 `nrec`, `rem_dy` 도 왔다. 만기일 당일 `rem_dy`는 `1` 이었다. 설명은 만기일을 포함한 잔존일이다. 2026-10-08 조회에서 `O_SPW2RV26` 행사가 258개, 범위 3200~10000 이었다. 같은 날 17:23 재조회는 `O_SPW2RV26` 258행, `O_SPW2FV26` 308행이었다. `nrec`는 `0258`, `0308`처럼 0으로 채운 문자열로 왔다.

### 해외 차트(분)

`POST /trade/v1/overseas/chart-minute`

공식 SDK 메서드 이름은 `getOverseasChartMinute` 이다. 경로는 2026-10-08에 호출해서 확인했다. `/trade/v1/overseas/chart/minute`, `/trade/v1/overseas/minute-chart` 는 HTTP 400, `API 경로가 올바르지 않습니다` 였다.

1분봉으로 확인한 요청:

```json
{
  "exch_cd": "OCBO",
  "sym": "O_SPW2FV26-C7785.0",
  "inq_strt_dt": "00000000",
  "strt_tm": "000000",
  "inq_ed_dt": "99999999",
  "ed_tm": "235959",
  "req_qty": "3",
  "dcnt": "1",
  "cidx_yn": "0",
  "nxt_key": ""
}
```

`sym`은 개별 종목코드다. `dcnt` `1`은 1분봉이다. SDK 설명의 기본값은 `5`이고 5분 묶음이다. `req_qty` 기본값은 300, 최대는 9999라고 명세에 있다. 이번 호출은 3건만 요청했다. `cidx_yn`은 `0`이다. SDK는 해외 연결선물을 지원하지 않는다고 적는다.

응답 상위 필드: `nxt_key`, `nrec`, `sym`, `exch_cd`. 이번 응답의 `sym`은 빈 문자열이고 `exch_cd`는 `OCBO` 였다. `nrec`는 `0003` 이었다. `nxt_key` 예시는 `20261008160500999999999999999999` 이다.

`Occurs1` 한 봉에서 확인한 필드:

- `occurs1_ref_dt`: `20261008`
- `occurs1_exch_tm`: `163000`. 시분초이고 응답은 최신 봉이 앞이다
- `occurs1_glb_biz_dt`: `20261008`
- `occurs1_open_pric`, `occurs1_high_pric`, `occurs1_low_pric`, `occurs1_close_pric`
- `occurs1_rt`, `occurs1_diff_rt`
- `occurs1_cum_trd_amt`, `occurs1_cum_trd_qty`

최근 3봉의 `occurs1_cum_trd_qty`가 모두 `2` 였다. 필드 이름이 누적이고, 이 구간에서는 봉마다 달라지지 않았다. 그 봉만의 거래량으로 쓰지 않는다.

받은 3봉:

| `occurs1_exch_tm` | 시가 | 고가 | 저가 | 종가 |
| --- | --- | --- | --- | --- |
| 163000 | 22.50 | 22.50 | 22.50 | 22.50 |
| 162900 | 22.50 | 22.50 | 22.50 | 22.50 |
| 162100 | 23.20 | 23.20 | 23.20 | 23.20 |

국내 분봉 SDK 메서드는 `getDomesticChartMinute` 이다. 필드 이름은 `nght_tp`, `sym`, `inq_strt_dt`, `strt_tm`, `inq_ed_dt`, `ed_tm`, `req_qty`, `dcnt`, `cidx_yn`, `nxt_key`, `fut_cidx_exp_mod` 이다. 국내 분봉 경로는 호출하지 않았다.

### 해외 호가

`POST /trade/v1/overseas/bid-price`

```json
{ "exch_cd": "OCBO", "sym": "O_SPW2RV26-C7775.0" }
```

응답에서 확인한 필드: `sym`, `real_sym`, `last_pric`, `bid_pric_1`, `ask_pric_1`, `bid_qty_1`, `ask_qty_1`, `biz_dt`. `real_sym`은 요청 종목코드와 같았다. 권한 반영 후 콜은 현재가 18.40, 매수 18.30, 매도 18.50 이었고 풋은 현재가 16.20, 매수 16.20, 매도 16.40 이었다. 이후 시각의 콜 호가는 18.10/18.40 으로 바뀌어 있었다.

### 해외 현재가

`POST /trade/v1/overseas/price`

JSON 키는 `Iccurs1` 이다. 소문자 `iccurs1` 은 거부된다. Java SDK 필드명과 다르다.

```json
{
  "Iccurs1": {
    "iccurs1_exch_cd": "OCBO",
    "iccurs1_sym": "O_SPW2RV26-C7775.0"
  }
}
```

응답 `Occurs1[0]` 에서 확인한 필드: `occurs1_sym_nm`, `occurs1_last_pric`, `occurs1_bid_pric`, `occurs1_ask_pric`, `occurs1_cum_trd_qty`, `occurs1_biz_dt`. 종목명은 `CBOE S&P 500 Weekly Option` 이었다.

### 해외 장운영

`POST /trade/v1/overseas/market-operations`

이름이 맞은 요청:

```json
{
  "nxt_key": "",
  "fut_opt_div_cd": "O",
  "prd_grp_cd": "",
  "exch_cd": "",
  "cur_cd": "",
  "prd_cd": "1",
  "prd_nm": "S&P"
}
```

`prd_cd` `1`은 품목명 검색이다. `exch_cd`를 `CBOE`로 두고 `prd_nm`을 `SPX`로 두면 0건이었다. `O_SPW` 행의 값은 다음과 같다.

- `occurs1_prd_cd`: `O_SPW`
- `occurs1_exch_cd`: `CBOE`
- `occurs1_ko_prd_nm`: `CBOE S&P 500 위클리 옵션`
- `occurs1_trd_tm_inf`: `09:15 ~ 22:25`
- `occurs1_trd_tm_inf_2`: `22:30 ~ 05:15`
- `occurs1_nxd_trd_tm_inf`: `05:15 ~ 06:00`

응답에 시간대 표기는 없다.

### 해외 만기일 조회

`POST /trade/v1/overseas/product-expiry`

`sym`은 필수이고, 개별 옵션 종목코드여야 한다. `rem_dy` 기본값은 30이고 기준 잔존일이다. `fut_opt_div_cd`는 공백 전체, `F` 선물, `O` 옵션, `S` 스프레드, `Z` 기타다.

`O_SPW`, `SPXW`, `O_SPW2RV26`, `O_ES`, `ES`, `CL` 은 HTTP 200, `nrec` 0 이었다. 아래처럼 행사가가 있는 종목을 넣으면 1건이 나온다.

```json
{
  "nxt_key": "",
  "rem_dy": "30",
  "sym": "O_SPW2RV26-C7775.0",
  "fut_opt_div_cd": "O"
}
```

응답 1건:

- `occurs1_sym`: `O_SPW2RV26`
- `occurs1_sym_nm`: `CBOE S&P 500 위클리 옵션`
- `occurs1_exp_dt`: `20261008`
- `occurs1_rem_dy`: `000000`
- `occurs1_prd_cd`: `O_SPW`
- `occurs1_exch_cd`: `CBOE`

이 API는 오늘 만기 위클리 목록을 주지 않는다. 이미 아는 종목의 만기일을 확인한다. 날짜별 코드 목록은 종목마스터를 쓴다.

### 해외 품목별 세부정보

`POST /trade/v1/overseas/product-detail` 에 `fut_opt_div_cd` `O`, `prd_nm` `SPX` 등을 넣어 호출했으나 `nrec` `00000` 이었다. SPXW 월물을 이 API로 고르지는 못했다. 요청 필드명만 명세에서 확인했다. `nxt_key`, `fut_opt_div_cd`, `prd_grp_cd`, `exch_cd`, `cur_cd`, `sett_cd`, `prd_cd`, `prd_nm`.

## 계좌 조회

이번 앱키는 해외 계좌 조회가 되고 국내 계좌 조회는 `EGW016` 이다. 국내 예수금 `POST /trade/v1/domestic/deposit` 와 국내 주문가능수량 `POST /trade/v1/domestic/orderable-quantity` 가 그 오류였다.

### 해외 예수금

`POST /trade/v1/overseas/deposit`

```json
{ "biz_dt": "20261008", "cur_cd": "USD", "cvs_div": "1" }
```

`cvs_div`는 `1` 개별 통화, `2` 환산이다. `cur_cd`는 필수다. 응답에서 확인한 필드: `ac_no`, `biz_dt`, `cur_cd`, `dps_tot_amt`, `dps_csh_amt`, `ord_psb_amt`, `wdw_psb_amt`, `opt_ord_mrgn`, `rsk_rt`. 테스트 계좌의 USD 금액은 모두 0이었다.

### 해외 잔고

`POST /trade/v1/overseas/open-interest`

```json
{ "nxt_key": "" }
```

HTTP 200, 보유 0건이었다. 보유가 있을 때의 행 필드는 명세 기준 `occurs1_sym`, `occurs1_trd_tp`, `occurs1_opint_qty`, `occurs1_avg_pric`, `occurs1_pl_amt`, `occurs1_exp_dt` 이다. 실제 잔고 행은 보지 못했다.

### 해외 주문내역

`POST /trade/v1/overseas/order-history`

```json
{ "nxt_key": "", "inq_div": "0", "prd_cd": "", "trd_div_cd": "0" }
```

`inq_div`는 `0` 전체, `2` 체결, `3` 미체결이다. `trd_div_cd`는 `0` 전체, `1` 매수, `2` 매도다. HTTP 200, 0건이었다. 명세의 행 필드는 `occurs1_ord_dt`, `occurs1_sym`, `occurs1_ord_sts`, `occurs1_trd_tp`, `occurs1_ord_qty`, `occurs1_exec_qty`, `occurs1_ord_pric`, `occurs1_exec_pric` 이다. 실제 체결 행은 보지 못했다. 체결 판단은 주문 응답의 주문번호만이 아니라 이 조회나 실시간 `O1`로 한다.

### 해외 주문가능수량

`POST /trade/v1/overseas/orderable-quantity`

조회이며 주문을 전송하지 않는다.

```json
{
  "sym": "O_SPW2RV26-C7775.0",
  "glb_ord_tp_cd": "2",
  "ord_pric": "18.50",
  "stop_pric": ""
}
```

시세 신청 전에는 `ETR010` 이었다. 신청과 토큰 재발급 뒤에는 `5033` 주문불가계좌였다. 성공 시 명세의 필드는 `cur_cd`, `ord_psb_amt`, `new_buy_ord_psb_qty`, `buy_ord_psb_qty` 이다. 성공 응답은 보지 못했다. 테스트 계좌는 조회일 당일 개설된 계좌였다.

## 웹소켓

1. `GET /permission/v1/web-socket-key`, 헤더 `Authorization: Bearer {token}`. 응답 `access_key`.
2. `wss://api.futures.co.kr/trade/ws-stream?access_key={access_key}` 로 연결한다. 키는 연결 1회용이다.
3. 연결 후 세션을 연다.

```json
{
  "header": { "api_ver": "1", "action": "0" },
  "body": { "action_code": "session_init", "session_token": "{access_token}" }
}
```

4. `{"code":"INIT","msg":"INIT_SUCCESS"}` 를 받은 뒤 구독한다. `action` `A`는 구독, `D`는 해제다.

```json
{
  "header": { "api_ver": "1", "action": "A" },
  "body": { "action_code": "FB", "action_sym": "O_SPW2RV26-C7775.0" }
}
```

확인한 `action_code`:

| 코드 | 내용 |
| --- | --- |
| `FA` | 해외 체결 |
| `FB` | 해외 호가 |
| `O1` | 해외 주문, 체결 |
| `KA`, `KC` | 국내 체결. 주간, 야간 |
| `KB`, `KD` | 국내 호가. 주간, 야간 |
| `O1_K` | 국내 주문, 체결 |

`FA` 체결 본문은 2026-10-08 VXV26에서 확인했다. `last_pric`와 `exec_qty`는 문자열이다. 시각은 `ko_trd_dt`와 `ko_exec_tm`(한국시각)이고, 없으면 `biz_dt`와 `exch_tm`이다. `sym`은 본문에 있다. `FB`는 `RT_INIT`까지만 확인했다. `O1`은 주문이 없어 확인하지 않았다.

## 주문 명세

아래는 포털 명세다. 실계좌 주문, 정정, 취소는 실행하지 않았다. 복합 주문 API는 없고 종목당 한 건이다. 양매수는 콜 주문과 풋 주문을 연속으로 낸다.

`POST /trade/v1/overseas/order`

| 필드 | 값 |
| --- | --- |
| `qt_div_cd` | `1` 신규, `2` 정정, `3` 취소 |
| `sym` | 종목코드 |
| `trd_div_cd` | `1` 매수, `2` 매도 |
| `glb_ord_tp_cd` | `1` 시장가, `2` 지정가, `3` STOP 시장가, `4` STOP 지정가 |
| `ord_qty` | 수량 |
| `ord_pric` | 지정가 가격. 지정가면 필수 |
| `stop_pric` | 스탑 가격. 스탑이면 필수 |
| `org_ord_no` | 정정, 취소의 원주문번호 |

정정과 취소도 같은 경로다. `qt_div_cd`만 바뀐다.

## 양매수에 쓰는 호출

구현에 바로 쓸 수 있는 것:

- 토큰 발급과, 시세 권한이 바뀐 뒤의 폐기 후 재발급
- `opra_opt.mst.gz`에서 당일 `O_SPW` 월물 코드를 고름
- `price-all-option`으로 체인 조회 후 콜·풋 호가가 맞닿는 행사가 선택
- `chart-minute`으로 그 종목의 1분봉을 받는다. `dcnt`는 `1`
- `bid-price` 또는 `price`로 그 콜과 풋의 매수, 매도 호가 확인
- `deposit`으로 USD 주문가능금액 확인
- `market-operations`로 `O_SPW` 거래 시간 확인
- `open-interest`, `order-history` 조회. 빈 응답만 확인했다.

계좌가 주문 가능이고 USD가 들어온 뒤에 확인할 것:

- `orderable-quantity` 성공 응답
- 신규, 정정, 취소
- 콜 매수와 풋 매수의 연속 전송, 한쪽만 체결된 경우의 취소 또는 청산
- 잔고에 잡힌 수량으로 매도 청산
- 주문내역의 체결 행과 웹소켓 `O1`
- 웹소켓 `FB` 틱 본문

## 자격증명

앱키와 시크릿은 환경변수 `NH_APP_KEY`, `NH_SECRET_KEY` 로 둔다. 코드와 로그에 토큰, 앱키, 시크릿, 계좌번호를 남기지 않는다. 토큰 응답과 예수금 응답에는 그 값이 들어 있다.
