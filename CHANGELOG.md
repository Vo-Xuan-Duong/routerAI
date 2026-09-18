# Changelog

## 0.8.6 - Windows x64 environment bootstrap

### Fixed

- Windows configure always activates the x64 MSVC environment, even when a generic Developer Command Prompt already exposes an x86 `cl.exe`.
- Incomplete or stale non-x64 CMake caches are discarded before reconfiguration.
- `build.cmd` now reconfigures when `build.ninja` is missing and activates the x64 MSVC environment before invoking Ninja.
- Prevents x86/x64 package-architecture mismatches such as 64-bit CURL being rejected by a 32-bit compiler context.

## 0.8.5 - Windows MSVC build

### Fixed

- Windows source builds no longer force the unsupported `x64-mingw-dynamic` cpp-httplib path.
- `configure.cmd` now locates Visual Studio 2022 / Build Tools through `vswhere`, activates the x64 MSVC environment, and configures Ninja with the `x64-windows` vcpkg triplet.
- Stale MinGW CMake caches are removed automatically when migrating an existing checkout.
- Windows builds explicitly target Windows 10 APIs required by the current cpp-httplib dependency.
- `run.cmd` and `package.cmd` now use the `x64-windows` runtime directory and no longer depend on MinGW runtime DLLs.

### CI

- Pull-request build verification now runs on both Linux and Windows.
- The Windows CI path exercises the same `configure.cmd`, `build.cmd`, and `test.cmd` helpers used by local development.

## 0.8.4 - Routing health

### Added

- Side-effect-free `RoutingManager::preview` APIs for inspecting a routing decision without consuming it.
- Routing Health TUI screen for all routing groups.
- Primary-candidate preview using the same selection rules as real routing.
- Automatic failover preview by excluding the primary candidate and evaluating the next eligible backend.
- Routing-group member diagnostics including status, priority, latest quota and cooldown state.
- Regression coverage proving round-robin previews do not advance the persistent cursor.

### Changed

- Version bumped to 0.8.4.
- Windows and Linux package filenames now derive their version from `CMakeLists.txt` instead of release-specific hard-coded strings.
- README package examples are version-neutral so release documentation does not drift.

### Routing boundary

- Automatic primary/backup preview remains limited to API-capable automatic groups.
- Manual Codex and Antigravity consumer groups remain operator-controlled; Routing Health does not auto-cycle consumer subscriptions.
- Previewing a round-robin group does not mutate its cursor or alter the next real request.

## 0.8.3 - Account priority controls

### Added

- Validated `AccountManager::setAccountPriority` lifecycle operation.
- Account Overview action to edit operator priority without changing config files or SQLite manually.
- Priority values in Account Overview and Quota Advisor recommendation labels.
- Dashboard priority-high-to-low sorting.
- Regression coverage for priority persistence, range rejection and recommendation tie-breaking.

### Behavior

- Supported priority range is `-100000..100000`, matching secret-free config import bounds.
- Higher values are preferred by the Priority routing strategy.
- Consumer recommendation order remains health, known/lower quota usage, then priority, then stable account ID.
- Changing priority does not alter credentials, auth state, quota snapshots or enabled state.

## 0.8.2 - Quota advisor

### Added

- Main TUI Quota Advisor for Codex subscription-runtime and Antigravity consumer-cli accounts.
- Provider + provider-mode account selection so recommendations do not mix consumer and API-project modes.
- Consumer status/quota refresh from the advisor using existing supported provider adapters.
- Operator action to apply a recommendation to the provider's manual default routing group.
- Regression coverage proving provider-mode recommendation filtering.

### Selection behavior

Recommendations rank only eligible enabled accounts outside cooldown and prefer:

1. READY over WARNING.
2. Accounts with known quota snapshots over unknown quota.
3. Lower latest normalized quota usage.
4. Higher operator priority.
5. Stable account ID ordering.

### Safety / provider boundary

- Recommendations do not automatically switch external desktop/browser sessions.
- Applying a recommendation changes routerAI's manual routing-group selection only.
- No cookies, OAuth state, client databases or credentials are copied between profiles.
- Quota refresh remains restricted to supported Codex runtime and Antigravity consumer CLI interfaces.

## 0.8.1 - Interactive dashboard

### Added

- In-place status refresh from the Usage Dashboard with the `r` key.
- In-place supported quota refresh with the `u` key.
- Provider filtering, health filtering and four dashboard sort modes.
- Up/Down scrolling for larger account sets and a visible filtered-row range.
- Dashboard activity feedback showing refresh counts and current filter/sort changes.
- One-key reset for dashboard view state.

### Changed

- Dashboard warning summary is now an `Attention` count covering every enabled non-ready account, not only the `WARNING` enum value.
- Usage Dashboard is now operational rather than a read-only snapshot.
- Version bumped to 0.8.1 and Windows package naming updated.

### Provider boundary

- Live quota refresh continues to call only existing supported quota readers.
- Unsupported provider/modes are counted as skipped; no private or guessed quota endpoints are introduced.

## 0.8.0 - Account overview

### Added

- Main TUI Account Overview with per-account status refresh and supported live quota refresh.
- Best-effort bulk account-status refresh that continues when an individual provider/account fails.
- Best-effort bulk quota refresh for providers/modes with supported machine-readable quota readers.
- Inline latest quota percentage in the selectable account list.
- Expanded account details with plan, latest quota, last error and cooldown information.

### Changed

- Main management menu now names the account screen `Account Overview` to reflect that it is an operational control surface rather than lifecycle-only controls.
- Version bumped to 0.8.0 and the local Windows package name updated accordingly.
- Provider-specific quota limitations remain explicit: unsupported provider/modes are skipped rather than guessed or scraped.

### Safety / behavior

- Bulk refresh isolates failures per account; a failed refresh does not stop remaining accounts.
- Refresh operations reuse the existing documented provider adapters and do not add browser-session scraping or private quota endpoints.
- Account enable/disable remains an operator routing switch and does not erase the last known auth/quota health.

## 0.7.1 - Maintenance release

### Added

- Automatic request-history retention with a default cap of 10,000 rows and 30 days.
- Main `Maintenance / Doctor` screen with database/request-log/runtime/routing diagnostics.
- Routing repair for stale account references and invalid manual selections.
- Explicit orphan runtime-folder cleanup under `.routerai/accounts`.
- Missing credential-reference and missing Codex-runtime diagnostics.
- `smoke-api.ps1` for local `/health`, authenticated `/v1/models`, and opt-in real completion checks.
- `docs/SMOKE_TEST.md` with Windows/provider smoke-test steps.
- Regression coverage for retention, routing repair, missing credentials and orphan runtime cleanup.

### Changed

- Version bumped to 0.7.1.
- Provider Console uses the generated project version instead of a hard-coded 0.6.0 label.
- Windows and Linux package names now use 0.7.1.
- Request-log auto-retention is best-effort so cleanup failure does not turn a successful provider response into an API failure.

### Safety / maintenance

- Runtime cleanup removes only direct orphan children of `.routerai/accounts`; it does not touch active account runtime paths.
- Request-history retention continues to store routing metadata only; prompt and successful completion bodies remain excluded.
- Provider completion smoke testing is opt-in with `-Completion` so the default smoke test does not consume provider quota.

### Verification

- PR #4 passed Linux Configure, Build and CTest before merge into `main`.
- GitHub Actions remains Linux-only.
- Windows validation remains local with `test.cmd` and `package.cmd`.

## 0.7.0 - Management release

### Added

- New management control-plane TUI as the default routerAI screen.
- Usage Dashboard with account/provider counts, latest quota bars and request success rate.
- Account Controls for enable, disable and permanent local removal.
- Secret-free configuration export/import for accounts and routing groups.
- Persistent request history containing routing metadata, HTTP/provider status, latency, stream flag and bounded errors; prompts and successful response bodies are not logged.
- Embedded Web Admin at `http://127.0.0.1:9000/admin`.
- Authenticated Web Admin APIs for overview, account controls, request history and config transfer.
- Web Admin provider setup for Codex, Antigravity consumer/API-project modes and Z.ai General/Coding Plan modes.
- Web Admin browser-login, account refresh and API-key configuration/reconfiguration actions.
- Web Admin usage dashboard, account controls, request log viewer and JSON config editor/download.
- Desktop account-switch capability abstraction that fails closed until a supported provider adapter exists.
- CPack install/package metadata.
- `package.cmd` for local Windows portable ZIP assembly, including vcpkg and discovered MinGW runtime DLLs.
- Linux-only GitHub `package` workflow producing a portable Linux artifact and creating a GitHub Release for `v*` tags.
- Management feature tests for config secrecy, provider/mode rebinding rejection, unsafe identifier/provider rejection, request history and account deletion/manual-routing cleanup.

### Changed

- Version bumped to 0.7.0.
- Provider Console now lives behind the management control plane and retains the existing provider/login/quota/routing TUI.
- Account `enabled` state is treated as an operator routing switch and no longer overwrites provider auth/quota health.
- Removed accounts are also removed from routing memberships and manual group selections; request history remains as an audit trail.
- GitHub build/test CI is Linux-only; Windows validation and packaging are performed locally with `test.cmd` and `package.cmd`.
- CI and packaging pin the vcpkg revision instead of following upstream `main` on each run.

### Security / privacy

- Exported config never contains provider secrets or `credential_ref` values.
- Imported config cannot set local credential references and uses restricted account/group/provider identifiers.
- Existing accounts cannot change `provider` or `provider_mode` through config import, preventing an existing local credential reference from being rebound to another provider adapter.
- Web Admin mutation/provider-setup endpoints require the same local Bearer key as the OpenAI-compatible API.
- Provider keys entered in Web Admin are sent only to the localhost router process and stored through `CredentialStore`.
- Request history deliberately excludes prompt bodies and successful completion bodies.
- Desktop switching does not copy cookies, OAuth tokens, client profile databases, or OS keyring entries.

### Verification

- PR #3 passed Linux Configure, Build and CTest before merge into `main`.
- Windows verification is intentionally local rather than consuming GitHub Actions minutes.

### Provider-dependent boundary

- Codex Desktop and Antigravity Desktop switching remain unavailable in this build because no supported external switch adapter is registered. The TUI and adapter boundary are ready for a future provider-supported interface.

## 0.6.0 - Release candidate

### Added

- Full-screen FTXUI control plane.
- Provider-first account setup for Codex, Google Antigravity, and Z.ai.
- Isolated Codex account runtimes and managed Codex bootstrap.
- Codex account/profile/quota integration through the official runtime.
- Antigravity consumer CLI quota/session management.
- Antigravity Gemini API project routing through the managed Antigravity agent.
- Z.ai General API routing plus separate Coding Plan metadata.
- Persistent routing groups with health-first, least-used, priority, round-robin, and manual strategies.
- Automatic API-backend cooldown/failover for supported API/project credentials.
- Local OpenAI-compatible `/v1/chat/completions` API and `/v1/models` routing-group discovery.
- Native streaming for Codex, Z.ai, and Antigravity API groups; buffered SSE fallback for mixed cross-provider routing.
- CredentialStore with Windows DPAPI, optional Linux Secret Service, and user-only POSIX fallback files.
- Local API key generation and rotation.
- Desktop application detection/launch helpers.
- Windows `configure.cmd`, `build.cmd`, `test.cmd`, and `run.cmd` helpers.
- Expanded CTest coverage for storage, selection, routing, streaming, credentials, local API, and desktop detection.

### Changed

- GitHub Actions run only for pull requests or explicit manual dispatch, not every push.
- Consumer Codex/Antigravity profiles remain manual and are separated from automatic API credential routing.
- Z.ai static metadata includes documented GLM-5.2 support.

### Known boundaries

- Desktop account/profile switching remains user-controlled until providers expose stable supported external switching interfaces.
- Z.ai's documented OpenAPI specification does not expose a dedicated no-cost `/models` validation endpoint.
- Native cross-provider streaming failover is not attempted after bytes have been emitted; mixed groups use buffered SSE instead.
