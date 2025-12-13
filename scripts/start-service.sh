#!/usr/bin/env bash

set -euo pipefail

mkdir -p .logs/tmp

existing_pids="$(pgrep -f "scripts/remove-comments.js" 2>/dev/null || true)"
if [ -n "${existing_pids}" ]; then
    for pid in ${existing_pids}; do
        args="$(ps -p "${pid}" -o args= 2>/dev/null || true)"
        if [[ "${args}" == *"--watch"* ]]; then
            if [ "${GLWALL_VERBOSE:-0}" = "1" ]; then
                echo "Background comment cleaner is already running."
            fi
            exit 0
        fi
    done

    for pid in ${existing_pids}; do
        kill "${pid}" 2>/dev/null || true
    done
fi

if command -v setsid >/dev/null 2>&1; then
    setsid -f node scripts/remove-comments.js --watch --format --quiet >> .logs/tmp/remove-comments.log 2>&1 < /dev/null
else
    nohup node scripts/remove-comments.js --watch --format --quiet >> .logs/tmp/remove-comments.log 2>&1 < /dev/null &
    disown
fi

if [ "${GLWALL_VERBOSE:-0}" = "1" ]; then
    echo "Started background comment cleaner."
fi
