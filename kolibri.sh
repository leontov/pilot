#!/usr/bin/env bash
# Copyright (c) 2024 Кочуров Владислав Евгеньевич

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BIN_DIR="$ROOT_DIR/bin"
LOG_DIR="$ROOT_DIR/logs"
DATA_DIR="$ROOT_DIR/data"
PID_FILE="$LOG_DIR/kolibri.pid"
NODE_BIN="$BIN_DIR/kolibri_node"
WEB_DIR="$ROOT_DIR/web"

mkdir -p "$BIN_DIR" "$LOG_DIR" "$DATA_DIR"

start_node() {
  if [ ! -x "$NODE_BIN" ]; then
    echo "[kolibri] building native node" >&2
    make -C "$ROOT_DIR" build
  fi

  if [ ! -d "$WEB_DIR/node_modules" ]; then
    echo "[kolibri] installing web dependencies" >&2
    (cd "$WEB_DIR" && npm install)
  fi
  echo "[kolibri] building web bundle" >&2
  (cd "$WEB_DIR" && npm run build)

  echo "[kolibri] launching node" >&2
  "$NODE_BIN" >"$LOG_DIR/kolibri.log" 2>&1 &
  echo $! > "$PID_FILE"
  echo "Kolibri Ω node is running at http://localhost:9000"
  echo "Status endpoint available at http://localhost:10000/status"
}

stop_node() {
  if [ -f "$PID_FILE" ]; then
    PID=$(cat "$PID_FILE")
    if kill -0 "$PID" 2>/dev/null; then
      kill "$PID"
      wait "$PID" 2>/dev/null || true
      echo "Stopped Kolibri Ω node ($PID)"
    fi
    rm -f "$PID_FILE"
  else
    echo "Kolibri Ω node is not running"
  fi
}

case "${1:-}" in
  up)
    start_node
    ;;
  stop)
    stop_node
    ;;
  bench)

    shift
    timestamp=$(date +%Y%m%dT%H%M%S)
    args=("$@")
    has_output=0
    has_warmup=0
    has_iterations=0
    has_profile_flag=0
    i=0
    while [ $i -lt ${#args[@]} ]; do
      case "${args[$i]}" in
        --output)
          has_output=1
          i=$((i + 2))
          continue
          ;;
        --no-output)
          has_output=1
          ;;
        --warmup)
          has_warmup=1
          i=$((i + 2))
          continue
          ;;
        --iterations)
          has_iterations=1
          i=$((i + 2))
          continue
          ;;
        --profile|--no-profile)
          has_profile_flag=1
          ;;
      esac
      i=$((i + 1))
    done
    bench_output=""
    if [ $has_output -eq 0 ]; then
      bench_output="$LOG_DIR/bench_$timestamp.json"
      args+=(--output "$bench_output")
    fi
    if [ $has_warmup -eq 0 ]; then
      args+=(--warmup 20)
    fi
    if [ $has_iterations -eq 0 ]; then
      args+=(--iterations 200)
    fi
    if [ $has_profile_flag -eq 0 ]; then
      args+=(--profile)
    fi
    BENCH_ARGS="${args[*]}"
    make -C "$ROOT_DIR" bench BENCH_ARGS="$BENCH_ARGS"
    if [ -n "$bench_output" ]; then
      echo "[kolibri] benchmark report saved to $bench_output"
    fi

    echo "[kolibri] running Δ-VM and F-KV microbenchmarks" >&2
    make -C "$ROOT_DIR" bench
    echo "[kolibri] benchmark metrics saved to $LOG_DIR/bench.log and $LOG_DIR/bench.json" >&2

    ;;
  clean)
    stop_node || true
    make -C "$ROOT_DIR" clean
    ;;
  *)
    echo "Usage: $0 {up|stop|bench|clean}"
    exit 1
    ;;
 esac
