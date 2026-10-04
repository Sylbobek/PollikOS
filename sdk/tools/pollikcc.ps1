# pollikcc: PollikOS C application compiler driver (cross-hosted, Windows-first).
#
# Usage:
#   pollikcc.ps1 hello.c -o hello.elf
#   pollikcc.ps1 -c utils.c
#   pollikcc.ps1 main.o utils.o -L. -lexample -o program.elf
#   pollikcc.ps1 multi/main.c multi/utils.c -O0 -g -o multi.elf
#
# The driver only invokes the host LLVM/NASM toolchain with the PollikOS SDK
# configuration; it never links a host CRT, host libc or dynamic loader.
$ErrorActionPreference = 'Stop'
# Deliberately a simple script: options like -o/-O2 must reach this driver
# unchanged instead of being bound as PowerShell common parameters.
$Arguments = @($args)
. (Join-Path $PSScriptRoot 'pollikos_sdk.ps1')

function Show-PollikccUsage {
    @'
pollikcc - PollikOS C compiler driver

usage: pollikcc.ps1 [options] <inputs...>

inputs: *.c sources, *.o objects, *.a static archives

options:
  -o <file>        output file (default: <first-input>.elf, or .o with -c)
  -c               compile to objects only
  -O0 -O1 -O2 -O3 -Os   optimization level (default -O2)
  -g               keep ELF debug symbols
  -D<name>[=value] define a preprocessor symbol
  -I<dir>          add an include directory
  -L<dir>          add a static library search directory
  -l<name>         link lib<name>.a from -L directories
  --runtime <dir>  SDK runtime directory (default build/x86_64/sdk)
  --rebuild-runtime  force rebuilding crt0/libpollikc.a first
  --allow-fpu      compatibility option (scalar float/double is enabled by default)
  --allow-simd     allow automatic SIMD vectorization (SSE2; AVX stays disabled)
  --small-code-model  emit TinyCC-compatible conventional ELF sections
  --verbose        print every tool invocation
  --version        print the PollikOS SDK version
  -h, --help       print this help
'@ | Write-Host
}

$optimization = '-O2'
$compileOnly = $false
$debug = $false
$output = $null
$runtimeDir = $null
$rebuildRuntime = $false
$allowFpu = $false
$allowSimd = $false
$codeModel = 'large'
$verbose = $false
$sources = @()
$objects = @()
$archives = @()
$libraryNames = @()
$libraryDirs = @()
$includeDirs = @()
$defines = @()
$index = 0
while ($index -lt $Arguments.Count) {
    $argument = $Arguments[$index]
    if ($argument -eq '-h' -or $argument -eq '--help') { Show-PollikccUsage; exit 0 }
    elseif ($argument -eq '--version') { Write-Host "pollikcc $(Get-PollikosSdkVersion) (host cross-build)"; exit 0 }
    elseif ($argument -eq '-o') { $index++; if ($index -ge $Arguments.Count) { throw 'pollikcc: -o needs a path' }; $output = $Arguments[$index] }
    elseif ($argument -eq '-c') { $compileOnly = $true }
    elseif ($argument -eq '-g') { $debug = $true }
    elseif ($argument -match '^-O[0-3s]$') { $optimization = $argument }
    elseif ($argument -eq '--runtime') { $index++; if ($index -ge $Arguments.Count) { throw 'pollikcc: --runtime needs a path' }; $runtimeDir = $Arguments[$index] }
    elseif ($argument -eq '--rebuild-runtime') { $rebuildRuntime = $true }
    elseif ($argument -eq '--allow-fpu') { $allowFpu = $true }
    elseif ($argument -eq '--allow-simd') { $allowSimd = $true }
    elseif ($argument -eq '--small-code-model') { $codeModel = 'small' }
    elseif ($argument -eq '--verbose') { $verbose = $true }
    elseif ($argument -ceq '-D' -or $argument -ceq '-I' -or $argument -ceq '-L' -or $argument -ceq '-l') {
        $index++
        if ($index -ge $Arguments.Count) { throw "pollikcc: $argument needs a value" }
        if ($argument -ceq '-D') { $defines += $Arguments[$index] }
        elseif ($argument -ceq '-I') { $includeDirs += $Arguments[$index] }
        elseif ($argument -ceq '-L') { $libraryDirs += $Arguments[$index] }
        else { $libraryNames += $Arguments[$index] }
    }
    elseif ($argument -cmatch '^-D(.+)$') { $defines += $Matches[1] }
    elseif ($argument -cmatch '^-I(.+)$') { $includeDirs += $Matches[1] }
    elseif ($argument -cmatch '^-L(.+)$') { $libraryDirs += $Matches[1] }
    elseif ($argument -cmatch '^-l(.+)$') { $libraryNames += $Matches[1] }
    elseif ($argument.StartsWith('-')) { throw "pollikcc: unknown option $argument" }
    else {
        switch ([IO.Path]::GetExtension($argument).ToLowerInvariant()) {
            '.c' { $sources += $argument }
            '.o' { $objects += $argument }
            '.a' { $archives += $argument }
            default { throw "pollikcc: unsupported input $argument (expected .c, .o or .a)" }
        }
    }
    $index++
}
if (-not $sources -and -not $objects -and -not $archives) { Show-PollikccUsage; throw 'pollikcc: no inputs' }
foreach ($source in $sources) { if (-not (Test-Path -LiteralPath $source)) { throw "pollikcc: missing source $source" } }
foreach ($input in @($objects) + @($archives)) { if (-not (Test-Path -LiteralPath $input)) { throw "pollikcc: missing input $input" } }

$runtimeDir = Ensure-PollikosRuntime -RuntimeDir $runtimeDir -Force:$rebuildRuntime

if ($compileOnly) {
    $targets = @()
    foreach ($source in $sources) {
        $target = if ($output -and $sources.Count -eq 1) { $output }
                  else { [IO.Path]::GetFileNameWithoutExtension($source) + '.o' }
        $targets += $target
        if ($verbose) { Write-Host "clang -c $source -o $target" }
        Compile-PollikosObject -Source $source -Object $target -RuntimeDir $runtimeDir `
            -Optimization $optimization -KeepDebugInfo:$debug -AllowFpu:$allowFpu -AllowSimd:$allowSimd -CodeModel $codeModel -ExtraIncludes $includeDirs -Defines $defines
        Write-Host "pollikcc: compiled $target"
    }
    exit 0
}

if (-not $output) {
    $stem = if ($sources.Count) { [IO.Path]::GetFileNameWithoutExtension($sources[0]) }
            elseif ($objects.Count) { [IO.Path]::GetFileNameWithoutExtension($objects[0]) }
            else { 'program' }
    $output = "$stem.elf"
}
$temporary = Join-Path ([IO.Path]::GetTempPath()) ("pollikcc-" + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Force $temporary | Out-Null
try {
    $linkObjects = @()
    foreach ($source in $sources) {
        $object = Join-Path $temporary (([IO.Path]::GetFileNameWithoutExtension($source)) + '.o')
        if ($verbose) { Write-Host "clang -c $source -o $object" }
        Compile-PollikosObject -Source $source -Object $object -RuntimeDir $runtimeDir `
            -Optimization $optimization -KeepDebugInfo:$debug -AllowFpu:$allowFpu -AllowSimd:$allowSimd -CodeModel $codeModel -ExtraIncludes $includeDirs -Defines $defines
        $linkObjects += $object
    }
    $linkObjects += $objects
    $libraries = @()
    foreach ($name in $libraryNames) {
        $found = $null
        foreach ($directory in @($libraryDirs + @('.'))) {
            $candidate = Join-Path $directory "lib$name.a"
            if (Test-Path -LiteralPath $candidate) { $found = (Resolve-Path -LiteralPath $candidate).Path; break }
        }
        if (-not $found) { throw "pollikcc: cannot find lib$name.a in $($libraryDirs -join ', ')" }
        $libraries += $found
    }
    $libraries += $archives
    if ($verbose) { Write-Host "ld.lld -T sdk/linker/pollik.ld <crt0> <objects> <libs> -o $output" }
    Invoke-PollikosLink -Objects $linkObjects -Output $output -RuntimeDir $runtimeDir -Libraries $libraries
    [void](Test-PollikosElf -Path $output)
    $size = (Get-Item -LiteralPath $output).Length
    Write-Host "pollikcc: built $output ($size bytes, $optimization, SDK $(Get-PollikosSdkVersion))"
} finally {
    Remove-Item -LiteralPath $temporary -Recurse -Force -ErrorAction SilentlyContinue
}
