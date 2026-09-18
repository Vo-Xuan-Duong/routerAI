# routerAI smoke test

Use this checklist after a release/maintenance update on Windows.

## 1. Local build and test

```cmd
git pull
test
package
run
```

Expected:

- CTest passes.
- `router.exe` opens the management TUI.
- Web Admin opens at `http://127.0.0.1:9000/admin`.
- `dist\routerAI-<version>-windows-x64.zip` is created by `package`.

## 2. Local API control-plane check

In routerAI:

```text
Web Admin
-> Show local API key
```

In another PowerShell window:

```powershell
$env:ROUTERAI_KEY="router-local-..."
.\smoke-api.ps1
```

This checks `GET /health` and authenticated `GET /v1/models`. It does not send a provider completion or consume provider quota.

## 3. Provider completion checks

Only add `-Completion` when you intentionally want to make a real provider request.

### Z.ai General API

```powershell
.\smoke-api.ps1 -Completion -Group zai-default -Model "<zai-model>"
```

### Antigravity Gemini API project

```powershell
.\smoke-api.ps1 -Completion -Group antigravity-api-default -Model "<antigravity-model>"
```

### Codex manual account

Select the account first in `codex-default`, then:

```powershell
.\smoke-api.ps1 -Completion -Group codex-default -Model "<codex-model>"
```

### Mixed routing

```powershell
.\smoke-api.ps1 -Completion -Group mixed-default -Model router/mixed-default -ZaiModel "<zai-model>" -AntigravityModel "<antigravity-model>"
```

## 4. Management checks

- disable an account and confirm it leaves automatic routing;
- re-enable it and confirm its previous auth/quota state remains;
- export config and confirm no provider key, OAuth token, local API key, or `credential_ref` exists in the JSON;
- inspect Request History after a completion;
- open Maintenance / Doctor and run request-log retention;
- if Doctor reports invalid routing references, run routing repair;
- remove only deliberately created orphan runtime folders.

## 5. Failure/failover check

For API-capable groups only, temporarily disable or misconfigure one test backend and verify another eligible API backend can serve the request. Restore the credential immediately after the test.

Consumer Codex/Antigravity subscription sessions remain manual and are not automatically cycled to bypass provider usage limits.
