// 종목별 데이터 피드 — 캐시 격리와 세대(generation) 추적 (계획서 §18, 다중 종목 Task 4).
// 칸(pane)은 종목(shcode)을 하나씩 보고, 같은 종목을 보는 칸들은 같은 캐시를 공유한다.
// status의 payload.shcode로 캐시를 골라 갱신한다 (엔진이 status 페이로드 끝에 shcode를 싣는다).
// DOM 없는 순수 로직 (node:test 단위 테스트 대상).
// 브라우저에서는 전역 Feed, node:test에서는 globalThis.Feed로 쓴다.
"use strict";

const Feed = (() => {
  function createCache(shcode) {
    return {
      shcode,
      name: "",          // 표시용 종목명 (watch 응답·검색 결과에서 채운다)
      bars: new Map(),   // time(sec) → candle
      barInd: new Map(), // time → 봉별 지표 엔트리 (mirae-layers barIndFrom* 형태)
      barSeq: [],        // 시각 오름차순 목록
      barPos: new Map(), // time → barSeq 인덱스
      tickRaw: 5,        // raw 단위 틱 크기 (엔진 tick 키가 갱신; 선물 5, 주식 100)
      generation: 0,     // 이 종목의 최신 세대 — 더 큰 세대가 오면 리셋 트리거
      seedToken: 0,      // 리셋 때마다 증가 — 진행 중 시딩의 늦은 응답 폐기에 쓴다
      gaps: [],          // 시딩 스냅샷의 구멍 구간 ([startSec, endSec]) — 시리즈 whitespace로 펼친다
      wsCount: 0,        // 시딩이 만든 whitespace 포인트 수 — pane-sync getLength가 봉 수에 더해 쓴다
      seriesTimes: [],   // 차트 시리즈(봉+whitespace) 항목별 시각(초) 오름차순 — pane-sync getTimes의
                         // 시각 변환 기준. renderSymbolPanes가 시딩 때 통째로 세우고 noteBar가 라이브를 민다
      ctx: null,         // 렌더러 컨텍스트 (app.js가 지연 생성해 붙인다)
    };
  }

  function create() {
    const caches = new Map(); // shcode → cache

    // 캐시 조회(없으면 생성). 라이브 status는 이 함수로 종목별 캐시에 모인다.
    function forSymbol(shcode) {
      let c = caches.get(shcode);
      if (!c) {
        c = createCache(shcode);
        caches.set(shcode, c);
      }
      return c;
    }
    const get = (shcode) => caches.get(shcode);
    const symbols = () => [...caches.keys()];

    // 캐시 제거 — 어느 칸도 보지 않는 종목을 unwatch할 때 정리한다.
    // 진행 중인 시딩이 있으면 고아 캐시를 채우지만 맵에서 빠졌으므로 렌더되지 않는다.
    const drop = (shcode) => caches.delete(shcode);

    // 캐시를 비운다 (세대 교체·엔진 재시작·재시딩). 맵/배열 참조는 유지하므로
    // 렌더러에 건넨 ctx가 끊기지 않는다. seedToken을 올려 진행 중인 시딩이
    // 리셋 이후 상태를 늦게 덮어쓰지 않게 한다.
    function reset(cache) {
      cache.bars.clear();
      cache.barInd.clear();
      cache.barSeq.length = 0;
      cache.barPos.clear();
      cache.tickRaw = 5;
      cache.gaps = [];   // ctx가 잡는 참조가 아니라 새 배열로 바꿔도 된다
      cache.wsCount = 0;
      cache.seriesTimes.length = 0; // renderSymbolPanes가 시딩 끝에 다시 세운다
      cache.seedToken++;
    }

    // 봉 기록: 라이브는 대부분 뒤에 붙는다. 늦은 정정 등 순서 역행만 이진 삽입으로 처리한다.
    // 반환: 새 시각이 추가됐으면 true, 같은 시각 갱신이면 false — 차트 범위 이벤트는
    // 새 봉이 붙을 때만 발생하므로, 호출자(라이브 뮤트)가 갱신과 추가를 구분할 수 있게 한다.
    function noteBar(cache, t, bar) {
      cache.bars.set(t, bar);
      if (cache.barPos.has(t)) return false;
      // seriesTimes에도 같은 시각을 넣는다 — whitespace가 섞여 있으면 barSeq 인덱스와
      // 어긋나므로(구멍 수만큼 왼쪽으로 당겨진다) 같은 인덱스가 아니라 별도 이진 탐색으로
      // 오름차순 위치를 찾는다. 구멍(whitespace) 자리를 늦은 봉이 채우면 그 시각은 이미
      // 있다 — 차트도 update가 그 자리를 캔들로 교체할 뿐 길이가 늘지 않으므로 중복 삽입
      // 없이 wsCount만 내린다 (getLength = barSeq + wsCount 정합 유지).
      const st = cache.seriesTimes;
      let sLo = 0, sHi = st.length;
      while (sLo < sHi) {
        const sMid = (sLo + sHi) >> 1;
        if (st[sMid] < t) sLo = sMid + 1; else sHi = sMid;
      }
      if (st[sLo] === t) {
        if (cache.wsCount > 0) cache.wsCount--;
      } else {
        st.splice(sLo, 0, t);
      }
      const seq = cache.barSeq;
      if (seq.length === 0 || t > seq[seq.length - 1]) {
        cache.barPos.set(t, seq.length);
        seq.push(t);
        return true;
      }
      let lo = 0, hi = seq.length;
      while (lo < hi) {
        const mid = (lo + hi) >> 1;
        if (seq[mid] < t) lo = mid + 1; else hi = mid;
      }
      seq.splice(lo, 0, t);
      for (let i = lo; i < seq.length; i++) cache.barPos.set(seq[i], i);
      return true;
    }

    // barSeq[pos]까지 최근 n개 봉 (오름차순) — ⑥⑦ 5봉 규칙에 사용
    function recentBars(cache, pos, n) {
      const out = [];
      if (pos === undefined) return out;
      for (let i = Math.max(0, pos - n + 1); i <= pos; i++) {
        const b = cache.bars.get(cache.barSeq[i]);
        if (b) out.push(b);
      }
      return out;
    }

    // 세대 확인: 새 세대가 오면 기록하고 true. 호출자가 reset과 칸 정리를 한다.
    // 종목 전환 후 낮은 세대의 늦은 메시지는 false라 리셋을 일으키지 않는다.
    function noteGeneration(cache, gen) {
      if (typeof gen !== "number" || gen <= cache.generation) return false;
      cache.generation = gen;
      return true;
    }

    return { forSymbol, get, symbols, drop, reset, noteBar, recentBars, noteGeneration };
  }

  return { create };
})();

if (typeof globalThis !== "undefined") {
  globalThis.Feed = Feed;
}
