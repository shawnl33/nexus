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

2026-10-01 추가 제공 (해외선물 계열: OSF 15 + WSF_FX 13 = 함수 28, 지표 22 — 전수 타입 검증으로 폴드와 일치 확인. zip 2개는 같은 파일들의 배포 묶음):

| 묶음 내 파일 | SHA-256 |
|---|---|
| `reference/yeslanguage/functions/OSF_1m_DailyAlignV2.txt` | `4150b417cc78e14216d54eea5fd3d06a5271d44599266ccc95de1cb4bdc10fc3` |
| `reference/yeslanguage/functions/OSF_1m_DailyTrendLinkV1.txt` | `f7c604f4bb2d0b3f3e8fdc48eed510984ffde18bf1bee443c732caef422dd1b2` |
| `reference/yeslanguage/functions/OSF_AutoSessionADXV1.txt` | `06d0c66ca3c934857b431186b450fb490800a3983d9d22d5513d5d4539601506` |
| `reference/yeslanguage/functions/OSF_ClvPressureV1.txt` | `ed6b47eec832eea72d61b04721ccca99a14ee11e40eb17e8e8c0b8d6e3271ebe` |
| `reference/yeslanguage/functions/OSF_ClvVolFlowV1.txt` | `2718144360b3d4aaf9a9010af7fa07ee2138d270ef638154b976920a75b4dd18` |
| `reference/yeslanguage/functions/OSF_Daily_LinRegTrendV1.txt` | `1b07fa02ef23b85b5ddbdbc4d0d246927f8c6c5b44ebb1edbc7ef4263ba279a0` |
| `reference/yeslanguage/functions/OSF_DailyMarketProfileV1.txt` | `1b9ceed2520dcbc8b352d7974a91ec681b40d7b72af34958110e442d6f4d7af3` |
| `reference/yeslanguage/functions/OSF_GapRegimeV1.txt` | `c70b101485bc300c0fd71093f4418b63b62381dbc274503819b6b0b9e5e4266d` |
| `reference/yeslanguage/functions/OSF_Htf_CurvePredict.txt` | `443d9b18fd49cf7ec5082659e0a401e5e4a8e519c13d2a8e4d8f275ad2270baf` |
| `reference/yeslanguage/functions/OSF_Mtf_LinRegPredictV4.txt` | `c3296c195276ae9c1d6a3762fc6a17dfade13b87f6fba6b1bd1db9f5e5d4a8e6` |
| `reference/yeslanguage/functions/OSF_Mtf_LinRegV3.txt` | `594941207b28779748789a65b334509f6339b7e82fae7235dd7cc1407f837b06` |
| `reference/yeslanguage/functions/OSF_OrderBookDirectionV1.txt` | `e94435f2ebf75bbf68dd8bac47022db0593e1d79a7e619fe23c13afca0cd2912` |
| `reference/yeslanguage/functions/OSF_OrderBookDirectionV2.txt` | `bba3a548a4f3705fb1a40c99d011c9a80d74d12316467e343475f13caa702380` |
| `reference/yeslanguage/functions/OSF_SwingFibMemoryV4.txt` | `57e4a1907193c863c9abb8f1c7830d53901eeac2a48b884de84df133c55ff4c7` |
| `reference/yeslanguage/functions/OSF_VolFlowV1.txt` | `3be37a1c495568674b9fae2c6f6f786c0be8af8ebd7d7844eab75242771bb2aa` |
| `reference/yeslanguage/functions/WSF_FXADXV1.txt` | `649cb1c8d59cd82cbc0bfd9dfbb82e5f980a765190d187840f437125b7dc42dd` |
| `reference/yeslanguage/functions/WSF_FXAlignV1.txt` | `8c74cfc81607ecc9c559040d004e5e80cd8baffabf6cd36f5782076b62f8c013` |
| `reference/yeslanguage/functions/WSF_FXCurveV1.txt` | `ecab4516d20a722c3fa475cac06e8dd0a96dcc253e4ea057d81a6b9e4df18470` |
| `reference/yeslanguage/functions/WSF_FXFutureValuesV1.txt` | `75ead13cbdbc67db0153cf7bada6a095a01a44d0f9c54247dd6bb102a7350c66` |
| `reference/yeslanguage/functions/WSF_FXGapV1.txt` | `ac22118c6bff120db140ea91674563f05d20b8b8cdf606aa5f134abe31ba3611` |
| `reference/yeslanguage/functions/WSF_FXMarketV1.txt` | `f815f4202e48a72f7dc2e3cdfb456eb864d209a785aa57a60a1bde3d89faf251` |
| `reference/yeslanguage/functions/WSF_FXPredictV2.txt` | `0ac8dcaa31277cf2f51fcd49f9d4b305330db239648aef31ae87d2c307d233ef` |
| `reference/yeslanguage/functions/WSF_FXRegV1.txt` | `fff4c0dae31d1cb1f78eeaf5cb4008af7684d5bd2b2220c204d27c180d3fbd82` |
| `reference/yeslanguage/functions/WSF_FXSessionKeyV1.txt` | `96b7f495cd4be1e5851e08378918d74cb0249fe9eb228d880e9e32916927c1be` |
| `reference/yeslanguage/functions/WSF_FXSwingV1.txt` | `120626fd573307d98aac3fc307d1a36d2487003b103748d0ab2b8ab48f5b9eb4` |
| `reference/yeslanguage/functions/WSF_FXSyntheticLinesV1.txt` | `0e6ba93274633c6ce1a1a41d8c72bad0737e8b0b557cb057a747edc06633af7e` |
| `reference/yeslanguage/functions/WSF_FXTrendStateV1.txt` | `c7edc984aea38a20b59bda755e21f14618d01853d3d5a3178790e75f14227fdb` |
| `reference/yeslanguage/functions/WSF_FXTrendV1.txt` | `542fa04827b891f89fa1a74c0f6d70ac2408c45386315b60591a8f922b1157e6` |
| `reference/yeslanguage/indicators/#우드스탁_스나이퍼스코프_해외선물_Data2.txt` | `6e1626dad21c086e49fcacc4075670e884b55a36423ff4532de50ebdf66e00eb` |
| `reference/yeslanguage/indicators/#우드스탁_가격거래량압축_해외선물V1.txt` | `001457a723dc5f8bd62e8b42e79cb0ba097b3e3fe5a003772089616bc055ebf2` |
| `reference/yeslanguage/indicators/#우드스탁_스나이퍼스코프_해외선물V1.txt` | `2c7e463369350520ab6ab3ecf3f5a1c3d79b28e864f28fbc5d322472bbbfc39c` |
| `reference/yeslanguage/indicators/#우드스탁_삼선비율점수_해외선물V1.txt` | `79063dfb5b2ffdceca694cde60eff786cfbdaaa6059dac2ff37badef589a4a62` |
| `reference/yeslanguage/indicators/#우드스탁_스나이퍼점수_해외선물V1.txt` | `c23589b11f509aea2b9e35ba73670316d406900578fe46fa2ddd8da7e7f2dc69` |
| `reference/yeslanguage/indicators/#우드스탁_점수통합_해외선물V1.txt` | `6f0a1d26ee13f6a20bd7fa88389cd87c1c3d41891ffc223db3abb72ba5788052` |
| `reference/yeslanguage/indicators/#우드스탁_스나이퍼스코프_해외선물V2.txt` | `2e056bbd77ca8aabd00ae3a2abaf6836666e02be953083d3c622575b41ccd9d5` |
| `reference/yeslanguage/indicators/#우드스탁_스나이퍼스코프_해외선물V3.txt` | `18be12f146da297777de7204cd902dc89c5ffd6392fb7b392909f3cf575141c1` |
| `reference/yeslanguage/indicators/woodstock_mirae_curve_1m_v16.txt` | `92a1a2c300e1d2566614ba068e2fd67cd7a3f982b69e2557dba7df8f9744da1d` |
| `reference/yeslanguage/indicators/#WSF_해외선물지속목표차삼선V1.txt` | `885962e86179528b11c2eaa2dca3466b931e5f65c4b0c7165c4b44583771819c` |
| `reference/yeslanguage/indicators/#WSF_해외선물지속목표차오선V1.txt` | `8e53371015e6dd6deae4a16268419678722e23a9a4f186fe68f21cd0c41399dc` |
| `reference/yeslanguage/indicators/#WSF_해외선물지속목표차통합V1.txt` | `6feeee644ceb6696be9673fc1d2515bf72394152140d78a073a187e2246a75b1` |
| `reference/yeslanguage/indicators/#WSF_해외선물삼선구간이탈V1.txt` | `b35292f8a2ec5c05e4b0d152d65c11ed4eac488e112fe8721220679bb13bc6ed` |
| `reference/yeslanguage/indicators/#WSF_해외선물압축첫이탈V1.txt` | `9f94f1fd00bd8d2efb9ddeb9e7d387b4f98506105398b74b20fe5e91f00e8575` |
| `reference/yeslanguage/indicators/#WSF_해외선물양매수가설V1.txt` | `e2181dcb97bcf290b521928f25a1a4d3b4370d13a0dc13ea3f87f5cd47bcf6db` |
| `reference/yeslanguage/indicators/#WSF_해외선물양매수통합V1.txt` | `46e4127cf5049fa45c6fd6303459d05b3666b08b056778b8ed4cc95f6d3d366e` |
| `reference/yeslanguage/indicators/#WSF_해외선물평탄회귀차V1.txt` | `7fc7e413913ed7c8585cef3b2c3baf66e7ef901e5eddffda493e97a267ba0e71` |
| `reference/yeslanguage/indicators/#WSF_해외선물미래곡선V1.txt` | `1760ec82b6d641d231f2a3abe9f885cc503475b1ab44105d98e6e3f5724aa01d` |
| `reference/yeslanguage/indicators/#WSF_해외선물삼선구간이탈V2.txt` | `42c3e10551941172a92384edba967eb445db7aefbf92230bf3bb27f443933e57` |
| `reference/yeslanguage/indicators/#WSF_해외선물양매수가설V2.txt` | `de8e67757fab1e09af049577d252d9f5a68a279cfeabb7edc4fb2adddd9f850d` |
| `reference/yeslanguage/indicators/#WSF_해외선물양매수통합V2.txt` | `3ae2c471ff3b1a8ba3ae389f78711f865d2199c5028b8c58ea91102ac61305b9` |
| `reference/yeslanguage/indicators/#WSF_해외선물미래곡선V2.txt` | `1760ec82b6d641d231f2a3abe9f885cc503475b1ab44105d98e6e3f5724aa01d` |
| `reference/yeslanguage/indicators/#WSF_해외선물미래곡선V3.txt` | `a1ab49bec9570705e49fb66ac8ca0915643722560d6fb6f2f292f33e6394b6c6` |
| `reference/yeslanguage/OSF_1m_DailyAlignV2.zip` | `efa26be046d9764f11e77f76c680fd33167b68bb375f5aa1f7e0921a4c56677e` |
| `reference/yeslanguage/#WSF_해외선물미래곡선V2.zip` | `fc9f63f922e9c85ab47e7e64c58d358737c83b54909920b06571248cf31b61e9` |
