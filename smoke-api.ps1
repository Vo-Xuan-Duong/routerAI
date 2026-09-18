param(
    [string]$BaseUrl = "http://127.0.0.1:9000",
    [string]$Key = $env:ROUTERAI_KEY,
    [switch]$Completion,
    [string]$Group = "mixed-default",
    [string]$Model = "router/mixed-default",
    [string]$ZaiModel = "",
    [string]$AntigravityModel = "",
    [string]$Prompt = "Reply with exactly: routerAI smoke OK"
)

$ErrorActionPreference = "Stop"

function Write-Check([string]$Name, [bool]$Ok, [string]$Detail = "") {
    $state = if ($Ok) { "PASS" } else { "FAIL" }
    $suffix = if ([string]::IsNullOrWhiteSpace($Detail)) { "" } else { " - $Detail" }
    Write-Host ("[{0}] {1}{2}" -f $state, $Name, $suffix)
}

try {
    $health = Invoke-RestMethod -Method Get -Uri "$BaseUrl/health" -TimeoutSec 5
    Write-Check "GET /health" $true ($health.status)
} catch {
    Write-Check "GET /health" $false $_.Exception.Message
    Write-Host "Start routerAI first with: run"
    exit 1
}

if ([string]::IsNullOrWhiteSpace($Key)) {
    Write-Host ""
    Write-Host "ROUTERAI_KEY is not set."
    Write-Host "Open routerAI -> Web Admin -> Show local API key, then:"
    Write-Host '  $env:ROUTERAI_KEY="router-local-..."'
    exit 2
}

$headers = @{ Authorization = "Bearer $Key" }

try {
    $models = Invoke-RestMethod -Method Get -Uri "$BaseUrl/v1/models" -Headers $headers -TimeoutSec 10
    $count = if ($null -eq $models.data) { 0 } else { @($models.data).Count }
    Write-Check "GET /v1/models" $true ("$count routable model/group entries")
} catch {
    Write-Check "GET /v1/models" $false $_.Exception.Message
    exit 3
}

if (-not $Completion) {
    Write-Host ""
    Write-Host "Control-plane smoke test passed."
    Write-Host "No provider completion was sent. Add -Completion to spend provider quota intentionally."
    exit 0
}

$router = @{ group = $Group; models = @{} }
if (-not [string]::IsNullOrWhiteSpace($ZaiModel)) { $router.models["zai"] = $ZaiModel }
if (-not [string]::IsNullOrWhiteSpace($AntigravityModel)) { $router.models["antigravity"] = $AntigravityModel }

$body = @{
    model = $Model
    messages = @(@{ role = "user"; content = $Prompt })
    stream = $false
    router = $router
} | ConvertTo-Json -Depth 8

$headers["X-Router-Group"] = $Group

try {
    $started = Get-Date
    $response = Invoke-RestMethod -Method Post -Uri "$BaseUrl/v1/chat/completions" -Headers $headers -ContentType "application/json" -Body $body -TimeoutSec 120
    $elapsed = [math]::Round(((Get-Date) - $started).TotalMilliseconds)
    $content = $response.choices[0].message.content
    Write-Check "POST /v1/chat/completions" $true ("group=$Group, ${elapsed}ms")
    Write-Host ""
    Write-Host "Assistant response:"
    Write-Host $content
} catch {
    Write-Check "POST /v1/chat/completions" $false $_.Exception.Message
    exit 4
}
