#!/usr/bin/env bash
# Unpacks the Linux package away from the build tree and starts the dedicated server from
# it: every library must resolve from the package itself, and the server must keep running.
set -euo pipefail

archive=$1
port=27999
run_seconds=10

dir=$(mktemp -d)
tar -xzf "$archive" -C "$dir" --strip-components=1

missing=$(find "$dir" -type f \( -name zodiac13 -o -name '*.so*' \) -exec ldd {} \; | grep "not found" || true)
if [ -n "$missing" ]; then
  echo "::error::unresolved libraries in the package"
  echo "$missing"
  exit 1
fi

# From elsewhere, so nothing is found relative to the working directory by accident.
cd "$(mktemp -d)"
status=0
timeout "$run_seconds" "$dir/zodiac13" --server="$port" || status=$?
if [ "$status" -ne 124 ]; then
  echo "::error::the packaged server exited with $status instead of running"
  exit 1
fi
echo "the packaged server ran for ${run_seconds}s"
