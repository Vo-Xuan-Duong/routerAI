# routerAI

Interactive multi-provider AI account/runtime manager and localhost router written in C++20.

Current development version: **0.6.0**.

routerAI deliberately separates three different concerns:

```text
Provider / Account Management
    -> login, credentials, provider status and quota

API / Runtime Routing
    -> select a supported backend for a localhost API request

Desktop Profile Management
    -> explicit user-controlled desktop profile activation
       (future layer; not mixed with automatic API failover)
```

## Current providers

```text
routerAI
|
+-- Codex
|    +-- multiple isolated CODEX_HOME profiles
|    +-- routerAI-managed Codex runtime
|    +-- ChatGPT/Codex login
|    +-- account metadata and quota history
|    +-- Codex app-server completion execution
|
+-- Google Antigravity
|    +-- Consumer CLI mode
|    |    +-- official `agy` integration
|    |    +-- active system-keyring session
|    |    +-- Antigravity `/usage` quota
|    |
|    +-- Gemini API project mode
|         +-- Gemini API key
|         +-- official Antigravity managed agent
|         +-- Gemini Interactions API
|         +-- automatic API routing / failover
|
+-- Z.ai
     +-- General API credentials
     +-- OpenAI-compatible request execution
     +-- Coding Plan credentials kept separate
```

The Antigravity consumer quota target is Antigravity developer/agent usage exposed by the official CLI. It is **not Gemini Chat web quota**.

## Terminal UI

Start routerAI without management subcommands:

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

### Add Provider

```text
Add Provider
  |
  +-- Codex
  |     +-- create isolated account
  |     +-- device-code or browser login
  |
  +-- Google Antigravity
  |     +-- Consumer CLI session
  |     |    +-- install official CLI if missing
  |     |    +-- login / verify active Google session
  |     |
  |     +-- Gemini API project
  |          +-- secure API-key entry
  |          +-- Antigravity managed-agent routing
  |
  +-- Z.ai
        +-- General API
        +-- Coding Plan
        +-- secure API-key entry
```

## Codex

Each routerAI Codex account owns an isolated `CODEX_HOME`:

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

The executable/runtime may be shared, while authentication and account state stay isolated per routerAI account.

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
- conversion to OpenAI-style chat-completion responses

Codex consumer profiles are kept in `codex-default`, which is forced to **Manual** selection. They are not inserted into automatic mixed-provider failover.

## Google Antigravity

routerAI supports two intentionally separate Antigravity modes.

### Consumer CLI session

The consumer adapter uses the official `agy` runtime instead of extracting browser sessions or private OAuth credentials.

Implemented:

- detect `agy`
- optionally launch Google's official CLI installer from the TUI
- login / verify the active system-keyring session
- read Antigravity `/usage`
- normalize quota percentages into routerAI quota snapshots
- persist quota history in SQLite
- explicit/manual account/session group

The consumer CLI mode represents one active Antigravity system-keyring session per OS user. routerAI does not copy private OAuth tokens or rewrite keyring entries to emulate unsupported parallel consumer sessions.

### Gemini API project

Antigravity is also available as a Google-managed agent through the Gemini Interactions API.

routerAI's API-project adapter uses:

```text
POST https://generativelanguage.googleapis.com/v1beta/interactions
Agent: antigravity-preview-05-2026
Authentication: x-goog-api-key
```

Implemented:

- secure Gemini API-key entry
- provider-specific credential reference
- Antigravity managed-agent request execution
- `system_instruction` mapping from OpenAI system/developer messages
- optional `router.models.antigravity` -> `agent_config.model`
- response `steps/model_output` -> OpenAI assistant text
- Gemini token usage -> OpenAI-style usage fields
- handling for completed/incomplete/budget-limited responses
- retryable transport/HTTP failure cooldown
- participation in `antigravity-api-default`
- participation in `mixed-default`

Official Antigravity managed-agent documentation:

```text
https://ai.google.dev/gemini-api/docs/antigravity-agent
```

## Z.ai

Supported endpoint modes:

```text
General API : https://api.z.ai/api/paas/v4
Coding Plan : https://api.z.ai/api/coding/paas/v4
```

Current implementation:

- provider-first account creation
- password-style API-key input
- credential secret stored outside SQLite
- General API `/chat/completions` execution
- Z.ai General API automatic routing
- participation in `zai-default`
- participation in `mixed-default`

Coding Plan credentials remain separate from the general-purpose API pool.

routerAI does not currently claim machine-readable Z.ai Coding Plan quota support because no documented quota endpoint has been integrated.

## Credential storage

Provider secrets are not written into `router.db`.

```text
SQLite account row
    -> credential_ref
    -> CredentialStore
```

Current local layout:

```text
.routerai/
+-- secrets/
+-- runtime/
+-- accounts/
```

On Windows, credential files are protected with Windows DPAPI.

On POSIX systems, the current fallback uses user-only file permissions (`0600`). A native keyring backend remains a future improvement.

The generated localhost API key is stored through the same `CredentialStore` and can be rotated from the TUI. Rotation invalidates the previous key immediately.

`.routerai/`, database files, WAL/SHM files and `.env` are ignored by Git.

## Quota and health

Provider quota is normalized into provider-independent snapshots:

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

Codex health derivation stays separate from request transport failures:

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

Temporary request/network failures are tracked independently:

```text
consecutiveFailures
cooldownUntilUnix
lastError
```

Failure state does not overwrite quota/auth status.

Cooldown uses bounded exponential backoff:

```text
30s -> 60s -> 120s -> 240s -> 480s -> max 15m
```

A successful backend request clears transport failure/cooldown state.

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

Available routing strategies:

```text
Health First
Least Used
Priority
Round Robin
Manual
```

The TUI provides explicit **Select manual account** behavior for consumer groups.

The routing core also enforces the boundary: a custom automatic group cannot contain a consumer-only account. Use `Manual` strategy when a group contains consumer profiles.

Automatic API routing currently accepts API-capable backends such as:

```text
Z.ai General API
Antigravity Gemini API project
```

Consumer profiles remain explicit/manual and are not automatically cycled based on subscription quota.

## Local OpenAI-compatible API

routerAI starts a localhost-only server by default:

```text
http://127.0.0.1:9000/v1
```

Implemented endpoints:

```text
GET  /health
GET  /v1/models
POST /v1/chat/completions
```

`/v1/models` lists only routing groups that contain a backend routerAI can execute as a completion backend.

Authentication:

```text
Authorization: Bearer <routerAI-local-key>
```

The local key is available under `Local API` in the TUI and can be rotated there.

### Choose a routing group

Using a header:

```text
X-Router-Group: zai-default
```

Using router metadata:

```json
{
  "router": {
    "group": "mixed-default"
  }
}
```

Or using a pseudo-model:

```json
{
  "model": "router/mixed-default"
}
```

`router/<group>` is a routing selector only. routerAI never forwards that pseudo-model to a provider as a real model name.

### Provider model mapping

For a mixed group, provide explicit provider model mappings when required:

```json
{
  "model": "router/mixed-default",
  "messages": [
    {"role": "user", "content": "hello"}
  ],
  "router": {
    "models": {
      "zai": "<zai-model>",
      "antigravity": "<supported-gemini-model>"
    }
  }
}
```

For Antigravity, omitting `router.models.antigravity` lets the managed agent use Google's current default underlying model.

For Z.ai General API, an actual Z.ai model must be supplied either as the normal `model` value or as `router.models.zai`.

### Failover

A single request maintains its own list of accounts already attempted. An account that is incompatible with a particular request can be skipped for that request without corrupting long-term provider health.

Retryable transport errors, HTTP `429`, and retryable server errors can place an API backend into cooldown and allow the routing group to try another eligible backend.

### Streaming

`stream=true` is accepted.

The current 0.6 implementation provides **buffered SSE compatibility**: routerAI waits for the selected backend response, converts the completed response into OpenAI `chat.completion.chunk` events, then emits:

```text
data: [DONE]
```

True token-by-token provider streaming remains a follow-up improvement.

## Desktop Profile Manager

Desktop profile management remains separate from API routing.

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

The intended action is explicit user-controlled activation. routerAI does not currently automate quota-triggered consumer desktop account cycling.

Where a desktop application does not expose a stable supported profile-switch interface, routerAI will prefer explicit login/profile isolation instead of copying cookies, private tokens, application databases or OS keyring entries.

## Architecture

```text
                         routerAI
                            |
          +-----------------+-----------------+
          |                 |                 |
          v                 v                 v
     TerminalApp       Local API       Desktop Profiles
          |                 |              (future)
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
    app-server        CLI / Gemini API       General API
```

## Data layout

```text
routerAI/
+-- router.db
+-- .routerai/
    +-- secrets/
    +-- runtime/
    |   +-- codex/
    +-- accounts/
        +-- codex-01/
        |   +-- codex-home/
        +-- codex-02/
            +-- codex-home/
```

Do not commit runtime data or credential files to source control.

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

CTest currently contains coverage for:

- SQLite schema migration and quota persistence
- account selection
- routing strategies
- round-robin cursor persistence
- cooldown/failure recovery
- request-local routing exclusions
- consumer/API routing boundaries
- rejection of consumer accounts from automatic custom pools
- OpenAI pseudo-model/group resolution
- provider-specific model overrides
- buffered SSE formatting and `[DONE]`

## GitHub Actions

CI is intentionally **not executed on every push**.

The workflow runs on:

```text
workflow_dispatch
pull_request
```

Normal iterative commits therefore do not repeatedly consume GitHub Actions minutes. Run the workflow manually when a development batch is ready for Windows/Linux verification, or let it run for a pull request.

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
[done] Z.ai General API request adapter
[done] Antigravity consumer CLI + quota adapter
[done] Antigravity Gemini API project adapter
[done] Antigravity managed-agent request execution
[done] Routing-group persistence
[done] Health/priority/least-used/round-robin/manual strategies
[done] Request-local candidate exclusion
[done] Failure cooldown state
[done] API-credential backend failover
[done] Local OpenAI-compatible API
[done] Local API key rotation
[done] Buffered SSE compatibility
[done] OpenAI compatibility unit tests
[done] Manual/PR-only GitHub Actions workflow

[next] One manual Windows/Linux CI verification for this development batch
[next] Native POSIX keyring credential backend
[next] Provider credential validation without consuming completion quota where supported
[next] True provider token streaming
[next] Local API HTTP integration tests
[next] Custom routing-group membership editor in the TUI
[next] Desktop Profile Manager
[next] Supported Codex Desktop profile activation where a stable interface exists
[next] Supported Antigravity desktop profile activation where a stable interface exists
[next] Optional web/desktop management UI
```

## Design principles

Provider-specific authentication, quota and request behavior belongs inside provider adapters.

routerAI prefers documented provider interfaces and supported credential/runtime mechanisms. It should not depend on browser-session scraping, credential extraction from official clients, private endpoint reverse engineering, CAPTCHA/2FA bypass, or spoofing first-party applications.
