// 칸 간 동기화 — 시간축(보이는 범위)과 크로스헤어를 모든 칸에 맞춘다.
// 시간축은 논리 인덱스 기준이되 종목별 데이터 길이 차이를 고려한다 (propagateRange 주석 참조).
// DOM 없는 순수 로직 (node:test 단위 테스트 대상): lightweight-charts 인터페이스
// (timeScale().subscribeVisibleLogicalRangeChange, subscribeCrosshairMove,
//  setVisibleLogicalRange, setCrosshairPosition, clearCrosshairPosition)만 맞으면 동작한다.
// 브라우저에서는 전역 PaneSync, node:test에서는 globalThis.PaneSync로 쓴다.
"use strict";

const PaneSync = (() => {
  // getPrice(timeSec): 그 시각 봉의 대표 가격(종가 등). 없으면 undefined를 돌려야 한다.
  // 칸마다 종목이 다르므로 add의 칸별 getPrice가 우선하고, 없으면 create의 공유 값을 쓴다.
  function create(getPrice) {
    const members = new Set(); // { chart, candleSeries, getPrice?, getLength?, unsubs: [fn] }
    let syncing = false;       // 적용이 다시 이벤트를 일으키는 재진입(무한 루프) 방지

    // 창 [from,to]를 대상 칸의 데이터 길이 안으로 클램프한다.
    // 데이터와 전혀 겹치지 않으면 가장 가까운 가장자리 창으로 이동한다.
    function clampRange(range, len) {
      const last = len - 1;
      if (range.from <= last && range.to >= 0) return range; // 데이터와 겹치면 그대로
      const width = range.to - range.from;
      return range.from > last
        ? { from: last - width, to: last } // 끝보다 오른쪽 → 최신 창
        : { from: 0, to: width };          // 처음보다 왼쪽 → 첫 창
    }

    // 시간축 동기화: 한 칸의 보이는 범위가 바뀌면 나머지 칸에 맞춘다.
    // 종목마다 데이터 길이가 다르므로(선물 2400봉 vs 주식 500봉) 같은 논리 인덱스를
    // 억지로 맞추면 대상 칸이 자기 데이터 밖이나 엉뚱한 과거 구간으로 끌린다:
    // - 발생 칸이 최신(오른쪽 가장자리)에 붙어 있으면 대상 칸은 자기 최신 창으로 보낸다
    //   (라이브 꼬리 따라가기·시딩 직후 초기화가 이 경우다).
    // - 그 외(중간 구간 탐색)는 같은 논리 범위를 대상 칸의 데이터 범위로 클램프해 적용한다.
    function propagateRange(src, range) {
      if (syncing || !range) return; // 데이터 없는 차트는 null 범위를 보낼 수 있다
      const srcLen = src.getLength?.();
      const srcAtTail = Number.isFinite(srcLen) && srcLen > 0 && range.to >= srcLen - 1;
      syncing = true;
      try {
        for (const m of members) {
          if (m === src) continue;
          const len = m.getLength?.();
          if (!Number.isFinite(len)) {
            m.chart.timeScale().setVisibleLogicalRange(range); // 길이 미제공 칸은 종전대로
            continue;
          }
          if (len <= 0) continue; // 데이터 없는 칸에는 적용하지 않는다
          const next = srcAtTail
            ? { from: len - 1 - (range.to - range.from), to: len - 1 } // 대상 칸의 최신 창
            : clampRange(range, len);
          m.chart.timeScale().setVisibleLogicalRange(next);
        }
      } finally {
        syncing = false;
      }
    }

    // 크로스헤어 동기화: 같은 시각의 수직선을 나머지 칸에 표시한다.
    // param.time이 없으면(마우스 이탈) 나머지 칸의 크로스헤어도 지운다.
    function propagateCrosshair(src, param) {
      if (syncing) return;
      syncing = true;
      try {
        const time = param?.time;
        for (const m of members) {
          if (m === src) continue;
          if (time === undefined || time === null) {
            m.chart.clearCrosshairPosition();
          } else {
            // 가로선은 칸마다 자기 종목 캐시의 값이어야 하므로 칸별 getPrice를 쓴다
            const price = (m.getPrice ?? getPrice)?.(time);
            if (Number.isFinite(price)) m.chart.setCrosshairPosition(price, time, m.candleSeries);
          }
        }
      } finally {
        syncing = false;
      }
    }

    // 칸 등록: 두 구독을 붙이고 해제 핸들을 돌려준다 (칸 삭제 시 remove에 넘긴다).
    // options.getPrice: 크로스헤어 가로선 값을 그 칸의 종목 캐시에서 찾는다.
    // options.getLength: 그 칸의 봉 개수 — 시간축 전파의 클램프/최신 창 계산에 쓴다.
    function add(chart, candleSeries, options) {
      const member = { chart, candleSeries,
                       getPrice: options?.getPrice, getLength: options?.getLength, unsubs: [] };
      const ts = chart.timeScale();
      const onRange = (range) => propagateRange(member, range);
      const onCross = (param) => propagateCrosshair(member, param);
      ts.subscribeVisibleLogicalRangeChange(onRange);
      chart.subscribeCrosshairMove(onCross);
      member.unsubs.push(
        () => ts.unsubscribeVisibleLogicalRangeChange(onRange),
        () => chart.unsubscribeCrosshairMove(onCross),
      );
      members.add(member);
      return member;
    }

    // 칸 해제: 구독을 모두 떼어 차트 제거 후에도 리스너가 남지 않게 한다
    function remove(member) {
      if (!member || !members.delete(member)) return;
      for (const u of member.unsubs) u();
      member.unsubs.length = 0;
    }

    return { add, remove, get size() { return members.size; } };
  }

  return { create };
})();

if (typeof globalThis !== "undefined") {
  globalThis.PaneSync = PaneSync;
}
