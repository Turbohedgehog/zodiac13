#!/usr/bin/env bash
# Turns the tails of a failed build's logs into error annotations, which the public API
# serves without a token (unlike the job log): check-runs/<id>/annotations.
# Usage: annotate_failure.sh <log or glob>...

shopt -s nullglob
# GitHub keeps 10 error annotations per step and cuts long ones.
max_annotations=10
tail_lines=60
count=0
for pattern in "$@"; do
  for log in $pattern; do
    # Applying a port's patches logs to stderr even when it succeeds.
    [[ -s "$log" && "$log" != */patch-* ]] || continue
    (( count++ < max_annotations )) || exit 0
    body=$(tail -n "$tail_lines" "$log" | sed -e 's/%/%25/g' -e 's/\r//g' | sed -e ':a;N;$!ba;s/\n/%0A/g')
    echo "::error title=${log}::${body}"
  done
done
