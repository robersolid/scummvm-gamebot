#!/bin/sh
APP_DIR="$(dirname "$0")"
export APPID="${APPID:-org.scummvm.webos}"
export XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR:-/tmp/xdg}"
export WAYLAND_DISPLAY="${WAYLAND_DISPLAY:-wayland-0}"
export LD_LIBRARY_PATH="$APP_DIR/lib:$LD_LIBRARY_PATH"
export SDL_WEBOS_CURSOR_SLEEP_TIME=1
exec "$APP_DIR/scummvm-bin" --extrapath="$APP_DIR/data" --themepath="$APP_DIR/data" --gui-theme=scummmodern "$@"
