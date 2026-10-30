// 시스템모니터는 켜 둔 신호의 진입·청산을 봉마다 한 줄로 모은다.

import { test } from "node:test";
import assert from "node:assert/strict";

await import("../public/os-layers.js");
await import("../public/sysmon.js");
const Sysmon = globalThis.Sysmon;

test("제로데이 매수는 발생신호 한 줄이다", () => {
  const book = Sysmon.create();
  Sysmon.note(book, {
    symbol: "ESZ26",
    time: 1760000000,
    close: 773400,
    os: { zdDir: 1, zdKind: 1, zdGrade: 3, zdQty: 2, zdTicks: 15, zdSuf: 2 },
    systems: ["fx_sig_zd"],
  });
  assert.equal(book.rows.length, 1);
  const row = book.rows[0];
  assert.equal(row.kind, "매수");
  assert.equal(row.occur, "Buy");
  assert.equal(row.orderType, "OnClose");
  assert.equal(row.name, "압축매수_약세");
  assert.equal(row.qty, 2);
  assert.equal(row.price, 773400);
  assert.equal(row.system, "제로데이기초자산");
  assert.equal(row.interval, "1분");
  assert.equal(row.symbol, "ESZ26");
});

test("같은 봉을 다시 계산하면 줄을 갈아끼운다", () => {
  const book = Sysmon.create();
  const bar = {
    symbol: "ESZ26",
    time: 1760000000,
    close: 773400,
    os: { zdDir: 1, zdKind: 1, zdGrade: 3, zdQty: 2 },
    systems: ["fx_sig_zd"],
  };
  Sysmon.note(book, bar);
  Sysmon.note(book, bar);
  assert.equal(book.rows.length, 1);
  Sysmon.note(book, { ...bar, os: { zdDir: 0 } });
  assert.equal(book.rows.length, 0);
});

test("다음 봉의 1차청산은 AtLimit으로 남긴다", () => {
  const book = Sysmon.create();
  Sysmon.note(book, {
    symbol: "ESZ26",
    time: 1760000000,
    close: 773400,
    os: { zdDir: -1, zdKind: 1, zdGrade: 3, zdQty: 2 },
    systems: ["fx_sig_zd"],
  });
  Sysmon.note(book, {
    symbol: "ESZ26",
    time: 1760000060,
    close: 773000,
    os: { zdLimit: -1, zdLimitQty: 1 },
    systems: ["fx_sig_zd"],
  });
  assert.equal(book.rows.length, 2);
  assert.equal(book.rows[1].occur, "ExitShort");
  assert.equal(book.rows[1].orderType, "AtLimit");
  assert.equal(book.rows[1].name, "1차청산매도");
  assert.equal(book.rows[1].qty, 1);
  assert.equal(book.rows[1].price, null);
});

test("색청산 손실은 OnClose 이름에 손절을 붙인다", () => {
  const book = Sysmon.create();
  Sysmon.note(book, {
    symbol: "ESZ26",
    time: 1760000120,
    close: 774000,
    os: { zdExit: 1, zdExitQty: 1, zdLoss: 1 },
    systems: ["fx_sig_zd"],
  });
  assert.equal(book.rows[0].kind, "청산");
  assert.equal(book.rows[0].occur, "ExitLong");
  assert.equal(book.rows[0].orderType, "OnClose");
  assert.equal(book.rows[0].name, "색청산매수_손절");
});

test("승률 진입과 1차·스톱 청산은 발생신호에 남는다", () => {
  const book = Sysmon.create();
  Sysmon.note(book, {
    symbol: "ESZ26",
    time: 1760000000,
    close: 775325,
    os: { wrDir: 1, wrName: "시가회복매수" },
    systems: ["fx_wr"],
  });
  Sysmon.note(book, {
    symbol: "ESZ26",
    time: 1760000420,
    close: 775625,
    os: { wrExitDir: 1, wrExitName: "1차청산매수" },
    systems: ["fx_wr"],
  });
  Sysmon.note(book, {
    symbol: "ESZ26",
    time: 1760002580,
    close: 775350,
    os: { wrExitDir: 1, wrExitName: "수익보호스톱매수" },
    systems: ["fx_wr"],
  });
  assert.equal(book.rows.length, 3);
  assert.equal(book.rows[0].system, "승률");
  assert.equal(book.rows[0].name, "시가회복매수");
  assert.equal(book.rows[0].occur, "Buy");
  assert.equal(book.rows[0].qty, 2);
  assert.equal(book.rows[1].name, "1차청산매수");
  assert.equal(book.rows[1].orderType, "AtLimit");
  assert.equal(book.rows[1].qty, 1);
  assert.equal(book.rows[2].name, "수익보호스톱매수");
  assert.equal(book.rows[2].orderType, "AtStop");
});

test("진입신호매매는 후보 이름을 쓴다", () => {
  const book = Sysmon.create();
  Sysmon.note(book, {
    symbol: "NQZ26",
    time: 1760000000,
    close: 2980000,
    os: { sigDir: 1, sigKind: 1, sigGrade: 8, sigQty: 0 },
    systems: ["fx_sig_v6"],
  });
  assert.equal(book.rows[0].name, "후보돌파매수");
  assert.equal(book.rows[0].occur, "Buy");
  assert.equal(book.rows[0].system, "진입신호매매");
  assert.equal(book.rows[0].qty, 0);
});

test("잔량 0인 짝 청산은 줄로 만들지 않는다", () => {
  const book = Sysmon.create();
  Sysmon.note(book, {
    symbol: "ESZ26",
    time: 1760000000,
    close: 773400,
    os: { pairExit: 1, pairExitQty: 0, pairDir: 0 },
    systems: ["fx_sig_pair"],
  });
  assert.equal(book.rows.length, 0);
});

function kst(y, m, d, hh, mm) {
  return Date.UTC(y, m - 1, d, hh || 0, mm || 0) / 1000 - 9 * 3600;
}

test("포트폴리오는 1차청산과 색청산을 한 거래로 합친다", () => {
  const book = Sysmon.create();
  const t0 = kst(2026, 10, 6, 9, 0);
  Sysmon.note(book, {
    symbol: "ESZ26", time: t0, close: 778000,
    os: { zdDir: 1, zdKind: 1, zdGrade: 3, zdQty: 2, zdTicks: 15 },
    systems: ["fx_sig_zd"],
  });
  Sysmon.note(book, {
    symbol: "ESZ26", time: t0 + 10 * 60, close: 778500,
    os: { zdLimit: 1, zdLimitQty: 1, zdTicks: 15 },
    systems: ["fx_sig_zd"],
  });
  Sysmon.note(book, {
    symbol: "ESZ26", time: t0 + 20 * 60, close: 778800,
    os: { zdExit: 1, zdExitQty: 1 },
    systems: ["fx_sig_zd"],
  });
  const t1 = kst(2026, 10, 15, 9, 0);
  Sysmon.note(book, {
    symbol: "ESZ26", time: t1, close: 790250,
    os: { zdDir: 1, zdKind: 1, zdGrade: 3, zdQty: 1, zdTicks: 15 },
    systems: ["fx_sig_zd"],
  });
  Sysmon.note(book, {
    symbol: "ESZ26", time: t1 + 4 * 60, close: 787950,
    os: { zdExit: 1, zdExitQty: 1, zdLoss: 1 },
    systems: ["fx_sig_zd"],
  });
  const report = Sysmon.portfolio(book.rows, { tickRaw: { ESZ26: 25 } });
  const all = report.columns[0];
  const one = report.columns[1];
  assert.equal(all.header, "종합");
  assert.equal(one.header, "ESZ26");
  assert.equal(one.system, "제로데이기초자산");
  assert.equal(one.interval, "1분");
  assert.equal(one.start, "2026/10/06");
  assert.equal(one.end, "2026/10/15");
  // 1차 15틱 = 3.75, 잔량 8.00 → 11.75. 다음 거래 7902.50 → 7879.50 = -23.
  assert.equal(one.net, 11.75 - 23);
  assert.equal(one.grossProfit, 11.75);
  assert.equal(one.grossLoss, -23);
  assert.equal(one.trades, 2);
  assert.equal(one.wins, 1);
  assert.equal(one.losses, 1);
  assert.equal(one.winRate, 50);
  assert.equal(one.maxWin, 11.75);
  assert.equal(one.maxLoss, -23);
  assert.equal(one.avgWinBars, 20);
  assert.equal(one.avgLossBars, 4);
  assert.equal(one.maxDd, -23);
  assert.equal(one.capital, 7902.5);
  assert.equal(Math.round(one.annual * 100) / 100, Math.round((11.75 - 23) * 365 / 10 * 100) / 100);
  assert.equal(Math.round(one.reward * 100) / 100, Math.round((11.75 - 23) / 23 * 100) / 100);
});

test("미청산 손익은 총손익과 따로 둔다", () => {
  const book = Sysmon.create();
  Sysmon.note(book, {
    symbol: "ESZ26", time: kst(2026, 10, 15, 10, 0), close: 778000,
    os: { zdDir: 1, zdKind: 4, zdQty: 2, zdTicks: 15 },
    systems: ["fx_sig_zd"],
  });
  const one = Sysmon.portfolio(book.rows, { tickRaw: { ESZ26: 25 }, lastClose: { ESZ26: 778500 } }).columns[1];
  assert.equal(one.trades, 0);
  assert.equal(one.net, 0);
  assert.equal(one.openNet, 10);
});

test("시스템 줄은 켜 둔 신호와 남은 수량만 보여 준다", () => {
  const book = Sysmon.create();
  Sysmon.note(book, {
    symbol: "ESZ26",
    time: 1760000000,
    close: 773400,
    os: { zdDir: 1, zdKind: 4, zdQty: 2 },
    systems: ["fx_sig_zd"],
  });
  Sysmon.note(book, {
    symbol: "ESZ26",
    time: 1760000060,
    close: 774000,
    os: { zdLimit: 1, zdLimitQty: 1 },
    systems: ["fx_sig_zd"],
  });
  let rows = Sysmon.systems(book, [
    { symbol: "ESZ26", ids: ["fx_sig_zd", "fx_judge"] },
    { symbol: "NQZ26", ids: [] },
  ]);
  assert.equal(rows.length, 1);
  assert.equal(rows[0].kind, "표시");
  assert.equal(rows[0].name, "제로데이기초자산");
  assert.equal(rows[0].symbol, "ESZ26");
  assert.equal(rows[0].interval, "1분");
  assert.equal(rows[0].position, "매수");
  assert.equal(rows[0].entryQty, 2);
  assert.equal(rows[0].qty, 1);
  Sysmon.note(book, {
    symbol: "ESZ26",
    time: 1760000120,
    close: 772000,
    os: { zdExit: 1, zdExitQty: 1 },
    systems: ["fx_sig_zd"],
  });
  rows = Sysmon.systems(book, [{ symbol: "ESZ26", ids: ["fx_sig_zd"] }]);
  assert.equal(rows[0].position, "포지션 없음");
  assert.equal(rows[0].qty, 0);
});
