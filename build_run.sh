#!/usr/bin/env bash

set -e

BUILD_DIR="build"
BUILD_TYPE=${1:-Debug}

export CLICOLOR_FORCE=1
export TERM=xterm-256color

echo -e "\033[1;34m[INFO]\033[0m Configuring project (${BUILD_TYPE} mode)..."

cmake -S . -B $BUILD_DIR -G Ninja -DCMAKE_BUILD_TYPE=$BUILD_TYPE

echo -e "\033[1;34m[INFO]\033[0m Building project using all CPU cores..."

cmake --build $BUILD_DIR -- -j$(nproc) -v

echo -e "\033[1;32m[INFO]\033[0m Running program..."

if [ "$BUILD_TYPE" = "Debug" ]; then
    ASAN_OPTIONS=detect_leaks=1:color=always \
    ./$BUILD_DIR/main
else
    ./$BUILD_DIR/main
fi