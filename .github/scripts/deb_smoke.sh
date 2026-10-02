#!/usr/bin/env bash
# Installs the .deb through apt, which must resolve its Depends, and starts the dedicated
# server through the command the package puts on PATH.
set -euo pipefail

deb=$(realpath "$1")
port=27998
run_seconds=10

sudo apt-get update -q
sudo apt-get install -y --no-install-recommends "$deb"

cd "$(mktemp -d)"
status=0
timeout "$run_seconds" zodiac13 --server="$port" || status=$?
if [ "$status" -ne 124 ]; then
  echo "::error::the installed server exited with $status instead of running"
  exit 1
fi
echo "the installed server ran for ${run_seconds}s"
