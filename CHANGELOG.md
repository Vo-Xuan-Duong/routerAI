# Changelog

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

- GitHub Actions now run only for pull requests or explicit manual dispatch, not every push.
- Consumer Codex/Antigravity profiles remain manual and are separated from automatic API credential routing.
- Z.ai static metadata now includes documented GLM-5.2 support.

### Known boundaries

- Desktop account/profile switching remains user-controlled until providers expose stable supported external switching interfaces.
- Z.ai's documented OpenAPI specification does not currently expose a dedicated no-cost `/models` validation endpoint.
- Native cross-provider streaming failover is not attempted after bytes have been emitted; mixed groups use buffered SSE instead.
