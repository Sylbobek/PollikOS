param([ValidateSet('x86_64')][string]$Target='x86_64', [switch]$Extended)
$ErrorActionPreference='Stop'
Push-Location (Split-Path -Parent $PSScriptRoot)
try {
    New-Item -ItemType Directory -Force build/ci | Out-Null
    function Run-Check([string]$Name,[string]$Program,[string[]]$Arguments) {
        Write-Host "CHECK $Name"
        & python tools/run_check.py --log "build/ci/$Name.log" -- $Program @Arguments
        if ($LASTEXITCODE -ne 0) { throw "$Name failed ($LASTEXITCODE)" }
    }
    Run-Check 'x64-only' python @('tests/x64_only.py')
    Run-Check 'shared-security-journal' python @('tests/security_account_native.py')
    Run-Check 'shared-read' python @('tests/pollikfs_read_native.py')
    Run-Check 'host-journal' python @('tests/host_journal.py')
    Run-Check 'utf8-ui' python @('tests/utf8_ui_native.py')
    Run-Check 'build-x64-selftest' ./build.ps1 @('-SelfTest')
    Run-Check 'guest-x64-selftest' python @('tests/kernel_checkpoint.py')
    Run-Check 'build-x64' ./build.ps1 @()
    Run-Check 'storage-x64' python @('tests/x86_64_storage.py')
    Run-Check 'terminal-x64' python @('tests/terminal_commands_x64.py')
    Run-Check 'web-engine-x64' python @('tests/web_engine_x64.py')
    Run-Check 'apps-x64' python @('tests/x86_64_app_checkpoint.py')
    $previousVariant=$env:POLLIK_X64_VARIANT
    try {
        $env:POLLIK_X64_VARIANT='kernel'
        Run-Check 'browser-x64' python @('tests/browser_images_x64.py')
    } finally { $env:POLLIK_X64_VARIANT=$previousVariant }
    Run-Check 'build-production' ./build.ps1 @('-Production')
    if ($Extended) {
        Run-Check 'boot-full-x64' python @('tests/x86_64_boot.py')
        Run-Check 'session-x64' python @('tests/x86_64_security_session.py')
        Run-Check 'network-x64' python @('tests/network_clients_x64.py')
        Run-Check 'console-x64' python @('tests/x86_64_console.py','--timeout','300')
        Run-Check 'usb-x64' python @('tests/usb_input_x64_guest.py')
    }
    Write-Host 'PASS CI commands: x86_64'
} finally { Pop-Location }
