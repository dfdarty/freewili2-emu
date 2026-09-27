# syntax=docker/dockerfile:1
#
# Builds the browser version of the emulator and serves it with nginx.
#   docker build -t freewili2-emu .
#   docker run --rm -p 127.0.0.1:8080:80 freewili2-emu   # then open http://127.0.0.1:8080
#
# Builds from a checkout with or without submodules, or straight from the Git
# URL: WiliBSP (with its nested libs/onewili) and SDL are fetched at pinned
# versions if they are not already present.

ARG EMSDK_VERSION=4.0.15

FROM emscripten/emsdk:${EMSDK_VERSION} AS build
ARG SDL_TAG=release-2.32.8
ARG WILIBSP_REPO=https://github.com/freewili/wilibsp.git
ARG WILIBSP_REF=45ad1c219a4e225a51bfb406bbed20460e882f81

# SDL2 source for Emscripten's SDL2 port (same version the port expects).
RUN git clone --quiet --depth 1 --branch "${SDL_TAG}" https://github.com/libsdl-org/SDL.git /opt/SDL
ENV EMCC_LOCAL_PORTS=sdl2=/opt/SDL

WORKDIR /src
COPY . .
# WiliBSP, and WiliBSP's own nested libs/onewili, when the checkout has no
# submodules (e.g. `docker build` straight from the Git URL). WILIBSP_REF is
# the commit this repository pins (checked in CI); the onewili commit and URL
# are read from WiliBSP at that commit, so both match what the submodules record.
RUN set -eu; \
    if [ ! -f third_party/wilibsp/bsp/fw2.h ]; then \
        rm -rf third_party/wilibsp; \
        git init --quiet third_party/wilibsp; \
        git -C third_party/wilibsp fetch --quiet --depth 1 "${WILIBSP_REPO}" "${WILIBSP_REF}"; \
        git -C third_party/wilibsp -c advice.detachedHead=false checkout --quiet FETCH_HEAD; \
    fi; \
    if [ ! -f third_party/wilibsp/libs/onewili/CMakeLists.txt ]; then \
        git init --quiet --bare /tmp/wilibsp.git; \
        git -C /tmp/wilibsp.git fetch --quiet --depth 1 "${WILIBSP_REPO}" "${WILIBSP_REF}"; \
        ONEWILI_REF=$(git -C /tmp/wilibsp.git rev-parse "FETCH_HEAD:libs/onewili"); \
        ONEWILI_URL=$(git -C /tmp/wilibsp.git config --blob FETCH_HEAD:.gitmodules --get submodule.libs/onewili.url); \
        echo "libs/onewili: ${ONEWILI_URL} at ${ONEWILI_REF}"; \
        rm -rf third_party/wilibsp/libs/onewili /tmp/wilibsp.git; \
        git init --quiet third_party/wilibsp/libs/onewili; \
        git -C third_party/wilibsp/libs/onewili fetch --quiet --depth 1 "${ONEWILI_URL}" "${ONEWILI_REF}"; \
        git -C third_party/wilibsp/libs/onewili -c advice.detachedHead=false checkout --quiet FETCH_HEAD; \
    fi
RUN emcmake cmake -S . -B build-web -DCMAKE_BUILD_TYPE=Release && \
    cmake --build build-web --parallel

FROM nginx:1.27-alpine
COPY docker/nginx.conf /etc/nginx/conf.d/default.conf
COPY --from=build /src/build-web/bin/ /usr/share/nginx/html/
EXPOSE 80
