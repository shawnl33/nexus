// 시스템모니터. 차트에 켜 둔 신호의 진입·청산을 봉 시각마다 한 번씩 줄로 모은다.
// 같은 봉을 다시 계산하면 그 봉의 줄을 갈아끼우고, 신호가 사라지면 그 줄도 뺀다.
"use strict";

const Sysmon = (() => {
  const CATALOG = {
    fx_sig_v6: { name: "진입신호매매", mode: "v6" },
    fx_sig_pair: { name: "짝진입", mode: "pair" },
    fx_sig_zd: { name: "제로데이기초자산", mode: "zd" },
    fx_wr: { name: "승률", mode: "wr" },
  };

  function create() {
    return { rows: [] };
  }

  function dirOf(os, mode) {
    if (mode === "v6") return os.sigDir || 0;
    if (mode === "zd") return os.zdDir || 0;
    if (mode === "wr") return os.wrDir || 0;
    return os.pairDir || 0;
  }

  function entryQty(os, mode) {
    if (mode === "v6") return os.sigQty || 0;
    if (mode === "zd") return os.zdQty || 0;
    if (mode === "wr") return os.wrQty || 0;
    return os.pairQty || 0;
  }

  function entryName(os, mode) {
    const Os = globalThis.OsLayers;
    if (mode === "wr") return os.wrName || "";
    if (!Os) return "";
    if (mode === "v6") return Os.orderName(os.sigDir, os.sigKind, os.sigGrade, os.adjKind, os.adjGrade);
    return String(Os.pairLabel(os, mode === "zd")).split("\n")[0];
  }

  function exitCode(os, mode) {
    if (mode === "v6") return os.sigExit || 0;
    if (mode === "zd") return os.zdExit || 0;
    if (mode === "wr") return os.wrExitDir || 0;
    return os.pairExit || 0;
  }

  function exitQty(os, mode) {
    if (mode === "v6") return os.sigExitQty;
    if (mode === "zd") return os.zdExitQty;
    if (mode === "wr") return os.wrExitQty || 0;
    return os.pairExitQty;
  }

  function exitLabel(os, mode, code) {
    const Os = globalThis.OsLayers;
    if (mode === "zd") {
      const side = code > 0 ? "매수" : "매도";
      const kindn = code === 2 || code === -2 ? "시간청산" : "색청산";
      return kindn + side + (os.zdLoss ? "_손절" : "");
    }
    return Os ? Os.exitName(code) : "";
  }

  function tickOf(os, mode) {
    if (mode === "zd") return os.zdTicks || 0;
    if (mode === "pair") return os.pairTicks || 0;
    return 0;
  }

  function limitOf(os, mode) {
    if (mode === "v6") return { hit: 0, qty: 0 };
    if (mode === "zd") return { hit: os.zdLimit || 0, qty: os.zdLimitQty };
    return { hit: os.pairLimit || 0, qty: os.pairLimitQty };
  }

  function base(ev, id, meta) {
    return {
      symbol: ev.symbol,
      time: ev.time,
      systemId: id,
      system: meta.name,
      interval: "1분",
    };
  }

  function rowsFor(ev, id) {
    const meta = CATALOG[id];
    const os = ev.os || {};
    const mode = meta.mode;
    const out = [];
    if (mode === "wr" && os.wrExitDir && os.wrExitName) {
      const sell = os.wrExitDir < 0;
      const first = String(os.wrExitName).startsWith("1차");
      const stop = String(os.wrExitName).includes("스톱");
      out.push({
        ...base(ev, id, meta),
        role: first ? "limit" : "exit",
        kind: "청산",
        occur: sell ? "ExitShort" : "ExitLong",
        orderType: first ? "AtLimit" : (stop ? "AtStop" : "OnClose"),
        price: first || stop ? null : ev.close,
        qty: first ? 1 : null,
        name: os.wrExitName,
      });
    }
    const code = exitCode(os, mode);
    const xqty = exitQty(os, mode);
    if (mode !== "wr" && code && (mode === "v6" || xqty !== 0)) {
      out.push({
        ...base(ev, id, meta),
        role: "exit",
        kind: "청산",
        occur: code > 0 ? "ExitLong" : "ExitShort",
        orderType: "OnClose",
        price: ev.close,
        qty: xqty == null ? null : xqty,
        name: exitLabel(os, mode, code),
      });
    }
    const lim = limitOf(os, mode);
    if (lim.hit && lim.qty !== 0) {
      const sell = lim.hit < 0;
      out.push({
        ...base(ev, id, meta),
        role: "limit",
        kind: "청산",
        occur: sell ? "ExitShort" : "ExitLong",
        orderType: "AtLimit",
        price: null,
        qty: lim.qty == null ? null : lim.qty,
        ticks: tickOf(os, mode),
        name: (mode === "zd" ? "1차청산" : "1차목표청산") + (sell ? "매도" : "매수"),
      });
    }
    const dir = dirOf(os, mode);
    if (dir) {
      out.push({
        ...base(ev, id, meta),
        role: "entry",
        kind: dir > 0 ? "매수" : "매도",
        occur: dir > 0 ? "Buy" : "Sell",
        orderType: "OnClose",
        price: ev.close,
        qty: entryQty(os, mode),
        ticks: tickOf(os, mode),
        name: entryName(os, mode),
      });
    }
    return out;
  }

  function place(rows, row) {
    let i = 0;
    while (i < rows.length && rows[i].time <= row.time) i += 1;
    rows.splice(i, 0, row);
  }

  function note(book, ev) {
    const systems = (ev.systems || []).filter((id) => CATALOG[id]);
    if (!book || !systems.length || !ev.os) return false;
    const before = JSON.stringify(book.rows);
    book.rows = book.rows.filter((r) => !(r.symbol === ev.symbol && r.time === ev.time && systems.includes(r.systemId)));
    for (const id of systems) {
      for (const row of rowsFor(ev, id)) place(book.rows, row);
    }
    return JSON.stringify(book.rows) !== before;
  }

  function positionOf(rows, symbol, id) {
    let side = 0;
    let qty = 0;
    let entryQty = 0;
    let sized = false;
    for (const r of rows) {
      if (r.symbol !== symbol || r.systemId !== id) continue;
      if (r.role === "entry") {
        side = r.occur === "Buy" ? 1 : -1;
        entryQty = r.qty || 0;
        qty = entryQty;
        sized = entryQty > 0;
      } else if (r.role === "limit" && sized) {
        qty = Math.max(0, qty - (r.qty || 0));
        if (qty === 0) side = 0;
      } else if (r.role === "exit") {
        side = 0;
        qty = 0;
        entryQty = 0;
        sized = false;
      }
    }
    return { side, qty, entryQty };
  }

  function systems(book, panes) {
    const out = [];
    const seen = new Set();
    for (const pane of panes || []) {
      for (const id of pane.ids || []) {
        const meta = CATALOG[id];
        if (!meta) continue;
        const key = pane.symbol + "\0" + id;
        if (seen.has(key)) continue;
        seen.add(key);
        const pos = positionOf(book.rows, pane.symbol, id);
        out.push({
          kind: "표시",
          systemId: id,
          name: meta.name,
          symbol: pane.symbol,
          interval: "1분",
          position: pos.side > 0 ? "매수" : pos.side < 0 ? "매도" : "포지션 없음",
          entryQty: pos.entryQty,
          qty: pos.qty,
        });
      }
    }
    return out;
  }

  const POINT_RAW = 100;

  function kstDate(sec) {
    const d = new Date((Number(sec) + 9 * 3600) * 1000);
    const m = String(d.getUTCMonth() + 1).padStart(2, "0");
    const day = String(d.getUTCDate()).padStart(2, "0");
    return d.getUTCFullYear() + "/" + m + "/" + day;
  }

  function dayIndex(sec) {
    const d = new Date((Number(sec) + 9 * 3600) * 1000);
    return Date.UTC(d.getUTCFullYear(), d.getUTCMonth(), d.getUTCDate());
  }

  function inclusiveDays(t0, t1) {
    if (t0 == null || t1 == null) return 0;
    return Math.round((dayIndex(t1) - dayIndex(t0)) / 86400000) + 1;
  }

  function blankColumn(header) {
    return {
      header, system: "", interval: "", start: "", end: "",
      net: 0, openNet: 0, grossProfit: 0, grossLoss: 0, annual: null,
      trades: 0, wins: 0, losses: 0, winRate: null, avg: null, payoff: null,
      maxWin: null, avgWin: null, maxWinStreak: 0, avgWinBars: null,
      maxLoss: null, avgLoss: null, maxLossStreak: 0, avgLossBars: null,
      maxDd: 0, reward: null, profitFactor: null, capital: null, returnPct: null,
    };
  }

  function summarize(header, trades, openPnl, capital, t0, t1, system, interval) {
    const col = blankColumn(header);
    col.system = system || "";
    col.interval = interval || "";
    col.openNet = openPnl;
    if (t0 != null) col.start = kstDate(t0);
    if (t1 != null) col.end = kstDate(t1);
    if (!trades.length && !openPnl) return col;
    let net = 0;
    let grossProfit = 0;
    let grossLoss = 0;
    let wins = 0;
    let losses = 0;
    let maxWin = null;
    let maxLoss = null;
    let winBars = 0;
    let lossBars = 0;
    let winStreak = 0;
    let lossStreak = 0;
    let maxWinStreak = 0;
    let maxLossStreak = 0;
    let eq = 0;
    let peak = 0;
    let maxDd = 0;
    for (const tr of trades) {
      net += tr.pnl;
      eq += tr.pnl;
      if (eq > peak) peak = eq;
      if (eq - peak < maxDd) maxDd = eq - peak;
      if (tr.pnl > 0) {
        wins += 1;
        grossProfit += tr.pnl;
        winBars += tr.bars;
        if (maxWin == null || tr.pnl > maxWin) maxWin = tr.pnl;
        winStreak += 1;
        lossStreak = 0;
        if (winStreak > maxWinStreak) maxWinStreak = winStreak;
      } else if (tr.pnl < 0) {
        losses += 1;
        grossLoss += tr.pnl;
        lossBars += tr.bars;
        if (maxLoss == null || tr.pnl < maxLoss) maxLoss = tr.pnl;
        lossStreak += 1;
        winStreak = 0;
        if (lossStreak > maxLossStreak) maxLossStreak = lossStreak;
      } else {
        winStreak = 0;
        lossStreak = 0;
      }
    }
    if (openPnl) {
      eq += openPnl;
      if (eq > peak) peak = eq;
      if (eq - peak < maxDd) maxDd = eq - peak;
    }
    const days = inclusiveDays(t0, t1);
    const avgWin = wins ? grossProfit / wins : null;
    const avgLoss = losses ? grossLoss / losses : null;
    col.net = net;
    col.grossProfit = grossProfit;
    col.grossLoss = grossLoss;
    col.annual = days > 0 ? net * 365 / days : null;
    col.trades = trades.length;
    col.wins = wins;
    col.losses = losses;
    col.winRate = trades.length ? wins / trades.length * 100 : null;
    col.avg = trades.length ? net / trades.length : null;
    col.payoff = avgWin != null && avgLoss != null && avgLoss !== 0 ? avgWin / Math.abs(avgLoss) : null;
    col.maxWin = maxWin;
    col.avgWin = avgWin;
    col.maxWinStreak = maxWinStreak;
    col.avgWinBars = wins ? winBars / wins : null;
    col.maxLoss = maxLoss;
    col.avgLoss = avgLoss;
    col.maxLossStreak = maxLossStreak;
    col.avgLossBars = losses ? lossBars / losses : null;
    col.maxDd = maxDd;
    col.reward = maxDd < 0 ? net / Math.abs(maxDd) : null;
    col.profitFactor = grossLoss < 0 ? grossProfit / Math.abs(grossLoss) : null;
    col.capital = capital > 0 ? capital : null;
    col.returnPct = capital > 0 ? net / capital * 100 : null;
    return col;
  }

  function portfolio(rows, opt) {
    const tickRaw = opt?.tickRaw || {};
    const lastClose = opt?.lastClose || {};
    const open = new Map();
    const grouped = new Map();
    function groupOf(row) {
      const key = row.symbol + "\0" + row.systemId;
      let g = grouped.get(key);
      if (!g) {
        g = {
          key, symbol: row.symbol, systemId: row.systemId, system: row.system, interval: row.interval,
          trades: [], openPnl: 0, capital: 0, t0: null, t1: null,
        };
        grouped.set(key, g);
      }
      return g;
    }
    function touch(g, time) {
      if (time == null) return;
      if (g.t0 == null || time < g.t0) g.t0 = time;
      if (g.t1 == null || time > g.t1) g.t1 = time;
    }
    for (const row of rows || []) {
      const g = groupOf(row);
      const key = g.key;
      if (row.role === "entry") {
        const prev = open.get(key);
        if (prev && prev.qty > 0) {
          const px = row.price;
          prev.pnl += (px - prev.entryPx) * prev.side * prev.qty / POINT_RAW;
          prev.exitTime = row.time;
          g.trades.push(prev);
          open.delete(key);
        }
        const qty = row.qty > 0 ? row.qty : 1;
        const px = row.price;
        if (px != null && px / POINT_RAW > g.capital) g.capital = px / POINT_RAW;
        open.set(key, {
          side: row.occur === "Buy" ? 1 : -1,
          qty, entryPx: px, entryTime: row.time, exitTime: row.time, pnl: 0,
        });
        touch(g, row.time);
      } else if (row.role === "limit" || row.role === "exit") {
        const pos = open.get(key);
        if (!pos || pos.qty <= 0 || pos.entryPx == null) continue;
        let px = row.price;
        if (px == null) {
          const dist = (row.ticks || 0) * (tickRaw[row.symbol] || 0);
          px = pos.side > 0 ? pos.entryPx + dist : pos.entryPx - dist;
        }
        const qty = row.qty == null || row.qty <= 0 ? pos.qty : Math.min(pos.qty, row.qty);
        pos.pnl += (px - pos.entryPx) * pos.side * qty / POINT_RAW;
        pos.qty -= qty;
        pos.exitTime = row.time;
        touch(g, row.time);
        if (row.role === "exit" || pos.qty <= 0) {
          g.trades.push(pos);
          open.delete(key);
        }
      }
    }
    for (const [key, pos] of open) {
      const g = grouped.get(key);
      const mark = lastClose[g.symbol];
      if (pos.qty > 0 && pos.entryPx != null && Number.isFinite(mark)) {
        g.openPnl += (mark - pos.entryPx) * pos.side * pos.qty / POINT_RAW;
        touch(g, pos.entryTime);
      }
    }
    for (const spec of opt?.include || []) {
      const meta = CATALOG[spec.systemId];
      if (!meta || !spec.symbol) continue;
      const key = spec.symbol + "\0" + spec.systemId;
      if (!grouped.has(key)) {
        grouped.set(key, {
          key, symbol: spec.symbol, systemId: spec.systemId, system: meta.name, interval: "1분",
          trades: [], openPnl: 0, capital: 0, t0: null, t1: null,
        });
      }
    }
    const columns = [blankColumn("종합")];
    const allTrades = [];
    let allOpen = 0;
    let allCapital = 0;
    let allT0 = null;
    let allT1 = null;
    for (const g of grouped.values()) {
      g.trades.sort((a, b) => a.exitTime - b.exitTime);
      for (const tr of g.trades) {
        tr.bars = Math.max(0, Math.round((tr.exitTime - tr.entryTime) / 60));
        allTrades.push(tr);
      }
      allOpen += g.openPnl;
      if (g.capital > allCapital) allCapital = g.capital;
      if (g.t0 != null && (allT0 == null || g.t0 < allT0)) allT0 = g.t0;
      if (g.t1 != null && (allT1 == null || g.t1 > allT1)) allT1 = g.t1;
      columns.push(summarize(g.symbol, g.trades, g.openPnl, g.capital, g.t0, g.t1, g.system, g.interval));
    }
    allTrades.sort((a, b) => a.exitTime - b.exitTime);
    columns[0] = summarize("종합", allTrades, allOpen, allCapital, allT0, allT1, "", "");
    return { columns };
  }

  return { create, note, systems, portfolio, ids: Object.keys(CATALOG) };
})();

if (typeof globalThis !== "undefined") globalThis.Sysmon = Sysmon;
