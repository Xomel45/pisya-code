# Pisya Code

A local AI coding assistant written in C++20. Connects to any OpenAI-compatible model running on your local network (Ollama, LM Studio, etc.) and edits your files directly — like Claude Code, but fully offline.

```
  ____  _                    ____          _
 |  _ \(_)___ _   _  __ _   / ___|___   __| | ___
 | |_) | / __| | | |/ _` | | |   / _ \ / _` |/ _ \
 |  __/| \__ \ |_| | (_| | | |__| (_) | (_| |  __|
 |_|   |_|___/\__, |\__,_|  \____\___/ \__,_|\___|
              |___/
```

## Features

- **Agentic loop** — the model reads, writes, and edits files autonomously until the task is done
- **File tools** — `read_file`, `write_file`, `edit_file`, `list_dir`, `glob_files`
- **Shell** — `bash` tool with per-command confirmation and session-level allowlist for dangerous ops
- **Interactive Q&A** — `ask_user` tool lets the model ask you questions mid-task
- **Session persistence** — auto-saves every conversation; resume with `pisya --resume`
- **Diff display** — shows `git`-style diffs for every file edit
- **Spinner + timing** — shows thinking time and token usage per response
- **Multilingual UI** — English, Русский, Deutsch (`/language` to switch)
- **Feedback timer** — asks how the model is doing after 6 hours, then every 48 hours

## Requirements

- GCC 13+ or Clang 16+ (C++20)
- CMake 3.20+
- A running OpenAI-compatible AI server ([Ollama](https://ollama.com), LM Studio, etc.)

## Build

```bash
git clone https://github.com/Xomel45/pisya-code
cd pisya-code
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
```

The binary is `build/pisya`. Optionally install it:

```bash
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

## Usage

```bash
pisya                        # start new session
pisya --resume               # pick a session to resume (arrow-key menu)
pisya --resume 2026-03-26_14-32-00  # resume specific session by ID
```

### Commands

| Command | Description |
|---------|-------------|
| `/clear` | Clear conversation history |
| `/config` | Show current config |
| `/session` | Show current session ID |
| `/language` | Switch UI language (En / Ru / De) |
| `/exit` | Quit |

## Project structure

```
src/
  main.cpp       — entry point, REPL, banner, session management
  agent.cpp/h    — agentic loop, spinner, diff renderer
  ai_client.cpp/h — HTTP client for OpenAI-compatible API
  tools.cpp/h    — file and shell tools
  ui.cpp/h       — interactive input widget, arrow-key menus
  config.cpp/h   — config loading/saving
  session.cpp/h  — session persistence (~/.pisya/sessions/)
  lang.cpp/h     — UI string tables (En/Ru/De)
third_party/
  json.hpp       — nlohmann/json v3.11.3
  httplib.h      — cpp-httplib v0.18.5
```

## Tested models

Works well with any model that supports the OpenAI tool-calling API:

- `qwen2.5-coder:14b` (recommended via Ollama)
- `qwen2.5-coder:7b`
- `deepseek-coder-v2`

## License

MIT
