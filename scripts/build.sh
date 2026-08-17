#!/usr/bin/env bash
set -euo pipefail

repository_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build_directory="${1:-${repository_root}/build}"

cmake -S "${repository_root}" -B "${build_directory}" -DCMAKE_BUILD_TYPE=Release
cmake --build "${build_directory}" --parallel "${BUILD_JOBS:-2}"

