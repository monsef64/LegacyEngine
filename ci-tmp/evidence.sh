#!/bin/bash
# TEMPORARY (test run of the release workflow on a real runner).
# Annotations are the only output of a run that can be read back from where the
# workflow was written, so every finding is reported as a notice. A notice
# holds 4096 characters.
set -u

out=$RUNNER_TEMP/evidence
mkdir -p "$out"

note() { # note <title> <text>
  printf '::notice title=%s::%s\n' "$1" \
    "$(printf '%s' "$2" | head -c 3900 | sed -z 's/%/%25/g; s/\r/%0D/g; s/\n/%0A/g')"
}

send() { # send <name>: the file $out/<name> as base64, 4000 characters per notice, 10 at most
  if [ ! -s "$out/$1" ]; then
    note "$1" "no file"
    return
  fi
  base64 -w0 "$out/$1" | fold -w 4000 > "$out/chunks"
  total=$(grep -c '' "$out/chunks")
  i=0
  while IFS= read -r chunk || [ -n "$chunk" ]; do
    i=$((i + 1))
    if [ "$i" -le 10 ]; then
      printf '::notice title=%s %02d of %02d::%s\n' "$1" "$i" "$total" "$chunk"
    fi
  done < "$out/chunks"
}

wait_exit() { # wait_exit <pid>: sets $status to the exit status of a process that should end by itself
  for _ in 1 2 3 4 5 6 7 8 9 10; do
    kill -0 "$1" 2> /dev/null || break
    sleep 0.5
  done
  if kill -0 "$1" 2> /dev/null; then
    kill -9 "$1"
    wait "$1"
    status="none: still running after 5 s"
  else
    status=0
    wait "$1" || status=$?
  fi
}

shot() { # shot <name>: the X screen as $out/<name>.z, its colour census as $out/<name>.census
  cp "$out/fb/Xvfb_screen0" "$out/$1.xwd"
  python3 ci-tmp/shot.py "$out/$1.xwd" "$out/$1.z" > "$out/$1.census" 2>&1
}

start_weston() {
  weston "$@" --width=1280 --height=720 --socket=wl-77 --debug > "$out/weston.log" 2>&1 &
  weston=$!
  for _ in 1 2 3 4 5 6 7 8 9 10; do
    [ -S "$XDG_RUNTIME_DIR/wl-77" ] && return 0
    sleep 0.5
  done
  kill "$weston" 2> /dev/null
  return 1
}

case "$1" in
  run)
    sudo apt-get install -y -q xdotool python3-xlib weston 2>&1 | tail -n 2

    sudo mkdir -p /tmp/.X11-unix
    sudo chmod 1777 /tmp/.X11-unix
    mkdir -p "$out/fb"
    Xvfb :77 -screen 0 1280x720x24 -fbdir "$out/fb" > "$out/xvfb.log" 2>&1 &
    xvfb=$!
    sleep 3
    export DISPLAY=:77

    # X11 with GLX, driven by real mouse and keyboard input (XTEST). The pointer
    # is parked in the corner for the screenshots.
    xdotool mousemove 1279 719
    build/LegacyEngine > "$out/glx.log" 2>&1 &
    engine=$!
    sleep 5
    geometry=$(xdotool search --name '^LegacyEngine$' getwindowgeometry 2>&1 | tr '\n' ' ')
    shot glx-start
    # Left-drag by (+100, +60), then move without a button.
    xdotool mousemove 640 360 sleep 0.3 mousedown 1 sleep 0.3 mousemove 740 420 sleep 0.3 mouseup 1 sleep 0.3 mousemove 1279 719
    sleep 1.5
    shot glx-drag
    # Two wheel steps up.
    xdotool mousemove 640 360 sleep 0.3 click 4 sleep 0.3 click 4 sleep 0.3 mousemove 1279 719
    sleep 1.5
    shot glx-wheel
    # Resize the window.
    resize=$(xdotool search --name '^LegacyEngine$' windowsize 800 600 2>&1)
    sleep 1.5
    geometry2=$(xdotool search --name '^LegacyEngine$' getwindowgeometry 2>&1 | tr '\n' ' ')
    shot glx-resize
    alive=no
    kill -0 "$engine" 2> /dev/null && alive=yes
    xdotool mousemove 400 300 sleep 0.3 key Escape
    wait_exit "$engine"
    note glx "running before Esc: $alive | exit status after Esc: $status
window: $geometry
after windowsize 800 600: $resize $geometry2
$(cat "$out/glx.log")"
    note glx-census "start: $(cat "$out/glx-start.census")
drag: $(cat "$out/glx-drag.census")
wheel: $(cat "$out/glx-wheel.census")
resize: $(cat "$out/glx-resize.census")"

    # X11 with EGL, then the close request that a window manager sends.
    xdotool mousemove 1279 719
    SDL_VIDEO_FORCE_EGL=1 build/LegacyEngine > "$out/egl.log" 2>&1 &
    engine=$!
    sleep 5
    shot egl-start
    alive=no
    kill -0 "$engine" 2> /dev/null && alive=yes
    python3 ci-tmp/close.py > "$out/close.log" 2>&1
    wait_exit "$engine"
    note egl "running before the close request: $alive | exit status after it: $status
$(cat "$out/close.log")
$(cat "$out/egl.log")
$(cat "$out/egl-start.census")"
    kill "$xvfb"
    unset DISPLAY

    # Wayland, on a headless compositor that shows the window full screen.
    export XDG_RUNTIME_DIR=$out/xdg
    mkdir -p "$XDG_RUNTIME_DIR"
    chmod 700 "$XDG_RUNTIME_DIR"
    start_weston --backend=headless --renderer=pixman --shell=kiosk ||
      start_weston --backend=headless-backend.so --use-pixman --shell=kiosk-shell.so
    WAYLAND_DISPLAY=wl-77 SDL_VIDEO_DRIVER=wayland build/LegacyEngine > "$out/wayland.log" 2>&1 &
    engine=$!
    sleep 6
    alive=no
    kill -0 "$engine" 2> /dev/null && alive=yes
    mkdir -p "$out/wlshot"
    (cd "$out/wlshot" && WAYLAND_DISPLAY=wl-77 timeout 20 weston-screenshooter) > "$out/shooter.log" 2>&1
    kill "$engine" 2> /dev/null
    wait_exit "$engine"
    shot=$(find "$out/wlshot" -name '*.png' | head -n 1)
    [ -n "$shot" ] && cp "$shot" "$out/wayland.png"
    kill "$weston" 2> /dev/null
    note wayland "running after 6 s: $alive | exit status after SIGTERM: $status
$(cat "$out/wayland.log")
$(weston --version 2>&1 | head -n 1)
$(grep -iE 'error|fatal|fail|unknown|unrecognized|usage' "$out/weston.log" | head -n 6)
screenshot: $(ls -l "$out/wayland.png" 2>&1) $(cat "$out/shooter.log")"
    ;;
  send)
    send "$2"
    ;;
esac
exit 0
