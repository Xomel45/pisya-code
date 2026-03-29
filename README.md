# Pisya Code  `v0.1.1`

A local AI coding assistant written in C++20. Connects to any OpenAI-compatible model running on your local network (Ollama, LM Studio, etc.) and edits your files directly — like Claude Code, but fully offline.

```
  ____  _                    ____          _
 |  _ \(_)___ _   _  __ _   / ___|___   __| | ___
 | |_) | / __| | | |/ _` | | |   / _ \ / _` |/ _ \
 |  __/| \__ \ |_| | (_| | | |__| (_) | (_| |  __|
 |_|   |_|___/\__, |\__,_|  \____\___/ \__,_|\___|
              |___/                          v0.1.1
```

## Features

- **Agentic loop** — the model reads, writes, and edits files autonomously until the task is done
- **File tools** — `read_file`, `create_file`, `write_file`, `edit_file`, `list_dir`, `glob_files`
- **Shell** — `bash` tool with per-command confirmation and session-level allowlist for dangerous ops
- **Interactive Q&A** — `ask_user` tool lets the model ask you questions mid-task
- **Session persistence** — auto-saves every conversation; resume with `pisya --resume`
- **Diff display** — shows `git`-style diffs for every file edit
- **Spinner + timing** — shows thinking time and token usage per response
- **Multilingual UI** — English, Русский, Deutsch (`/language` to switch)
- **Feedback timer** — asks how the model is doing after 6 hours, then every 48 hours
- **Permissions** — global `~/.pisya/permissions.json` lets you extend allowed/denied commands across all projects

## Security

Pisya Code has a two-layer security model:

**Constitution (hardcoded, cannot be overridden):**
- Refuses to run as `root` or via `sudo`
- Permanently blocks: `sudo`/`su`, `reboot`/`shutdown`/`poweroff`, `pkill`/`killall`, `dd`/`mkfs`/`fdisk`/`parted`
- Blocks access to `/root`, `/etc`, `/usr`, `/bin`, `/sys`, `/proc`, `/dev`, and other system directories
- `wget`/`curl` — always ask per request, no "allow for session"
- `rm -f`/`rm -rf` — always ask per request, no "allow for session"
- Any path outside the project directory requires explicit one-time confirmation

**Federal Law (`~/.pisya/permissions.json`, user-editable):**
- `allowed` — commands that run without confirmation (e.g. `git`, `make`)
- `denied` — commands that are always blocked
- Constitution rules always win regardless of what's in this file

## Requirements

- GCC 13+ or Clang 16+ (C++20)
- CMake 3.20+
- A running OpenAI-compatible AI server ([Ollama](https://ollama.com), LM Studio, etc.)
- Linux or macOS (see Windows note below)

## Build & Install

```bash
git clone https://github.com/Xomel45/pisya-code
cd pisya-code
./install.sh
```

The script builds the project and installs the binary to `/usr/local/bin/pisya` so you can run `pisya` from any directory. It will ask for your password only for the install step.

Or manually:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
sudo cp build/pisya /usr/local/bin/
```

## Configuration

On first run, a default config is created at `~/.pisya/config`:

```
host  = 127.0.0.1
port  = 11434
model = qwen2.5-coder:14b
lang  = en
```

Edit it to point to your AI server. `system_prompt` can also be overridden here.

A permissions file is also auto-created at `~/.pisya/permissions.json` — see [Security](#security).

## Usage

```bash
pisya                                      # start new session
pisya --resume                             # pick a session to resume (arrow-key menu)
pisya --resume 2026-03-26_14-32-00         # resume specific session by ID
```

### Commands

| Command | Description |
|---------|-------------|
| `/clear` | Clear conversation history |
| `/config` | Show current config |
| `/session` | Show current session ID |
| `/language` | Switch UI language (En / Ru / De) |
| `/exit` | Quit |

## Windows

Pisya Code doesn't run natively on Windows, but it works great via **WSL (Windows Subsystem for Linux)**.

**One-time setup:**

1. Open PowerShell as Administrator and install WSL:
   ```powershell
   wsl --install
   ```
   Reboot when prompted. This installs Ubuntu by default.

2. Open the Ubuntu terminal and install dependencies:
   ```bash
   sudo apt update && sudo apt install -y git cmake g++
   ```

3. Clone and build as usual:
   ```bash
   git clone https://github.com/Xomel45/pisya-code
   cd pisya-code
   cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
   cmake --build build -j$(nproc)
   sudo cp build/pisya /usr/local/bin/
   ```

4. Your Windows files are at `/mnt/c/Users/YourName/` inside WSL. Navigate there and run `pisya`.

> **Ollama on Windows:** if you're running Ollama on Windows (not inside WSL), change the host in `~/.pisya/config` to point at the Windows host IP. From WSL you can find it with:
> ```bash
> cat /etc/resolv.conf | grep nameserver | awk '{print $2}'
> ```
> Then set `host = <that IP>` in `~/.pisya/config`.

## Project structure

```
src/
  main.cpp        — entry point, REPL, banner, root guard, session management
  agent.cpp/h     — agentic loop, spinner, diff renderer
  ai_client.cpp/h — HTTP client for OpenAI-compatible API
  tools.cpp/h     — file and shell tools, permissions loader
  ui.cpp/h        — interactive input widget, arrow-key menus
  config.cpp/h    — config loading/saving
  session.cpp/h   — session persistence (~/.pisya/sessions/)
  lang.cpp/h      — UI string tables (En/Ru/De)
third_party/
  json.hpp        — nlohmann/json v3.11.3
  httplib.h       — cpp-httplib v0.18.5
```

## Tested models

Works well with any model that supports the OpenAI tool-calling API:

- `qwen2.5-coder:14b` (recommended via Ollama)
- `qwen2.5-coder:7b`
- `deepseek-coder-v2`

## License

[Mozilla Public License 2.0](LICENSE)
