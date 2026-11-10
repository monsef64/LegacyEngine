# LegacyEngine

Trying to make a custom game engine with AI

![A 7 × 7 grid on the ground with an orange 1 m box in the centre](docs/screenshot.png)

## What it does so far

LegacyEngine opens a window onto a small 3D world:

- a 7 × 7 grid on the ground, 1 m per grid block
- a 1 m box standing on the centre block

One unit is one metre, +Y points up and the ground is the XZ plane.

| Input | Action |
| --- | --- |
| Drag with the left mouse button | Orbit around the box |
| Mouse wheel | Zoom in and out |
| `Esc`, or closing the window | Quit |

It is written in C++17 with:

- [SDL3](https://www.libsdl.org/) for the window, the input and the OpenGL context
- OpenGL 3.3 (core profile) for the rendering
- [GLAD](https://github.com/Dav1dde/glad) for loading the OpenGL functions (generated code in `third_party/glad`)

Only Linux has been built and tested so far.

## Download

Linux x86_64 binaries are published on the
[releases page](https://github.com/monsef64/LegacyEngine/releases). For example, for version `v0.1.0`:

```sh
tar -xzf LegacyEngine-v0.1.0-linux-x86_64.tar.gz
./LegacyEngine-v0.1.0-linux-x86_64/LegacyEngine
```

The binary is built on Ubuntu 24.04. It needs a graphics driver with OpenGL 3.3 and glibc 2.38 or
newer, so it runs on Ubuntu 24.04 and Debian 13, for example, but not on Ubuntu 22.04. SDL is
linked into it, so there is nothing else to install.

## Build

You need a C++17 compiler, CMake 3.16 or newer, Git, and the development packages that SDL builds
against. On Ubuntu and Debian:

```sh
sudo apt-get install build-essential cmake git pkg-config \
  libx11-dev libxext-dev libxrandr-dev libxcursor-dev libxfixes-dev \
  libxi-dev libxss-dev libxtst-dev \
  libwayland-dev libxkbcommon-dev libdecor-0-dev \
  libgl1-mesa-dev libegl1-mesa-dev libgles2-mesa-dev libdrm-dev libgbm-dev \
  libdbus-1-dev libibus-1.0-dev libudev-dev libusb-1.0-0-dev \
  libasound2-dev libpulse-dev libpipewire-0.3-dev libjack-dev libsndio-dev \
  libfribidi-dev libthai-dev liburing-dev
```

Then:

```sh
cmake -S . -B build
cmake --build build --parallel 4
./build/LegacyEngine
```

The first command downloads SDL 3.4.20, and the second one builds it into the executable. If SDL3
is already installed on your system, that one is used instead; add `-DLEGACY_VENDORED_SDL=ON` to
the first command to download it anyway.

## Release

Pushing a tag that starts with `v` runs the [release workflow](.github/workflows/release.yml). It
builds the Linux binary, checks that it starts, and publishes it as a GitHub release:

```sh
git tag v0.1.0
git push origin v0.1.0
```

The workflow can also be started by hand from the Actions tab. On a branch, that only builds the
binary and attaches it to the workflow run.
