// 시간축 균일 분 그리드 — 모든 칸의 시리즈를 매분 1칸으로 채운다 (2026-10-01 확정,
// 2026-09-30 RT 공백 사건 후속 G2의 재설계).
// 연속 봉 사이의 1분 초과 공백을 전부 분당 1칸 whitespace({time}만 있는 항목)로 펼쳐
// 캔들 사이에 섞는다 — 밤·주말·프리마켓·무틱 공백을 가리지 않는다. lightweight-charts
// whitespace 항목은 가격 필드가 없어 그 자리는 캔들 없이 빈 칸으로 그려진다.
//
// 왜 엔진 gaps 페이로드를 쓰지 않는가: 종목마다 구멍 채움 정책이 달라(선물은 쉬는
// 시간을 분당 1칸으로 채우지만 밤은 1칸으로 압축, 주식은 밤 전체가 1칸) 엔진 gaps는
// 종목별 정책의 잔여 구멍만 알려준다. 그대로 쓰면 1칸이 나타내는 시간이 종목·구간마다
// 달라 밤샘 경계를 포함한 창에서 같은 시각이 칸마다 다른 x 픽셀에 놓인다 (2026-10-01
// 실측: 같은 09:05가 선물 칸 x=633, 주식 칸 x=47). 균일 분 그리드는 엔진 gaps 채움의
// 상위 집합이라(엔진 구멍은 연속 봉 공백의 부분집합) 표시 경로에서는 엔진 gaps를 더
// 쓰지 않는다 — 페이로드 수신·보관은 유지한다 (app.js seedSymbolNow 참조).
//
// 불변식 (1칸=1분): 반환 배열에서 첫 봉~마지막 봉 사이의 인접 항목 시각 차는 항상
// 정확히 60초다. 그래서 모든 칸에서 logicalAt/timeAt(pane-sync)이 기울기 60초의
// 아핀 변환이 되고, 창 가장자리가 같은 시계 창이면 모든 공유 시각의 x가 픽셀 단위로
// 일치한다 — 다중 차트 칸 시간축 동기화(엄밀 시각 정렬)가 이 성질에 기댄다.
// 첫 봉 이전과 마지막 봉 이후는 채우지 않는다 (스팬 남부만 — 스팬은 데이터가 정한다).
// 라이브 꼬리는 feed.js noteBar가 같은 규칙으로 채우며 시리즈를 민다.
// DOM 없는 순수 로직 (node:test 단위 테스트 대상).
// 브라우저에서는 전역 Gaps, node:test에서는 globalThis.Gaps로 쓴다.
"use strict";

const Gaps = (() => {
  const MIN_SEC = 60; // 1분봉 간격 (엔진 timeframe_sec=60) — 균일 그리드의 칸 폭

  // withWhitespace(bars): 시각 오름차순 캔들 행({time: 초, open, ...})을 받아, 연속 봉
  // 사이의 빈 매분을 {time}만 가진 whitespace 항목으로 펼쳐 섞은 새 배열을 돌려준다.
  // - 봉이 2개 미만이면 채울 공백이 없으므로 복사본 그대로다.
  // - whitespace는 양끝 봉의 엄격한 사이 시각에만 만든다 — 봉과 겹치는 포인트는 절대
  //   생기지 않는다 (같은 time이 두 번이면 setData가 거부하므로 구조적으로 방어).
  // - 반환 길이 − bars.length = whitespace 수. pane-sync의 getLength가 시리즈 길이
  //   (봉 + whitespace)를 돌려주는 데 이 차이(wsCount)를 쓴다 (app.js 주석 참조).
  function withWhitespace(bars) {
    if (!Array.isArray(bars) || bars.length < 2) return Array.isArray(bars) ? bars.slice() : [];
    const sorted = bars.slice().sort((x, y) => x.time - y.time); // 방어 — 호출 측은 이미 오름차순
    const out = [sorted[0]];
    for (let i = 1; i < sorted.length; i++) {
      const prev = sorted[i - 1].time, cur = sorted[i].time;
      for (let t = prev + MIN_SEC; t < cur; t += MIN_SEC) out.push({ time: t });
      out.push(sorted[i]);
    }
    return out;
  }

  return { withWhitespace, MIN_SEC };
})();

if (typeof globalThis !== "undefined") {
  globalThis.Gaps = Gaps;
}
