# WiliBSP's agent tools

WiliBSP ships a command-line tool, `tools/fw.py`, and an in-app harness
called **agentio** that let a person or an AI agent drive a real board over
the debug probe: press buttons, touch the screen, type, and take screenshots.
Its [`AGENTS.md`](https://github.com/freewili/wilibsp/blob/master/AGENTS.md)
workflow and Claude Code skills are built on them.

The emulator serves the same RTT channels on the same TCP ports that WiliBSP
uses with OpenOCD, so the agentio commands — `press`, `hold`, `release`,
`touch`, `type` and `screenshot` — work with no board attached. The
commands that need the board itself don't: you build and run with
`tools/fw2emu` instead of `fw build` / `fw flash`, and read `DIAG()` output
from the emulator instead of `fw rtt` (see the table below).

## What the app needs

agentio runs inside the app, so an app only answers `fw.py` if it starts
the harness. That takes three calls, as in WiliBSP's `hello_agentio`:

```c
    static fw2kb_t kb;           // outlives main's setup; agentio keeps a pointer to it
    fw2kb_init(&kb);
    agentio_init();              // before drawing anything a capture should see
    agentio_bind_keyboard(&kb);  // lets `fw type` go through the chord keyboard
    ...
    for (;;) {
        fw2_app_recovery_task();
        ...
        agentio_task();          // serves press / touch / type / screenshot
    }
```

[Your first app](first-app.md#5-optional-make-it-drivable-by-wilibsps-agent-tools)
shows them in context. The app must be built with agentio (`FW2_AGENTIO`),
which is on by default in native builds; the browser build leaves it off.

If the app hasn't called `agentio_init()` within 5 s of the first `fw.py`
connection, the emulator turns `fw.py` away instead of letting it wait out
its 30 s timeout, and says why:

```text
[emu] rtt: agentio client connected, but the app hasn't called agentio_init() -- closing it (see docs/wilibsp-tools.md)
```

`fw.py` then stops with `RuntimeError: agentio connection closed`.

## Using it

Start an app with `--rtt` (it works with or without a window) and wait
until it is serving RTT:

```sh
build/bin/hello_agentio --rtt &
tools/fw2emu wait-rtt
```

`wait-rtt` returns as soon as the ports are up (it waits up to 300 s, which
covers a first build when you start the app with `tools/fw2emu run APP
--rtt &`). A `fw.py` command that arrives while the app is still starting is
held until the app calls `agentio_init()`, for up to 5 s.

Then run `fw.py` from the repository root, so the files it writes (like
screenshots) land in your checkout rather than in the WiliBSP submodule:

```sh
python3 third_party/wilibsp/tools/fw.py press ok
python3 third_party/wilibsp/tools/fw.py touch 240 100
python3 third_party/wilibsp/tools/fw.py type "hello"
python3 third_party/wilibsp/tools/fw.py screenshot -o shot.png
```

| Command | Works | Notes |
|---|---|---|
| `fw press BUTTON` | yes | through agentio, the same path as on the board |
| `fw hold BUTTON`, `fw release BUTTON` | yes | a sustained press, e.g. HOME for 5 s |
| `fw touch X Y` | yes | |
| `fw type TEXT` | yes | |
| `fw screenshot -o FILE` | yes | reads the app's own agentio framebuffer copy, exactly as on hardware |
| `fw rtt` | no | it starts OpenOCD itself; read `DIAG()` output from the emulator's terminal, or `nc 127.0.0.1 9090` |
| `fw build`, `fw flash`, `fw install-app`, `fw run-app` | — | these are for the real board; use `tools/fw2emu run` |

The ports are 9090 (DIAG text) and 9091 (agentio), on 127.0.0.1 only.
`fw.py` has these numbers built in, so only one emulator at a time can run
with `--rtt`; a second one stops with "port 9090 on 127.0.0.1 is already in
use".

![fw.py screenshot of an app in the emulator](fw_screenshot.png)

## With an AI agent

An agent following WiliBSP's `AGENTS.md` can check an app on the emulator
before anyone flashes a board: build and start it with `tools/fw2emu run
APP --rtt &`, wait with `tools/fw2emu wait-rtt`, then drive and inspect it
with the same `fw press` / `fw touch` / `fw type` / `fw screenshot` commands
and output it would use on hardware, reading `DIAG()` from the emulator's
output instead of `fw rtt`. For agents that prefer files over sockets, headless
[input scripts](scripting.md) with `screenshot` commands and `.expect` log
checks do the same job without a background process.
