param([switch]$SelfTest, [switch]$PerturbSchedule, [switch]$CrashWriteLog,
      [switch]$Production, [switch]$LegacyBrowserJS)
$ErrorActionPreference = 'Stop'
& (Join-Path $PSScriptRoot 'build-x86_64.ps1') @PSBoundParameters
