#!/bin/bash
# TEMPORARY (test run of the release workflow on a real runner).
# Annotations are the only output of a run that can be read back from where the
# workflow was written, so every finding is reported as a notice.
set -u

out=$RUNNER_TEMP/evidence
mkdir -p "$out"

packages="libx11-dev libxext-dev libxrandr-dev libxcursor-dev libxfixes-dev libxi-dev libxss-dev libxtst-dev libwayland-dev libxkbcommon-dev libdecor-0-dev libgl1-mesa-dev libegl1-mesa-dev libgles2-mesa-dev libdrm-dev libgbm-dev libdbus-1-dev libibus-1.0-dev libudev-dev libusb-1.0-0-dev libasound2-dev libpulse-dev libpipewire-0.3-dev libjack-dev libsndio-dev libfribidi-dev libthai-dev liburing-dev xvfb xauth libgl1-mesa-dri"

note() { # note <title> <text>
  printf '::notice title=%s::%s\n' "$1" \
    "$(printf '%s' "$2" | head -c 6000 | sed -z 's/%/%25/g; s/\r/%0D/g; s/\n/%0A/g')"
}

send() { # send <title> <file>: the file as base64, 12000 characters per notice, 10 at most
  if [ ! -s "$2" ]; then
    note "$1" "no file"
    return
  fi
  base64 -w0 "$2" | fold -w 12000 > "$out/chunks"
  total=$(grep -c '' "$out/chunks")
  i=0
  while IFS= read -r chunk || [ -n "$chunk" ]; do
    i=$((i + 1))
    if [ "$i" -le 10 ]; then
      printf '::notice title=%s %02d of %02d::%s\n' "$1" "$i" "$total" "$chunk"
    fi
  done < "$out/chunks"
}

stop() { # stop <pid>: SIGTERM; sets $status to the exit status (137: had to be killed)
  kill "$1" 2> /dev/null
  for _ in 1 2 3 4 5 6 7 8 9 10; do
    kill -0 "$1" 2> /dev/null || break
    sleep 0.5
  done
  kill -9 "$1" 2> /dev/null
  status=0
  wait "$1" || status=$?
}

run_x11() { # run_x11 <name> [VAR=value...]: 6 s on the X display, a screenshot, then SIGTERM
  name=$1
  shift
  env DISPLAY=:77 "$@" build/LegacyEngine > "$out/$name.log" 2>&1 &
  engine=$!
  sleep 6
  alive=no
  kill -0 "$engine" 2> /dev/null && alive=yes
  cp "$out/fb/Xvfb_screen0" "$out/$name.xwd"
  stop "$engine"
  python3 ci-tmp/shot.py "$out/$name.xwd" "$out/$name.z" > "$out/$name.census" 2>&1
  note "$name" "running after 6 s: $alive | exit status after SIGTERM: $status
$(cat "$out/$name.log")
$(cat "$out/$name.census")"
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

try_image() { # try_image <image>: run the packaged binary on a clean system
  log=$out/docker-$(printf '%s' "$1" | tr ':/' '--').log
  result=0
  # shellcheck disable=SC2016  # the script is for the shell inside the container
  timeout 420 docker run --rm -v "$PWD/$PACKAGE.tar.gz:/pkg.tar.gz:ro" -e PACKAGE "$1" bash -c '
    export DEBIAN_FRONTEND=noninteractive
    apt-get update -qq > /dev/null 2>&1
    apt-get install -y -qq xvfb xauth libgl1 libegl1 libgl1-mesa-dri > /tmp/apt.log 2>&1 || { tail -n 5 /tmp/apt.log; exit 90; }
    cd /tmp && tar -xzf /pkg.tar.gz
    ldd --version | head -n 1
    status=0
    xvfb-run -a -s "-screen 0 1280x720x24" timeout -k 5 8 "./$PACKAGE/LegacyEngine" || status=$?
    echo "engine exit status: $status"
  ' > "$log" 2>&1 || result=$?
  note "docker $(printf '%s' "$1" | tr ':/' '--')" "docker exit status: $result
$(tail -n 8 "$log")"
}

case "$1" in
  run)
    note tools "$(lsb_release -ds) | $(ldd --version | head -n 1) | $(cmake --version | head -n 1) | ninja $(ninja --version) at $(command -v ninja) | $(g++ --version | head -n 1) | pkg-config $(pkg-config --version) | $(gh --version | head -n 1) | $(nproc) CPUs"

    unknown=""
    missing=""
    for p in $packages; do
      apt-cache show "$p" > /dev/null 2>&1 || unknown="$unknown $p"
      dpkg-query -W -f='${Status}' "$p" 2> /dev/null | grep -q 'ok installed' || missing="$missing $p"
    done
    note apt "unknown to apt:${unknown:- none} | not installed:${missing:- none}"

    view=0
    gh release view v0.0.0-not-a-release > "$out/gh-view.log" 2>&1 || view=$?
    note gh "release view of a missing release: exit $view ($(cat "$out/gh-view.log")) | create: $(gh release create --help | grep -oE -- '--(verify-tag|generate-notes|title)' | sort -u | tr '\n' ' ') | upload: $(gh release upload --help | grep -oE -- '--clobber' | sort -u | tr '\n' ' ')"

    note binary "$(ls -l build/LegacyEngine)
$(ldd build/LegacyEngine 2>&1)
newest glibc symbol versions: $(objdump -T build/LegacyEngine | grep -oE 'GLIBC_[0-9.]+' | sort -uV | tail -n 3 | tr '\n' ' ')"

    config=$(find build/_deps/sdl3-build -name SDL_build_config.h)
    note sdl "$(grep -hoE '^#define SDL_(VIDEO_DRIVER|VIDEO_OPENGL|VIDEO_VULKAN|AUDIO_DRIVER|INPUT|HAVE_LIBDECOR|USE_LIBDBUS|USE_IME|HAVE_LIBUDEV)[A-Z0-9_]*' "$config" | sed 's/#define SDL_//' | tr '\n' ' ')"

    note package "PACKAGE=${PACKAGE:-unset}
$(ls -l ./*.tar.gz 2>&1)
$(tar -tvzf "${PACKAGE:-none}.tar.gz" 2>&1)"

    # X11 with GLX, then X11 with EGL.
    sudo mkdir -p /tmp/.X11-unix
    sudo chmod 1777 /tmp/.X11-unix
    mkdir -p "$out/fb"
    Xvfb :77 -screen 0 1280x720x24 -fbdir "$out/fb" > "$out/xvfb.log" 2>&1 &
    xvfb=$!
    sleep 3
    run_x11 x11
    run_x11 x11-egl SDL_VIDEO_FORCE_EGL=1
    kill "$xvfb"

    # Wayland, on a headless compositor that shows the window full screen.
    sudo apt-get install -y -q weston 2>&1 | tail -n 2
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
    stop "$engine"
    shot=$(find "$out/wlshot" -name '*.png' | head -n 1)
    [ -n "$shot" ] && cp "$shot" "$out/wayland.png"
    kill "$weston" 2> /dev/null
    note wayland "running after 6 s: $alive | exit status after SIGTERM: $status
$(cat "$out/wayland.log")
$(weston --version 2>&1 | head -n 1)
$(grep -iE 'error|fatal|fail|unknown|unrecognized|usage' "$out/weston.log" | head -n 6)
screenshot: $(ls -l "$out/wayland.png" 2>&1) $(cat "$out/shooter.log")"
    ;;
  docker)
    try_image ubuntu:24.04
    try_image debian:13
    try_image ubuntu:22.04
    python3 -c "print('::notice title=probe::' + ''.join('%05d ' % i for i in range(0, 40000, 6)))"
    ;;
  send-x11)
    send x11-shot "$out/x11.z"
    ;;
  send-wayland)
    send wl-shot "$out/wayland.png"
    ;;
esac
exit 0
