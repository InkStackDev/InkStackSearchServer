#!/usr/bin/env bash
#
# collect.sh — 收集 InkStackSearchServer 部署所需文件并打包
#
# 用法:
#   ./collect.sh [ENV]      ENV: dev|prod，默认 prod
#
# 说明:
#   - 配置由 switch-env.sh 从 bin/conf/<ENV>.env + *.yml.tpl 渲染到 dist/conf
#   - prod 需要先准备 bin/conf/prod.env
#   - 产物包含 server / module / lib / conf / dict / scripts / deploy.env
#
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")" && pwd)"
ENV_NAME="${1:-prod}"
STAGE_DIR="$ROOT_DIR/dist"
PKG_NAME="$ROOT_DIR/inkstack-search-$(date +%Y%m%d%H%M%S).tar.gz"

CHEN_SDK_LIB=""
for lib in "$ROOT_DIR"/chen-sdk-*/lib/libchen.so; do
    if [ -f "$lib" ]; then
        CHEN_SDK_LIB="$lib"
        break
    fi
done

check_file() {
    local path="$1"
    if [ ! -e "$path" ]; then
        echo "错误: 缺少文件 $path" >&2
        exit 1
    fi
}

check_file "$ROOT_DIR/bin/server"
check_file "$ROOT_DIR/dict"
check_file "$ROOT_DIR/scripts/searchctl.sh"
if [ -z "$CHEN_SDK_LIB" ]; then
    echo "错误: 找不到 chen-sdk-*/lib/libchen.so" >&2
    exit 1
fi

ENV_FILE="$ROOT_DIR/bin/conf/$ENV_NAME.env"
if [ ! -f "$ENV_FILE" ]; then
    if [ "$ENV_NAME" = "prod" ]; then
        echo "错误: 缺少 $ENV_FILE" >&2
        echo "请先执行: cp bin/conf/prod.env.example bin/conf/prod.env 并填写真实值" >&2
    else
        echo "错误: 缺少 $ENV_FILE" >&2
    fi
    exit 1
fi

shopt -s nullglob
module_sos=("$ROOT_DIR"/bin/module/*.so)
shopt -u nullglob
if [ ${#module_sos[@]} -eq 0 ]; then
    echo "错误: bin/module/ 下没有 .so 模块" >&2
    exit 1
fi

echo "==> 打包环境 [$ENV_NAME]"

rm -rf "$STAGE_DIR"
mkdir -p "$STAGE_DIR"/{conf,module,lib,dict,scripts}

# 1) 渲染配置
OUT_CONF_DIR="$STAGE_DIR/conf" "$ROOT_DIR/switch-env.sh" "$ENV_NAME"

# 2) 二进制 / 模块 / 框架库
cp "$ROOT_DIR/bin/server" "$STAGE_DIR/server"
chmod +x "$STAGE_DIR/server"
for so in "${module_sos[@]}"; do
    cp "$so" "$STAGE_DIR/module/"
done
cp "$CHEN_SDK_LIB" "$STAGE_DIR/lib/libchen.so"

# 3) 词典、许可证、控制脚本
cp -R "$ROOT_DIR/dict/." "$STAGE_DIR/dict/"
cp "$ROOT_DIR/LICENSE" "$STAGE_DIR/LICENSE"
cp "$ROOT_DIR/scripts/searchctl.sh" "$STAGE_DIR/scripts/searchctl.sh"
chmod +x "$STAGE_DIR/scripts/searchctl.sh"

# 4) deploy.env：控制脚本用（与渲染配置保持同一套默认值）
set -a
# shellcheck disable=SC1090
. "$ENV_FILE"
set +a

WORK_PATH="${WORK_PATH:-$ROOT_DIR}"
LOG_DIR="${LOG_DIR:-$WORK_PATH/logs}"
INDEX_PATH="${INDEX_PATH:-$WORK_PATH/indices/search.index}"
DICT_PATH="${DICT_PATH:-$WORK_PATH/dict}"
PID_FILE="${PID_FILE:-search.pid}"
RPC_PORT="${RPC_PORT:-9010}"

cat > "$STAGE_DIR/deploy.env" <<EOF
WORK_PATH=$WORK_PATH
LOG_DIR=$LOG_DIR
INDEX_PATH=$INDEX_PATH
DICT_PATH=$DICT_PATH
PID_FILE=$PID_FILE
RPC_PORT=$RPC_PORT
EOF

# 5) 打包
tar czf "$PKG_NAME" -C "$STAGE_DIR" .
echo "==> 打包完成: $PKG_NAME"
echo "==> 部署目录: $STAGE_DIR"
