#!/usr/bin/env bash
# Simple run script for kolibri web server
BASE_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BIN="$BASE_DIR/build/web_interface"
LOG="/tmp/web_interface.log"
PIDFILE="/tmp/web_interface.pid"

case "$1" in
  start)
    if [ -f "$PIDFILE" ] && kill -0 "$(cat $PIDFILE)" 2>/dev/null; then
      echo "Already running: $(cat $PIDFILE)"
      exit 0
    fi
    nohup "$BIN" </dev/null >"$LOG" 2>&1 &
    echo $! >"$PIDFILE"
    echo "Started $BIN with PID $(cat $PIDFILE)"
    ;;
  stop)
    if [ -f "$PIDFILE" ]; then
      kill "$(cat $PIDFILE)" 2>/dev/null || true
      rm -f "$PIDFILE"
      echo "Stopped"
    else
      echo "Not running"
    fi
    ;;
  status)
    if [ -f "$PIDFILE" ] && kill -0 "$(cat $PIDFILE)" 2>/dev/null; then
      echo "Running pid=$(cat $PIDFILE)"
      curl -sS --connect-timeout 2 http://127.0.0.1:8888/api/v1/status || true
    else
      echo "Not running"
    fi
    ;;
  tail)
    tail -n 200 "$LOG"
    ;;
  frontend)
    # serve web/ on port 8000 for local frontend development
    (cd "$(dirname "${BASH_SOURCE[0]}")/.." && python3 -m http.server 8000 --directory web)
    ;;
  *)
    echo "Usage: $0 {start|stop|status|tail}"
    exit 2
    ;;
esac
