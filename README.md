# routerAI

Multi-provider AI account/runtime manager, localhost OpenAI-compatible router, terminal control plane, and embedded Web Admin written in C++20.

Current release: **0.8.4**.

routerAI separates four concerns:

```text
Provider / Account Management
    -> login, credentials, status, models and quota

API / Runtime Routing
    -> select supported backends for localhost API requests

Management Control Plane
    -> TUI + localhost Web Admin + request history + config transfer

Desktop Applications
    -> detect/launch clients and expose a safe future switching boundary
```

Automatic API routing is intentionally separate from consumer desktop/session switching.

## Providers

### Codex

- isolated `CODEX_HOME` per routerAI account
- routerAI-managed Codex runtime bootstrap
- official browser/device-code login through Codex runtime
- account/profile metadata
- quota/rate-limit snapshots and history
- Codex `app-server` completion execution
- native app-server delta streaming
- explicit/manual consumer-account selection

Codex consumer profiles are not automatically pooled into cross-account quota rotation.

### Google Antigravity

Two modes are supported.

**Consumer CLI session**

- official `agy` integration
- optional official CLI installation action
- browser login / active-session verification
- Antigravity `/usage` quota collection
- normalized quota history
- manual consumer-session management

**Gemini API project**

- Gemini API-key validation
- Antigravity managed agent through the Gemini Interactions API
- model discovery
- native SSE translation to OpenAI-style chunks
- automatic routing through `antigravity-api-default`
- participation in `mixed-default`

The consumer quota target is Antigravity developer/agent usage exposed by the official CLI, not Gemini Chat web quota.

### Z.ai

- General API mode
- Coding Plan mode kept separate from general-purpose routing
- secure API-key storage
- OpenAI-compatible `/chat/completions`
- native provider SSE passthrough
- documented static model metadata including GLM-5.2
- General API participation in `zai-default` and `mixed-default`

routerAI does not invent private Z.ai endpoints for credential/quota discovery.

## Management TUI

`router.exe` opens the 0.8 management control plane:

```text
routerAI
├─ Usage Dashboard
├─ Quota Advisor
├─ Routing Health
├─ Account Overview
├─ Provider Console
├─ Request History
├─ Config Export / Import
├─ Maintenance / Doctor
├─ Web Admin
└─ Exit
```

`Provider Console` retains the detailed provider-first TUI for login, quota, routing groups, model discovery, Doctor, Local API controls and Desktop Applications.

### Usage Dashboard

The dashboard is an interactive terminal control surface. It shows:

- account/provider counts
- ready/attention/disabled counts
- latest normalized quota usage bars
- recent request success rate
- per-account provider/status/identity
- provider and health filters
- account ID, quota, provider, priority and health sorting
- keyboard scrolling for larger account sets

Dashboard controls:

```text
r         refresh all account statuses
u         refresh all supported quota snapshots
p         cycle provider filter
f         cycle health filter
s         cycle sort mode
Up/Down   scroll account rows
0         reset filters/sort
Esc/q     back
```

### Quota Advisor

Quota Advisor is the operator-facing bridge between consumer quota monitoring and manual account selection.

It supports:

- Codex `subscription-runtime` accounts
- Antigravity `consumer-cli` accounts
- explicit status + quota refresh using the existing supported provider integrations
- recommendation by health, known/lower quota usage, priority, then account ID
- writing the recommended account into the provider's manual default routing group

The recommendation is advisory until the operator selects it. Applying it updates routerAI's manual routing selection only; it does not copy credentials, browser cookies, OAuth state, or switch an external desktop application session.

### Routing Health

Routing Health previews the routing decision for every group without consuming a request or changing routing state.

It shows:

- group enabled/disabled state and strategy
- the primary candidate a real request would select
- a second automatic candidate with the primary excluded
- manual selection for consumer/manual groups
- member status, priority, latest quota snapshot and cooldown state
- whether the group is completion-capable and whether it is currently routable

Round-robin previews are side-effect-free: opening Routing Health does not advance the persistent round-robin cursor. Manual consumer groups do not auto-select a backup; use Quota Advisor or explicit routing-group controls to change those selections.

### Account overview and lifecycle

The main Account Overview is operational rather than read-only. From the same selectable terminal screen you can refresh one account's provider status, refresh supported live quota, set its operator priority, enable/disable routing, inspect details, or remove the local account. It also provides best-effort bulk status and quota refresh so one failing account does not stop the remaining refreshes.

Account priority is an integer from -100000 to 100000. Higher values are preferred by the Priority routing strategy and are used as a tie-break after health/quota in consumer recommendations.

Accounts can be enabled/disabled without destroying provider auth/quota health.

Permanent remove deletes local account metadata, quota history, routing memberships, local credential reference, and isolated runtime profile. Request-history rows remain as an audit trail but contain only account/provider identifiers, not credentials or prompt contents.

## Embedded Web Admin

Web Admin is served from the same localhost process:

```text
http://127.0.0.1:9000/admin
```

It supports normal management without using the terminal UI:

- add Codex accounts
- add Antigravity consumer sessions or Gemini API projects
- add Z.ai General API / Coding Plan entries
- browser-login supported consumer accounts through official provider flows
- configure/reconfigure API keys
- refresh account state
- enable/disable/remove accounts
- usage dashboard and routing-group overview
- request-history viewer/clear
- secret-free config export/download/import

Web Admin mutation/data APIs require the same local Bearer key as the OpenAI-compatible API. The HTML shell itself is localhost-readable so the browser can show the connection screen.

Provider API keys submitted from Web Admin are sent only to the localhost router process and are stored through `CredentialStore`; they are not exported by config backup.

## Secret-free config export/import

Config transfer includes management metadata only:

```text
accounts
  id
  provider
  provider_mode
  display_name
  email
  plan_type
  priority
  enabled

routing_groups
  id
  display_name
  strategy
  enabled
  manual_account_id
  account_ids
```

It deliberately excludes:

```text
provider API keys
OAuth tokens
credential_ref
local API key
runtime session databases
```

Imported new credential-backed accounts return as `AUTH_EXPIRED` until the credential/login is configured locally. Imported identifiers are restricted to supported provider names and safe account/group identifiers.

For an account that already exists locally, config import cannot change its `provider` or `provider_mode`. This prevents a metadata backup from rebinding an existing local `credential_ref` to a different provider adapter.

## Request history

routerAI records local routing telemetry in SQLite:

```text
time
group
account id
provider
requested model/status
duration
streaming flag
success/failure
bounded error text
```

Prompt bodies and successful provider response bodies are not stored in request history.

Request-history retention is automatic and bounded by default:

```text
maximum rows  10,000
maximum age   30 days
```

The main `Maintenance / Doctor` screen can run the same retention pass manually and shows database footprint, request-log count, missing credential references, invalid routing references, orphan runtime folders and missing Codex runtime directories.

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

Custom automatic groups only accept API-capable credentials. Consumer subscription/session profiles remain manual. The core enforces this rule even for legacy or manually edited database state.

Temporary backend failure state is separate from provider quota/auth status:

```text
consecutiveFailures
cooldownUntilUnix
lastError
```

Cooldown uses bounded exponential backoff:

```text
30s -> 60s -> 120s -> 240s -> 480s -> max 15m
```

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

Select a routing group by header:

```text
X-Router-Group: zai-default
```

request metadata:

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

`router/<group>` is a routing selector only; it is never forwarded to a provider as a real model name.

Mixed routing supports provider-specific model mapping:

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

`stream=true` uses `text/event-stream`.

```text
Codex-only
    -> native app-server delta streaming

Z.ai-only
    -> native provider SSE passthrough

Antigravity API-only
    -> native Interactions SSE translated to OpenAI chunks

Mixed cross-provider
    -> buffered SSE fallback
```

Mixed-provider streams stay buffered because failover after bytes have already reached the client is unsafe.

## Credential storage

Provider secrets are not stored directly in `router.db`.

```text
SQLite account row
    -> credential_ref
    -> CredentialStore
```

Backends:

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

Local runtime/secret state is ignored by Git:

```text
.routerai/
*.db
*.db-wal
*.db-shm
.env
```

## Desktop Applications

routerAI detects and explicitly launches supported ChatGPT/Codex and Antigravity desktop installations where discoverable.

A `DesktopAccountSwitcher` capability boundary exists so a future supported provider API can be plugged in without redesigning the UI. Current builds fail closed: if no official stable switching interface is available, `switchAccount()` returns unsupported.

routerAI does not copy desktop cookies, OAuth tokens, profile databases, or OS keyring entries to simulate unsupported switching.

## Windows quick start from source

```cmd
git pull
configure
test
run
```

Helpers use `VCPKG_ROOT` when set; otherwise they default to:

```text
C:\dev\vcpkg
```

Development stack:

```text
Compiler       MinGW/GCC
Generator      Ninja
Build type     Release
vcpkg triplet  x64-mingw-dynamic
Executable     build\router.exe
```

Do not mix MinGW/GCC with the MSVC `x64-windows` triplet.

## Binary / portable packaging

0.7 adds packaging so end users do not need to build from source.

### Local Windows package

Windows validation and packaging are intentionally local:

```cmd
test
package
```

`package.cmd` assembles a portable directory and:

```text
routerAI-<version>-windows-x64.zip
```

It copies vcpkg dynamic libraries and discoverable MinGW runtime DLLs into the portable package.

### GitHub Linux packaging workflow

GitHub Actions is Linux-only. The `package` workflow can be launched manually and also runs for version tags. It builds, tests and produces:

```text
routerAI-<version>-linux-x64.tar.gz
```

For a `v*` tag, the workflow creates a GitHub Release and attaches the Linux archive. A Windows ZIP can be produced locally with `package.cmd` and attached separately when desired.

Windows and Linux packaging derive the package version from `CMakeLists.txt`, so release filenames stay aligned with the project version. The vcpkg revision used by CI/packaging is pinned for reproducible builds instead of following vcpkg `main` on every run.

## Tests

CTest includes:

- SQLite migration/quota persistence
- selection and routing strategies
- persistent round-robin
- failure cooldown/recovery
- API-vs-consumer routing boundaries
- OpenAI compatibility parsing
- provider metadata
- HTTP/SSE transport
- credential-store contract
- config export/import secrecy and credential-rebinding rejection
- request-history persistence/clear
- account deletion and manual-routing cleanup
- Local API/Web Admin auth and management endpoints
- Desktop detection and switch-capability fail-closed behavior

Run Windows tests locally:

```cmd
test
```

For the release/provider smoke-test flow, see `docs/SMOKE_TEST.md`. The included `smoke-api.ps1` checks `/health` and authenticated `/v1/models` without consuming provider quota; a real completion runs only when `-Completion` is supplied explicitly.

## CI policy

Normal pushes do not run build CI.

Build/test workflow triggers only on:

```text
workflow_dispatch
pull_request
```

GitHub build/test verification is **Linux-only**. Windows is validated locally with `test.cmd` as requested.

The package workflow is also Linux-only on GitHub Actions and runs manually or for `v*` tags. Development pushes therefore do not repeatedly consume Actions minutes.

`0.7.0` passed Linux Configure + Build + CTest on PR #3. `0.7.1` passed on PR #4. `0.8.3` passed on PR #8 before merge.

## Architecture

```text
                              routerAI
                                 |
          +----------------------+----------------------+
          |                      |                      |
          v                      v                      v
  Management TUI           Web Admin             Local /v1 API
          |                      |                      |
          +----------+-----------+-----------+----------+
                     |                       |
                     v                       v
               AccountManager          RoutingManager
                     |                       |
               Quota / Config          CompletionRouter
                     |                       |
          +----------+-----------+-----------+----------+
          |                      |                      |
          v                      v                      v
       Codex                Antigravity                Z.ai
    app-server             CLI / Gemini API          General API

          DesktopProfileManager / DesktopAccountSwitcher
                 -> detect/launch + supported future switch adapters
```

## 0.8.4 status

```text
[done] side-effect-free routing preview API with round-robin cursor protection
[done] Routing Health primary/backup failover preview for routing groups
[done] package versions derived from CMake instead of hard-coded release numbers
[done] Account priority editing from the TUI with validated persistence
[done] Quota Advisor for mode-aware consumer recommendations + manual selection
[done] interactive Usage Dashboard with refresh/filter/sort/scroll controls
[done] interactive Account Overview with per-account status/quota refresh
[done] best-effort bulk status/quota refresh with per-account failures isolated
[done] remove/enable/disable account controls
[done] secret-free config export/import + credential-rebinding hardening
[done] persistent request history / log viewer
[done] automatic request-log retention (10,000 rows / 30 days)
[done] Maintenance / Doctor diagnostics and repair actions
[done] routing-reference repair + orphan runtime cleanup
[done] improved usage dashboard
[done] local Windows package helper + Linux-only GitHub packaging
[done] embedded localhost Web Admin
[done] provider smoke-test guide + opt-in PowerShell smoke script
[done] desktop switching capability abstraction (fails closed when unsupported)

[verified] Linux Configure + Build + CTest through PR #8
[local verification] Windows test.cmd + package.cmd
[provider-dependent] actual Codex Desktop external account switching
[provider-dependent] actual Antigravity external profile switching
[optional] native macOS Keychain backend
```

## Design principles

Provider-specific authentication, quota and request behavior belongs inside provider adapters.

routerAI prefers documented provider interfaces and supported credential/runtime mechanisms. It does not depend on browser-session scraping, credential extraction from official clients, private endpoint reverse engineering, CAPTCHA/2FA bypass, or spoofing first-party applications.
