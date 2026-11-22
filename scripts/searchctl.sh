#!/usr/bin/env bash
#
# searchctl.sh — InkStackSearchServer 启停 / 状态 / 健康检查（框架自带 -d / -s）
#
# 用法（在部署目录根下执行）:
#   ./scripts/searchctl.sh start
#   ./scripts/searchctl.sh stop
#   ./scripts/searchctl.sh restart
#   ./scripts/searchctl.sh reload
#   ./scripts/searchctl.sh status
#   ./scripts/searchctl.sh health
#
# 依赖部署目录结构:
#   server  module/  lib/  conf/  dict/  deploy.env  scripts/
#
# 可用环境变量:
#   SERVER_BIN       server 路径（默认 <root>/server）
#   START_TIMEOUT    启动等待秒数（默认 30）
#   STOP_TIMEOUT     停止等待秒数（默认 30）
#
set -euo pipefail

ROOT="$(cd "$(pwd)" && pwd)"
SERVER="${SERVER_BIN:-$ROOT/server}"
START_TIMEOUT="${START_TIMEOUT:-30}"
STOP_TIMEOUT="${STOP_TIMEOUT:-30}"

# libchen.so 在 <root>/lib；-d 守护进程会继承
export LD_LIBRARY_PATH="$ROOT/lib:${LD_LIBRARY_PATH:-}"

if [ -f "$ROOT/deploy.env" ]; then
    set -a
    # shellcheck disable=SC1091
    . "$ROOT/deploy.env"
    set +a
fi

WORK_PATH="${WORK_PATH:-$ROOT}"
LOG_DIR="${LOG_DIR:-$WORK_PATH/logs}"
INDEX_PATH="${INDEX_PATH:-$WORK_PATH/indices/search.index}"
PID_FILE_PATH="${PID_FILE_PATH:-$WORK_PATH/${PID_FILE:-search.pid}}"
RPC_PORT="${RPC_PORT:-9010}"

log() { echo "[search] $*"; }
die() { echo "[search][ERROR] $*" >&2; exit 1; }

[ -x "$SERVER" ] || die "找不到可执行文件: $SERVER"
[ -f "$ROOT/lib/libchen.so" ] || die "找不到共享库: $ROOT/lib/libchen.so"
[ -f "$ROOT/conf/system.yml" ] || die "找不到配置: $ROOT/conf/system.yml"

pid_of() {
    [ -f "$PID_FILE_PATH" ] || return 1
    tr -dc '0-9' < "$PID_FILE_PATH"
}

pid_alive() {
    local pid
    pid="$(pid_of 2>/dev/null || true)"
    [ -n "$pid" ] && kill -0 "$pid" 2>/dev/null
}

health() {
    (exec 3<>"/dev/tcp/127.0.0.1/$RPC_PORT") >/dev/null 2>&1
}

wait_health() {
    local deadline=$(( $(date +%s) + START_TIMEOUT ))
    while [ "$(date +%s)" -lt "$deadline" ]; do
        health && return 0
        sleep 1
    done
    return 1
}

start() {
    if pid_alive; then
        log "已在运行 (pid=$(pid_of))"
        return 0
    fi
    mkdir -p "$WORK_PATH" "$LOG_DIR"
    [ -n "$INDEX_PATH" ] && mkdir -p "$(dirname "$INDEX_PATH")"

    log "启动 (conf=$ROOT/conf, rpc_port=$RPC_PORT)"
    ( cd "$ROOT" && "$SERVER" -c "$ROOT/conf" -d ) || die "启动命令失败"

    local deadline=$(( $(date +%s) + START_TIMEOUT ))
    while [ "$(date +%s)" -lt "$deadline" ]; do
        pid_alive && break
        sleep 1
    done
    pid_alive || die "启动后 ${START_TIMEOUT}s 内未写出 pid 文件"

    wait_health || die "已启动但 RPC 端口 ${RPC_PORT} 未就绪"
    log "启动完成 (pid=$(pid_of))"
}

stop() {
    if ! pid_alive; then
        log "未运行"
        rm -f "$PID_FILE_PATH"
        return 0
    fi
    local pid; pid="$(pid_of)"
    log "停止 (pid=$pid)"
    ( cd "$ROOT" && "$SERVER" -c "$ROOT/conf" -s stop ) >/dev/null 2>&1 || true

    local deadline=$(( $(date +%s) + STOP_TIMEOUT ))
    while [ "$(date +%s)" -lt "$deadline" ]; do
        pid_alive || { log "已停止"; return 0; }
        sleep 1
    done

    log "优雅停止超时，发送 SIGTERM"
    kill -TERM "$pid" 2>/dev/null || true
    deadline=$(( $(date +%s) + 10 ))
    while [ "$(date +%s)" -lt "$deadline" ]; do
        pid_alive || { log "已停止"; return 0; }
        sleep 1
    done

    log "仍在运行，发送 SIGKILL"
    kill -KILL "$pid" 2>/dev/null || true
    rm -f "$PID_FILE_PATH"
    pid_alive && die "无法停止"
    log "已强制停止"
}

reload() {
    pid_alive || die "未运行，无法 reload"
    log "热重载"
    ( cd "$ROOT" && "$SERVER" -c "$ROOT/conf" -s reload ) || die "reload 失败"
    wait_health || die "reload 后 RPC 端口 ${RPC_PORT} 未就绪"
    log "热重载完成 (pid=$(pid_of))"
}

status() {
    if pid_alive; then
        printf 'running pid=%s rpc_port=%s\n' "$(pid_of)" "$RPC_PORT"
    else
        printf 'stopped rpc_port=%s\n' "$RPC_PORT"
    fi
}

case "${1:-}" in
    start)   start ;;
    stop)    stop ;;
    restart) stop; start ;;
    reload)  reload ;;
    status)  status ;;
    health)
        if health; then log "健康 (rpc_port=$RPC_PORT)"; else die "RPC 端口 ${RPC_PORT} 不可达"; fi
        ;;
    *)
        sed -n '2,20p' "$0"
        [ -z "${1:-}" ] && exit 2 || exit 0
        ;;
esac
