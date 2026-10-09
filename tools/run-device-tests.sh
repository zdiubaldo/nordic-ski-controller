#!/usr/bin/env bash
# Uses the current network. Never changes Wi-Fi or reads network credentials.
set -euo pipefail
cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.."
if command -v node >/dev/null 2>&1; then
  device_node=$(command -v node)
else
  device_node="${HOME}/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/bin/node"
fi
if [[ ! -x "$device_node" || ! -f node_modules/@playwright/test/cli.js ]]; then
  echo 'Node and Playwright must be installed before going offline.' >&2
  exit 1
fi
export NORDIC_BASE_URL="${NORDIC_BASE_URL:-http://192.168.4.1}"
mkdir -p .local
printf 'Testing %s using your current network. Close other controller pages first.\n' "$NORDIC_BASE_URL"
printf 'No network settings will be changed. Results: .local/device-report/index.html\n'
"$device_node" node_modules/@playwright/test/cli.js test "$@" 2>&1 | tee .local/device-test-run.log
