# routerAI

Console-first AI account router written in C++20.

The project manages AI provider accounts as isolated local runtime profiles. The Codex integration delegates ChatGPT authentication and token refresh to the official Codex CLI, and reads account metadata plus usage/quota through the official Codex app-server protocol instead of scraping ChatGPT pages or storing OAuth tokens inside routerAI.

## Current scope

- C++20 + CMake
- SQLite account persistence and additive schema migration
- Provider abstraction
- One isolated `CODEX_HOME` per Codex account
- Official Codex CLI authentication
- Official `codex app-server --listen stdio://` JSONL transport
- Codex `account/read` identity and plan metadata retrieval
- Codex `account/rateLimits/read` quota retrieval
- Persistent quota snapshot/window history in SQLite
- Account health status derived from authoritative quota permission plus high-usage warning thresholds
- Cross-platform child-process transport for Windows and POSIX
- CLI commands:
  - `router status`
  - `router doctor`
  - `router quota [account-id]`
  - `router quota-history <account-id> [--limit N]`
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
    +-- SQLite quota snapshots/windows
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
                          +-- account/read
                          +-- account/rateLimits/read
                          |
                          +-- DuplexProcess

Each Codex account:

.routerai/accounts/<id>/codex-home
                    |
                    +-- CODEX_HOME for Codex CLI/app-server
                    +-- Codex-managed credentials and runtime state
```

routerAI stores account metadata such as email, plan, status and runtime-home path. ChatGPT OAuth credentials remain inside the Codex-managed credential store for that isolated `CODEX_HOME`.

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

Check the account and refresh its profile metadata:

```bash
router account status codex-01
```

Refresh authentication status, email and plan for every known account before listing:

```bash
router account list --refresh
```

Example list shape:

```text
ID            PROVIDER    PLAN        STATUS            PRIORITY  ACCOUNT
------------------------------------------------------------------------------------------------
codex-01      codex       plus        READY             100       account1@example.com
codex-02      codex       pro         READY             100       account2@example.com
```

Read quota for one account:

```bash
router quota codex-01
```

Read quota for every configured Codex account:

```bash
router quota
```

Every successful quota read is persisted as a snapshot plus its individual rate-limit windows. View recent history with:

```bash
router quota-history codex-01
router quota-history codex-01 --limit 100
```

Quota output is derived from Codex app-server rate-limit buckets and can contain multiple windows. routerAI intentionally does not hard-code assumptions such as exactly one 5-hour and one weekly window.

Example shape:

```text
Account      : codex-01
Provider     : codex
Email        : account1@example.com
Plan         : plus
Usage        : ALLOWED

Bucket       : codex
Plan         : plus
  primary   used 31.0% | remaining 69.0% | window 5h | reset 2026-09-16 22:15:00
  secondary used 18.0% | remaining 82.0% | window 7d | reset 2026-09-21 09:00:00
```

Quota-derived account status currently follows a conservative rule: an explicit backend `ordinaryUsageAllowed=false` marks the account `LIMITED`; when usage is explicitly allowed, a reached signal or any window at 90%+ marks it `WARNING`; otherwise it is `READY`. If the backend does not provide the permission field, routerAI preserves the existing status rather than inferring recovery from percentages.

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

Account metadata and quota history are stored in `router.db` by default. Existing databases are migrated additively when new metadata columns or tables are introduced. Provider runtime state is stored below `.routerai/`. Both are local runtime data and should not be committed.

## Development status

1. Console skeleton + SQLite persistence - done
2. Codex account isolation + official CLI authentication - done
3. Codex app-server quota retrieval - done
4. Account identity and plan metadata sync - done
5. Quota history + basic account health status - implemented
6. Persistent Codex worker pool and active health monitoring - next
7. Multi-account selection, cooldown and failover
8. OpenAI-compatible local API
9. Additional providers
10. Web/desktop management UI
