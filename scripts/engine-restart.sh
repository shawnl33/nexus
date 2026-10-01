#!/bin/bash
#
# Script: engine-restart.sh
# Description: trading-engine 안전 재기동.
#   정지(traderctl → SIGTERM → SIGKILL) 후 프로세스 소멸을 확인하고 시작한다.
#   2026-10-01 traderctl stop 타임아웃 시 구 프로세스가 남아 새 엔진이 포트 충돌
#   (zmq: Address already in use)로 즉사한 사건의 재발 방지용.
# Usage: scripts/engine-restart.sh [symbol] [log]
#   symbol  기본 A016C000 (--live-fut 인자)
#   log     기본 /tmp/engine-lived2.log
#

set -euo pipefail

readonly ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
readonly ENGINE="$ROOT/build/trading-engine"
readonly CTL="$ROOT/build/traderctl"
readonly ENV_FILE="$ROOT/.env"
readonly SYMBOL="${1:-A016C000}"
readonly LOG="${2:-/tmp/engine-lived2.log}"
readonly STOP_WAIT_SEC=8
readonly READY_WAIT_SEC=240   # 선물 2일치 백필은 수 분 걸릴 수 있다

log() { echo "[$(date '+%H:%M:%S')] $*"; }
die() { echo "[ERROR] $*" >&2; exit 1; }

engine_pids() { pgrep -x trading-engine || true; }

wait_gone() { # $1=초. 소멸하면 0, 아니면 1
    local i
    for ((i=0; i<$1; i++)); do
        [[ -z "$(engine_pids)" ]] && return 0
        sleep 1
    done
    return 1
}

stop_engine() {
    local pids
    pids="$(engine_pids)"
    [[ -z "$pids" ]] && { log "실행 중인 엔진 없음"; return 0; }

    log "정지 요청: $pids"
    # traderctl 응답 타임아웃은 흔하므로 결과는 프로세스 기준으로만 판정한다
    "$CTL" engine stop >/dev/null 2>&1 || true
    if wait_gone "$STOP_WAIT_SEC"; then
        log "정상 정지됨"
        return 0
    fi

    pids="$(engine_pids)"
    log "응답 없음 — SIGTERM: $pids"
    kill $pids 2>/dev/null || true
    if wait_gone "$STOP_WAIT_SEC"; then
        log "SIGTERM으로 정지됨"
        return 0
    fi

    pids="$(engine_pids)"
    log "SIGKILL: $pids"
    kill -9 $pids 2>/dev/null || true
    wait_gone 3 || die "엔진을 죽일 수 없음: $(engine_pids)"
}

start_engine() {
    [[ -x "$ENGINE" ]] || die "엔진 바이너 없음: $ENGINE (cmake --build build 먼저)"
    [[ -f "$ENV_FILE" ]] || die ".env 없음: $ENV_FILE"
    set -a; source "$ENV_FILE"; set +a
    cd "$ROOT"
    nohup "$ENGINE" --live-fut "$SYMBOL" > "$LOG" 2>&1 &
    local pid=$!
    sleep 2
    kill -0 "$pid" 2>/dev/null || { tail -5 "$LOG" >&2; die "엔진이 기동 직후 종료됨"; }
    # 준비 확인은 IPC로 한다 — stdout은 파일 리다이렉트 시 블록 버퍼링이라
    # 로그의 streaming 줄이 늦게 찍혀 grep 기준은 부정확하다 (2026-10-01 실측).
    # traderctl status는 엔진이 백필을 끝내고 라이브 루프(명령 처리)에 들어가야 응답한다.
    local i
    for ((i=0; i<READY_WAIT_SEC; i++)); do
        if "$CTL" status >/dev/null 2>&1; then
            log "기동 완료: pid $pid ($SYMBOL, 로그 $LOG)"
            return 0
        fi
        kill -0 "$pid" 2>/dev/null || { tail -5 "$LOG" >&2; die "엔진이 백필 중 종료됨"; }
        sleep 1
    done
    log "경고: ${READY_WAIT_SEC}초 내 준비 확인 못함 — $LOG 확인 요망"
}

stop_engine
start_engine
