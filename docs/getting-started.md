# Install and run

There are three ways in, from zero setup to a full local toolchain.

## 1. In the browser — nothing to install

Open the [hosted emulator](https://dfdarty.github.io/freewili2-emu/emulator/)
and pick an app, with sliders for the sensors and the sound in the room.

!!! warning "Example apps only"
    The hosted page runs WiliBSP's example apps. It can't load your own
    code: apps are compiled from source into the emulator, so yours has to
    be built first. Use option 2 or 3, then `fw2emu run` for a window or
    `fw2emu web` for the same browser page with your app in it.

## 2. GitHub Codespaces — a full dev environment in a browser tab

The repository has a dev container with everything preinstalled: CMake,
Ninja, SDL2, Python, Emscripten, the Pico SDK and Arm GCC. The Arm GCC is
Ubuntu's 13.2; `tools/fw2emu hwcheck --fetch-toolchain` fetches the Arm GNU
Toolchain 14.2.Rel1 that WiliBSP builds with, for exact numbers.

1. On the [repository page](https://github.com/dfdarty/freewili2-emu), choose
   **Code → Codespaces → Create codespace on main**.
2. When it opens, build and run an app from the terminal:

    ```sh
    tools/fw2emu run third_party/wilibsp/apps/hello_display --run-ms 5000 --shot-on-exit out/shot.png
    tools/fw2emu web third_party/wilibsp/apps/hello_display    # then open the forwarded port
    ```

A Codespace has no display, so the emulator runs headless there and says
so; take screenshots with `--shot-on-exit` or a [script](scripting.md).
`fw2emu web` gives you the interactive version on a forwarded port. The same container works locally in VS
Code with the Dev Containers extension.

## 3. Native on Linux

```sh
sudo apt install git cmake ninja-build libsdl2-dev zlib1g-dev python3
git clone --recurse-submodules https://github.com/dfdarty/freewili2-emu
cd freewili2-emu
cmake -S . -B build -G Ninja
cmake --build build
build/bin/hello_display          # opens a window
tests/smoke.sh                   # runs every app headless; screenshots and logs in out/
```

Every app in the build ends up in `build/bin/`. Run one with `--help` to see
the [emulator's options](cli.md).

- `--recurse-submodules` fetches WiliBSP and its own nested `libs/onewili`
  (the OneWili client), which the build needs. In a clone made without it,
  run `git submodule update --init --recursive`.
- `zlib1g-dev` is optional: with it, screenshots are compressed PNGs.
- **No display** (SSH, a container, WSL without WSLg): the emulator notices
  that neither `DISPLAY` nor `WAYLAND_DISPLAY` is set, prints `no display …
  running headless`, and runs without a window. Use `--run-ms`, `--script`
  or `--rtt` to work with it, or ++ctrl+c++ to stop it.

To build and run a single app folder — including one outside the repository —
use the helper:

```sh
tools/fw2emu run path/to/my_app                              # window
tools/fw2emu run path/to/my_app --headless --script test.txt # emulator flags go after the folder
```

!!! info "Other platforms"
    Native builds are developed and tested on Ubuntu 24.04, on x86-64 and on
    64-bit Arm. **A Raspberry Pi 5** with 64-bit Raspberry Pi OS is a
    complete development machine: the same `apt install` line, and
    `tools/fw2emu hwcheck --fetch-toolchain` downloads the Arm toolchain
    built for it, so the UF2 for the board is built on the Pi too. **On
    Windows**, use WSL2: [Windows (WSL2) and VS Code](windows.md) has the
    steps, and the repository's VS Code tasks and debug setup. macOS is not
    tested. The browser version works everywhere.

## The browser build yourself

The browser build needs [Emscripten](https://emscripten.org) 4.0.15. Install
it once with emsdk:

```sh
git clone https://github.com/emscripten-core/emsdk.git ~/emsdk
~/emsdk/emsdk install 4.0.15
~/emsdk/emsdk activate 4.0.15
source ~/emsdk/emsdk_env.sh      # in every new shell (or add it to ~/.bashrc)
```

Then build one app and serve it:

```sh
tools/fw2emu web path/to/my_app              # builds and serves on http://127.0.0.1:8080
```

Or build every app and serve the folder:

```sh
emcmake cmake -S . -B build-web -G Ninja
cmake --build build-web
python3 -m http.server -d build-web/bin 8080
```

**The first build downloads SDL2.** Emscripten fetches its SDL2 port
(`github.com/libsdl-org/SDL/archive/release-2.32.8.zip`) the first time and
caches it. On a network that blocks that download (the build fails with
`HTTP Error 403` in `retrieving port: sdl2`), point Emscripten at a local
SDL checkout of the same release instead:

```sh
git clone --depth 1 --branch release-2.32.8 https://github.com/libsdl-org/SDL.git ~/SDL
export EMCC_LOCAL_PORTS=sdl2=$HOME/SDL
```

Keep it exported for every later build too (put it in `~/.bashrc` next to
`emsdk_env.sh`); without it Emscripten tries the download again. The dev
container and the Dockerfile already set it.

Or use Docker, which fetches Emscripten, WiliBSP and SDL at pinned versions:

```sh
docker build -t freewili2-emu .
docker run --rm -p 127.0.0.1:8080:80 freewili2-emu     # open http://127.0.0.1:8080
```

To put your own apps on the page too, list their repositories in
`EXTRA_APPS`: Git URLs separated by spaces, each optionally ending in
`#branch` or `#tag`. Each repository needs its app's `CMakeLists.txt` at the
top, like one made with [`fw2emu new --repo`](first-app.md). Its Git
submodules come too. A [`fw2emu-web.json`](cli.md#the-apps-page-fw2emu-webjson)
next to it gives the app its start-up options, a guide and a source link on
the page.

```sh
docker build -t freewili2-emu \
  --build-arg EXTRA_APPS="https://github.com/you/my-app.git https://github.com/you/other-app.git#v2" .
```

In a Compose file that builds from the Git URL:

```yaml
services:
  fw2-emu:
    build:
      context: https://github.com/dfdarty/freewili2-emu.git#v2
      args:
        EXTRA_APPS: "https://github.com/you/my-app.git"
```

Docker caches the step that fetches them, so an ordinary rebuild keeps the
versions it already has. When an app gets new commits, rebuild without the
cache (`docker compose build --no-cache fw2-emu`), or point `EXTRA_APPS` at
a new tag, which changes the step and fetches again.

## Next

- [Write your first app](first-app.md)
- [Controls](controls.md) — keys, mouse and the browser panels
