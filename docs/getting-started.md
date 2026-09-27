# Install and run

There are three ways in, from zero setup to a full local toolchain.

## 1. In the browser — nothing to install

Open the [hosted emulator](https://dfdarty.github.io/freewili2-emu/emulator/)
and pick an app. It runs the WiliBSP example apps, with sliders for the
sensors and the sound in the room. To run **your own** app in a browser, use
option 2 or 3 and `fw2emu web`.

## 2. GitHub Codespaces — a full dev environment in a browser tab

The repository has a dev container with everything preinstalled: CMake,
Ninja, SDL2, Python, Arm GCC and Emscripten.

1. On the [repository page](https://github.com/dfdarty/freewili2-emu), choose
   **Code → Codespaces → Create codespace on main**.
2. When it opens, build and run an app from the terminal:

    ```sh
    tools/fw2emu run third_party/wilibsp/apps/hello_display --run-ms 5000 --shot-on-exit shot.png
    tools/fw2emu web third_party/wilibsp/apps/hello_display    # then open the forwarded port
    ```

A Codespace has no display, so `fw2emu run` runs headless there (take
screenshots with `--shot-on-exit` or a [script](scripting.md)), and `fw2emu
web` gives you the interactive version on a forwarded port. The same container works locally in VS
Code with the Dev Containers extension.

## 3. Native on Linux

```sh
sudo apt install git cmake ninja-build libsdl2-dev python3
git clone --recurse-submodules https://github.com/dfdarty/freewili2-emu
cd freewili2-emu
cmake -S . -B build -G Ninja
cmake --build build
build/bin/hello_display          # opens a window
tests/smoke.sh                   # runs every app headless; screenshots and logs in out/
```

Every app in the build ends up in `build/bin/`. Run one with `--help` to see
the [emulator's options](cli.md).

To build and run a single app folder — including one outside the repository —
use the helper:

```sh
tools/fw2emu run path/to/my_app                              # window
tools/fw2emu run path/to/my_app --headless --script test.txt # emulator flags go after the folder
```

!!! info "Other platforms"
    Native builds are developed and tested on Ubuntu 24.04. macOS and
    Windows are not tested; on Windows, WSL2 or a Codespace is the
    straightforward route. The browser version works everywhere.

## The browser build yourself

With [Emscripten](https://emscripten.org) 4.0.15 installed:

```sh
tools/fw2emu web path/to/my_app              # builds and serves on http://127.0.0.1:8080
```

Or build every app and serve the folder:

```sh
emcmake cmake -S . -B build-web -G Ninja
cmake --build build-web
python3 -m http.server -d build-web/bin 8080
```

Or with Docker, which fetches Emscripten, WiliBSP and SDL at pinned versions:

```sh
docker build -t freewili2-emu .
docker run --rm -p 127.0.0.1:8080:80 freewili2-emu     # open http://127.0.0.1:8080
```

## Next

- [Write your first app](first-app.md)
- [Controls](controls.md) — keys, mouse and the browser panels
