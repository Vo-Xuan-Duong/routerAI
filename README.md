# routerAI

Interactive terminal AI account router written in C++20.

routerAI manages provider accounts as isolated local runtime profiles. The Codex integration delegates ChatGPT authentication and token refresh to the official Codex runtime, and reads account metadata plus usage/quota through the Codex app-server protocol instead of scraping ChatGPT pages or storing OAuth tokens inside routerAI.

## Current scope

- C++20 + CMake
- FTXUI full-screen terminal interface
- Arrow-key navigation with Enter/Esc/q controls
- SQLite account persistence and additive schema migration
- Provider abstraction
- One isolated `CODEX_HOME` per Codex account
- routerAI-managed Codex runtime bootstrap
- Official Codex CLI authentication
- Official `codex app-server --listen stdio://` JSONL transport
- Codex `account/read` identity and plan metadata retrieval
- Codex `account/rateLimits/read` quota retrieval
- Persistent quota snapshot/window history in SQLite
- Account health status derived from quota signals
- Deterministic account selection using status, latest usage and priority
- Cross-platform child-process transport for Windows and POSIX
- CTest coverage for SQLite migration/history and account selection

## Terminal UI

There are no management subcommands to memorize. Start the application:

```bash
router
```

The main screen provides:

```text
routerAI 0.4.0

Navigation                 Preview
--------------------       ----------------------------
Dashboard                  Current account health
Accounts                   Account management
Add Codex account          Create isolated profile
Best account               Route selection preview
Doctor                     Dependency checks
Exit

Up/Down navigate   Enter select   Esc/q exit
```

### Dashboard

The dashboard shows total accounts, Ready/Warning/Unavailable counts, account plan/status, and account identity. Press `r` to refresh account authentication/profile state.

### Accounts

Select an account with Up/Down and Enter. The account screen then exposes:

- Details
- Login
- Refresh
- Quota
- Quota history

No account ID needs to be typed manually.

### Managed Codex runtime

You do **not** need to install Codex CLI manually.

When an operation such as Login, Profile or Quota needs Codex and no usable runtime is available, routerAI invokes OpenAI's official standalone installer and installs Codex inside the project runtime directory:

```text
.routerai/
└── runtime/
    └── codex/
        ├── bin/
        │   └── codex.exe        # Windows
        └── installer-home/
```

On Linux/macOS the executable is `.routerai/runtime/codex/bin/codex`.

If a working `codex` already exists on `PATH`, routerAI can use it as a fallback. A routerAI-managed runtime is preferred when present.

The runtime installer storage is separate from account credentials. Each account still gets an isolated `CODEX_HOME`:

```text
.routerai/
├── runtime/
│   └── codex/                   # shared executable/runtime
└── accounts/
    ├── codex-01/
    │   └── codex-home/          # account 1 auth/state
    └── codex-02/
        └── codex-home/          # account 2 auth/state
```

### Quota

Quota is rendered as terminal progress bars. Each rate-limit window shows:

- used percentage
- window duration
- reset time
- backend usage permission

Every successful quota read is also persisted in SQLite for history and routing decisions.

## Architecture

```text
FTXUI TerminalApp
       |
       v
AccountManager
       |
       +-- AccountSelector
       |     +-- READY before WARNING
       |     +-- lower known usage first
       |     +-- higher priority as tie-break
       |
       +-- SQLite account metadata
       +-- SQLite quota snapshots/windows
       |
       +-- Provider abstraction
               |
               +-- CodexProvider
                       |
                       +-- CodexCli
                       |     +-- managed runtime bootstrap
                       |     +-- login / login status
                       |
                       +-- CodexAppServerClient
                             +-- JSONL over stdin/stdout
                             +-- account/read
                             +-- account/rateLimits/read
                             |
                             +-- DuplexProcess
```

routerAI stores account metadata and runtime-home paths. ChatGPT OAuth credentials remain inside the Codex-managed credential store for the matching account `CODEX_HOME`.

## Requirements

- CMake 3.24+
- C++20 compiler
- vcpkg
- Internet access the first time routerAI bootstraps the Codex runtime
- A terminal with ANSI/interactive input support (Windows Terminal, PowerShell, modern Linux/macOS terminals)

You do not need npm, Homebrew, or a globally installed Codex CLI.

Dependencies managed through vcpkg:

- FTXUI
- SQLite3
- spdlog
- nlohmann/json

## Build with vcpkg

```bash
git clone https://github.com/Vo-Xuan-Duong/routerAI.git
cd routerAI

cmake -S . -B build \
  -DCMAKE_TOOLCHAIN_FILE=/path/to/vcpkg/scripts/buildsystems/vcpkg.cmake

cmake --build build --config Release
ctest --test-dir build --output-on-failure
```

On Windows/Visual Studio:

```powershell
cmake -S . -B build `
  -DCMAKE_TOOLCHAIN_FILE=C:/path/to/vcpkg/scripts/buildsystems/vcpkg.cmake

cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
.\build\Release\router.exe
```

On Windows with MinGW + Ninja:

```cmd
cmake -S . -B build -G Ninja ^
  -DCMAKE_BUILD_TYPE=Release ^
  -DCMAKE_TOOLCHAIN_FILE=C:/dev/vcpkg/scripts/buildsystems/vcpkg.cmake ^
  -DVCPKG_TARGET_TRIPLET=x64-mingw-dynamic

cmake --build build -j 8
build\router.exe
```

## Account lifecycle

Create an account from the TUI:

```text
Add Codex account
    |
    +-- Authenticate with device code
    +-- Authenticate in browser
    +-- Do this later
```

On the first authentication, routerAI automatically bootstraps its Codex runtime if necessary. After authentication, routerAI synchronizes account identity and plan metadata through `account/read`.

Quota-derived account status follows a conservative rule:

- explicit `ordinaryUsageAllowed=false` -> `LIMITED`
- usage allowed + reached signal or >=90% window usage -> `WARNING`
- usage allowed below warning threshold -> `READY`
- missing permission signal -> preserve existing status

## Data

Account metadata and quota history are stored in `router.db` by default. Existing databases are migrated additively. Provider runtime state lives under `.routerai/`. Both are local runtime data and should not be committed.

## Development status

1. SQLite persistence and provider abstraction - done
2. Codex account isolation + official authentication - done
3. Codex app-server account/quota retrieval - done
4. Account identity/plan metadata sync - done
5. Quota history + health status - done
6. Deterministic multi-account selection - done
7. Full-screen interactive terminal UI - done
8. Self-managed Codex runtime bootstrap - implemented
9. Cooldown, failure tracking and failover - next
10. Persistent Codex worker pool / request execution
11. OpenAI-compatible local API
12. Additional providers
13. Optional web/desktop management UI
