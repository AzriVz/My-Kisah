#!/usr/bin/env bash
set -euo pipefail

repository_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build_directory="${1:-${repository_root}/build}"

"${repository_root}/scripts/build.sh" "${build_directory}"
ctest --test-dir "${build_directory}" --output-on-failure

