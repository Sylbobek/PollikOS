param([ValidateSet('i386','x86_64')][string]$Target='x86_64',[switch]$Extended)
$ErrorActionPreference='Stop'
$ciRoot=Split-Path -Parent $PSScriptRoot
Push-Location $ciRoot
try {
    New-Item -ItemType Directory -Force build/ci | Out-Null
    function Run-Check([string]$Name,[string]$Program,[string[]]$Arguments) {
        Write-Host "CHECK $Name"
        $ciLog="build/ci/$Name.log"
        try {
            if($Program.EndsWith('.ps1')) {
                & powershell -NoProfile -File $Program @Arguments *> $ciLog
            } else { & $Program @Arguments *> $ciLog }
            if($LASTEXITCODE -ne 0) { throw "$Name failed ($LASTEXITCODE)" }
            Get-Content -LiteralPath $ciLog -Tail 4
        } catch {
            if(Test-Path -LiteralPath $ciLog) { Get-Content -LiteralPath $ciLog -Tail 80 }
            throw
        }
    }
    Run-Check 'shared-security-journal' python @('tests/security_account_native.py')
    Run-Check 'shared-read' python @('tests/pollikfs_read_native.py')
    Run-Check 'host-journal' python @('tests/host_journal.py')
    Run-Check 'utf8-ui' python @('tests/utf8_ui_native.py')
    if($Target -eq 'i386') {
        Run-Check 'build-i386' ./build.ps1 @('-NoSync')
        Run-Check 'process-i386' python @('tests/process_stress.py','--ram','256','--timeout','120')
        if($Extended) { Run-Check 'account-i386' python @('tests/i386_security_account.py') }
    } else {
        Run-Check 'build-x64-selftest' ./build-x86_64.ps1 @('-SelfTest')
        Run-Check 'guest-x64-selftest' python @('tests/kernel_checkpoint.py')
        Run-Check 'build-x64' ./build-x86_64.ps1 @()
        Run-Check 'terminal-x64' python @('tests/terminal_commands_x64.py')
        Run-Check 'web-engine-x64' python @('tests/web_engine_x64.py')
        Run-Check 'apps-x64' python @('tests/x86_64_app_checkpoint.py')
        $previousVariant=$env:POLLIK_X64_VARIANT
        try {
            $env:POLLIK_X64_VARIANT='kernel'
            Run-Check 'browser-x64' python @('tests/browser_images_x64.py')
        } finally { $env:POLLIK_X64_VARIANT=$previousVariant }
        if($Extended) {
            Run-Check 'session-x64' python @('tests/x86_64_security_session.py')
            Run-Check 'network-x64' python @('tests/network_clients_x64.py')
            Run-Check 'console-x64' python @('tests/x86_64_console.py','--timeout','300')
        }
    }
    Write-Host "PASS CI commands: $Target"
} finally { Pop-Location }
