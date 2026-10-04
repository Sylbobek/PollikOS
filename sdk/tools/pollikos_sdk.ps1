# PollikOS C SDK host configuration (canonical compiler/linker settings).
# Dot-source this file; it never runs at load time and defines only helpers.
# Tested with PowerShell 5.1 and the LLVM/NASM tools used by build-x86_64.ps1.
$script:PollikosSdkRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)

function Get-PollikosSdkRoot { return $script:PollikosSdkRoot }

function Get-PollikosSdkVersion {
    $versionFile = Join-Path $script:PollikosSdkRoot 'sdk/VERSION'
    if (Test-Path -LiteralPath $versionFile) { (Get-Content -LiteralPath $versionFile -Raw).Trim() }
    else { '0.0.0' }
}

function New-PollikosAbiHeader {
    param([Parameter(Mandatory=$true)][string]$IncludeDir)
    $target = Join-Path $IncludeDir 'pollikos/abi_numbers.h'
    New-Item -ItemType Directory -Force (Split-Path -Parent $target) | Out-Null
    $kernelAbi = Join-Path $script:PollikosSdkRoot 'kernel/arch/x86_64/user_abi.h'
    $header = Get-Content -LiteralPath $kernelAbi | ForEach-Object {
        if ($_ -match '^#define (\w+) (0x[0-9a-fA-F]+|[0-9]+)$') { "#define $($matches[1]) $($matches[2])" }
    }
    Set-Content -LiteralPath $target -Value $header -Encoding ascii
}

# Sanitized freestanding flags. Applications may use the x86-64 baseline FPU
# and SSE2; per-process state is saved with FXSAVE64. AVX remains disabled.
function Get-PollikosCFlags {
    param(
        [string]$Optimization = '-O2',
        [switch]$KeepDebugInfo,
        [string]$IncludeDir,
        [string]$CodeModel = 'large',
        [string[]]$ExtraIncludes = @(),
        [string[]]$Defines = @()
    )
    $flags = @(
        '--target=x86_64-none-elf',
        '-ffreestanding', '-fno-builtin', '-fno-pic', '-fno-pie',
        '-fno-stack-protector', '-mno-red-zone', '-msse2', '-mno-avx',
        '-fno-vectorize', '-fno-slp-vectorize',
        "-mcmodel=$CodeModel", $Optimization,
        '-Wall', '-Wextra', '-Werror',
        "-I$(Join-Path $script:PollikosSdkRoot 'sdk/include')",
        "-I$(Join-Path $script:PollikosSdkRoot 'include')"
    )
    if ($KeepDebugInfo) { $flags += '-g' }
    if ($IncludeDir) { $flags += "-I$IncludeDir" }
    foreach ($include in $ExtraIncludes) { if ($include) { $flags += "-I$include" } }
    foreach ($define in $Defines) { if ($define) { $flags += "-D$define" } }
    return $flags
}

function Ensure-PollikosRuntime {
    param([string]$RuntimeDir, [switch]$Force)
    if (-not $RuntimeDir) { $RuntimeDir = Join-Path $script:PollikosSdkRoot 'build/x86_64/sdk' }
    $repo = $script:PollikosSdkRoot
    $crt0 = Join-Path $RuntimeDir 'crt0.o'
    $archive = Join-Path $RuntimeDir 'libpollikc.a'
    $includeDir = Join-Path $RuntimeDir 'include'
    $sources = @(Get-ChildItem -LiteralPath (Join-Path $repo 'sdk/lib') -Filter '*.c' |
        ForEach-Object { $_.FullName } | Sort-Object)
    $crtSource = Join-Path $repo 'sdk/crt/crt0.asm'
    $abiSource = Join-Path $repo 'kernel/arch/x86_64/user_abi.h'
    $abiInfoSource = Join-Path $repo 'include/pollikos_abi.h'
    $stale = $Force -or -not (Test-Path -LiteralPath $crt0) -or -not (Test-Path -LiteralPath $archive) `
        -or -not (Test-Path -LiteralPath (Join-Path $includeDir 'pollikos/abi_numbers.h')) `
        -or -not (Test-Path -LiteralPath (Join-Path $includeDir 'pollikos_abi.h'))
    if (-not $stale) {
        $stamp = (Get-Item -LiteralPath $archive).LastWriteTimeUtc
        foreach ($input in @($crtSource, $abiSource, $abiInfoSource) + $sources) {
            if ((Get-Item -LiteralPath $input).LastWriteTimeUtc -gt $stamp) { $stale = $true; break }
        }
    }
    if (-not $stale) { return $RuntimeDir }
    Write-Host "pollikos-sdk: building runtime (crt0 + libpollikc.a) in $RuntimeDir"
    New-PollikosAbiHeader -IncludeDir $includeDir
    Copy-Item -LiteralPath $abiInfoSource -Destination (Join-Path $includeDir 'pollikos_abi.h') -Force
    New-Item -ItemType Directory -Force (Join-Path $RuntimeDir 'libc') | Out-Null
    $flags = Get-PollikosCFlags -Optimization '-O2' -IncludeDir $includeDir
    $objects = @()
    foreach ($source in $sources) {
        $name = [IO.Path]::GetFileNameWithoutExtension($source)
        $object = Join-Path (Join-Path $RuntimeDir 'libc') ($name + '.o')
        & clang @($flags + @('-c', $source, '-o', $object))
        if ($LASTEXITCODE -ne 0) { throw "pollikos-sdk: clang failed for $source" }
        $objects += $object
    }
    & llvm-ar @(@('rcs', $archive) + $objects)
    if ($LASTEXITCODE -ne 0) { throw 'pollikos-sdk: llvm-ar failed' }
    & nasm @('-f', 'elf64', $crtSource, '-o', $crt0)
    if ($LASTEXITCODE -ne 0) { throw 'pollikos-sdk: nasm failed for crt0.asm' }
    return $RuntimeDir
}

function Compile-PollikosObject {
    param(
        [Parameter(Mandatory=$true)][string]$Source,
        [Parameter(Mandatory=$true)][string]$Object,
        [Parameter(Mandatory=$true)][string]$RuntimeDir,
        [string]$Optimization = '-O2',
        [switch]$KeepDebugInfo,
        [switch]$AllowFpu,
        [switch]$AllowSimd,
        [string]$CodeModel = 'large',
        [string[]]$ExtraIncludes = @(),
        [string[]]$Defines = @()
    )
    $flags = Get-PollikosCFlags -Optimization $Optimization -KeepDebugInfo:$KeepDebugInfo `
        -IncludeDir (Join-Path $RuntimeDir 'include') -CodeModel $CodeModel -ExtraIncludes $ExtraIncludes -Defines $Defines
    if ($AllowSimd) {
        $flags = @($flags | Where-Object { $_ -ne '-fno-vectorize' -and $_ -ne '-fno-slp-vectorize' })
    }
    & clang @($flags + @('-c', $Source, '-o', $Object))
    if ($LASTEXITCODE -ne 0) { throw "pollikcc: clang failed for $Source" }
}

function Invoke-PollikosLink {
    param(
        [Parameter(Mandatory=$true)][string[]]$Objects,
        [Parameter(Mandatory=$true)][string]$Output,
        [Parameter(Mandatory=$true)][string]$RuntimeDir,
        [string[]]$Libraries = @()
    )
    $crt0 = Join-Path $RuntimeDir 'crt0.o'
    $archive = Join-Path $RuntimeDir 'libpollikc.a'
    $linker = Join-Path $script:PollikosSdkRoot 'sdk/linker/pollik.ld'
    $inputs = @($crt0) + $Objects + $Libraries + @($archive)
    & ld.lld @(@('-m', 'elf_x86_64', '-T', $linker) + $inputs + @('-o', $Output))
    if ($LASTEXITCODE -ne 0) { throw "pollikcc: link failed for $Output" }
}

# Verifies a built application is a static PollikOS ELF64 with no host runtime
# dependency, no interpreter/dynamic table and a defined _start entry.
function Test-PollikosElf {
    param([Parameter(Mandatory=$true)][string]$Path)
    $undefined = & llvm-nm --undefined-only $Path
    if ($undefined) { throw "pollikcc: undefined symbols in $Path`: $undefined" }
    $defined = & llvm-nm --defined-only $Path
    if (-not ($defined | Select-String -Pattern '\bT _start$')) {
        throw "pollikcc: $Path does not define the crt0 _start entry symbol"
    }
    $headers = & llvm-readobj --file-headers --program-headers --dynamic-table $Path 2>&1
    if (-not ($headers -match 'Type: (Executable|EXEC)\b')) { throw "pollikcc: $Path is not a static ET_EXEC" }
    if ($headers -match 'PT_INTERP|PT_DYNAMIC|NEEDED') {
        throw "pollikcc: $Path depends on a dynamic loader or host library"
    }
    if (-not ($headers -match 'EM_X86_64')) { throw "pollikcc: $Path is not x86_64" }
    return $true
}
