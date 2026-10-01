// 칸 간 동기화 — 시간축(보이는 범위)과 크로스헤어를 모든 칸에 맞춘다.
// 시간축 규칙 (2026-10-01 개정): 모든 칸이 '같은 px 봉 간격(같은 줌) + 같은 오른쪽 끝
// 시각'을 쓰도록 맞춘다. 종목마다 봉 수·구멍(whitespace) 수가 달라 같은 논리 인덱스는
// 칸마다 다른 시각을 가리키므로, 발생 칸의 오른쪽 끝 논리 인덱스를 시각으로 환산한 뒤
// (timeAt) 대상 칸에서 그 시각의 논리 인덱스(logicalAt)를 구해 오른쪽 끝에 두고, 대상 칸
// 폭에 맞는 슬롯 수만큼 왼쪽으로 펼친다. 차트 영역의 오른쪽 끝은 칸의 접이식 지표 패널
// 상태와 무관하게 항상 같은 윈도우 x라서(패널은 왼쪽·차트는 오른쪽 정렬 — app.js
// createPane), 오른쪽 끝 시각과 px 봉 간격이 같으면 모든 공유 시각이 모든 칸에서 같은
// x에 놓인다 — 칸 폭이 달라도 수직선이 어긋나지 않고, 넓은 칸은 왼쪽에 더 많은 이력이
// 보일 뿐이다. 균일 분 그리드(1칸=1분)라 시간↔논리 인덱스가 아핀이라 이 환산이 정확하다.
// 폭 정보(getWidth)가 없는 칸(테스트·구 호출자)은 종전 '같은 시계 창' 규칙으로 폴백한다.
//
// 시간축 전파 계약:
// - 모든 칸은 항상 같은 오른쪽 끝 시각과 같은 px 봉 간격을 보여야 한다 (2026-10-01
//   개정) — 꼬리(최신) 창이든 과거 탐색 창이든 예외 없다. 대상 칸에 데이터가 없는 구간
//   (마감 종목의 미래 등)은 빈 영역으로 둔다: 없는 데이터를 없는 대로 보이는 것은
//   구멍 whitespace 표시와 같은 데이터 정직성 원칙이다. 라이브러리 제약으로 딱 하나
//   예외가 있다: 창이 대상 칸 데이터와 전혀 겹치지 않으면(무겹침 0) lightweight-charts가
//   폭을 유지한 채 데이터에 닿는 위치로 강제 시프트하므로, 그 칸은 같은 규칙의 가장
//   가까운 가장자리 창으로 먼저 클램프해 적용한다 — 겹치는 시각대로 돌아오면 규칙
//   적용이 즉시 재개된다 (propagateRange 주석의 2026-09-30 계측 참조).
// - 이 계약은 fixLeftEdge/fixRightEdge를 끈 상태에서만 성립한다 (app.js chartOptions
//   주석의 실측 참조 — 켜져 있으면 프로그램적 적용도 클램프되어 expectEcho가 어긋난다).
// - 전파는 정확히 1회다: 대상 칸에 적용한 범위가 에코 이벤트로 돌아와도 다시 전파하지
//   않는다 (expectEcho — 에코는 rAF에서 비동기로 온다. 무겹침 0 창의 강제 시프트는
//   선클램프가 막고(propagateRange 주석의 계측 참조), 남은 1ulp 수준 재구성 노이즈는
//   절대차 1e-6의 허용오차 비교가 2차 방어로 삼킨다).
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
  // 에코 판정 허용오차(논리 인덱스 단위) — propagateRange 주석의 실측(극단 범위 에코에
  // 붙는 1ulp 부동소수점 노이즈) 참조. 사용자 제스처의 최소 변화보다 수십만 배 작다.
  const ECHO_TOLERANCE = 1e-6;

  // t 이상인 첫 인덱스 — 시각→인덱스 변환(logicalAt)의 이진 탐색
  function lowerBound(times, t) {
    let lo = 0, hi = times.length;
    while (lo < hi) {
      const mid = (lo + hi) >> 1;
      if (times[mid] < t) lo = mid + 1; else hi = mid;
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

  // 시각(초)을 시리즈의 논리 인덱스로 환산한다 — timeAt의 정확한 역함수.
  // 항목과 정확히 겹치는 시각은 그 정수 인덱스, 사이 시각은 양옆 항목의 선형 보간(소수),
  // 범위 밖 시각은 양끝 간격으로 외삽한 음수·초과 인덱스를 돌려준다 (클램프 없음) —
  // 엄밀 시각 정렬이 대상 칸 데이터 밖의 창도 빈 영역으로 보이게 하는 데 쓴다.
  // 불변식: 모든 실수 idx에 대해 logicalAt(times, timeAt(times, idx)) === idx
  // (부동소수점 오차 허용 — 단위 테스트로 검증한다).
  function logicalAt(times, t) {
    const n = times.length;
    if (n === 0) return undefined; // 호출 측에서 가드한다
    if (n < 2) return 0; // 항목이 하나뿐이면 인덱스는 0뿐이다
    const last = n - 1;
    if (t <= times[0]) {
      if (t === times[0]) return 0;
      const step = times[1] - times[0];
      return step > 0 ? (t - times[0]) / step : 0; // 처음보다 왼쪽: 첫 간격으로 외삽 (음수)
    }
    if (t >= times[last]) {
      if (t === times[last]) return last;
      const step = times[last] - times[last - 1];
      return step > 0 ? last + (t - times[last]) / step : last; // 끝보다 오른쪽 외삽
    }
    const i = lowerBound(times, t); // times[i] >= t인 첫 인덱스
    if (times[i] === t) return i;
    return (i - 1) + (t - times[i - 1]) / (times[i] - times[i - 1]); // 사이 시각은 선형 보간
  }

  // getPrice(timeSec): 그 시각 봉의 대표 가격(종가 등). 없으면 undefined를 돌려야 한다.
  // 칸마다 종목이 다르므로 add의 칸별 getPrice가 우선하고, 없으면 create의 공유 값을 쓴다.
  function create(getPrice) {
    const members = new Set(); // { chart, candleSeries, getPrice?, getLength?, getTimes?, getWidth?, muteCount, expectEcho?, unsubs: [fn] }
    let syncing = false;       // 적용이 다시 이벤트를 일으키는 재진입(무한 루프) 방지

    // 창 [from,to]를 대상 칸의 데이터 길이 안으로 클램프한다 — 레거시 경로(시각 정보
    // 미제공 칸)의 폴백 전용. 데이터와 전혀 겹치지 않으면 가장 가까운 가장자리 창으로 이동한다.
    function clampRange(range, len) {
      const last = len - 1;
      if (range.from <= last && range.to >= 0) return range; // 데이터와 겹치면 그대로
      const width = range.to - range.from;
      return range.from > last
        ? { from: last - width, to: last } // 끝보다 오른쪽 → 최신 창
        : { from: 0, to: width };          // 처음보다 왼쪽 → 첫 창
    }


    // 시간축 동기화: 한 칸의 보이는 범위가 바뀌면 나머지 칸을 '같은 px 봉 간격 + 같은
    // 오른쪽 끝 시각'으로 맞춘다. 발생 칸의 오른쪽 끝 논리 인덱스를 시각으로 환산하고
    // (timeAt), 대상 칸에서는 그 시각의 논리 인덱스를 logicalAt으로 구해 오른쪽 끝에 둔
    // 뒤, 대상 칸 폭 / 발생 칸 px 봉 간격 = 슬롯 수만큼 왼쪽으로 펼친다. 기하학적 근거는
    // 파일 헤더 참조 — 차트 영역의 오른쪽 끝이 패널 상태와 무관하게 같은 윈도우 x이므로,
    // 오른쪽 끝 정렬 + 같은 간격이면 공유 시각의 x가 칸 폭과 무관하게 일치한다.
    // - 사용자 제스처(휠 줌·드래그)든 꼬리(최신) 창이든 예외 없이 같은 규칙이다 —
    //   발생 칸이 최신에 붙어 있어도 대상 칸을 자기 꼬리로 별도 처리하지 않는다. 마감
    //   종목의 미래 구간처럼 대상 칸에 데이터가 없는 구간은 빈 영역으로 두는 게 의도된
    //   표시다 (구멍 whitespace와 같은 데이터 정직성 원칙).
    // - 폭 정보(getWidth)가 어느 한쪽이라도 없으면(테스트·구 호출자) 종전 '같은 시계
    //   창' 규칙으로 폴백한다: 발생 칸의 논리 범위를 시각 창 [t0,t1]로 환산해 대상 칸의
    //   같은 시각 창으로 되돌린다.
    // - getTimes를 제공하지 않는 칸(테스트·구 호출자)은 레거시 논리 규칙을 그대로 따른다
    //   (꼬리면 대상 칸의 인덱스 폭 최신 창, 아니면 데이터와 겹치게 클램프 — clampRange).
    // 적용은 대상 칸에서 에코 이벤트를 낳는다 (실측: rAF에서 비동기로). 데이터에 조금이라도
    // 겹치는 창(데이터 밖 초과 포함)은 적용 값이 정확히 그대로 돌아온다 (2026-09-30 실측:
    // {from:570.5,to:650.25}, {from:-40.5,to:30.25} 정확 왕복). 그러나 데이터와 전혀
    // 겹치지 않는 창(무겹침 0)은 라이브러리가 폭을 유지한 채 데이터에 닿는 위치로 강제
    // 시프트한다 — 2026-09-30 라이브 계측(적용 값과 이벤트 값을 동시 기록): pane-sync가
    // {from:-2745,to:-1325}를 적용했는데 발생한 이벤트는 {from:-1418.9999999999998,to:1}
    // (폭 1420을 유지한 채 오른쪽으로 1326 시프트). 이 시프트된 에코는 expectEcho와 대폭
    // 어긋나 실제 변경으로 오인되어 재전파·진동을 일으켰다 (같은 날 로그: 에코가 되돌아와
    // 발생 칸이 {from:806,to:2000}으로 점프). 그래서 무겹침 0은 아래 엄밀 경로에서 우리가
    // 먼저 같은 규칙(폭 유지, 가장 가까운 가장자리)으로 클램프해 적용한다 — 주 방어로,
    // 라이브러리가 받아들이는 값과 expectEcho가 일치해 에코가 정상적으로 삼켜진다.
    // 시프트 과정의 1ulp 수준 재구성 노이즈(위 계측의 …9999999999999998)에는 expectEcho의
    // 허용오차 비교(from/to 각각 절대차 1e-6, 논리 인덱스)가 2차 방어다 — 실제 사용자
    // 제스처(드래그 팬 ≥ ~0.5봉, 휠 줌 수 %)는 이보다 수십만 배 커 오삼킴 위험이 없다.
    // (fixLeftEdge/fixRightEdge가 켜져 있으면 라이브러리가 프로그램적 적용도 클램프해
    // 에코가 크게 어긋난다 — 602개 항목에 {from:570,to:650} 적용 → 읽기 {from:521,to:601},
    // 2026-09-30 실측. 두 옵션은 꺼 둔다.)
    // 뮤트된 칸(mute)의 이벤트는 전파하지 않는다 — 시딩·라이브 꼬리 이동 같은 프로그램적
    // 변경이 다른 칸의 탐색 위치를 빼앗지 않게 호출 측에서 뮤트한다.
    function propagateRange(src, range) {
      if (!range) return; // 데이터 없는 차트는 null 범위를 보낼 수 있다
      // 우리가 이 칸에 적용한 범위가 돌아온 에코는 삼킨다 — 무겹침 0 창의 강제 시프트는
      // 아래 선클램프가 막고, 남은 1ulp 수준 재구성 노이즈는 이 허용오차 비교가 막는다
      // (2차 방어, 위 계측 참조). 불일치면 그 이벤트는 에코가 아니라 실제 변경이라는
      // 뜻이므로(에코는 적용 직후 프레임에 오는 게 계약) 예약을 지우고 전파를 진행한다 —
      // 남겨 두면 먼 미래의 우연한 근사 일치를 삼킬 수 있다.
      if (src.expectEcho) {
        const echo = Math.abs(range.from - src.expectEcho.from) <= ECHO_TOLERANCE
                  && Math.abs(range.to - src.expectEcho.to) <= ECHO_TOLERANCE;
        src.expectEcho = null;
        if (echo) return;
      }
      if (syncing) return;
      if (src.muteCount > 0) return; // 프로그램적 변경(뮤트된 칸)은 전파하지 않는다
      // 발생 칸의 논리 범위를 시각 창으로 환산한다 — 시각이 칸 간 공통 기준이다
      const srcTimes = src.getTimes?.();
      const tWin = Array.isArray(srcTimes) && srcTimes.length > 0
        ? { from: timeAt(srcTimes, range.from), to: timeAt(srcTimes, range.to) }
        : null;
      // srcAtTail은 레거시 경로(시각 정보 미제공 칸)의 폴백에서만 쓴다 — 새 규칙에는
      // 꼬리 분기가 없다
      const srcLen = src.getLength?.();
      const srcAtTail = Number.isFinite(srcLen) && srcLen > 0 && range.to >= srcLen - 1;
      // 발생 칸 차트 영역의 px 폭 — 폭 제공 칸끼리는 이것으로 px 봉 간격을 구한다
      const srcWidth = src.getWidth?.();
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
            const mWidth = m.getWidth?.();
            let raw;
            if (Number.isFinite(srcWidth) && srcWidth > 0 && range.to > range.from
                && Number.isFinite(mWidth) && mWidth > 0) {
              // 새 규칙: 같은 px 봉 간격 + 같은 오른쪽 끝 시각. px 봉 간격 =
              // 폭 / (보이는 슬롯 수 + 1) — lightweight-charts 4.2.3의 실제 관계다
              // (읽기 _private__updateVisibleRange: leftBorder = rightBorder −
              // width/barSpacing + 1, 쓰기 _internal_setVisibleRange: barSpacing =
              // width/(to−from+1)). 대상 칸에는 슬롯 수 = mWidth/spacingPx − 1을 적용한다 —
              // 라이브러리가 그 창에 설정할 간격 mWidth/(slots+1)이 spacingPx와 정확히 같아
              // 모든 폭에서 '같은 간격' 불변식이 구성상 성립한다 (단순 폭/슬롯 식은 폭이
              // 다른 칸에서 ~1% 어긋나 오른쪽 끝에서 멀어질수록 x 드리프트가 커진다).
              // (라이브러리의 timeScale().barSpacing()을 읽지 않는 이유: 목 차트와 무관하게
              // 같은 식을 쓸 수 있고, 남아 있는 불일치가 없어 expectEcho 계약이 깨질 경로를 없앤다.)
              const spacingPx = srcWidth / (range.to - range.from + 1);
              const slots = mWidth / spacingPx - 1;
              const toM = logicalAt(mTimes, tWin.to); // 오른쪽 끝 시각을 대상 칸 인덱스로
              raw = { from: toM - slots, to: toM };
            } else {
              // 폭 미제공 칸(테스트·구 호출자)은 종전 '같은 시계 창' 규칙으로 폴백한다 —
              // 데이터에 조금이라도 겹치는 창(오른쪽/왼쪽 초과 포함)은 raw 그대로 적용해
              // 빈 영역으로 보인다 (마감 종목의 미래 구간 등, 의도된 표시)
              raw = { from: logicalAt(mTimes, tWin.from), to: logicalAt(mTimes, tWin.to) };
            }
            // 무겹침 0 창은 라이브러리가 폭 유지로 강제 시프트하므로(위 계측 로그 참조)
            // 우리가 먼저 같은 규칙의 가장 가까운 가장자리 창으로 클램프한다 — 라이브러리가
            // 받아들이는 값과 expectEcho가 일치해 에코가 삼켜지고 재전파가 없다. 사용자가
            // 창을 데이터가 전혀 없는 시각대로 옮기면 그 칸은 자기 데이터의 가장 가까운
            // 가장자리에 고정되고, 겹치는 시각대로 돌아오면 규칙 적용이 즉시 재개된다
            next = (raw.to < 0 || raw.from > mTimes.length - 1) ? clampRange(raw, mTimes.length) : raw;
          } else {
            // 레거시 경로: 시각 정보가 없는 쪽이 끼어 있으면 종전 논리 규칙으로 폴백한다 (호환)
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
    // options.getLength: 그 칸의 시리즈 길이 — 데이터 없는 칸(len<=0) 스킵과 레거시
    //   경로(시각 정보 미제공 칸)의 폴백(꼬리 판정·클램프)에만 쓴다. 시각 경로에서는 안 쓴다.
    // options.getTimes: 그 칸 시리즈(봉+whitespace)의 항목별 시각(초) 오름차순 배열 —
    //   시간축 전파를 시각 도메인으로 환산하는 기준 (엄밀 시각 정렬). 없으면 레거시
    //   논리 규칙으로 동작한다.
    // options.getWidth: 그 칸 차트 영역의 px 폭 — 새 규칙(같은 px 봉 간격 + 같은
    //   오른쪽 끝 시각)의 줌 기준. 패널 접기로 폭이 바뀌므로 호출 시점에 잰다.
    //   없으면(테스트·구 호출자) 종전 '같은 시계 창' 규칙으로 폴백한다.
    function add(chart, candleSeries, options) {
      const member = { chart, candleSeries,
                       getPrice: options?.getPrice, getLength: options?.getLength,
                       getTimes: options?.getTimes, getWidth: options?.getWidth,
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

  return { create, logicalAt, timeAt };
})();

if (typeof globalThis !== "undefined") {
  globalThis.PaneSync = PaneSync;
}
