// HTS가 전략 차트에 그리는 주문 화살표. 이름은 Sell/ExitShort 이름.
// 매도는 연두, 매수·청산은 주황. 브라우저 전역 PairMarkers, node:test도 같다.
"use strict";

const PairMarkers = (() => {
  const SELL = "#d4ff4a";
  const COVER = "#ff8c00";

  function textFor(kind) {
    return kind === 1 ? "진입" : kind === 2 ? "부분" : "청산";
  }

  // HTS는 매도를 음수로 보여 준다. 수량 인자 없는 ExitShort(이름, AtMarket)는 0이다.
  function shownQty(side, kind, qty) {
    if (!Number.isFinite(qty)) return null;
    if (kind !== 1 && kind !== 2) return 0;
    if (side < 0) return -Math.abs(qty);
    return qty;
  }

  function fromEvents(events) {
    const list = [];
    for (const ev of events || []) {
      const time = Number(ev?.t);
      if (!Number.isFinite(time)) continue;
      const side = Number(ev.side) < 0 ? -1 : 1;
      const kind = Number(ev.k);
      const sell = kind === 1 ? side < 0 : side > 0;
      const name = typeof ev.n === "string" && ev.n.trim() ? ev.n.trim() : textFor(kind);
      list.push({
        time,
        symbol: typeof ev.symbol === "string" ? ev.symbol : "",
        position: sell ? "aboveBar" : "belowBar",
        color: sell ? SELL : COVER,
        shape: sell ? "arrowDown" : "arrowUp",
        text: name,
        qty: shownQty(side, kind, Number(ev.q)),
      });
    }
    list.sort((a, b) => a.time - b.time || (a.text < b.text ? -1 : a.text > b.text ? 1 : 0));
    return list;
  }

  // HTS 주문 이름은 봉 옆의 작은 글자다.
  const labelFont = "12px sans-serif";
  const labelPx = 12;

  function seriesMarkers(marks) {
    return (marks || []).map((mark) => {
      const next = { ...mark };
      delete next.text;
      return next;
    });
  }

  function labelLines(mark) {
    const lines = [mark.text];
    if (Number.isFinite(mark.qty)) lines.push(String(mark.qty));
    return lines;
  }

  function placeLabel(mark, box) {
    const gap = 6;
    const lines = labelLines(mark);
    const y = mark.position === "aboveBar"
      ? box.highY - gap - labelPx / 2
      : box.lowY + gap + labelPx / 2;
    return { x: box.x, y, lines, color: mark.color, font: labelFont, above: mark.position === "aboveBar" };
  }

  // 글자 상자. y는 첫 줄의 가운데다. 위 화살표는 위로, 아래 화살표는 아래로 줄을 쌓는다.
  function labelBox(label, width) {
    const step = labelPx + 1;
    const n = Math.max(1, label.lines.length);
    const top = label.above ? label.y - (n - 1) * step - labelPx / 2 : label.y - labelPx / 2;
    return { x: label.x + 8, y: top, w: width, h: (n - 1) * step + labelPx };
  }

  function boxesOverlap(a, b, pad) {
    return a.x < b.x + b.w + pad && b.x < a.x + a.w + pad && a.y < b.y + b.h + pad && b.y < a.y + a.h + pad;
  }

  // 측정값이 없어도 한글 한 글자를 글자 크기만큼 잡는다. 진입 이름이 청산 글자 위를 덮지 않게 한다.
  function lineWidth(line, measured) {
    const guess = [...String(line)].length * labelPx;
    return Math.max(Number(measured) || 0, guess);
  }

  // 왼쪽 글자를 먼저 두고, 겹치는 글자는 그 상자 밖으로 민다. 진입·청산을 가리지 않는다.
  function separateLabels(labels, widths) {
    const pad = 4;
    const order = labels.map((_, i) => i).sort((i, j) => labels[i].x - labels[j].x || labels[i].y - labels[j].y);
    const occupied = [];
    for (const i of order) {
      const label = labels[i];
      const width = lineWidth("", widths[i]);
      for (let n = 0; n < 12; n++) {
        const box = labelBox(label, width);
        const hit = occupied.find((other) => boxesOverlap(box, other, pad));
        if (!hit) break;
        const boxMid = box.y + box.h / 2;
        const hitMid = hit.y + hit.h / 2;
        if (boxMid <= hitMid) {
          const dy = (box.y + box.h + pad) - hit.y;
          label.y -= dy > 0 ? dy : box.h + pad;
        } else {
          const dy = (hit.y + hit.h + pad) - box.y;
          label.y += dy > 0 ? dy : box.h + pad;
        }
      }
      occupied.push(labelBox(label, width));
    }
    return labels;
  }

  function priceOf(bar, side) {
    const direct = side === "high" ? bar.high : bar.low;
    if (Number.isFinite(direct)) return direct;
    if (Number.isFinite(bar.value)) return bar.value;
    if (Number.isFinite(bar.close)) return bar.close;
    return null;
  }

  function labels() {
    let marks = [];
    let requestUpdate = () => {};
    let series = null;
    let chart = null;
    return {
      attached(param) {
        series = param.series;
        chart = param.chart;
        if (typeof param.requestUpdate === "function") requestUpdate = param.requestUpdate;
      },
      detached() {
        series = null;
        chart = null;
      },
      updateAllViews() {},
      setMarks(next) {
        marks = next || [];
        requestUpdate();
      },
      paneViews() {
        return [{
          zOrder() { return "top"; },
          renderer() {
            return {
              draw(target) {
                if (!series || !chart || !marks.length) return;
                const opt = typeof series.options === "function" ? series.options() : null;
                if (opt && opt.visible === false) return;
                const raw = series.data;
                const bars = typeof raw === "function" ? raw.call(series) : [];
                const byTime = new Map();
                for (const bar of bars) if (bar && Number.isFinite(bar.time)) byTime.set(bar.time, bar);
                const placed = [];
                for (const mark of marks) {
                  const bar = byTime.get(mark.time);
                  if (!bar) continue;
                  const x = chart.timeScale().timeToCoordinate(mark.time);
                  if (x == null) continue;
                  const high = priceOf(bar, "high");
                  const low = priceOf(bar, "low");
                  if (high == null || low == null) continue;
                  const highY = series.priceToCoordinate(high);
                  const lowY = series.priceToCoordinate(low);
                  if (highY == null || lowY == null) continue;
                  placed.push(placeLabel(mark, { x, highY, lowY }));
                }
                if (!placed.length) return;
                target.useBitmapCoordinateSpace((scope) => {
                  const ctx = scope.context;
                  ctx.save();
                  ctx.font = labelFont;
                  const widths = placed.map((label) => {
                    let w = 0;
                    for (const line of label.lines) w = Math.max(w, lineWidth(line, ctx.measureText(line).width));
                    return w;
                  });
                  separateLabels(placed, widths);
                  ctx.scale(scope.horizontalPixelRatio, scope.verticalPixelRatio);
                  ctx.textAlign = "left";
                  ctx.textBaseline = "middle";
                  ctx.lineWidth = 3;
                  ctx.strokeStyle = "#131722";
                  for (const label of placed) {
                    label.lines.forEach((line, i) => {
                      const y = label.above ? label.y - i * (labelPx + 1) : label.y + i * (labelPx + 1);
                      ctx.strokeText(line, label.x + 8, y);
                      ctx.fillStyle = label.color;
                      ctx.fillText(line, label.x + 8, y);
                    });
                  }
                  ctx.restore();
                });
              },
            };
          },
        }];
      },
    };
  }

  return { fromEvents, seriesMarkers, placeLabel, labelLines, labelFont, labelBox, lineWidth, separateLabels, labels };
})();

if (typeof globalThis !== "undefined") globalThis.PairMarkers = PairMarkers;
