# 원본 자료 목록

확인일: 2026-09-28 (Asia/Seoul)

[통합 구현 계획서](../C_TRADING_MASTER_PLAN.md)

사용자가 제공한 자료 8개를 바이트 변경 없이 복사했다. 일부 파일명은 경로 사용 편의를 위해 영문으로 바꾸었으며 원래 이름과 대응 관계를 아래에 기록한다. 해시는 내용 동일성을 확인하는 값이며 원본의 기능 정확성이나 버전 호환성을 보장하지 않는다.

이 자료는 해당 프로젝트의 구현 참고용이다. 원본을 수정할 때에는 별도 작업 파일과 변경 이력을 사용한다. HTML의 동봉되지 않은 설명서 링크는 원본 그대로이며, HTML 자체를 원본 수치 비교의 정답으로 사용하지 않는다.

원본은 종류별로 세 폴드에 둔다: 함수 원본은 `yeslanguage/functions/`, 지표 원본은 `yeslanguage/indicators/`, 전략 원본은 `yeslanguage/strategies/`이며, C 포팅 쪽도 같은 3분할(`src/core/functions|indicators|strategies/`)을 거울처럼 따른다(규약은 [docs/PORTING.md](../docs/PORTING.md)). 새 원본은 처음부터 종류에 맞는 폴드에 직접 추가한다.

| 원래 파일명 | 묶음 내 위치 | 바이트 | 용도 |
|---|---|---:|---|
| `#우드스탁_미래곡선_1분봉.txt` | [woodstock_mirae_curve_1m_v16.txt](yeslanguage/indicators/woodstock_mirae_curve_1m_v16.txt) | 43248 | 메인 지표 원본, 헤더 V16_1분봉용 |
| `WSF_MiraeCurve1mFullOutputV1.txt` | [WSF_MiraeCurve1mFullOutputV1.txt](yeslanguage/functions/WSF_MiraeCurve1mFullOutputV1.txt) | 32641 | 69개 OUT 출력, 계산 분리 참고 |
| `WSF_Mtf_LinRegV3.txt` | [WSF_Mtf_LinRegV3.txt](yeslanguage/functions/WSF_Mtf_LinRegV3.txt) | 6025 | 제공 정의 19개 인자, 호출부 버전 차이 있음 |
| `WSF_Mtf_LinRegPredictV4.txt` | [WSF_Mtf_LinRegPredictV4.txt](yeslanguage/functions/WSF_Mtf_LinRegPredictV4.txt) | 3829 | 제약을 적용한 회귀 예측 |
| `WSF_OrderBookDirectionV2.txt` | [WSF_OrderBookDirectionV2.txt](yeslanguage/functions/WSF_OrderBookDirectionV2.txt) | 5242 | 호가 기반 지표 V2, V1 미포함 |
| `WSF_Htf_CurvePredict.txt` | [WSF_Htf_CurvePredict.txt](yeslanguage/functions/WSF_Htf_CurvePredict.txt) | 2645 | 19개 표본 기반 별도 회귀 예측 |
| `미래곡선그리기.html` | [mirae_curve_demo.html](explanations/mirae_curve_demo.html) | 57327 | 설명용 근사 재현, HTS 기준 출력 아님 |
| `미래곡선그리기_코드엑스레이.html` | [mirae_curve_code_xray.html](explanations/mirae_curve_code_xray.html) | 177457 | 설명용 근사 재현, HTS 기준 출력 아님 |

2026-09-28 추가 제공 (기존 미제공 함수 7종):

| 원래 파일명 | 묶음 내 위치 | 바이트 | 용도 |
|---|---|---:|---|
| `WSF_Mtf_LinRegV3_구형.txt` | [WSF_Mtf_LinRegV3_구형.txt](yeslanguage/functions/WSF_Mtf_LinRegV3_구형.txt) | 4971 | 메인이 호출하는 7인자 구형 V3 |
| `WSF_1m_DailyAlignV2.txt` | [WSF_1m_DailyAlignV2.txt](yeslanguage/functions/WSF_1m_DailyAlignV2.txt) | 4771 | 일봉 정렬·최종 운영 상태 |
| `WSF_1m_DailyTrendLinkV1.txt` | [WSF_1m_DailyTrendLinkV1.txt](yeslanguage/functions/WSF_1m_DailyTrendLinkV1.txt) | 4535 | 1분봉-일봉 추세 연결 |
| `WSF_GapRegimeV1.txt` | [WSF_GapRegimeV1.txt](yeslanguage/functions/WSF_GapRegimeV1.txt) | 4626 | 갭 상태·일봉 가중 |
| `WSF_OrderBookDirectionV1.txt` | [WSF_OrderBookDirectionV1.txt](yeslanguage/functions/WSF_OrderBookDirectionV1.txt) | 5242 | 호가 기반 지표 V1 |
| `WSF_AutoSessionADXV1.txt` | [WSF_AutoSessionADXV1.txt](yeslanguage/functions/WSF_AutoSessionADXV1.txt) | 3552 | 세션 ADX |
| `WSF_DailyMarketProfileV1.txt` | [WSF_DailyMarketProfileV1.txt](yeslanguage/functions/WSF_DailyMarketProfileV1.txt) | 1600 | 일봉 이상 마켓 프로파일 |
| `WSF_Daily_LinRegTrendV1.txt` | [WSF_Daily_LinRegTrendV1.txt](yeslanguage/functions/WSF_Daily_LinRegTrendV1.txt) | 2491 | 일봉 회귀 추세 판정 (DailyTrendLinkV1 하위) |
| `YLHelp.pdf` | [YLHelp.pdf](yeslanguage/YLHelp.pdf) | 3011362 | 예스랭귀지 공식 매뉴얼 PDF (265페이지, 함수·데이터 정의의 공식 근거) |

## SHA-256

| 묶음 내 파일 | SHA-256 |
|---|---|
| `reference/yeslanguage/indicators/woodstock_mirae_curve_1m_v16.txt` | `92a1a2c300e1d2566614ba068e2fd67cd7a3f982b69e2557dba7df8f9744da1d` |
| `reference/yeslanguage/functions/WSF_MiraeCurve1mFullOutputV1.txt` | `5a7a27db4d04672624a3b5f4e1b4851ee096e9146db3e0dc38dae9eaf9a7ee0e` |
| `reference/yeslanguage/functions/WSF_Mtf_LinRegV3.txt` | `fef012e926d4f837e5d16e9aae5d85e816bc8c697522cb6e5c69e9e476db4f8d` |
| `reference/yeslanguage/functions/WSF_Mtf_LinRegPredictV4.txt` | `0779a1ec16dd3951ae7b1bca8068fbcb6756c5aa0360c813454dfd5e6dc9732a` |
| `reference/yeslanguage/functions/WSF_OrderBookDirectionV2.txt` | `5d9715cbd704415484ef9c000f44448fb1130bfdda74411d68a0f4a6bf49073e` |
| `reference/yeslanguage/functions/WSF_Htf_CurvePredict.txt` | `aede9596fb7760766cb639315e52462968c661a310bf8015c6328502b94912a5` |
| `reference/explanations/mirae_curve_demo.html` | `f0d35c7068aed72f9306c56d6a35f9fb7fdca09f2b40f3a28bd6fdeb950ef493` |
| `reference/explanations/mirae_curve_code_xray.html` | `12bbec9ba1503b28b4ea0448841c538f1dc060cc304c15fa111b4e5c93e398c0` |
| `reference/yeslanguage/functions/WSF_Mtf_LinRegV3_구형.txt` | `97a55256d9867c65bdd7d4abca5ff24793bfdb9ee62164d83afcc0916b8d5a77` |
| `reference/yeslanguage/functions/WSF_1m_DailyAlignV2.txt` | `94caff2c23a9c9dc05670234f6e741f774e2fa0223a257ba18717a72ca72c30a` |
| `reference/yeslanguage/functions/WSF_1m_DailyTrendLinkV1.txt` | `a3490108f8bcb791254da315785552b032aaa7e6ca1beee1adf56bf2bf818e57` |
| `reference/yeslanguage/functions/WSF_GapRegimeV1.txt` | `c057ce345f66fe01c7a09096a26a3071f3d3738c179cda72f0f4f49ac1e96716` |
| `reference/yeslanguage/functions/WSF_OrderBookDirectionV1.txt` | `e0a0371797588da90a9492edcf7b5b7bd4fdee94f530dcfe7939fec3dc1fe19b` |
| `reference/yeslanguage/functions/WSF_AutoSessionADXV1.txt` | `4b5eed6903d0fdb12f85a86c70984868e76c086e96b3426d3b4ec7991fda3279` |
| `reference/yeslanguage/functions/WSF_DailyMarketProfileV1.txt` | `a0127e49e016db69120cb64117a0ebfae8f45e99f9201ed172a6a0707369368b` |
| `reference/yeslanguage/functions/WSF_Daily_LinRegTrendV1.txt` | `666a01c472146b725eed70d9a8103fb68b098fdb1641578e214875d9a44ddfdd` |
| `reference/yeslanguage/YLHelp.pdf` | `192e5253fd09e041b18f6456ce21a198ceca995db7a4d2a0edc74cf86c938424` |
