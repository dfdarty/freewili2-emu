# Development container for freewili2-emu (GitHub Codespaces / VS Code Dev Containers).
# Everything needed for `tools/fw2emu run`, `tools/fw2emu web`, `tools/fw2emu hwcheck`,
# tests/smoke.sh and the sanitizer / 32-bit builds.
FROM mcr.microsoft.com/devcontainers/base:ubuntu-24.04

ARG EMSDK_VERSION=4.0.15
ARG SDL_TAG=release-2.32.8
ARG PICO_SDK_VERSION=2.3.0

RUN apt-get update \
 && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
      build-essential cmake ninja-build gdb gcc-multilib python3 git ca-certificates xz-utils \
      libsdl2-dev \
      gcc-arm-none-eabi libnewlib-arm-none-eabi libstdc++-arm-none-eabi-newlib \
 && rm -rf /var/lib/apt/lists/*

# Emscripten, for the browser build. The SDL2 port is built from a pinned local
# checkout (as in the top-level Dockerfile) rather than downloaded on first use.
RUN git clone --quiet --depth 1 https://github.com/emscripten-core/emsdk.git /opt/emsdk \
 && /opt/emsdk/emsdk install "${EMSDK_VERSION}" \
 && /opt/emsdk/emsdk activate "${EMSDK_VERSION}" \
 && git clone --quiet --depth 1 --branch "${SDL_TAG}" https://github.com/libsdl-org/SDL.git /opt/SDL \
 && chown -R vscode:vscode /opt/emsdk
ENV EMSDK=/opt/emsdk \
    EM_CONFIG=/opt/emsdk/.emscripten \
    EMCC_LOCAL_PORTS=sdl2=/opt/SDL \
    PATH=/opt/emsdk:/opt/emsdk/upstream/emscripten:${PATH}

# Pico SDK at the version WiliBSP pins, for `tools/fw2emu hwcheck`.
RUN git clone --quiet --depth 1 -b "${PICO_SDK_VERSION}" https://github.com/raspberrypi/pico-sdk.git /opt/pico-sdk \
 && git -C /opt/pico-sdk submodule update --init --depth 1 --quiet
ENV PICO_SDK_PATH=/opt/pico-sdk
