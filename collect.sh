#!/usr/bin/env bash
#
# collect.sh — 收集 InkStackSearchServer 部署所需文件并打包
#
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")" && pwd)"
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
if [ -z "$CHEN_SDK_LIB" ]; then
    echo "错误: 找不到 chen-sdk-*/lib/libchen.so" >&2
    exit 1
fi

shopt -s nullglob
module_sos=("$ROOT_DIR"/bin/module/*.so)
shopt -u nullglob
if [ ${#module_sos[@]} -eq 0 ]; then
    echo "错误: bin/module/ 下没有 .so 模块" >&2
    exit 1
fi

rm -rf "$STAGE_DIR"
mkdir -p "$STAGE_DIR/conf" "$STAGE_DIR/module" "$STAGE_DIR/lib" "$STAGE_DIR/dict"

cp "$ROOT_DIR/bin/conf/"*.yml "$STAGE_DIR/conf/" 2>/dev/null || true
cp "$ROOT_DIR/bin/server" "$STAGE_DIR/server"
chmod +x "$STAGE_DIR/server"
for so in "${module_sos[@]}"; do
    cp "$so" "$STAGE_DIR/module/"
done
cp "$CHEN_SDK_LIB" "$STAGE_DIR/lib/"
cp -R "$ROOT_DIR/dict/." "$STAGE_DIR/dict/"
cp "$ROOT_DIR/LICENSE" "$STAGE_DIR/LICENSE"

tar czf "$PKG_NAME" -C "$STAGE_DIR" .
echo "==> 打包完成: $PKG_NAME"
echo "==> 部署目录: $STAGE_DIR"
