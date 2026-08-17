#!/usr/bin/env bash
set -euo pipefail

repository_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
output_directory="${1:-${repository_root}/build/test-binaries}"
mkdir -p "${output_directory}"

compile_sample() {
    local source_name="$1"
    local output_name="$2"
    g++ "${repository_root}/tests/samples/${source_name}" \
        -std=c++20 \
        -O1 \
        -fno-inline \
        -fno-omit-frame-pointer \
        -fno-optimize-sibling-calls \
        -no-pie \
        -o "${output_directory}/${output_name}"
}

compile_sample arithmetic.cpp arithmetic
compile_sample simple_if.cpp simple-if
compile_sample if_else.cpp if-else
compile_sample simple_loop.cpp simple-loop
compile_sample function_call.cpp function-call
compile_sample nested_condition.cpp nested-condition
compile_sample recursion.cpp recursion
compile_sample strings.cpp strings

echo "Mandatory sample binaries were written to ${output_directory}"
