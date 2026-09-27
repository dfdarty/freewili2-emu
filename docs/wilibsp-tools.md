# WiliBSP's agent tools

WiliBSP ships a command-line tool, `tools/fw.py`, and an in-app harness
called **agentio** that let a person or an AI agent drive a real board over
the debug probe: press buttons, touch the screen, type, and take screenshots.
Its [`AGENTS.md`](https://github.com/freewili/wilibsp/blob/main/AGENTS.md)
workflow and Claude Code skills are built on them.

The emulator serves the same RTT channels on the same TCP ports that WiliBSP
uses with OpenOCD, so these commands work with no board attached.

## What the app needs

agentio runs inside the app, so an app only answers `fw.py` if it starts
the harness. That takes three calls, as in WiliBSP's `hello_agentio`:

```c
    fw2kb_t kb;
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

If the app never calls `agentio_init()`, the emulator turns `fw.py` away at
once instead of letting it wait, and says why:

```text
[emu] rtt: agentio client connected, but the app never called agentio_init() -- closing it (see docs/wilibsp-tools.md)
```

and `fw.py` stops with `RuntimeError: agentio connection closed`. (On the
board the same app would leave `fw.py` waiting for its 30 s timeout.)

## Using it

Start an app with `--rtt` (it works with or without a window):

```sh
build/bin/hello_agentio --rtt &
```

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
| `fw touch X Y` | yes | |
| `fw type TEXT` | yes | |
| `fw screenshot -o FILE` | yes | reads the app's own agentio framebuffer copy, exactly as on hardware |
| `fw rtt` | no | it starts OpenOCD itself; read `DIAG()` output from the emulator's terminal, or `nc 127.0.0.1 9090` |
| `fw build`, `fw flash`, `fw install-app` | — | these are for the real board |

The ports are 9090 (DIAG text) and 9091 (agentio), on 127.0.0.1 only.
`fw.py` has these numbers built in, so only one emulator at a time can run
with `--rtt`; a second one stops with "port 9090 on 127.0.0.1 is already in
use".

![fw.py screenshot of an app in the emulator](fw_screenshot.png)

## With an AI agent

Because the commands and their output are the same, an agent following
WiliBSP's `AGENTS.md` can build an app for the emulator, run it with
`--rtt`, and verify it with `fw press` and `fw screenshot` before anyone
flashes a board. For agents that prefer files over sockets, headless
[input scripts](scripting.md) with `screenshot` commands and `.expect` log
checks do the same job without a background process.
