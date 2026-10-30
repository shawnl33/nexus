#!/usr/bin/env python3
"""Translate #우드스탁_ES기초자산_승률.txt body into C statements."""
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
SRC = ROOT / "reference/yeslanguage/signals/#우드스탁_ES기초자산_승률.txt"
OUT = ROOT / "src/core/signals/fx_wr_yl.c"

ARRAYS = {
    "도달기록": 50,
    "회복진폭오늘": 391, "회복진폭1": 391, "회복진폭2": 391, "회복진폭3": 391,
    "회복진폭4": 391, "회복진폭5": 391,
    "회복량오늘": 391, "회복량1": 391, "회복량2": 391, "회복량3": 391,
    "회복량4": 391, "회복량5": 391,
}


def strip_comments(text):
    out = []
    i = 0
    n = len(text)
    while i < n:
        if text[i] == '"':
            j = i + 1
            while j < n and text[j] != '"':
                j += 1
            out.append(text[i:j + 1])
            i = j + 1
            continue
        if text.startswith("//", i):
            while i < n and text[i] != "\n":
                i += 1
            continue
        out.append(text[i])
        i += 1
    return "".join(out)


def tokenize(src):
    spec = [
        ("STR", r'"(?:[^"\\]|\\.)*"'),
        ("NUM", r"\d+\.\d+|\d+"),
        ("OP", r"&&|\|\||==|!=|<=|>=|[+\-*/<>]=?|=|[,()\[\]{};]"),
        ("ID", r"[A-Za-z_가-힣][A-Za-z0-9_가-힣]*"),
        ("WS", r"\s+"),
    ]
    rx = re.compile("|".join(f"(?P<{n}>{p})" for n, p in spec))
    pos = 0
    toks = []
    while pos < len(src):
        m = rx.match(src, pos)
        if not m:
            raise SystemExit(f"bad token at {pos}: {src[pos:pos+40]!r}")
        if m.lastgroup != "WS":
            toks.append((m.lastgroup, m.group()))
        pos = m.end()
    return toks


class P:
    def __init__(self, toks):
        self.t = toks
        self.i = 0

    def peek(self, k=0):
        j = self.i + k
        return self.t[j] if j < len(self.t) else ("EOF", "")

    def take(self, kind=None, val=None):
        k, v = self.peek()
        if kind and k != kind:
            return None
        if val and v != val:
            return None
        self.i += 1
        return v

    def need(self, val=None, kind=None):
        got = self.take(kind, val)
        if got is None:
            raise SystemExit(f"expected {val or kind} at {self.peek()} index {self.i}")
        return got


def c_id(name):
    if name in ("O", "H", "L", "C", "V"):
        return {"O": "b->open", "H": "b->high", "L": "b->low", "C": "b->close", "V": "b->volume"}[name]
    if name in ("sDate", "sTime", "PriceScale", "CurrentBar", "MarketPosition", "DataCompress", "BarInterval"):
        return {
            "sDate": "b->date", "sTime": "b->time", "PriceScale": "b->price_scale",
            "CurrentBar": "s->bar_n", "MarketPosition": "b->market",
            "DataCompress": "2", "BarInterval": "1",
        }[name]
    return f"s->cur[YL_F_{name}]"


def expr(p):
    return e_or(p)


def e_or(p):
    left = e_and(p)
    while p.peek()[1] == "||":
        p.take()
        left = f"({left} || {e_and(p)})"
    return left


def e_and(p):
    left = e_not(p)
    while p.peek()[1] == "&&":
        p.take()
        left = f"({left} && {e_not(p)})"
    return left


def e_not(p):
    return e_cmp(p)


def e_cmp(p):
    left = e_sum(p)
    while p.peek()[1] in ("==", "!=", "<", ">", "<=", ">="):
        op = p.take()
        left = f"({left} {op} {e_sum(p)})"
    return left


def e_sum(p):
    left = e_prod(p)
    while p.peek()[1] in ("+", "-") and p.peek(1)[1] not in ("]", ",", ")", ";"):
        # unary minus is handled in e_unary; here only binary if left exists
        op = p.take()
        left = f"({left} {op} {e_prod(p)})"
    return left


def e_prod(p):
    left = e_unary(p)
    while p.peek()[1] in ("*", "/"):
        op = p.take()
        left = f"({left} {op} {e_unary(p)})"
    return left


def e_unary(p):
    if p.peek()[1] == "-":
        p.take()
        return f"(-{e_unary(p)})"
    return e_post(p)


def e_post(p):
    base = e_prim(p)
    while p.peek()[1] == "[":
        p.take()
        idx = expr(p)
        p.need("]")
        base = f"yl_at({base}, {idx})"
    return base


def e_prim(p):
    k, v = p.peek()
    if v == "(":
        p.take()
        inner = expr(p)
        p.need(")")
        return f"({inner})"
    if k == "NUM":
        p.take()
        return v if "." in v else v + ".0" if False else v
    if k == "STR":
        p.take()
        return v
    if k != "ID":
        raise SystemExit(f"expr at {p.peek()} {p.i}")
    name = p.take()
    if name == "IFF":
        p.need("(")
        a = expr(p)
        p.need(",")
        b = expr(p)
        p.need(",")
        c = expr(p)
        p.need(")")
        return f"({a} ? {b} : {c})"
    if name in ("Max", "Min", "Abs"):
        p.need("(")
        args = [expr(p)]
        while p.peek()[1] == ",":
            p.take()
            args.append(expr(p))
        p.need(")")
        fn = {"Max": "yl_max", "Min": "yl_min", "Abs": "fabs"}[name]
        return f"{fn}({', '.join(args)})"
    if name == "IntPortion":
        p.need("(")
        a = expr(p)
        p.need(")")
        return f"trunc({a})"
    if name == "Round":
        p.need("(")
        a = expr(p)
        if p.peek()[1] == ",":
            p.take()
            expr(p)
        p.need(")")
        return f"round({a})"
    if name == "Mod":
        p.need("(")
        a = expr(p)
        p.need(",")
        b = expr(p)
        p.need(")")
        return f"fmod({a}, {b})"
    if name == "DayOfWeek":
        p.need("(")
        a = expr(p)
        p.need(")")
        return f"yl_dow({a})"
    if name == "RGB":
        p.need("(")
        r, g, b = expr(p), None, None
        p.need(",")
        g = expr(p)
        p.need(",")
        b = expr(p)
        p.need(")")
        return f"yl_rgb({r}, {g}, {b})"
    if name in ("Average", "Highest", "Lowest"):
        p.need("(")
        series = p.need(kind="ID")
        p.need(",")
        n = expr(p)
        p.need(")")
        fn = {"Average": "yl_avg", "Highest": "yl_hi", "Lowest": "yl_lo"}[name]
        return f'{fn}(s, "{series}", {n})'
    if name.startswith("WSF_"):
        p.need("(")
        args = []
        if p.peek()[1] != ")":
            args.append(expr(p))
            while p.peek()[1] == ",":
                p.take()
                args.append(expr(p))
        p.need(")")
        return f"yl_{name}(s, b)"
    if p.peek()[1] == "(":
        p.take()
        args = []
        if p.peek()[1] != ")":
            args.append(expr(p))
            while p.peek()[1] == ",":
                p.take()
                args.append(expr(p))
        p.need(")")
        return f"{name}({', '.join(args)})"
    if p.peek()[1] == "[":
        p.take()
        idx = expr(p)
        p.need("]")
        if name in ARRAYS:
            return f"yl_arr(s, YL_A_{name}, (int)({idx}))"
        if name in ("O", "H", "L", "C", "V"):
            return f"yl_px(s, '{name}', {idx})"
        return f"yl_lag(s, YL_F_{name}, {idx})"
    return c_id(name)


def stmt(p, ind):
    if p.peek()[1] == ";":
        p.take()
        return ""
    if p.peek()[1] == "{":
        return block(p, ind)
    if p.peek()[1] == "For":
        return for_stmt(p, ind)
    if p.peek()[1] == "If":
        return if_stmt(p, ind)
    if p.peek()[0] == "ID" and p.peek()[1] in ("Buy", "Sell", "ExitLong", "ExitShort"):
        return order_stmt(p, ind)
    if p.peek()[0] == "ID" and p.peek(1)[1] == "(":
        call = expr(p)
        if p.peek()[1] == ";":
            p.take()
        return f"{ind}{call};"
    if p.peek()[0] == "ID" and p.peek(1)[1] in ("=", "["):
        return assign_stmt(p, ind)
    raise SystemExit(f"stmt at {p.peek()} {p.i}")


def block(p, ind):
    p.need("{")
    lines = []
    while p.peek()[1] not in ("}", "EOF"):
        lines.append(stmt(p, ind + "    "))
        if p.peek()[1] == ";":
            p.take()
    p.need("}")
    body = "\n".join(x for x in lines if x)
    return ind + "{\n" + body + "\n" + ind + "}"


def for_stmt(p, ind):
    p.need("For")
    name = p.need(kind="ID")
    p.need("=")
    start = expr(p)
    p.need("To")
    end = expr(p)
    body = stmt(p, ind + "    ")
    if p.peek()[1] == ";":
        p.take()
    slot = c_id(name)
    return (
        f"{ind}for ({slot} = ({start}); {slot} <= ({end}); {slot} += 1) "
        + (body if body.strip().startswith("{") else "\n" + body)
    )


def if_stmt(p, ind):
    p.need("If")
    cond = expr(p)
    if p.peek()[1] == "Then":
        p.take()
    body = stmt(p, ind + "    ")
    if p.peek()[1] == ";":
        p.take()
    out = [f"{ind}if ({cond}) {body if body.strip().startswith('{') else chr(10)+body}"]
    while p.peek()[1] == "Else":
        p.take()
        if p.peek()[1] == "If":
            p.take()
            c2 = expr(p)
            if p.peek()[1] == "Then":
                p.take()
            b2 = stmt(p, ind + "    ")
            if p.peek()[1] == ";":
                p.take()
            out.append(f"{ind}else if ({c2}) {b2 if b2.strip().startswith('{') else chr(10)+b2}")
        else:
            b2 = stmt(p, ind + "    ")
            if p.peek()[1] == ";":
                p.take()
            out.append(f"{ind}else {b2 if b2.strip().startswith('{') else chr(10)+b2}")
            break
    return "\n".join(out)


def assign_stmt(p, ind):
    name = p.need(kind="ID")
    if p.peek()[1] == "[":
        p.take()
        idx = expr(p)
        p.need("]")
        p.need("=")
        val = expr(p)
        if p.peek()[1] == ";":
            p.take()
        return f"{ind}yl_arr_set(s, YL_A_{name}, (int)({idx}), {val});"
    p.need("=")
    val = expr(p)
    if p.peek()[1] == ";":
        p.take()
    return f"{ind}{c_id(name)} = {val};"


def order_stmt(p, ind):
    kind = p.need(kind="ID")
    p.need("(")
    name = p.need(kind="STR")
    price_type = "TR_YL_ONCLOSE"
    price = "0"
    qty = "0"
    if p.peek()[1] == ",":
        p.take()
        style = p.peek()[1]
        if style in ("OnClose", "AtMarket", "AtStop", "AtLimit", "Def"):
            p.take()
            price_type = {
                "OnClose": "TR_YL_ONCLOSE", "AtMarket": "TR_YL_MARKET",
                "AtStop": "TR_YL_STOP", "AtLimit": "TR_YL_LIMIT", "Def": "TR_YL_ONCLOSE",
            }[style]
            # ExitLong(name, AtLimit, price, "", qty): 가격은 유형 바로 다음이다.
            if style in ("AtStop", "AtLimit") and p.peek()[1] == ",":
                p.take()
                if p.peek()[0] != "STR" and p.peek()[1] != ")":
                    price = expr(p)
        else:
            price = expr(p)
        if p.peek()[1] == ",":
            p.take()
            if p.peek()[1] in ("OnClose", "AtMarket", "AtStop", "AtLimit", "Def"):
                style = p.take()
                price_type = {
                    "OnClose": "TR_YL_ONCLOSE", "AtMarket": "TR_YL_MARKET",
                    "AtStop": "TR_YL_STOP", "AtLimit": "TR_YL_LIMIT", "Def": "TR_YL_ONCLOSE",
                }[style]
            if p.peek()[1] == ",":
                p.take()
            if p.peek()[1] not in (")",):
                if p.peek()[0] == "STR":
                    p.take()
                    if p.peek()[1] == ",":
                        p.take()
                if p.peek()[1] != ")":
                    qty = expr(p)
                    while p.peek()[1] == ",":
                        p.take()
                        if p.peek()[1] != ")":
                            expr(p)
    p.need(")")
    if p.peek()[1] == ";":
        p.take()
    action = {
        "Buy": "TR_YL_BUY", "Sell": "TR_YL_SELL",
        "ExitLong": "TR_YL_EXIT_LONG", "ExitShort": "TR_YL_EXIT_SHORT",
    }[kind]
    return f'{ind}yl_order(s, {action}, {price_type}, {price}, (int)({qty}), {name});'


def parse_body(toks):
    p = P(toks)
    lines = []
    while p.peek()[0] != "EOF":
        if p.peek()[1] == ";":
            p.take()
            continue
        lines.append(stmt(p, "    "))
    return "\n".join(x for x in lines if x)


def main():
    text = SRC.read_text()
    body = text.split("FX세션키 = WSF_FXSessionKeyV1(sDate,sTime);", 1)[1]
    body = "FX세션키 = WSF_FXSessionKeyV1(sDate,sTime);\n" + body
    src = strip_comments(body)
    toks = tokenize(src)
    code = parse_body(toks)
    header = '''#include "core/signals/fx_wr_yl.h"
#include <math.h>
void tr_fxwr_yl_body(tr_fxwr_t *s, const tr_fxwr_bar_t *b) {
'''
    OUT.write_text(header + code + "\n}\n")
    fields = sorted(set(re.findall(r"YL_F_([A-Za-z0-9_가-힣]+)", code)))
    arrs = [("도달기록", 50), ("회복진폭오늘", 391), ("회복진폭1", 391), ("회복진폭2", 391),
            ("회복진폭3", 391), ("회복진폭4", 391), ("회복진폭5", 391),
            ("회복량오늘", 391), ("회복량1", 391), ("회복량2", 391), ("회복량3", 391),
            ("회복량4", 391), ("회복량5", 391)]
    h = ["#ifndef TR_FX_WR_YL_H", "#define TR_FX_WR_YL_H",
         "#include <stdint.h>", '#include "core/backtest/yl_fill.h"', ""]
    h.append("enum {")
    for i, f in enumerate(fields):
        h.append(f"    YL_F_{f} = {i},")
    h.append(f"    YL_F_N = {len(fields)}")
    h.append("};")
    h.append("enum {")
    for i, (n, _) in enumerate(arrs):
        h.append(f"    YL_A_{n} = {i},")
    h.append("    YL_A_N")
    h.append("};")
    h.append("#define YL_LAG 32")
    h.append("typedef struct {")
    h.append("    int64_t date, time, bar_open;")
    h.append("    double open, high, low, close, volume, price_scale;")
    h.append("    int market;")
    h.append("    int is_new_bar;")
    h.append("} tr_fxwr_bar_t;")
    h.append("typedef struct {")
    h.append("    int action, price_type, qty;")
    h.append("    double price;")
    h.append("    char name[64];")
    h.append("} tr_fxwr_ord_t;")
    h.append("typedef struct tr_fxwr {")
    h.append("    double cur[YL_F_N];")
    h.append("    double lag[YL_LAG][YL_F_N];")
    h.append("    double arr[YL_A_N][391];")
    h.append("    double ph[64], hh[64], lh[64], ch[64], vh[64];")
    h.append("    int hist_n;")
    h.append("    int bar_n;")
    h.append("    int ord_n;")
    h.append("    int inited;")
    h.append("    tr_fxwr_ord_t ords[24];")
    h.append("    int mkt_side;")
    h.append("} tr_fxwr_t;")
    h.append("void tr_fxwr_yl_body(tr_fxwr_t *s, const tr_fxwr_bar_t *b);")
    h.append("double yl_lag(const tr_fxwr_t *s, int id, double k);")
    h.append("double yl_px(const tr_fxwr_t *s, char which, double k);")
    h.append("double yl_arr(const tr_fxwr_t *s, int id, int i);")
    h.append("void yl_arr_set(tr_fxwr_t *s, int id, int i, double v);")
    h.append("double yl_avg(const tr_fxwr_t *s, const char *which, double n);")
    h.append("double yl_hi(const tr_fxwr_t *s, const char *which, double n);")
    h.append("double yl_lo(const tr_fxwr_t *s, const char *which, double n);")
    h.append("double yl_rgb(double r, double g, double b);")
    h.append("double yl_dow(double yyyymmdd);")
    h.append("double yl_max(double a, double b);")
    h.append("double yl_min(double a, double b);")
    h.append("static inline double yl_at(double v, double i) { (void)i; return v; }")
    h.append("void yl_order(tr_fxwr_t *s, int action, int price_type, double price, int qty, const char *name);")
    for w in sorted(set(re.findall(r"yl_(WSF_[A-Za-z0-9_]+)", code))):
        h.append(f"double yl_{w}(tr_fxwr_t *s, const tr_fxwr_bar_t *b);")
    h.append("#endif")
    (ROOT / "src/core/signals/fx_wr_yl.h").write_text("\n".join(h) + "\n")
    init = ROOT / "src/core/signals/fx_wr_init.inc"
    text = SRC.read_text()
    text = re.sub(r"//.*", "", text)
    defaults = {}
    for m in re.finditer(r"([A-Za-z_가-힣][A-Za-z0-9_가-힣]*)\(([-0-9.]+)\)", text):
        defaults.setdefault(m.group(1), m.group(2))
    lines = ["void tr_fxwr_load_defaults(tr_fxwr_t *s) {"]
    for f in fields:
        if f in defaults:
            raw = defaults[f]
            if raw.startswith("0") and raw not in ("0", "0.0") and "." not in raw:
                raw = str(int(raw, 10))
            lines.append(f"    s->cur[YL_F_{f}] = {raw};")
    lines.append("}")
    init.write_text("\n".join(lines) + "\n")
    print("wrote", OUT, "fields", len(fields), "bytes", OUT.stat().st_size)


if __name__ == "__main__":
    main()
