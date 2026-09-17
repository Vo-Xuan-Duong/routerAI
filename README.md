# routerAI

Interactive multi-provider AI account/runtime manager and localhost router written in C++20.

Current development version: **0.6.0**.

routerAI deliberately separates three concerns:

```text
Provider / Account Management
    -> login, credentials, provider status, models and quota

API / Runtime Routing
    -> select a supported backend for a localhost API request

Desktop Applications
    -> detect and explicitly launch supported desktop clients
```

Automatic API failover is separate from consumer desktop/session switching.

## Providers

```text
routerAI
|
+-- Codex
|    +-- isolated CODEX_HOME per account
|    +-- routerAI-managed Codex runtime
|    +-- ChatGPT/Codex login
|    +-- profile + quota history
|    +-- Codex app-server completion execution
|    +-- native delta streaming
|
+-- Google Antigravity
|    +-- Consumer CLI mode
|    |    +-- official agy integration
|    |    +-- active system-keyring session
|    |    +-- Antigravity /usage quota
|    |
|    +-- Gemini API project mode
|         +-- Gemini API key validation
|         +-- official Antigravity managed agent
|         +-- Interactions API
|         +-- native SSE -> OpenAI SSE translation
|         +-- automatic routing / failover
|
+-- Z.ai
     +-- General API credentials
     +-- OpenAI-compatible request execution
     +-- native SSE passthrough
     +-- Coding Plan kept as a separate mode
```

The Antigravity consumer quota target is the developer/agent quota exposed by the official Antigravity CLI. It is not Gemini Chat web quota.

## Terminal UI

Run:

```bash
router
```

Current navigation:

```text
Dashboard
Accounts
Add Provider
Routing Groups
Local API
Best account
Doctor
Desktop Applications
Exit
```

There are no management subcommands to memorize.

## Add Provider

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
  |     |    +-- login / verify active session
  |     |
  |     +-- Gemini API project
  |          +-- secure API-key entry
  |          +-- credential validation
  |          +-- managed-agent routing
  |
  +-- Z.ai
        +-- General API
        +-- Coding Plan
        +-- secure API-key entry
```

## Codex

Each Codex account has an isolated `CODEX_HOME`:

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

The runtime executable may be shared; authentication/state is isolated per account.

Current Codex integration includes:

- managed runtime bootstrap
- official login flow
- `account/read`
- `account/rateLimits/read`
- quota snapshot/history persistence
- `thread/start`
- `turn/start`
- `item/agentMessage/delta`
- `turn/completed`
- OpenAI chat-completion conversion
- native token/delta streaming through the local API

Codex consumer profiles stay in `codex-default`, which is forced to **Manual** selection. They are not inserted into automatic mixed-provider failover.

## Google Antigravity

### Consumer CLI session

The consumer adapter uses the official `agy` runtime.

Implemented:

- detect/install `agy`
- login / verify the active system-keyring session
- read `/usage`
- normalize quota snapshots
- persist quota history
- explicit/manual consumer group

routerAI does not copy private OAuth tokens or rewrite OS keyring entries to emulate parallel consumer sessions.

### Gemini API project

Antigravity API projects use the official Gemini Interactions API and managed Antigravity agent.

```text
POST https://generativelanguage.googleapis.com/v1beta/interactions
streaming: ?alt=sse
agent: antigravity-preview-05-2026
```

Implemented:

- secure Gemini API-key entry
- non-completion credential validation using model listing
- model discovery
- `system_instruction` mapping
- optional `router.models.antigravity` -> `agent_config.model`
- non-streaming response conversion
- native Interactions SSE parsing
- only `step.delta` text output is exposed as OpenAI chat chunks
- thought/tool stream events are not exposed as assistant text
- final `interaction.completed` -> OpenAI finish chunk + `[DONE]`
- automatic routing through `antigravity-api-default`
- participation in `mixed-default`

## Z.ai

```text
General API : https://api.z.ai/api/paas/v4
Coding Plan : https://api.z.ai/api/coding/paas/v4
```

Implemented:

- provider-first account creation
- password-style API-key input
- credential secret stored outside SQLite
- documented model metadata in the TUI
- General API `/chat/completions`
- native provider SSE passthrough
- `zai-default`
- participation in `mixed-default`

Z.ai credentials are conservatively marked for provider verification because routerAI does not depend on an undocumented validation endpoint. Coding Plan credentials are not inserted into the general-purpose mixed API pool.

## Credential storage

Provider secrets are not stored in `router.db`.

```text
SQLite account row
    -> credential_ref
    -> CredentialStore
```

Local layout:

```text
.routerai/
+-- secrets/
+-- runtime/
+-- accounts/
```

Windows uses DPAPI for credential files. POSIX currently uses user-only file permissions (`0600`); a native POSIX keyring backend remains future work.

The localhost API key uses the same credential store and can be rotated from the TUI. Rotation invalidates the previous key immediately.

`.routerai/`, database files, WAL/SHM files and `.env` are ignored by Git.

## Routing groups

Default groups:

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

Strategies:

```text
Health First
Least Used
Priority
Round Robin
Manual
```

The TUI can create custom routing groups and edit custom memberships. Automatic custom groups only accept API-capable accounts; manual groups may contain consumer profiles.

The routing core enforces the same boundary even for legacy or manually edited database state.

### Failure state

Transport failures are tracked separately from quota/auth health:

```text
consecutiveFailures
cooldownUntilUnix
lastError
```

Cooldown uses bounded exponential backoff:

```text
30s -> 60s -> 120s -> 240s -> 480s -> max 15m
```

A successful backend request clears failure/cooldown state.

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

Choose a group with:

```text
X-Router-Group: zai-default
```

or:

```json
{
  "router": {"group": "mixed-default"}
}
```

or:

```json
{
  "model": "router/mixed-default"
}
```

`router/<group>` is only a routing selector and is never forwarded as a provider model name.

Provider-specific model mapping:

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

### Streaming

`stream=true` uses chunked `text/event-stream` output.

Current behavior:

```text
Codex-only group
    -> native app-server delta streaming

Z.ai-only group
    -> native provider SSE passthrough

Antigravity API-only group
    -> native Interactions SSE translated to OpenAI chunks

Mixed cross-provider group
    -> buffered SSE fallback
```

Mixed routing stays buffered because failover between providers is only safe before bytes have been emitted to the client. The response still follows OpenAI SSE framing and terminates with:

```text
data: [DONE]
```

## Quota and health

Quota is normalized into provider-independent snapshots with limit buckets/windows, usage percentages and reset timestamps when available.

Codex quota health remains separate from request failure state:

```text
ordinaryUsageAllowed = false -> LIMITED
reached signal / >=90% used  -> WARNING
otherwise                    -> READY
```

## Desktop Applications

routerAI now has a Desktop Applications screen and a small `DesktopProfileManager` abstraction.

Implemented today:

- detect supported Codex/ChatGPT desktop surface where discoverable
- detect Antigravity desktop application where discoverable
- show installation/executable metadata
- explicitly launch a detected application

Not implemented as an automatic feature:

- quota-triggered desktop account cycling
- copying desktop cookies/tokens/session databases
- rewriting OS keyring entries

A true account/profile switch action will only be added when the corresponding desktop application exposes a stable external mechanism. Until then, desktop launching and API/runtime routing remain separate.

## Tests

CTest covers:

- SQLite migration/quota persistence
- account selection
- routing strategies
- round-robin persistence
- cooldown/failure recovery
- request-local candidate exclusion
- consumer/API routing boundaries
- custom automatic-group rejection rules
- OpenAI pseudo-model/group resolution
- provider model metadata
- local HTTP API auth/model listing/key rotation
- chunked SSE error framing and `[DONE]`
- Desktop application manager metadata/detection behavior

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

After initial configure:

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

## GitHub Actions

CI is intentionally not run on every push.

```text
workflow_dispatch
pull_request
```

Use one manual workflow run when a development batch is ready for Windows/Linux verification.

## Development status

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
[done] Antigravity consumer CLI + quota adapter
[done] Antigravity Gemini API project adapter
[done] Antigravity native Interactions SSE translation
[done] Z.ai General API adapter
[done] Z.ai native SSE passthrough
[done] Routing-group persistence and custom group editor
[done] Automatic API-backend failover + cooldown
[done] Local OpenAI-compatible API
[done] Local API key rotation
[done] HTTP integration tests
[done] Desktop application detection/launch
[done] Manual/PR-only GitHub Actions workflow

[next] One manual Windows/Linux CI verification for this batch
[next] Native POSIX keyring backend
[next] Z.ai no-cost credential validation if a documented endpoint becomes available
[next] Cross-provider streaming failover design before first emitted byte
[next] Stable desktop account/profile switching if providers expose supported interfaces
[next] Optional web/desktop management UI
```

## Design principles

Provider-specific authentication, quota and request behavior stays inside provider adapters.

routerAI prefers documented provider interfaces and supported credential/runtime mechanisms. It does not rely on browser-session scraping, credential extraction from official clients, private endpoint reverse engineering, CAPTCHA/2FA bypass, or spoofing first-party applications.
