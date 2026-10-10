param([ValidateSet('tcg','whpx')][string]$Accel='tcg', [switch]$Headless,
      [switch]$NoLaunch, [string]$DataImage='', [switch]$NoNetwork,
      [switch]$Audio, [switch]$PersistData, [int]$MemoryMiB=8192,
      [ValidateSet('system','kernel')][string]$Variant='system')
$ErrorActionPreference = 'Stop'
& (Join-Path $PSScriptRoot 'run-x86_64.ps1') @PSBoundParameters
