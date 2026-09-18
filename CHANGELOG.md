# Changelog

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
