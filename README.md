# routerAI

Interactive multi-provider AI account router written in C++20.

routerAI manages provider accounts as isolated local profiles and is moving toward provider-specific routing groups behind one local OpenAI-compatible API.

## Current scope

- C++20 + CMake
- FTXUI full-screen terminal interface
- Arrow-key navigation with Enter/Esc/q controls
- SQLite account persistence and additive schema migration
- Provider abstraction
- One isolated `CODEX_HOME` per Codex account
- routerAI-managed Codex runtime bootstrap
- Official Codex authentication and app-server account/quota reads
- Z.ai provider scaffold with official Coding Plan and general API endpoints
- Google Antigravity provider slot reserved for a documented account/project adapter
- Persistent quota snapshot/window history for providers that expose supported quota data
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
routerAI 0.5.0

Navigation                 Preview
--------------------       ----------------------------
Dashboard                  Current provider/account health
Accounts                   Account management
Add Provider               Codex / Google Antigravity / Z.ai
Best account               Route selection preview
Doctor                     Runtime/provider checks
Exit

Up/Down navigate   Enter select   Esc/q exit
```

### Add Provider

`Add Provider` is provider-first:

```text
Add Provider
  |
  +-- Codex
  |     +-- ChatGPT account profile
  |
  +-- Google Antigravity
  |     +-- account/project adapter (in progress)
  |
  +-- Z.ai
        +-- Coding Plan/API provider account
```

Z.ai currently uses the documented endpoints:

```text
Coding Plan : https://api.z.ai/api/coding/paas/v4
General API : https://api.z.ai/api/paas/v4
```

The Z.ai account record and provider adapter are present. Secure API-key entry/validation and request execution are the next implementation step. routerAI does not scrape undocumented Z.ai quota endpoints; quota will only be enabled when a supported machine-readable path is available.

### Dashboard and Accounts

The dashboard shows provider, plan, status and identity for every local account. Codex accounts currently expose Details, Login, Refresh, Quota and Quota History. Provider-specific actions for Z.ai and Antigravity are being added independently.

### Managed Codex runtime

You do **not** need to install Codex CLI manually.

When Login/Profile/Quota needs Codex and no usable runtime exists, routerAI invokes OpenAI's official standalone installer and installs Codex below `.routerai/runtime/codex/`. If a working `codex` already exists on `PATH`, routerAI can use it as a fallback.

Each Codex account still receives its own isolated `CODEX_HOME` below `.routerai/accounts/<id>/codex-home`.

## Architecture

```text
FTXUI TerminalApp
       |
       v
AccountManager
       |
       +-- AccountSelector
       |
       +-- SQLite accounts / quota history
       |
       +-- Provider abstraction
               |
               +-- CodexProvider
               |     +-- managed Codex runtime
               |     +-- account/read
               |     +-- account/rateLimits/read
               |
               +-- ZaiProvider
               |     +-- Coding Plan endpoint
               |     +-- General API endpoint
               |
               +-- AntigravityProvider (planned adapter)
```

Future routing groups are designed around provider membership:

```text
Codex only
Antigravity only
Z.ai only
Mixed
```

A group will eventually select only enabled, healthy and authorized accounts/projects, then apply routing/failover policy independently from the TUI.

## Requirements

- CMake 3.24+
- C++20 compiler
- vcpkg
- A terminal with ANSI/interactive input support
- Internet access the first time routerAI bootstraps provider runtimes

Dependencies managed through vcpkg:

- FTXUI
- SQLite3
- spdlog
- nlohmann/json

## Build with vcpkg

### Windows + MinGW + Ninja

```cmd
cmake -S . -B build -G Ninja ^
  -DCMAKE_BUILD_TYPE=Release ^
  -DCMAKE_TOOLCHAIN_FILE=C:/dev/vcpkg/scripts/buildsystems/vcpkg.cmake ^
  -DVCPKG_TARGET_TRIPLET=x64-mingw-dynamic

cmake --build build -j 8
ctest --test-dir build --output-on-failure
build\router.exe
```

Do not mix MinGW/GCC with the `x64-windows` MSVC triplet.

### Windows + Visual Studio

```powershell
cmake -S . -B build `
  -DCMAKE_TOOLCHAIN_FILE=C:/dev/vcpkg/scripts/buildsystems/vcpkg.cmake

cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
.\build\Release\router.exe
```

## Data

Account metadata and quota history are stored in `router.db` by default. Provider runtime state lives under `.routerai/`. Both are local runtime data and should not be committed.

## Development status

1. SQLite persistence and provider abstraction - done
2. Codex account isolation + official auth/runtime - done
3. Codex account/quota retrieval - done
4. Quota history + health status - done
5. Full-screen interactive terminal UI - done
6. Self-managed Codex runtime bootstrap - done
7. Provider-first `Add Provider` flow - done
8. Z.ai provider scaffold - done
9. Z.ai secure credential + request adapter - next
10. Google Antigravity account/project adapter
11. Routing groups: Codex-only / Antigravity-only / Z.ai-only / Mixed
12. Cooldown, failure tracking and failover
13. OpenAI-compatible local API
14. Optional web/desktop management UI
