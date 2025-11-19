#!/usr/bin/env bash
set -e

cd "$(dirname "$0")/../src"
make clean
make
echo "Built successfully. Running with debug..."
./glwall -s ../shaders/template.glsl --debug
