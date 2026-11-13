# Native Calendar Todo bootstrap entry point.
# The real installer lives inside the skill so imported skill packages remain self-contained.
$bootstrap = Join-Path $PSScriptRoot "skills\native-calendar-todo\scripts\bootstrap.ps1"
if (-not (Test-Path $bootstrap)) {
    Write-Error "bootstrap.ps1 not found: $bootstrap"
    exit 2
}
& powershell.exe -NoProfile -ExecutionPolicy Bypass -File $bootstrap @args
exit $LASTEXITCODE
