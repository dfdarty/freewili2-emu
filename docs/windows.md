# On Windows (WSL2) and in VS Code

The emulator is a Linux program. On Windows it runs in **WSL2**, Microsoft's
built-in Linux, with its window on your Windows desktop (WSLg) and sound
through your speakers. Nothing else changes: the commands are the Linux
ones.

!!! info "Not tried on every setup"
    These are the Linux steps run inside WSL2. If something differs on your
    machine, please [open an issue](https://github.com/dfdarty/freewili2-emu/issues)
    so this page can say so. With no setup at all, the
    [browser version](https://dfdarty.github.io/freewili2-emu/emulator/)
    runs WiliBSP's example apps and SquachWatch, and a [Codespace](getting-started.md#2-github-codespaces-a-full-dev-environment-in-a-browser-tab)
    gives you the full tool in a browser tab.

## 1. Install WSL2 and Ubuntu (once)

In **PowerShell as Administrator**:

```powershell
wsl --install -d Ubuntu-24.04
```

Restart when it asks, then open **Ubuntu 24.04** from the Start menu and
choose a Linux user name and password. On a PC that already has WSL, run
`wsl --update` so the window support (WSLg) is current.

## 2. Get the emulator (once)

In the Ubuntu terminal:

```sh
sudo apt update
sudo apt install -y git cmake ninja-build build-essential gdb libsdl2-dev zlib1g-dev python3
git clone --recurse-submodules https://github.com/dfdarty/freewili2-emu ~/freewili2-emu
cd ~/freewili2-emu
tools/fw2emu run third_party/wilibsp/apps/hello_display
```

A window with the FREE-WILi 2 opens on your Windows desktop. Click the
screen to touch it.

!!! tip "Keep your work in the Linux file system"
    Clone into your Linux home (`~/...`), not under `/mnt/c/...`: building on
    the Windows drive through WSL is many times slower. Windows Explorer
    reaches the Linux files at `\\wsl$\Ubuntu-24.04\home\<you>`.

## 3. Your own app

```sh
tools/fw2emu new apps/my_app          # a working app with its test
tools/fw2emu run apps/my_app          # the window
tools/fw2emu test apps/my_app         # PASS or FAIL
```

[Your first app](first-app.md) goes through it.

## VS Code

Install [VS Code](https://code.visualstudio.com/) on **Windows**, then, in
the Ubuntu terminal:

```sh
cd ~/freewili2-emu
code .
```

VS Code opens the folder through WSL and offers the recommended extensions
(C/C++, WSL, Dev Containers): accept them. The repository comes with:

- **Tasks** (*Terminal → Run Task…*, or ++ctrl+shift+b++ for the first one).
  Each works on the app whose file is open in the editor, e.g.
  `apps/my_app/main.c`:

    | Task | Does |
    |---|---|
    | FW2: run this app | build it and open the emulator window |
    | FW2: test this app (test.txt) | run its test; a failing `expect` shows in *Problems* at its line in `test.txt` |
    | FW2: record a test (writes recorded.txt) | open the window and record what you do into `recorded.txt` next to the app ([details](scripting.md#recording-a-script)) |
    | FW2: check it fits the real chip (hwcheck) | [`fw2emu hwcheck`](debugging.md#real-hardware-check-toolsfw2emu-hwcheck) |
    | FW2: new app in apps/ | `fw2emu new`, asks for the name |

- **Debugging** (*Run and Debug*): **FW2: debug this app** builds the open
  app as a Debug build and starts it under gdb with the window open, so
  breakpoints in your code and in WiliBSP work. **FW2: debug this app's
  test (headless)** runs its `test.txt` headless under the debugger.
- **IntelliSense** from the build of the app you last ran (it reads
  `build-run/compile_commands.json`), so WiliBSP's functions complete and
  jump to their definitions.

The same `.vscode` setup works on Linux, and inside the dev container
(*Dev Containers: Reopen in Container*), which has everything installed.

## If something doesn't work

| What you see | Try |
|---|---|
| `no display … running headless` and no window | WSLg isn't running: `wsl --update` in PowerShell, then `wsl --shutdown` and open Ubuntu again. Windows 10 needs build 19044 or later for WSLg. |
| The window opens but there is no sound | Check the Windows volume; audio goes through WSLg's PulseAudio. `--mute` silences the emulator. |
| Builds are very slow | The repository is under `/mnt/c`: clone it into `~` instead. |
| `code: command not found` | Install VS Code on Windows (not in Ubuntu) and restart the Ubuntu terminal. |
| Debugging says gdb is missing | `sudo apt install gdb` |
