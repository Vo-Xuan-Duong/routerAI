# routerAI

Interactive multi-provider AI account and runtime router written in C++20.

routerAI is designed to manage multiple AI providers, multiple accounts/projects per provider, provider-specific quota/health information, desktop profiles, and future routing groups behind one local OpenAI-compatible API.

The project deliberately separates two different concepts:

```text
Desktop Profile Management
    -> user chooses which desktop/account profile to activate

API / Runtime Routing
    -> routerAI selects an eligible backend/account for a request
```

Desktop profile switching is intended for explicit user-controlled profile activation. Automatic request routing/failover is handled separately through provider runtimes and supported APIs.

## Current providers

```text
routerAI
|
+-- Codex
|    +-- multiple isolated ChatGPT/Codex profiles
|    +-- managed Codex runtime
|    +-- account metadata
|    +-- quota/rate-limit snapshots
|
+-- Google Antigravity
|    +-- provider adapter planned/in progress
|    +-- target: Antigravity developer/agent usage and quota
|    +-- NOT Gemini Chat web quota
|
+-- Z.ai
     +-- provider scaffold
     +-- Coding Plan endpoint
     +-- General API endpoint
```

The long-term routing model supports provider-local pools and mixed pools:

```text
Codex only
Antigravity only
Z.ai only
Mixed
```

## Current scope

- C++20 + CMake
- FTXUI full-screen terminal interface
- Arrow-key navigation with Enter/Esc/q controls
- SQLite account persistence and additive schema migration
- Provider abstraction
- Deterministic account selection based on health, latest usage and priority
- Persistent quota snapshot/window history for supported providers
- Cross-platform child-process transport for Windows and POSIX
- CTest coverage for SQLite migration/history and account selection

### Codex

- One isolated `CODEX_HOME` per routerAI Codex account
- routerAI-managed Codex runtime bootstrap
- Official Codex login/runtime flow
- Codex app-server JSONL transport
- account/profile retrieval
- rate-limit/quota retrieval
- per-account quota history

### Google Antigravity

The Antigravity provider is being designed around Antigravity developer/agent usage rather than Gemini Chat consumer quota.

Planned capabilities:

- account/project registration
- provider health
- supported quota/usage retrieval when exposed through a documented machine-readable interface
- explicit desktop-account activation workflow
- supported API/project routing

routerAI will not depend on copying browser cookies, extracting private OAuth tokens from another application, or reverse-engineering undocumented private quota endpoints.

### Z.ai

Z.ai provider scaffolding is present.

Documented endpoints currently represented by the provider:

```text
Coding Plan : https://api.z.ai/api/coding/paas/v4
General API : https://api.z.ai/api/paas/v4
```

Planned capabilities:

- secure API-key credential entry
- credential validation
- model discovery
- request execution
- provider health
- participation in Z.ai-only and Mixed routing groups

routerAI does not currently claim machine-readable Z.ai Coding Plan quota support because no documented quota endpoint has been integrated yet.

## Terminal UI

There are no management subcommands to memorize. Start the application:

```bash
router
```

The current main screen is moving toward:

```text
routerAI 0.5.0

Navigation
--------------------------------
Dashboard
Accounts
Providers
Add Provider
Routing Groups        (planned)
Desktop Profiles      (planned)
Local API             (planned)
Doctor
Exit
```

The currently implemented provider-add flow is provider-first:

```text
Add Provider
  |
  +-- Codex
  |     +-- Add ChatGPT/Codex account
  |
  +-- Google Antigravity
  |     +-- account/project adapter in progress
  |
  +-- Z.ai
        +-- Coding Plan/API account scaffold
```

## Accounts

Accounts are stored independently from provider runtimes.

Example:

```text
Accounts

codex-01         Codex        READY
codex-02         Codex        WARNING
antigravity-01   Antigravity  READY
zai-01           Z.ai         READY
```

Each account has provider-specific capabilities. A Codex account can currently expose:

```text
Details
Login
Refresh
Quota
Quota history
```

Antigravity and Z.ai actions will be enabled as their provider adapters are completed.

## Managed Codex runtime

You do not need to install Codex CLI manually for routerAI.

When a Codex operation requires a runtime and no usable runtime is available, routerAI can bootstrap a managed Codex runtime below:

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

The executable/runtime is shared, while authentication/state remains isolated per routerAI account through separate `CODEX_HOME` directories.

If a compatible `codex` executable already exists on `PATH`, routerAI can use it as a fallback.

## Quota and health

Quota is normalized into provider-independent snapshots when the provider exposes supported usage information.

Conceptually:

```text
QuotaSnapshot
  +-- provider/account
  +-- usage allowed
  +-- limit buckets
       +-- model/limit name
       +-- window
            +-- used percent
            +-- duration
            +-- reset time
```

Current Codex health derivation is conservative:

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

Quota/history from one provider is not assumed to have the same window structure as another provider.

For Antigravity, the target is Antigravity developer/agent quota rather than Gemini Chat web quota.

## Routing groups

Routing Groups are the core of the future local API.

A group contains eligible provider accounts/projects and a routing policy.

Example:

```text
Routing Group: codex-pool

Members
[x] codex-01
[x] codex-02
[x] codex-03

Strategy
Health First
```

Provider-local pools:

```text
codex-pool
  +-- codex-01
  +-- codex-02

antigravity-pool
  +-- antigravity-project-01
  +-- antigravity-project-02

zai-pool
  +-- zai-01
  +-- zai-02
```

Mixed pool:

```text
mixed-default
  +-- codex-01
  +-- codex-02
  +-- antigravity-project-01
  +-- zai-01
```

Planned strategies:

```text
Health First
Least Used
Priority
Round Robin
Manual
```

A request will eventually flow through:

```text
Client request
    |
    v
Local API
    |
    v
Routing Group
    |
    v
Account / Project Selector
    |
    +-- enabled?
    +-- authenticated / authorized?
    +-- healthy?
    +-- quota available?
    +-- cooldown?
    +-- priority?
    |
    v
Selected provider backend
```

Automatic failover belongs to this routing layer, not to desktop-account session manipulation.

## Local OpenAI-compatible API

The planned local API provides one local credential and hides provider/account selection from the client.

Target architecture:

```text
Client
  |
  | Authorization: Bearer <routerAI-local-key>
  v
127.0.0.1:9000/v1
  |
  v
routerAI
  |
  +-- Codex routing group
  +-- Antigravity routing group
  +-- Z.ai routing group
  +-- Mixed routing group
```

This will allow tools that understand an OpenAI-compatible endpoint to connect to one local router while routerAI performs supported provider/backend selection behind it.

## Desktop Profile Manager

Desktop profile management is a separate feature from API routing.

Target UI:

```text
Desktop Profiles

Codex Desktop
  +-- Personal       ACTIVE
  +-- Work

Antigravity
  +-- Google A       ACTIVE
  +-- Google B
```

The intended interaction is explicit:

```text
Desktop Profiles
  -> Codex Desktop
  -> Work
  -> Activate
```

or:

```text
Desktop Profiles
  -> Antigravity
  -> Google B
  -> Activate
```

routerAI may detect, launch, stop and re-open supported desktop applications as part of profile activation where a reliable mechanism exists.

Important distinction:

```text
Manual Desktop Switching
    user explicitly activates another desktop profile

Automatic API Routing
    routerAI automatically selects another supported backend/account
```

routerAI does not currently implement automatic consumer-desktop account cycling based on quota exhaustion. Desktop applications do not expose a stable cross-provider account-switch API that routerAI can rely on today.

The project will prefer documented profile/runtime mechanisms and provider-supported login flows instead of modifying private application session databases, copying cookies, extracting credentials, or rewriting OS keyring entries.

### Advanced desktop isolation

A possible future Windows mode is stronger OS-level profile isolation:

```text
Desktop Profile A
  -> isolated Windows user/profile
  -> isolated credential store

Desktop Profile B
  -> isolated Windows user/profile
  -> isolated credential store
```

This is intentionally considered an advanced mode because it is heavier than the default account/runtime isolation used by routerAI.

## Architecture

```text
                         routerAI
                            |
             +--------------+--------------+
             |              |              |
             v              v              v
      TerminalApp     Desktop Profiles   Local API
             |                             |
             +-------------+---------------+
                           |
                           v
                    AccountManager
                           |
          +----------------+----------------+
          |                |                |
          v                v                v
   AccountSelector    Quota/Health     Routing Groups
          |                                 |
          +----------------+----------------+
                           |
                           v
                    Provider abstraction
                           |
          +----------------+----------------+
          |                |                |
          v                v                v
    CodexProvider   AntigravityProvider   ZaiProvider
          |
          +-- managed Codex runtime
          +-- isolated CODEX_HOME
          +-- app-server
```

## Data layout

Current local runtime layout is conceptually:

```text
routerAI/
+-- router.db
+-- .routerai/
    +-- runtime/
    |   +-- codex/
    |
    +-- accounts/
        +-- codex-01/
        |   +-- codex-home/
        +-- codex-02/
            +-- codex-home/
```

Account metadata and quota history are stored in `router.db` by default.

Provider runtime state lives below `.routerai/`.

Local runtime data should not be committed to source control.

## Requirements

- CMake 3.24+
- C++20 compiler
- vcpkg
- terminal with ANSI/interactive input support
- internet access when provider runtime installation/authentication requires it

Dependencies managed through vcpkg:

- FTXUI
- SQLite3
- spdlog
- nlohmann/json

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

For day-to-day development after configuration, rebuilding normally only requires:

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

## Development roadmap

```text
[done] SQLite persistence and additive migration
[done] Provider abstraction
[done] Codex account isolation
[done] Managed Codex runtime bootstrap
[done] Codex account/profile retrieval
[done] Codex quota history and health
[done] Deterministic Codex account selection
[done] Full-screen FTXUI terminal UI
[done] Provider-first Add Provider flow
[done] Z.ai provider scaffold

[next] Secure provider credential store
[next] Z.ai credential validation + request adapter
[next] Google Antigravity account/project adapter
[next] Antigravity developer/agent quota integration where officially supported
[next] Provider management screen
[next] Routing-group persistence and TUI
[next] Cooldown and failure tracking
[next] Automatic backend failover
[next] Local OpenAI-compatible API
[next] Desktop Profile Manager
[next] Codex Desktop profile activation support where reliable
[next] Antigravity desktop profile activation support where reliable
[next] Optional web/desktop management UI
```

## Design principles

routerAI aims to keep provider integrations explicit and replaceable:

```text
UI / Local API
      |
      v
Provider interface
      |
      +-- Codex
      +-- Antigravity
      +-- Z.ai
```

Provider-specific authentication, quota and request behavior should remain inside each provider adapter.

The router should prefer documented provider interfaces and supported credential/runtime mechanisms. It should not depend on browser-session scraping, credential extraction from official clients, private endpoint reverse engineering, CAPTCHA/2FA bypass, or spoofing first-party applications.
