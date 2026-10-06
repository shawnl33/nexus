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
  const PORTS = {
    "woodstock_mirae_curve_1m_v16.txt": { kind: "indicator", id: "mirae_v16" },
    "#WSF_해외선물미래곡선V1.txt": { kind: "indicator", id: "fx_mirae_v1" },
    "#WSF_해외선물미래곡선V2.txt": { kind: "indicator", id: "fx_mirae_v1", note: "파일 내용이 V1과 같습니다" },
    "#WSF_해외선물미래곡선V3.txt": { kind: "indicator", id: "fx_mirae_v3" },
    "#WSF_해외선물지속목표차삼선V1.txt": { kind: "indicator", id: "fx_pgap3" },
    "#WSF_해외선물지속목표차오선V1.txt": { kind: "indicator", id: "fx_pgap5" },
    "#WSF_해외선물평탄회귀차V1.txt": { kind: "indicator", id: "fx_rgap" },
    "#WSF_해외선물지속목표차통합V1.txt": { kind: "indicator", id: "fx_ugap" },
    "#WSF_해외선물양매수통합V2.txt": { kind: "indicator", id: "fx_ymae" },
    "#우드스탁_가격거래량압축_해외선물V1.txt": { kind: "indicator", id: "fx_pvc" },
    "#우드스탁_스나이퍼스코프_해외선물V3.txt": { kind: "indicator", id: "fx_sniper" },
    "#우드스탁_스나이퍼스코프_해외선물_Data2.txt": { kind: "indicator", id: "fx_data2" },
    "#우드스탁_스나이퍼스코프_국내선물_Data2.txt": { kind: "indicator", id: "ks_data2" },
    "#우드스탁_위클리_합산수익률_양매수.txt": { kind: "indicator", id: "w_ret_long" },
    "#우드스탁_위클리_합산수익률_양매도.txt": { kind: "indicator", id: "w_ret_short" },
    "#우드스탁_위클리_프라이스링크_양매수.txt": { kind: "indicator", id: "w_link_long" },
    "#우드스탁_위클리_프라이스링크_양매도.txt": { kind: "indicator", id: "w_link_short" },
    "#우드스탁_위클리_페어시스템_양매수.txt": { kind: "system", id: "pair-long", side: 1, leg: "w_link_long", inputs: LONG_INPUTS },
    "#우드스탁_위클리_페어시스템_양매도.txt": { kind: "system", id: "pair-short", side: -1, leg: "w_link_short", inputs: SHORT_INPUTS },
  };

  function inputsFor(id) {
    for (const port of Object.values(PORTS)) {
      if (port.kind === "system" && port.id === id && Array.isArray(port.inputs)) return port.inputs;
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
        const port = PORTS[file] ? { ...PORTS[file], inputs: PORTS[file].inputs || null } : null;
        items.push({ dir: name, file, label: stem(file), port });
      }
      items.sort((a, b) => a.file.localeCompare(b.file, "ko"));
      if (items.length) out.push({ id: name, name, items });
    }
    out.sort((a, b) => a.name.localeCompare(b.name, "ko"));
    return out;
  }

  return { SKIP_DIRS, PORTS, build, indicatorIds, stem, inputsFor };
})();

if (typeof globalThis !== "undefined") {
  globalThis.YlTree = YlTree;
}
