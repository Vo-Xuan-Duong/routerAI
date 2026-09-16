# routerAI

Interactive multi-provider AI runtime/account manager and localhost router written in C++20.

routerAI separates three concerns that should not be mixed together:

```text
Account / Provider Management
    -> login, credentials, provider status and quota

API / Runtime Routing
    -> select a supported backend for a localhost API request

Desktop Profile Management
    -> explicit user-controlled desktop profile activation
```

Current development version: **0.6.0**.

## Current providers

```text
routerAI
|
+-- Codex
|    +-- multiple isolated CODEX_HOME profiles
|    +-- routerAI-managed Codex runtime
|    +-- ChatGPT/Codex login
|    +-- account metadata
|    +-- quota/rate-limit history
|    +-- Codex app-server request execution
|
+-- Google Antigravity
|    +-- official `agy` CLI integration
|    +-- active consumer session management
|    +-- Antigravity developer/agent quota via `/usage`
|    +-- optional official CLI installer action
|
+-- Z.ai
     +-- General API credentials
     +-- Coding Plan credentials kept as a separate mode
     +-- OpenAI-compatible General API request execution
```

The Antigravity quota target is Antigravity developer/agent usage. It is **not Gemini Chat web quota**.

## Terminal UI

Start the application without management subcommands:

```bash
router
```

Current main navigation:

```text
routerAI 0.6.0

Dashboard
Accounts
Add Provider
Routing Groups
Local API
Best account
Doctor
Exit
```

`Add Provider` is provider-first:

```text
Add Provider
  |
  +-- Codex
  |     +-- create isolated account
  |     +-- device-code or browser login
  |
  +-- Google Antigravity
  |     +-- install official CLI when missing
  |     +-- login / verify active Google session
  |
  +-- Z.ai
        +-- General API
        +-- Coding Plan
        +-- secure API-key entry
```

## Codex

Each routerAI Codex account has an isolated `CODEX_HOME`:

```text
.routerai/
+-- runtime/
|   +-- codex/
|       +-- bin/
|       +-- installer-home/
|
+-- accounts/
    +-- codex-01/
    |   +-- codex-home/
    +-- codex-02/
        +-- codex-home/
```

The Codex executable/runtime may be shared, while authentication and account state stay isolated.

routerAI can bootstrap a managed Codex runtime when no compatible `codex` executable exists on `PATH`.

Current Codex integration includes:

- official Codex login/runtime flow
- `account/read`
- `account/rateLimits/read`
- quota snapshot/history persistence
- `thread/start`
- `turn/start`
- `item/agentMessage/delta`
- `turn/completed`

Codex consumer profiles are kept in an explicit/manual routing group rather than automatically pooled into mixed provider failover.

## Google Antigravity

The current Antigravity integration uses the official `agy` runtime rather than browser-session extraction.

Implemented:

- detect `agy`
- optionally launch the official installer from the TUI
- login / verify the active system-keyring session
- read Antigravity `/usage`
- normalize quota percentages into routerAI quota snapshots
- persist quota history in SQLite

The current consumer CLI model represents one active Antigravity system-keyring session per OS user. routerAI does not copy private OAuth tokens or rewrite keyring entries to simulate unsupported parallel consumer profiles.

Future API/project-backed Antigravity support can participate in mixed routing when a documented provider interface is available for that credential type.

## Z.ai

Supported endpoint modes:

```text
General API : https://api.z.ai/api/paas/v4
Coding Plan : https://api.z.ai/api/coding/paas/v4
```

Current implementation:

- provider-first account creation
- password-style API-key input in the terminal
- credential reference stored in SQLite
- credential secret stored outside SQLite
- General API `/chat/completions` execution
- Z.ai-only routing groups
- participation of General API credentials in `mixed-default`

Coding Plan credentials remain separate from the general-purpose mixed API pool because the Coding Plan endpoint is for coding scenarios rather than interchangeable general API traffic.

routerAI does not currently claim machine-readable Z.ai Coding Plan quota support because no documented quota endpoint has been integrated.

## Credential storage

Provider secrets are not written into `router.db`.

```text
SQLite account row
    -> credential_ref
    -> CredentialStore
```

On Windows, `CredentialStore` protects credential files with Windows DPAPI.

On POSIX systems, the current fallback stores the credential in a user-only file (`0600`) below `.routerai/credentials`; a native keyring backend is still planned.

## Quota and health

Quota is normalized into provider-independent snapshots:

```text
QuotaSnapshot
  +-- provider/account
  +-- usage allowed
  +-- limit buckets
       +-- model/limit name
       +-- windows
            +-- used percent
            +-- duration when available
            +-- reset time when available
```

Codex health derivation remains separate from request transport failures:

```text
ordinaryUsageAllowed = false
    -> LIMITED

usage allowed + reached signal
    -> WARNING

usage allowed + >= 90% used
    -> WARNING

usage allowed below warning threshold
    -> READY
```

Temporary request/network failures are tracked separately as:

```text
consecutiveFailures
cooldownUntilUnix
lastError
```

They do not overwrite quota/auth health status.

## Routing groups

Default groups are persisted in SQLite:

```text
codex-default
antigravity-default
zai-default
mixed-default
```

Available strategies in the routing core:

```text
Health First
Least Used
Priority
Round Robin
Manual
```

Failure cooldown uses bounded exponential backoff:

```text
30s -> 60s -> 120s -> 240s -> 480s -> max 15m
```

A successful request clears transport failure/cooldown state.

### Routing boundary

Automatic failover is intended for provider-supported API/project credentials.

Current default behavior:

```text
codex-default
    -> manual account selection

antigravity-default
    -> manual active consumer session

zai-default
    -> API routing strategies available

mixed-default
    -> Z.ai General API
    -> future supported Antigravity API/project credentials
    -> does not automatically pool Codex or Antigravity consumer subscriptions
```

This keeps desktop/consumer profile switching separate from API backend failover.

## Local OpenAI-compatible API

routerAI starts a localhost-only API by default:

```text
http://127.0.0.1:9000/v1
```

A local key is generated on first run and stored through `CredentialStore`.

Implemented endpoints:

```text
GET  /health
GET  /v1/models
POST /v1/chat/completions
```

Authentication:

```text
Authorization: Bearer <routerAI-local-key>
```

Choose a routing group with one of the following:

```text
X-Router-Group: zai-default
```

or:

```json
{
  "router": {
    "group": "mixed-default"
  }
}
```

or use a routing-group model ID:

```json
{
  "model": "router/mixed-default"
}
```

For mixed groups, provider-specific model mapping can be supplied in the request:

```json
{
  "model": "router/mixed-default",
  "messages": [
    {"role": "user", "content": "hello"}
  ],
  "router": {
    "models": {
      "zai": "glm-5.3",
      "codex": "gpt-5.3-codex"
    }
  }
}
```

### Streaming

`stream=true` is accepted.

The current 0.6 implementation uses **buffered SSE compatibility**: routerAI waits for the selected backend to complete, then emits OpenAI-style `chat.completion.chunk` events followed by `[DONE]`.

True token-by-token streaming remains a follow-up improvement.

## Desktop Profile Manager

Desktop profile management is intentionally separate from routing.

Target UX:

```text
Desktop Profiles

Codex Desktop
  +-- Personal   ACTIVE
  +-- Work

Antigravity
  +-- Google A   ACTIVE
  +-- Google B
```

The intended action is explicit user-controlled activation, for example:

```text
Desktop Profiles
  -> Codex Desktop
  -> Work
  -> Activate
```

routerAI does not currently automate quota-triggered consumer desktop account cycling. Where desktop applications do not expose a stable account-switch interface, the project will prefer explicit login/profile isolation rather than copying cookies, private tokens or application session databases.

## Architecture

```text
                         routerAI
                            |
          +-----------------+-----------------+
          |                 |                 |
          v                 v                 v
     TerminalApp       Local API       Desktop Profiles
          |                 |              (planned)
          +--------+--------+
                   |
                   v
             AccountManager
                   |
          +--------+---------+
          |                  |
          v                  v
     Quota/Health      RoutingManager
                             |
                       CompletionRouter
                             |
          +------------------+------------------+
          |                  |                  |
          v                  v                  v
    CodexProvider    AntigravityProvider    ZaiProvider
          |                  |                  |
    app-server          official agy        General API
```

## Data layout

```text
routerAI/
+-- router.db
+-- .routerai/
    +-- credentials/
    +-- runtime/
    |   +-- codex/
    +-- accounts/
        +-- codex-01/
        |   +-- codex-home/
        +-- codex-02/
            +-- codex-home/
```

Do not commit local runtime data or credential files to source control.

## Requirements

- CMake 3.24+
- C++20 compiler
- vcpkg
- ANSI/interactive terminal
- internet access when provider install/login/API operations require it

vcpkg dependencies:

- FTXUI
- SQLite3
- spdlog
- nlohmann/json
- libcurl
- cpp-httplib

## Build

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

Do not mix MinGW/GCC with the `x64-windows` MSVC vcpkg triplet.

After the first configure, normal development usually only needs:

```cmd
cmake --build build -j 8
build\router.exe
```

### Windows + Visual Studio

```powershell
cmake -S . -B build `
  -DCMAKE_TOOLCHAIN_FILE=C:/dev/vcpkg/scripts/buildsystems/vcpkg.cmake

cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
.\build\Release\router.exe
```

## Tests

CTest currently covers:

- SQLite schema migration and quota persistence
- account selection
- routing strategies
- round-robin cursor persistence
- cooldown/failure recovery
- default routing-group boundaries

## GitHub Actions

CI is intentionally **not executed on every push**.

The workflow currently runs on:

```text
workflow_dispatch
pull_request
```

This prevents normal iterative commits from repeatedly consuming Actions minutes. Run the workflow manually when a development batch is ready for Windows/Linux verification, or let it run for a pull request.

## Development status

```text
[done] SQLite persistence and additive migration
[done] Provider abstraction
[done] Codex account isolation
[done] Managed Codex runtime bootstrap
[done] Codex profile/quota integration
[done] Codex app-server request execution
[done] Full-screen FTXUI control plane
[done] Provider-first Add Provider flow
[done] Windows DPAPI credential protection
[done] Z.ai General API adapter
[done] Antigravity active-session + quota adapter
[done] Routing-group persistence
[done] Health/priority/least-used/round-robin/manual strategies
[done] Failure cooldown state
[done] API-credential backend failover
[done] Local OpenAI-compatible chat/completions API
[done] Buffered SSE compatibility
[done] Manual/PR-only GitHub Actions workflow

[next] Native POSIX keyring credential backend
[next] Z.ai model discovery / credential validation
[next] Antigravity API/project credential adapter when supported
[next] True provider streaming
[next] Local API integration tests
[next] Custom routing-group membership editor
[next] Desktop Profile Manager
[next] Supported Codex Desktop profile activation
[next] Supported Antigravity desktop profile activation
[next] Optional web/desktop management UI
```

## Design principles

Provider-specific authentication, quota and request behavior belongs inside provider adapters.

routerAI prefers documented provider interfaces and supported credential/runtime mechanisms. It should not depend on browser-session scraping, credential extraction from official clients, private endpoint reverse engineering, CAPTCHA/2FA bypass, or spoofing first-party applications.
