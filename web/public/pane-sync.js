// 칸 간 동기화 — 시간축(보이는 범위)과 크로스헤어를 모든 칸에 맞춘다.
// 시간축은 '시각 도메인' 기준이다: 종목마다 봉 수·구멍(whitespace) 수가 달라 같은 논리
// 인덱스 창은 칸마다 다른 시각을 가리키므로, 발생 칸의 논리 범위를 시각 창으로 환산한 뒤
// 대상 칸에서 그 시각 창을 덮는 봉 범위로 되돌린다 (propagateRange 주석 참조).
//
// 시간축 전파 계약:
// - 사용자 제스처(휠 줌·드래그 스크롤)로 바뀐 범위는 과거 탐색 중인 칸을 포함한
//   모든 칸에 전파된다 (예외 없음).
// - 전파는 정확히 1회다: 대상 칸에 적용한 범위가 에코 이벤트로 돌아와도 다시 전파하지
//   않는다 (expectEcho — 실측상 에코 값은 적용 값과 정확히 같고 rAF에서 비동기로 온다).
// - 프로그램적 변경(시딩의 setData/scrollToRealTime, 라이브 꼬리 이동의
//   candleSeries.update)은 전파하지 않는다 — 호출 측(app.js)이 그 칸을 mute한다.
//   뮤트된 칸의 범위 이벤트는 그냥 버린다.
//   이벤트 타이밍 실측(lightweight-charts 4.2.3): candleSeries.update는 동기 +
//   다음 프레임의 비동기 이벤트를 모두 내고, setData/setVisibleLogicalRange/
//   scrollToRealTime은 rAF draw에서 비동기로만 낸다 (scrollToRealTime은 400ms
//   스크롤 애니메이션으로 프레임마다 1개씩). 그래서 뮤트는 동기 호출 전에 걸어
//   이벤트가 멈추는 시점(프레임/애니메이션 창)까지 유지해야 한다 — mute/unmute 주석 참조.
// DOM 없는 순수 로직 (node:test 단위 테스트 대상): lightweight-charts 인터페이스
// (timeScale().subscribeVisibleLogicalRangeChange, subscribeCrosshairMove,
//  setVisibleLogicalRange, setCrosshairPosition, clearCrosshairPosition)만 맞으면 동작한다.
// 브라우저에서는 전역 PaneSync, node:test에서는 globalThis.PaneSync로 쓴다.
"use strict";

const PaneSync = (() => {
  // getPrice(timeSec): 그 시각 봉의 대표 가격(종가 등). 없으면 undefined를 돌려야 한다.
  // 칸마다 종목이 다르므로 add의 칸별 getPrice가 우선하고, 없으면 create의 공유 값을 쓴다.
  function create(getPrice) {
    const members = new Set(); // { chart, candleSeries, getPrice?, getLength?, getTimes?, muteCount, expectEcho?, unsubs: [fn] }
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

    // t 이상인 첫 인덱스 / t 이하인 마지막 인덱스의 다음 — 시각→인덱스 변환의 이진 탐색
    function lowerBound(times, t) {
      let lo = 0, hi = times.length;
      while (lo < hi) {
        const mid = (lo + hi) >> 1;
        if (times[mid] < t) lo = mid + 1; else hi = mid;
      }
      return lo;
    }
    function upperBound(times, t) {
      let lo = 0, hi = times.length;
      while (lo < hi) {
        const mid = (lo + hi) >> 1;
        if (times[mid] <= t) lo = mid + 1; else hi = mid;
      }
      return lo;
    }

    // 논리 인덱스(소수 가능)를 시각으로 환산한다. 정수는 그 항목의 시각, 소수는 양옆 항목
    // 시각의 선형 보간. 범위 밖 인덱스는 양끝 항목에 고정한 채 끝 간격으로 외삽한다 —
    // 꼬리 초과 드래그처럼 인덱스가 데이터 밖으로 나가도 같은 시각 폭으로 환산된다.
    function timeAt(times, idx) {
      const n = times.length, last = n - 1;
      if (idx <= 0) {
        if (n < 2 || idx === 0) return times[0];
        return times[0] + idx * (times[1] - times[0]); // 처음보다 왼쪽: 첫 간격으로 외삽
      }
      if (idx >= last) {
        if (n < 2 || idx === last) return times[last];
        return times[last] + (idx - last) * (times[last] - times[last - 1]); // 끝보다 오른쪽
      }
      const i = Math.floor(idx);
      return times[i] + (idx - i) * (times[i + 1] - times[i]); // 소수 인덱스는 선형 보간
    }

    // 시각 창 [tFrom,tTo]를 대상 칸의 논리 범위로 옮긴다:
    // fromIdx = tFrom 이상인 첫 항목, toIdx = tTo 이하인 마지막 항목 — 두 칸이 같은
    // '시계 창'을 보게 된다 (대상 칸의 구멍(whitespace)도 항목으로 세므로 인덱스가 맞다).
    // 창이 대상 데이터와 전혀 겹치지 않으면(전부 과거/미래 밖) 종전 규칙으로 되돌린다 —
    // 발생 칸의 논리 범위를 대상 칸 길이에 클램프하는 가장 가까운 가장자리 창. 시각이 아예
    // 안 닿는 칸까지 같은 창으로 끌어가는 것보다 '같은 폭의 가장 가까운 창'이 덜 놀랍다.
    function timeWindowToLogical(times, tFrom, tTo, srcRange, len) {
      const last = times.length - 1;
      if (tTo < times[0] || tFrom > times[last]) return clampRange(srcRange, len); // 무겹침 폴백
      let fromIdx = lowerBound(times, tFrom);
      let toIdx = upperBound(times, tTo) - 1;
      if (fromIdx > toIdx) {
        // 창이 봉과 봉 사이(구멍)에 들어갔다 — 창 중심에 가까운 쪽 봉 하나라도 보여준다
        fromIdx = toIdx = (times[fromIdx] - (tFrom + tTo) / 2 < (tFrom + tTo) / 2 - times[toIdx])
          ? fromIdx : toIdx;
      }
      return { from: fromIdx, to: toIdx };
    }

    // 꼬리 정렬: 대상 칸을 자기 최신 창으로내되 폭은 '시각 폭'으로 — 발생 칸과 같은
    // 시계 길이를 보게 한다. 시각 창 시작이 대상 데이터보다 앞이면(그만큼의 과거가 없으면)
    // 시각으로는 못 덮으므로 종전 인덱스 폭 창으로 되돌린다.
    function tailWindowByTime(times, widthSec, srcRange) {
      const last = times.length - 1;
      const fromT = times[last] - widthSec;
      if (fromT < times[0]) return { from: last - (srcRange.to - srcRange.from), to: last };
      return { from: lowerBound(times, fromT), to: last };
    }

    // 시간축 동기화: 한 칸의 보이는 범위가 바뀌면 나머지 칸에 '같은 시계 창'으로 맞춘다.
    // 종목마다 봉 수와 구멍(whitespace) 수가 달라(선물 2400봉 vs 주식 500봉) 같은 논리
    // 인덱스를 억지로 맞추면 칸마다 다른 시각을 보게 된다 — 그래서 발생 칸의 논리 범위를
    // 먼저 시각 창으로 환산하고(getTimes, 소수 인덱스는 보간 — timeAt), 대상 칸에서는
    // 그 시각 창을 덮는 항목 범위를 이진 탐색으로 찾아 적용한다 (timeWindowToLogical).
    // - 발생 칸이 최신(오른쪽 가장자리)에 붙어 있으면(논리 판정, srcAtTail) 대상 칸은
    //   같은 시각 폭의 자기 최신 창으로 보낸다 (tailWindowByTime — 최대 축소의 꼬리 정렬이면
    //   모든 칸이 전체를 보게 된다).
    // - 시각 창이 대상 칸 데이터와 전혀 겹치지 않으면 종전대로 논리 범위의 가장 가까운
    //   가장자리 창으로 클램프한다 (clampRange — 시각이 안 닿는 칸을 끌어가지 않는다).
    // - getTimes를 제공하지 않는 칸(테스트·구 호출자)은 종전 논리 규칙을 그대로 따른다.
    // 적용은 대상 칸에서 에코 이벤트를 낳는다 (실측: rAF에서 비동기로, 값은 적용 값과
    // 정확히 같다). 이 에코가 다시 전파되면 예를 들어 발생 칸의 깊은 과거 탐색이 대상 칸
    // 데이터 밖이라 꼬리 창으로 클램프됐을 때, 그 꼬리 에코가 발생 칸을 꼬리로 끌어간다 —
    // 그래서 expectEcho와 정확히 일치하는 이벤트는 전파하지 않고 삼킨다 (다른 값이면
    // 사용자 제스처 등 실제 변경이므로 그대로 전파한다).
    // 뮤트된 칸(mute)의 이벤트는 전파하지 않는다 — 시딩·라이브 꼬리 이동 같은 프로그램적
    // 변경이 다른 칸의 탐색 위치를 빼앗지 않게 호출 측에서 뮤트한다.
    function propagateRange(src, range) {
      if (!range) return; // 데이터 없는 차트는 null 범위를 보낼 수 있다
      // 우리가 이 칸에 적용한 범위가 그대로 돌아온 에코는 삼킨다
      if (src.expectEcho && range.from === src.expectEcho.from && range.to === src.expectEcho.to) {
        src.expectEcho = null;
        return;
      }
      if (syncing) return;
      if (src.muteCount > 0) return; // 프로그램적 변경(뮤트된 칸)은 전파하지 않는다
      const srcLen = src.getLength?.();
      const srcAtTail = Number.isFinite(srcLen) && srcLen > 0 && range.to >= srcLen - 1;
      // 발생 칸의 논리 범위를 시각 창으로 환산한다 — 시각이 칸 간 공통 기준이다
      const srcTimes = src.getTimes?.();
      const tWin = Array.isArray(srcTimes) && srcTimes.length > 0
        ? { from: timeAt(srcTimes, range.from), to: timeAt(srcTimes, range.to) }
        : null;
      syncing = true;
      try {
        for (const m of members) {
          if (m === src) continue;
          const len = m.getLength?.();
          if (!Number.isFinite(len)) {
            m.expectEcho = range;
            m.chart.timeScale().setVisibleLogicalRange(range); // 길이 미제공 칸은 종전대로
            continue;
          }
          if (len <= 0) continue; // 데이터 없는 칸에는 적용하지 않는다
          const mTimes = m.getTimes?.();
          let next;
          if (tWin && Array.isArray(mTimes) && mTimes.length > 0) {
            next = srcAtTail
              ? tailWindowByTime(mTimes, tWin.to - tWin.from, range) // 같은 시각 폭의 자기 최신 창
              : timeWindowToLogical(mTimes, tWin.from, tWin.to, range, len); // 같은 시계 창
          } else {
            // 시각 정보가 없는 쪽이 끼어 있으면 종전 논리 규칙으로 폴백한다 (호환)
            next = srcAtTail
              ? { from: len - 1 - (range.to - range.from), to: len - 1 } // 대상 칸의 최신 창
              : clampRange(range, len);
          }
          // 에코 예약은 적용 전에 건다 — 이벤트가 동기로 나오는 구현이어도 삼킬 수 있게
          m.expectEcho = next;
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
    // options.getLength: 그 칸의 시리즈 길이 — 꼬리 판정(srcAtTail)과 시각 정보 없는
    //   칸의 폴백(클램프·인덱스 폭 최신 창)에 쓴다.
    // options.getTimes: 그 칸 시리즈(봉+whitespace)의 항목별 시각(초) 오름차순 배열 —
    //   시간축 전파를 시각 도메인으로 환산하는 기준. 없으면 종전 논리 규칙으로 동작한다.
    function add(chart, candleSeries, options) {
      const member = { chart, candleSeries,
                       getPrice: options?.getPrice, getLength: options?.getLength,
                       getTimes: options?.getTimes,
                       muteCount: 0, expectEcho: null, unsubs: [] };
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

    // 그 칸의 범위 이벤트 전파를 끈다/켠다 (시딩·라이브 꼬리 이동 같은 프로그램적 변경용).
    // 겹친 뮤트 창이 서로를 풀지 않게 중첩을 센다. 뮤트는 발생 이벤트만 막을 뿐,
    // 다른 칸의 전파를 받는 대상 규칙은 그대로다.
    // 주의: 범위 이벤트는 동기 호출 안과 뒤따르는 rAF 프레임(애니메이션이면 그 전체)에
    // 걸쳐 나오므로(파일 헤더 실측 참조), 호출 측이 그 창만큼 뮤트를 유지해야 한다.
    function mute(member) {
      if (member) member.muteCount++;
    }
    function unmute(member) {
      if (member && member.muteCount > 0) member.muteCount--;
    }

    return { add, remove, mute, unmute, get size() { return members.size; } };
  }

  return { create };
})();

if (typeof globalThis !== "undefined") {
  globalThis.PaneSync = PaneSync;
}
