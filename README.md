# routerAI

Console-first AI account router written in C++20.

The project manages AI provider accounts as isolated local runtime profiles. The Codex integration delegates ChatGPT authentication and token refresh to the official Codex CLI, and reads usage/quota through the official Codex app-server protocol instead of scraping ChatGPT pages or storing OAuth tokens inside routerAI.

## Current scope

- C++20 + CMake
- SQLite account persistence and schema migration
- Provider abstraction
- One isolated `CODEX_HOME` per Codex account
- Official Codex CLI authentication
- Official `codex app-server --listen stdio://` JSONL transport
- Codex `account/rateLimits/read` quota retrieval
- Cross-platform child-process transport for Windows and POSIX
- CLI commands:
  - `router status`
  - `router doctor`
  - `router quota [account-id]`
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
                    |     +-- login / login status
                    |
                    +-- CodexAppServerClient
                          +-- JSONL over stdin/stdout
                          +-- account/rateLimits/read
                          |
                          +-- DuplexProcess

Each Codex account:

.routerai/accounts/<id>/codex-home
                    |
                    +-- CODEX_HOME for Codex CLI/app-server
                    +-- Codex-managed credentials and runtime state
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

Authenticate it using the console-oriented device-code flow:

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

Read quota for one account:

```bash
router quota codex-01
```

Read quota for every configured Codex account:

```bash
router quota
```

Quota output is derived from Codex app-server rate-limit buckets and can contain multiple windows. routerAI intentionally does not hard-code assumptions such as exactly one 5-hour and one weekly window.

Example shape:

```text
Account      : codex-01
Provider     : codex
Usage        : ALLOWED

Bucket       : codex
Plan         : plus
  primary   used 31.0% | remaining 69.0% | window 5h | reset 2026-09-16 22:15:00
  secondary used 18.0% | remaining 82.0% | window 7d | reset 2026-09-21 09:00:00
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

## Development status

1. Console skeleton + SQLite persistence - done
2. Codex account isolation + official CLI authentication - done
3. Codex app-server quota retrieval - implemented, CI validation in place
4. Account identity/plan metadata + quota history - next
5. Persistent Codex worker pool and health monitoring
6. Multi-account selection, cooldown and failover
7. OpenAI-compatible local API
8. Additional providers
9. Web/desktop management UI
