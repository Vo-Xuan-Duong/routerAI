# routerAI

Interactive multi-provider AI account/runtime manager and localhost router written in C++20.

Current verified release-candidate version: **0.6.0**.

routerAI separates three concerns:

```text
Provider / Account Management
    -> login, credentials, provider status, models and quota

API / Runtime Routing
    -> select a supported backend for a localhost OpenAI-compatible API

Desktop Applications
    -> detect and explicitly launch supported desktop clients
```

Automatic API failover is intentionally separate from consumer desktop/session switching.

## Providers

### Codex

- isolated `CODEX_HOME` per routerAI account
- routerAI-managed Codex runtime bootstrap
- ChatGPT/Codex device-code or browser login
- account/profile metadata
- quota/rate-limit snapshots and history
- Codex `app-server` completion execution
- native app-server delta streaming
- explicit/manual consumer-account selection

Codex consumer profiles are not automatically pooled into cross-account quota rotation.

### Google Antigravity

Two separate modes are supported.

**Consumer CLI session**

- official `agy` integration
- optional official CLI installation action
- login / active-session verification
- Antigravity `/usage` quota collection
- quota normalization/history
- explicit/manual consumer-session management

**Gemini API project**

- Gemini API key validation
- Antigravity managed agent through the Gemini Interactions API
- model discovery
- native SSE translation to OpenAI-style chunks
- automatic API routing through `antigravity-api-default`
- participation in `mixed-default`

The consumer quota target is Antigravity developer/agent usage exposed by the official CLI, not Gemini Chat web quota.

### Z.ai

- General API mode
- Coding Plan mode kept separate from general-purpose routing
- secure API-key storage
- OpenAI-compatible `/chat/completions`
- native provider SSE passthrough
- static documented model catalog including GLM-5.2
- General API participation in `zai-default` and `mixed-default`

Z.ai's current documented OpenAPI specification does not expose a dedicated `/models` endpoint, so routerAI does not invent one for no-cost credential validation.

## Terminal UI

Run the application without management subcommands:

```text
routerAI
├─ Dashboard
├─ Accounts
├─ Add Provider
├─ Routing Groups
├─ Local API
├─ Best account
├─ Doctor
├─ Desktop Applications
└─ Exit
```

`Add Provider` is provider-first:

```text
Add Provider
├─ Codex
│  └─ isolated ChatGPT/Codex account
├─ Google Antigravity
│  ├─ Consumer CLI session
│  └─ Gemini API project
└─ Z.ai
   ├─ General API
   └─ Coding Plan
```

## Routing groups

Default groups are persisted in SQLite:

```text
codex-default
    -> Codex consumer profiles
    -> Manual only

antigravity-default
    -> Antigravity consumer CLI session
    -> Manual only

antigravity-api-default
    -> Antigravity Gemini API projects
    -> automatic strategies allowed

zai-default
    -> Z.ai General API credentials
    -> automatic strategies allowed

mixed-default
    -> Antigravity Gemini API projects
    -> Z.ai General API credentials
    -> automatic strategies allowed
```

Available strategies:

```text
Health First
Least Used
Priority
Round Robin
Manual
```

The TUI can also create custom groups and edit their memberships. Automatic custom groups only accept API-capable credentials; consumer profiles must use `Manual` strategy. The routing core enforces the same rule even for legacy or manually edited database state.

Temporary backend failures are tracked separately from provider quota/auth status:

```text
consecutiveFailures
cooldownUntilUnix
lastError
```

Cooldown uses bounded exponential backoff:

```text
30s -> 60s -> 120s -> 240s -> 480s -> max 15m
```

A successful backend request clears the transport failure state.

## Local OpenAI-compatible API

Default bind:

```text
http://127.0.0.1:9000/v1
```

Endpoints:

```text
GET  /health
GET  /v1/models
POST /v1/chat/completions
```

Authentication:

```text
Authorization: Bearer <routerAI-local-key>
```

The local API key is generated on first run, stored through `CredentialStore`, shown from the TUI, and can be rotated without restarting routerAI.

Select a routing group by header:

```text
X-Router-Group: zai-default
```

or request metadata:

```json
{
  "router": {
    "group": "mixed-default"
  }
}
```

or pseudo-model:

```json
{
  "model": "router/mixed-default"
}
```

`router/<group>` is a routing selector only. It is never forwarded to the provider as a real model name.

For mixed routing, provider-specific model mapping is supported:

```json
{
  "model": "router/mixed-default",
  "messages": [
    {"role": "user", "content": "hello"}
  ],
  "router": {
    "models": {
      "zai": "glm-5.2",
      "antigravity": "gemini-3.8-flash"
    }
  }
}
```

## Streaming

`stream=true` returns chunked `text/event-stream` data.

```text
Codex-only group
    -> native app-server delta streaming

Z.ai-only group
    -> native provider SSE passthrough

Antigravity API-only group
    -> native Gemini Interactions SSE translated to OpenAI chunks

Mixed cross-provider group
    -> buffered SSE fallback
```

Cross-provider groups remain buffered because failover after bytes have already been emitted to the client is unsafe. Streams use OpenAI-style framing and terminate with:

```text
data: [DONE]
```

## Quota and health

Provider quota is normalized into provider-independent snapshots containing limit buckets/windows, usage percentages, durations and reset timestamps when available.

Codex quota health is separate from network/request failure state:

```text
ordinaryUsageAllowed = false -> LIMITED
reached signal / >=90% used  -> WARNING
otherwise                    -> READY
```

## Credential storage

Provider secrets are not stored directly in `router.db`.

```text
SQLite account row
    -> credential_ref
    -> CredentialStore
```

Credential backends:

```text
Windows
    -> DPAPI-protected credential files

Linux desktop with DBus + secret-tool
    -> Secret Service / OS keyring

Linux without usable Secret Service
    -> user-only fallback file (0600)

macOS
    -> user-only fallback file (0600) currently
```

Linux Secret Service integration is optional and detected at runtime; headless environments do not acquire a hard dependency on DBus or `secret-tool`.

Local state is ignored by Git:

```text
.routerai/
*.db
*.db-wal
*.db-shm
.env
```

## Desktop Applications

routerAI can detect and explicitly launch supported Codex/ChatGPT and Antigravity desktop installations where discoverable.

Desktop account/profile switching is not automated unless the corresponding application exposes a stable supported external switching mechanism. routerAI does not copy desktop cookies, private OAuth tokens, application session databases, or OS keyring entries to simulate unsupported profile switching.

## Windows quick start

The repository includes helper scripts for the MinGW + Ninja setup used during development.

First configure:

```cmd
configure
```

Build only:

```cmd
build
```

Build and run tests:

```cmd
test
```

Build and launch routerAI:

```cmd
run
```

`configure.cmd` uses `VCPKG_ROOT` when set; otherwise it defaults to:

```text
C:\dev\vcpkg
```

The expected Windows stack is:

```text
Compiler       MinGW/GCC
Generator      Ninja
Build type     Release
vcpkg triplet  x64-mingw-dynamic
Executable     build\router.exe
```

Do not mix MinGW/GCC with the MSVC `x64-windows` triplet.

### Manual Windows configure

```cmd
cmake -S . -B build -G Ninja ^
  -DCMAKE_BUILD_TYPE=Release ^
  -DCMAKE_TOOLCHAIN_FILE=C:/dev/vcpkg/scripts/buildsystems/vcpkg.cmake ^
  -DVCPKG_TARGET_TRIPLET=x64-mingw-dynamic

cmake --build build -j 8
ctest --test-dir build --output-on-failure
build\router.exe
```

### Visual Studio

```powershell
cmake -S . -B build `
  -DCMAKE_TOOLCHAIN_FILE=C:/dev/vcpkg/scripts/buildsystems/vcpkg.cmake

cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
.\build\Release\router.exe
```

## Requirements

- CMake 3.24+
- C++20 compiler
- vcpkg
- ANSI/interactive terminal
- internet access for provider installation/login/API requests when needed

vcpkg dependencies:

- curl
- cpp-httplib
- FTXUI
- SQLite3
- spdlog
- nlohmann/json

The application version is defined by the CMake project version and a generated `Version.hpp` is available to runtime code.

## Tests

CTest currently covers:

- SQLite schema migration and quota persistence
- account selection
- routing strategies
- round-robin cursor persistence
- cooldown/failure recovery
- request-local candidate exclusion
- consumer/API routing boundaries
- custom automatic-group rejection rules
- OpenAI pseudo-model/group resolution
- provider model metadata
- HTTP streaming transport
- CredentialStore put/get/overwrite/erase contract
- local API auth/model listing/key rotation
- SSE error framing and `[DONE]`
- desktop application manager detection metadata

Run locally:

```cmd
test
```

## GitHub Actions

CI is intentionally not executed on every push.

The workflow runs only on:

```text
workflow_dispatch
pull_request
```

This keeps ordinary development commits from repeatedly consuming GitHub Actions minutes.

Release-candidate verification run **#138** passed on both Windows and Linux: Configure, Build, and CTest all succeeded.

## Architecture

```text
                         routerAI
                            |
          +-----------------+------------------+
          |                 |                  |
          v                 v                  v
     TerminalApp       Local API       Desktop Applications
          |                 |
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
       Codex             Antigravity            Z.ai
    app-server          CLI / Gemini API      General API
```

## 0.6.0 verified release-candidate status

```text
[done] SQLite persistence and additive migration
[done] Provider abstraction
[done] Managed/isolated Codex runtime
[done] Codex profile + quota integration
[done] Codex app-server completion execution
[done] Codex native streaming
[done] FTXUI control plane
[done] Provider-first Add Provider flow
[done] Windows DPAPI credential protection
[done] Optional Linux Secret Service credential backend
[done] Antigravity consumer CLI + quota adapter
[done] Antigravity Gemini API project adapter
[done] Antigravity native Interactions SSE translation
[done] Z.ai General API adapter
[done] Z.ai native SSE passthrough
[done] Z.ai GLM-5.2 metadata
[done] Routing-group persistence and custom group editor
[done] Automatic API-backend failover + cooldown
[done] Local OpenAI-compatible API
[done] Local API key rotation
[done] HTTP integration/streaming tests
[done] Desktop application detection/launch
[done] Windows configure/build/test/run helper scripts
[done] Manual/PR-only GitHub Actions workflow
[done] Windows CI verification
[done] Linux CI verification

[provider-dependent] stable desktop account/profile switching
[provider-dependent] Z.ai no-cost credential validation endpoint
[optional] native macOS Keychain backend
[optional] web/desktop management UI
```

## Design principles

Provider-specific authentication, quota and request behavior belongs inside provider adapters.

routerAI prefers documented provider interfaces and supported credential/runtime mechanisms. It does not depend on browser-session scraping, credential extraction from official clients, private endpoint reverse engineering, CAPTCHA/2FA bypass, or spoofing first-party applications.
