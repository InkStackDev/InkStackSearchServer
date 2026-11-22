#!/usr/bin/env bash
#
# switch-env.sh — 渲染 bin/conf/*.yml.tpl 为实际配置
#
# 用法:
#   ./switch-env.sh dev      # 使用 bin/conf/dev.env
#   ./switch-env.sh prod     # 使用 bin/conf/prod.env
#
# 说明:
#   - WORK_PATH 优先取自 <ENV>.env，未设置时使用仓库根目录
#   - LOG_DIR / INDEX_PATH / DICT_PATH 未设置时按 WORK_PATH 推导
#   - 可用 OUT_CONF_DIR 指定渲染输出目录（打包时使用）
#
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")" && pwd)"
CONF_DIR="$ROOT_DIR/bin/conf"
OUT_CONF_DIR="${OUT_CONF_DIR:-$CONF_DIR}"

ENV_NAME="${1:-}"
case "$ENV_NAME" in
    dev | prod) ;;
    *) echo "Usage: $0 <dev|prod>" >&2; exit 2 ;;
esac

ENV_FILE="$CONF_DIR/$ENV_NAME.env"
if [ ! -f "$ENV_FILE" ]; then
    if [ "$ENV_NAME" = "prod" ]; then
        echo "缺少生产配置: $ENV_FILE" >&2
        echo "请先执行: cp bin/conf/prod.env.example bin/conf/prod.env 并填写真实值" >&2
    else
        echo "缺少配置: $ENV_FILE" >&2
    fi
    exit 1
fi

set -a
# shellcheck disable=SC1090
. "$ENV_FILE"
set +a

export WORK_PATH="${WORK_PATH:-$ROOT_DIR}"
export LOG_DIR="${LOG_DIR:-$WORK_PATH/logs}"
export INDEX_PATH="${INDEX_PATH:-$WORK_PATH/indices/search.index}"
export DICT_PATH="${DICT_PATH:-$WORK_PATH/dict}"
export DATA_PATH="${DATA_PATH:-}"
export PID_FILE="${PID_FILE:-search.pid}"
export INSTANCE_ID="${INSTANCE_ID:-1}"
export LOG_LEVEL="${LOG_LEVEL:-info}"
export RPC_PORT="${RPC_PORT:-9010}"
export RELAY_TIMEOUT_MS="${RELAY_TIMEOUT_MS:-3000}"
export IO_THREADS="${IO_THREADS:-1}"
export ACCEPT_THREADS="${ACCEPT_THREADS:-1}"
export REBUILD_ON_START="${REBUILD_ON_START:-true}"
export SAVE_INTERVAL_MS="${SAVE_INTERVAL_MS:-30000}"
export REBUILD_TIMEOUT_MS="${REBUILD_TIMEOUT_MS:-600000}"
export MAX_PAGE_SIZE="${MAX_PAGE_SIZE:-100}"

render_one() {
    local tpl="$1" out="$2"
    if command -v envsubst >/dev/null 2>&1; then
        envsubst < "$tpl" > "$out"
    else
        awk '{
            while (match($0, /\$\{[A-Za-z_][A-Za-z0-9_]*\}/)) {
                name = substr($0, RSTART + 2, RLENGTH - 3)
                $0 = substr($0, 1, RSTART - 1) ENVIRON[name] substr($0, RSTART + RLENGTH)
            }
            print
        }' "$tpl" > "$out"
    fi

    if grep -qE '\$\{[A-Za-z_][A-Za-z0-9_]*\}' "$out"; then
        echo "错误: $out 中存在未替换的占位符，请检查 $ENV_FILE" >&2
        exit 1
    fi
}

mkdir -p "$OUT_CONF_DIR"
shopt -s nullglob
tpls=("$CONF_DIR"/*.yml.tpl)
shopt -u nullglob
if [ ${#tpls[@]} -eq 0 ]; then
    echo "错误: $CONF_DIR 下没有 *.yml.tpl" >&2
    exit 1
fi

for tpl in "${tpls[@]}"; do
    out="$OUT_CONF_DIR/$(basename "${tpl%.tpl}")"
    render_one "$tpl" "$out"
    echo "  已生成 $(basename "$out")"
done

echo "配置已生成 [$ENV_NAME]"
echo "  work_path: $WORK_PATH"
echo "  输出目录:  $OUT_CONF_DIR"
