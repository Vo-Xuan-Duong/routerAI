# routerAI

Console-first AI account router written in C++20.

The project manages AI provider accounts as isolated local runtime profiles. The current Codex integration deliberately delegates ChatGPT authentication and token refresh to the official Codex CLI instead of storing OAuth tokens inside routerAI.

## Current scope

- C++20 + CMake
- SQLite account persistence and schema migration
- Provider abstraction
- One isolated `CODEX_HOME` per Codex account
- Official Codex CLI authentication
- CLI commands:
  - `router status`
  - `router doctor`
  - `router account list [--refresh]`
  - `router account add codex [--login] [--browser]`
  - `router account login <account-id> [--browser]`
  - `router account status <account-id>`

## Architecture

```text
router CLI
    |
    v
AccountManager
    |
    +-- SQLite account metadata
    |
    +-- Provider abstraction
            |
            +-- CodexProvider
                    |
                    +-- CodexCli
                            |
                            +-- CODEX_HOME=.routerai/accounts/<id>/codex-home
                            +-- codex login
                            +-- codex login status
```

routerAI stores only account metadata and the runtime-home path. ChatGPT OAuth credentials remain inside the Codex-managed credential store for that isolated `CODEX_HOME`.

## Requirements

- CMake 3.24+
- C++20 compiler
- vcpkg
- OpenAI Codex CLI available as `codex` on `PATH`

Run the dependency check after building:

```bash
router doctor
```

## Build with vcpkg

```bash
git clone https://github.com/Vo-Xuan-Duong/routerAI.git
cd routerAI

cmake -S . -B build \
  -DCMAKE_TOOLCHAIN_FILE=/path/to/vcpkg/scripts/buildsystems/vcpkg.cmake

cmake --build build --config Release
```

On Windows/Visual Studio, the executable is typically under `build/Release/router.exe`.

## Usage

Create an isolated Codex account profile:

```bash
router account add codex
```

Then authenticate it using the console-oriented device-code flow:

```bash
router account login codex-01
```

Or use Codex's browser callback login:

```bash
router account login codex-01 --browser
```

You can create and authenticate in one step:

```bash
router account add codex --login
```

Check the account:

```bash
router account status codex-01
```

Refresh all known account auth states before listing:

```bash
router account list --refresh
```

Each Codex account gets its own runtime directory:

```text
.routerai/
└── accounts/
    ├── codex-01/
    │   └── codex-home/
    └── codex-02/
        └── codex-home/
```

This isolation is the basis for multi-account routing later: each worker can run Codex with the matching `CODEX_HOME` without routerAI copying credentials between accounts.

## Data

Account metadata is stored in `router.db` by default. Provider runtime state is stored below `.routerai/`. Both are local runtime data and should not be committed.

## Roadmap

1. Console skeleton + SQLite persistence - done
2. Codex account isolation and official CLI authentication - current
3. Account metadata discovery, health and quota tracking
4. Codex app-server integration for requests
5. Multi-account routing, cooldown and failover
6. OpenAI-compatible local API
7. Additional providers
8. Web/desktop management UI
