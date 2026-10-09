// 좌측 사이드바의 원본 트리. reference/yeslanguage 폴더와 파일 이름을 그대로 쓴다.
// experiments, functions 는 고를 항목이 아니므로 뺀다. 차트에 연결된 파일만 port 를 가진다.
// 브라우저 전역 YlTree, node:test 에서는 globalThis.YlTree.
"use strict";

const YlTree = (() => {
  const SKIP_DIRS = new Set(["experiments", "functions"]);

  // 파일 이름 → 이 대시보드가 실제로 켜는 항목. 없는 파일은 이름만 보인다.
  // inputs 는 예스랭귀지 Input 줄의 이름과 기본값이다.
  const SHORT_INPUTS = [
    ["매매종료", 0], ["총투자금", 10000000], ["익절률", 10], ["손절률", 10],
    ["진입시작", 90000], ["전량청산", 151500], ["만남방식", 1], ["허용오차", 0],
    ["진단출력사용", 1], ["분할청산", 1], ["만기잔량익절률", 60], ["잔량익절대기분", 15],
    ["저수익대기분", 60], ["저수익기준률", 5], ["저수익청산비율", 30],
  ];
  const LONG_INPUTS = [
    ["매매종료", 0], ["총투자금", 10000000], ["익절률", 10], ["손절률", 10],
    ["진입시작봉수", 15], ["오후전환봉수", 195], ["전량청산", 151500],
    ["오전삼선만남최대비율", 70], ["오후삼선만남최대비율", 50],
    ["만남방식", 1], ["허용오차", 0], ["진단출력사용", 1], ["분할청산", 1],
    ["만기잔량익절률", 60], ["잔량익절대기분", 15], ["저수익대기분", 60],
    ["저수익기준률", 5], ["저수익청산비율", 30],
  ];
  // #우드스탁_해외선물진입신호매매V1 Input 줄의 이름과 기본값.
  const SIG_INPUTS = [
    ["수량", 1],
    ["예측변수", 10], ["가치영역배수", 1.5], ["최소봉수", 5],
    ["ADX기간", 14], ["ADX추세기준", 20], ["ADX강세기준", 35], ["매물대기준모드", 1],
    ["판정방식", 4], ["폭계산봉수", 30], ["비율기준봉수", 120], ["좁음기준", 40],
    ["단계1봉수", 15], ["방향확정봉수", 5], ["확정봉판정", 0], ["해제유효봉수", 10],
    ["등급C사용", -1], ["돌파여유틱", -1], ["돌파통합최소", 50], ["돌파확인봉수", 1],
    ["DI일치필수", 1], ["확장확인봉수", 5], ["진입시작시각", 200000], ["진입종료시각", 20000],
    ["해제A사용", 1], ["재돌파허용봉수", 3], ["B보완사용", 1], ["B사용", 1],
    ["구조사용", 1], ["구조폭기준", 50], ["구조유효봉수", 10], ["구조매수사용", 1], ["구조해제포함", 1],
    ["S사용", 1], ["S구조폭기준", 50], ["S압축봉수", 30], ["S폭비율기준", 50],
    ["S진행중봉수", 60], ["S대기봉수", 2], ["반대차단시작시각", 230000], ["반대차단봉수", 60],
    ["R사용", 1], ["R삼선기준", 40], ["R구조폭기준", 50], ["R조합", 1], ["R폭비율기준", 100],
    ["R반전봉수", 5], ["R위치사용", 1], ["R이탈여유틱", 1], ["R직전구간최소봉수", 3],
    ["약신호확정분", 120], ["약신호대기봉수", 10], ["반대압축확인분", 90],
    ["반대압축최소봉수", 20], ["반대압축구조포함", 1],
    ["T사용", 1], ["T시작시각", 230000], ["T종료시각", 20000], ["T무신호분", 120],
    ["T삼선기준", 60], ["T폭비율기준", 70], ["T구조폭기준", 70], ["T위치사용", 1],
    ["돌파사용", 1], ["돌파통합기준", 50], ["돌파위치사용", 1], ["돌파과열분", 120],
    ["예측봉수1", 5], ["예측봉수2", 10], ["예측봉수3", 15], ["예측봉수4", 30], ["예측봉수5", 60],
    ["최소신뢰도", 0.4], ["지속봉수", 5], ["마켓계산기간", 20], ["최소전환유지봉수", 3],
    ["스윙연결", 0], ["기세무시틱", 1],
    ["최소구간봉수", 3], ["최소추세봉수", 15], ["최소반대봉수", 3], ["복귀확인봉수", 2],
    ["강가격기준", 38.2], ["강시간기준", 23.6], ["중가격기준", 61.8],
    ["시작가B만갱신", 1], ["구조등급상향", 1], ["구조중가격기준", 78.6],
    ["신호제한봉수", 120], ["전환반대기울기비", 61.8], ["완화단계", 1],
    ["조정통합확인", 1], ["조정최소비", 23.6], ["돌파통합확인", 1], ["기울기기준", 2],
    ["매매시작시각", 210000], ["매매종료시각", 20000], ["조정재개1회", 1], ["돌파1회", 1],
    ["직전기울기제한", 30], ["제한등급", 2], ["조정매수382제한", 1],
    ["조정상태봉수", 60], ["조정상태비율", 50], ["조정연빨강포함", 1], ["본장시작시각", 223000],
    ["조정매도통합100", 1], ["조정매도저점근접비", 38.2], ["선행통합100", 1],
  ];
  // 수익관리가 계산에 쓰는 이름. 시그널 설정에 같은 이름이 있으면 그 값이 우선이다.
  const PNL_INPUTS = [
    ["최초진입수량", 2],
    ["일부청산수량", 1],
  ];
  const PORTS = {
    "woodstock_mirae_curve_1m_v16.txt": { kind: "indicator", id: "mirae_v16" },
    "#WSF_해외선물미래곡선V1.txt": { kind: "indicator", id: "fx_mirae_v1" },
    "#WSF_해외선물미래곡선V2.txt": { kind: "indicator", id: "fx_mirae_v1", note: "파일 내용이 V1과 같습니다" },
    "#WSF_해외선물미래곡선V3.txt": { kind: "indicator", id: "fx_mirae_v3" },
    "#우드스탁_미래곡선_해외선물.txt": { kind: "indicator", id: "fx_curve_os" },
    "#WSF_해외선물지속목표차삼선V1.txt": { kind: "indicator", id: "fx_pgap3" },
    "#WSF_해외선물지속목표차오선V1.txt": { kind: "indicator", id: "fx_pgap5" },
    "#WSF_해외선물평탄회귀차V1.txt": { kind: "indicator", id: "fx_rgap" },
    "#WSF_해외선물지속목표차통합V1.txt": { kind: "indicator", id: "fx_ugap" },
    "#WSF_해외선물양매수통합V2.txt": { kind: "indicator", id: "fx_ymae" },
    "#우드스탁_가격거래량압축_해외선물V1.txt": { kind: "indicator", id: "fx_pvc" },
    "#우드스탁_스나이퍼스코프_해외선물V3.txt": { kind: "indicator", id: "fx_sniper" },
    "#우드스탁_스나이퍼스코프_해외선물_CO_V3.txt": { kind: "indicator", id: "fx_snco" },
    "#우드스탁_해외선물1분통합판정.txt": { kind: "indicator", id: "fx_judge" },
    "#우드스탁_해외선물1분통합판정강조.txt": { kind: "indicator", id: "fx_paint_v12" },
    "#우드스탁_해외선물평탄카운트_V3.txt": { kind: "indicator", id: "fx_flat_v4" },
    "#우드스탁_해외선물조정분석V1.txt": { kind: "indicator", id: "fx_adj_v1" },
    "#우드스탁_해외선물조정분석V2.txt": { kind: "indicator", id: "fx_adj_v2" },
    "#우드스탁_해외선물조정분석V4.txt": { kind: "indicator", id: "fx_adj_v4" },
    "#우드스탁_해외선물조정분석V5.txt": { kind: "indicator", id: "fx_adj_v5" },
    "#우드스탁_해외선물조정분석V6.txt": { kind: "indicator", id: "fx_adj_v6" },
    "#우드스탁_해외선물평탄카운트.txt": { kind: "indicator", id: "fx_flat_v2" },
    "#우드스탁_해외선물진입후보V19.txt": { kind: "indicator", id: "fx_ec19" },
    "#우드스탁_해외선물진입후보V20.txt": { kind: "indicator", id: "fx_ec20" },
    "paintbar/#우드스탁_미래곡선_1분봉용_해외.txt": { kind: "indicator", id: "fx_paint_os" },
    "#우드스탁_해외선물하락최고돌파V1.txt": { kind: "indicator", id: "fx_dnbrk" },
    "2023_우드스탁_N선물_수익관리.txt": { kind: "indicator", id: "fx_pnl", inputs: PNL_INPUTS },
    "#우드스탁_해외선물진입신호매매V1.txt": { kind: "indicator", id: "fx_sig_v6", inputs: SIG_INPUTS },
    "#우드스탁_해외선물매물대압축지속V4.txt": { kind: "indicator", id: "fx_pack_v4" },
    "#우드스탁_스나이퍼스코프_해외선물_Data2.txt": { kind: "indicator", id: "fx_data2" },
    "#우드스탁_스나이퍼스코프_국내선물_Data2.txt": { kind: "indicator", id: "ks_data2" },
    "#우드스탁_위클리_합산수익률_양매수.txt": { kind: "indicator", id: "w_ret_long" },
    "#우드스탁_위클리_합산수익률_양매도.txt": { kind: "indicator", id: "w_ret_short" },
    "#우드스탁_위클리_프라이스링크_양매수.txt": { kind: "indicator", id: "w_link_long" },
    "#우드스탁_위클리_프라이스링크_양매도.txt": { kind: "indicator", id: "w_link_short" },
    "#우드스탁_위클리_페어시스템_양매수.txt": { kind: "system", id: "pair-long", side: 1, leg: "w_link_long", inputs: LONG_INPUTS },
    "#우드스탁_위클리_페어시스템_양매도.txt": { kind: "system", id: "pair-short", side: -1, leg: "w_link_short", inputs: SHORT_INPUTS },
  };

  // 시그널 바구니에 같은 이름이 있으면 그 값, 없으면 지표 자신의 값, 그것도 없으면 기본값.
  function sharedValue(signalBag, ownBag, name, fallback) {
    if (signalBag && Object.prototype.hasOwnProperty.call(signalBag, name)) {
      const s = Number(signalBag[name]);
      if (Number.isFinite(s)) return s;
    }
    if (ownBag && Object.prototype.hasOwnProperty.call(ownBag, name)) {
      const o = Number(ownBag[name]);
      if (Number.isFinite(o)) return o;
    }
    return fallback;
  }

  function inputsFor(id) {
    for (const port of Object.values(PORTS)) {
      if (port.id === id && Array.isArray(port.inputs)) return port.inputs;
    }
    return [];
  }

  // 화면에는 확장자를 붙이지 않는다.
  function stem(file) {
    const i = file.lastIndexOf(".");
    return i > 0 ? file.slice(0, i) : file;
  }

  function indicatorIds() {
    const ids = new Set();
    for (const port of Object.values(PORTS)) {
      if (port.kind === "indicator") ids.add(port.id);
    }
    return ids;
  }

  // catalog: { dirs: [{ name, files: ["이름.txt"] }] }. 빈 폴더와 제외 폴더는 없다.
  function build(catalog) {
    const dirs = Array.isArray(catalog?.dirs) ? catalog.dirs : [];
    const out = [];
    for (const dir of dirs) {
      const name = typeof dir?.name === "string" ? dir.name : "";
      if (!name || SKIP_DIRS.has(name) || name.startsWith(".")) continue;
      const items = [];
      for (const file of Array.isArray(dir.files) ? dir.files : []) {
        if (typeof file !== "string" || !file.endsWith(".txt") || file.startsWith(".")) continue;
        const keyed = PORTS[name + "/" + file] || PORTS[file];
        const port = keyed ? { ...keyed, inputs: keyed.inputs || null } : null;
        items.push({ dir: name, file, label: stem(file), port });
      }
      items.sort((a, b) => a.file.localeCompare(b.file, "ko"));
      if (items.length) out.push({ id: name, name, items });
    }
    out.sort((a, b) => a.name.localeCompare(b.name, "ko"));
    return out;
  }

  // 배포본은 reference/yeslanguage 가 없어 /api/yeslang 이 { dirs: [] } 를 준다.
  // 그 응답은 목록이 아니므로 정적 yl-catalog.json 으로 넘어간다.
  function hasEntries(catalog) {
    return build(catalog).length > 0;
  }

  // signals 는 화면틀의 첫 차트(위)에서만 고른다. paneIndex 는 그 틀 안의 0부터의 순서.
  function selectable(dirName, paneIndex) {
    if (dirName === "signals") return paneIndex === 0;
    return true;
  }

  return { SKIP_DIRS, PORTS, build, indicatorIds, stem, inputsFor, hasEntries, selectable, sharedValue };
})();

if (typeof globalThis !== "undefined") {
  globalThis.YlTree = YlTree;
}
