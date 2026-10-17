// 시간축 분 그리드. 세션이 있으면 거래시간 밖의 분(야간·주말·장 마감 이후)과
// 봉이 하나도 없는 날(휴장일)은 빈 칸으로 펼치지 않고 다음 봉에 붙인다.
// 봉이 있는 날의 장중 빈 분만 whitespace로 남긴다.
// 세션이 없으면 연속 봉 사이 1분 초과 공백을 전부 분당 1칸으로 채운다 (구 계약).
// whitespace 항목은 가격 필드가 없다. 그 칸은 캔들 없이 남는다.
//
// 왜 엔진 gaps 페이로드를 쓰지 않는가: 종목마다 구멍 채움 정책이 달라(선물은 쉬는
// 시간을 분당 1칸으로 채우지만 밤은 1칸으로 압축, 주식은 밤 전체가 1칸) 엔진 gaps는
// 종목별 정책의 잔여 구멍만 알려준다. 그대로 쓰면 1칸이 나타내는 시간이 종목·구간마다
// 달라 밤샘 경계를 포함한 창에서 같은 시각이 칸마다 다른 x 픽셀에 놓인다 (2026-10-01
// 실측: 같은 09:05가 선물 칸 x=633, 주식 칸 x=47). 균일 분 그리드는 엔진 gaps 채움의
// 상위 집합이라(엔진 구멍은 연속 봉 공백의 부분집합) 표시 경로에서는 엔진 gaps를 더
// 쓰지 않는다 — 페이로드 수신·보관은 유지한다 (app.js seedSymbolNow 참조).
//
// 세션이 없으면 인접 항목 시각 차는 60초다. 세션이 있으면 휴장·무자료 날은 건너뛰므로
// 그 경계에서 시각이 60초보다 크게 뛴다. pane-sync는 seriesTimes로 시각을 환산한다.
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
  // 엔진 tr_session_span과 같다. session = { offset, open, close, days }.
  // days 비트: bit0=일요일 .. bit6=토요일. close <= open 이면 익일 폐장.
  function inSession(unixSec, session) {
    if (!session) return true;
    const off = Number(session.offset) || 0;
    const d = new Date((unixSec + off * 60) * 1000);
    const wd = d.getUTCDay();
    const min = d.getUTCHours() * 60 + d.getUTCMinutes();
    const open = session.open;
    const close = session.close;
    const days = session.days >>> 0;
    const overnight = close <= open;
    if ((days & (1 << wd)) !== 0 && min >= open && (overnight || min < close)) return true;
    if (overnight && (days & (1 << ((wd + 6) % 7))) !== 0 && min < close) return true;
    return false;
  }

  function localDateKey(unixSec, session) {
    const off = session ? (Number(session.offset) || 0) : 0;
    const d = new Date((unixSec + off * 60) * 1000);
    return d.getUTCFullYear() * 10000 + (d.getUTCMonth() + 1) * 100 + d.getUTCDate();
  }

  // 선물 마감 단일가 직전 10분. 체결이 없어 빈 칸으로 두지 않고 15:45 봉에 붙인다.
  function closingAuctionQuiet(unixSec, session) {
    if (!session || session.open !== 8 * 60 + 45 || session.close !== 15 * 60 + 46) return false;
    const off = Number(session.offset) || 0;
    const d = new Date((unixSec + off * 60) * 1000);
    const min = d.getUTCHours() * 60 + d.getUTCMinutes();
    return min >= 15 * 60 + 37 && min < 15 * 60 + 46;
  }

  // prev와 cur 사이(양쪽 제외)에서 빈 칸으로 남길 시각.
  // 거래시간 밖은 뺀다. 세션이 있으면, 양끝 봉의 날짜가 아닌 날은 봉이 없으므로 붙인다.
  function gapMinutes(prev, cur, session) {
    const out = [];
    if (!Number.isFinite(prev) || !Number.isFinite(cur) || cur <= prev + MIN_SEC) return out;
    const prevDay = session ? localDateKey(prev, session) : null;
    const curDay = session ? localDateKey(cur, session) : null;
    for (let t = prev + MIN_SEC; t < cur; t += MIN_SEC) {
      if (!inSession(t, session)) continue;
      if (closingAuctionQuiet(t, session)) continue;
      if (session && localDateKey(t, session) !== prevDay && localDateKey(t, session) !== curDay) continue;
      out.push(t);
    }
    return out;
  }

  function withWhitespace(bars, session) {
    if (!Array.isArray(bars) || bars.length < 2) return Array.isArray(bars) ? bars.slice() : [];
    const sorted = bars.slice().sort((x, y) => x.time - y.time); // 방어 — 호출 측은 이미 오름차순
    const out = [sorted[0]];
    for (let i = 1; i < sorted.length; i++) {
      const prev = sorted[i - 1].time, cur = sorted[i].time;
      for (const t of gapMinutes(prev, cur, session)) out.push({ time: t });
      out.push(sorted[i]);
    }
    return out;
  }

  // 가격이 없는 연속 항목을 {start, end}로 묶는다. start·end는 그 구간의 첫·마지막 시각(초).
  // open·close·value가 있으면 봉이다. 0도 가격이다. 앞뒤 공백도 구간에 넣는다.
  function whitespaceRuns(rows) {
    if (!Array.isArray(rows) || rows.length === 0) return [];
    const runs = [];
    let start = null;
    let end = null;
    for (const row of rows) {
      const priced = row != null && (row.open != null || row.close != null || row.value != null);
      if (row == null || row.time == null || priced) {
        if (start != null) {
          runs.push({ start, end });
          start = null;
        }
        continue;
      }
      if (start == null) start = row.time;
      end = row.time;
    }
    if (start != null) runs.push({ start, end });
    return runs;
  }

  return { withWhitespace, whitespaceRuns, gapMinutes, inSession, MIN_SEC };
})();

if (typeof globalThis !== "undefined") {
  globalThis.Gaps = Gaps;
}
