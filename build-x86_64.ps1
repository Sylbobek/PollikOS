param([switch]$SelfTest, [switch]$PerturbSchedule, [switch]$CrashWriteLog, [switch]$Production)
$ErrorActionPreference = 'Stop'
if($Production -and ($SelfTest -or $CrashWriteLog -or $PerturbSchedule)){throw '-Production is a separate release variant'}
if ($PerturbSchedule -and !$SelfTest) { throw '-PerturbSchedule requires -SelfTest' }
if ($CrashWriteLog -and $SelfTest) { throw '-CrashWriteLog is a normal-kernel instrumentation build' }
Push-Location $PSScriptRoot
try {
    # Separate artifacts; never open the desktop image or PollikData.img.
    $variant = if ($Production) { 'system' } elseif ($SelfTest) { 'selftest' } else { 'kernel' }
    $output = "build/x86_64/$variant"
    New-Item -ItemType Directory -Force $output | Out-Null
    function Invoke-Checked { param([string]$Program, [string[]]$Arguments)
        & $Program @Arguments
        if ($LASTEXITCODE -ne 0) { throw "$Program failed ($LASTEXITCODE)" }
    }
    $defines = @('-DPOLLIK_X64=1')
    if ($SelfTest) { $defines += '-DSELFTEST=1' }
    if ($Production) { $defines += '-DPRODUCTION=1' }
    $productionCompileFlags = if($Production){@('-ffunction-sections','-fdata-sections')}else{@()}
    if ($PerturbSchedule) { $defines += '-DPOLLIK_TEST_TIMER_HZ=137' }
    $abi = Get-Content kernel/arch/x86_64/user_abi.h | ForEach-Object {
        if ($_ -match '^#define (\w+) (0x[0-9a-fA-F]+|[0-9]+)$') { "%define $($matches[1]) $($matches[2])" }
    }
    Set-Content -LiteralPath "$output/user_abi.inc" -Value $abi -Encoding ascii
    # The C userspace ABI header is generated from the same kernel source of
    # truth as the NASM include, so operation numbers can never drift.
    $abiHeader = Get-Content kernel/arch/x86_64/user_abi.h | ForEach-Object {
        if ($_ -match '^#define (\w+) (0x[0-9a-fA-F]+|[0-9]+)$') { "#define $($matches[1]) $($matches[2])" }
    }
    $userOutput = "$output/userspace"
    New-Item -ItemType Directory -Force $userOutput | Out-Null
    New-Item -ItemType Directory -Force "$userOutput/include/pollikos" | Out-Null
    Set-Content -LiteralPath "$userOutput/include/pollikos/abi_numbers.h" -Value $abiHeader -Encoding ascii
    Invoke-Checked nasm @('-f','elf64',"-I$output/",'apps/x86_64/hello.asm','-o',"$userOutput/hello.o")
    Invoke-Checked ld.lld @('-m','elf_x86_64','-T','sdk/linker/pollik.ld',"$userOutput/hello.o",'-o',"$userOutput/hello.elf")
    Invoke-Checked nasm @('-f','elf64',"-I$output/",'apps/x86_64/schedule.asm','-o',"$userOutput/schedule.o")
    Invoke-Checked ld.lld @('-m','elf_x86_64','-T','sdk/linker/pollik.ld',"$userOutput/schedule.o",'-o',"$userOutput/schedule.elf")
    Invoke-Checked nasm @('-f','elf64',"-I$output/",'-DARGV_TEST=1','apps/x86_64/hello.asm','-o',"$userOutput/argvtest.o")
    Invoke-Checked ld.lld @('-m','elf_x86_64','-T','sdk/linker/pollik.ld',"$userOutput/argvtest.o",'-o',"$userOutput/argvtest.elf")
    Invoke-Checked nasm @('-f','elf64',"-I$output/",'-DFAULT_TEST=1','apps/x86_64/schedule.asm','-o',"$userOutput/faulttest.o")
    Invoke-Checked ld.lld @('-m','elf_x86_64','-T','sdk/linker/pollik.ld',"$userOutput/faulttest.o",'-o',"$userOutput/faulttest.elf")
    Invoke-Checked nasm @('-f','elf64',"-I$output/",'apps/x86_64/readtest.asm','-o',"$userOutput/readtest.o")
    Invoke-Checked ld.lld @('-m','elf_x86_64','-T','sdk/linker/pollik.ld',"$userOutput/readtest.o",'-o',"$userOutput/readtest.elf")
    Invoke-Checked nasm @('-f','elf64',"-I$output/",'apps/x86_64/stattest.asm','-o',"$userOutput/stattest.o")
    Invoke-Checked ld.lld @('-m','elf_x86_64','-T','sdk/linker/pollik.ld',"$userOutput/stattest.o",'-o',"$userOutput/stattest.elf")
    Invoke-Checked nasm @('-f','elf64',"-I$output/",'apps/x86_64/dirtest.asm','-o',"$userOutput/dirtest.o")
    Invoke-Checked ld.lld @('-m','elf_x86_64','-T','sdk/linker/pollik.ld',"$userOutput/dirtest.o",'-o',"$userOutput/dirtest.elf")
    Invoke-Checked nasm @('-f','elf64',"-I$output/",'apps/x86_64/runtime.asm','-o',"$userOutput/runtime.o")
    Invoke-Checked ld.lld @('-m','elf_x86_64','-T','sdk/linker/pollik.ld',"$userOutput/runtime.o",'-o',"$userOutput/runtime.elf")
    Invoke-Checked nasm @('-f','elf64',"-I$output/",'apps/x86_64/memtest.asm','-o',"$userOutput/memtest.o")
    Invoke-Checked ld.lld @('-m','elf_x86_64','-T','sdk/linker/pollik.ld',"$userOutput/memtest.o",'-o',"$userOutput/memtest.elf")
    # PollikOS C SDK: canonical flags, crt0, libpollikc.a and ELF verification
    # come from sdk/tools/pollikos_sdk.ps1 so tests, examples and the pollikcc
    # driver can never diverge.
    . (Join-Path $PSScriptRoot 'sdk/tools/pollikos_sdk.ps1')
    $runtimeDir = Ensure-PollikosRuntime -RuntimeDir "$output/sdk"
    $sdkApplications = [ordered]@{
        # C3 regression fixtures.
        'hello_c'           = @{ Sources = @('sdk/tests/hello_c.c');           Optimization = '-O2' }
        'argv_c'            = @{ Sources = @('sdk/tests/argv_c.c');            Optimization = '-O2' }
        'allocator_test_c'  = @{ Sources = @('sdk/tests/allocator_test_c.c');  Optimization = '-O2' }
        'filesystem_test_c' = @{ Sources = @('sdk/tests/filesystem_test_c.c'); Optimization = '-O2' }
        'runtime_test_c'    = @{ Sources = @('sdk/tests/runtime_test_c.c');    Optimization = '-O2' }
        'mutation_c'        = @{ Sources = @('sdk/tests/mutation_c.c');        Optimization = '-O2' }
        'spawn_child_c'     = @{ Sources = @('sdk/tests/spawn_child_c.c');     Optimization = '-O2' }
        'spawn_parent_c'    = @{ Sources = @('sdk/tests/spawn_parent_c.c');    Optimization = '-O2' }
        'stdio_c'           = @{ Sources = @('sdk/tests/stdio_c.c');           Optimization = '-O2' }
        'libc_c'            = @{ Sources = @('sdk/tests/libc_c.c');            Optimization = '-O2' }
        'alloc_stress_c'    = @{ Sources = @('sdk/tests/alloc_stress_c.c');    Optimization = '-O2' }
        'tool_stage_c'      = @{ Sources = @('sdk/tests/tool_stage_c.c');      Optimization = '-O2' }
        'tool_driver_c'     = @{ Sources = @('sdk/tests/tool_driver_c.c');     Optimization = '-O2' }
        'big_elf_c'         = @{ Sources = @('sdk/tests/big_elf_c.c');         Optimization = '-O2' }
        'selfhost_driver_c' = @{ Sources = @('sdk/tests/selfhost_driver_c.c'); Optimization = '-O2' }
        # Interactive userspace shell (delivered as /bin/pollish).
        'pollish'           = @{ Sources = @('sdk/apps/pollish.c','common/calc.c'); Optimization = '-O2' }
        'calculator'        = @{ Sources = @('sdk/apps/calculator.c','common/calc.c'); Optimization = '-O2' }
        'media_player'      = @{ Sources = @('sdk/apps/media_player.c'); Optimization = '-O2' }
        'pipe_nowait'       = @{ Sources = @('sdk/tests/pipe_nowait.c');       Optimization = '-O2' }
        'windowdemo'        = @{ Sources = @('sdk/tests/windowdemo.c');       Optimization = '-O2' }
        'terminal'          = @{ Sources = @('sdk/apps/terminal.c');            Optimization = '-O2' }
        'desktop'           = @{ Sources = @('sdk/apps/desktop.c','sdk/apps/icon_assets.c'); Optimization = '-O2' }
        'files'             = @{ Sources = @('sdk/apps/files.c');              Optimization = '-O2' }
        'browser'           = @{ Sources = @('sdk/apps/browser.c','sdk/apps/browser_js.c','kernel/browser/html_parser.c','kernel/browser/css_engine.c','kernel/browser/layout.c','kernel/browser/render.c','third_party/elk/elk.c'); Optimization = '-O2'; Defines = @('POLLIK_BROWSER_STANDALONE=1') }
        'notes'             = @{ Sources = @('sdk/apps/notes.c');              Optimization = '-O2' }
        # C4 ABI, fault and preemption fixtures.
        'abi_test_o2'       = @{ Sources = @('sdk/tests/abi_test_c.c');        Optimization = '-O2' }
        'stack_guard_c'     = @{ Sources = @('sdk/tests/stack_guard_c.c');     Optimization = '-O2' }
        'null_fault_c'      = @{ Sources = @('sdk/tests/null_fault_c.c');      Optimization = '-O2' }
        'spin_c'            = @{ Sources = @('sdk/tests/spin_c.c');            Optimization = '-O2' }
        # Official SDK examples (no private headers, no application assembly).
        'sdk_files'         = @{ Sources = @('sdk/examples/files.c');          Optimization = '-O2' }
        'sdk_dirs'          = @{ Sources = @('sdk/examples/dirs.c');           Optimization = '-O2' }
        'sdk_memory'        = @{ Sources = @('sdk/examples/memory.c');         Optimization = '-O2' }
        'sdk_cwd'           = @{ Sources = @('sdk/examples/cwd.c');            Optimization = '-O2' }
        'sdk_time'          = @{ Sources = @('sdk/examples/time.c');           Optimization = '-O2' }
        'sdk_errno'         = @{ Sources = @('sdk/examples/errno.c');          Optimization = '-O2' }
        'sdk_write'         = @{ Sources = @('sdk/examples/write.c');          Optimization = '-O2' }
    }
    foreach ($application in $sdkApplications.Keys) {
        $definition = $sdkApplications[$application]
        $objects = @()
        foreach ($source in $definition.Sources) {
            $object = "$userOutput/$application-$([IO.Path]::GetFileNameWithoutExtension($source)).o"
            Compile-PollikosObject -Source $source -Object $object -RuntimeDir $runtimeDir `
                -Optimization $definition.Optimization -Defines $definition.Defines
            $objects += $object
        }
        Invoke-PollikosLink -Objects $objects -Output "$userOutput/$application.elf" -RuntimeDir $runtimeDir
        [void](Test-PollikosElf -Path "$userOutput/$application.elf")
    }
    # The pollikcc driver itself builds the ABI -O0 variant, the multi-source
    # example and the static-library example, proving the public workflow.
    function Invoke-Pollikcc {
        param([string[]]$Arguments)
        & powershell -NoProfile -ExecutionPolicy Bypass -File sdk/tools/pollikcc.ps1 `
            --runtime $runtimeDir @Arguments
        if ($LASTEXITCODE -ne 0) { throw "pollikcc failed: $($Arguments -join ' ')" }
    }
    # Optional MP4/H264/AAC userspace library; never linked into the kernel.
    Invoke-Checked python @('tools/build_mp4_codecs.py','--native','--output',"$output/codecs")
    Compile-PollikosObject -Source 'sdk/media/movie.c' -Object "$userOutput/movie-api.o" -RuntimeDir $runtimeDir `
        -ExtraIncludes @('third_party/h264bsd/src','third_party/faad2/include')
    Invoke-Checked llvm-ar @('rcs',"$output/codecs/libpollikvideo.a","$userOutput/movie-api.o")
    Compile-PollikosObject -Source 'sdk/apps/video_player.c' -Object "$userOutput/video-player.o" -RuntimeDir $runtimeDir
    Invoke-PollikosLink -Objects @("$userOutput/video-player.o") -Libraries @("$output/codecs/libpollikvideo.a") `
        -Output "$userOutput/video_player.elf" -RuntimeDir $runtimeDir
    [void](Test-PollikosElf -Path "$userOutput/video_player.elf")
    Invoke-Pollikcc @('-O0','sdk/tests/abi_test_c.c','-o',"$userOutput/abi_test_o0.elf")
    Invoke-Pollikcc @('sdk/examples/multifile/main.c','sdk/examples/multifile/utils.c',
        'sdk/examples/multifile/parser.c','-o',"$userOutput/sdk_multifile.elf")
    Invoke-Pollikcc @('-c','sdk/examples/library/libexample.c','-o',"$userOutput/libexample.o")
    Invoke-Checked llvm-ar @('rcs',"$userOutput/libexample.a","$userOutput/libexample.o")
    Invoke-Pollikcc @('sdk/examples/library/main.c','-L',"$userOutput",'-lexample',
        '-o',"$userOutput/sdk_library.elf")
    Invoke-Pollikcc @('sdk/examples/hello.c','-o',"$userOutput/sdk_hello.elf")
    Invoke-Pollikcc @('--allow-fpu','third_party/tinycc/tcc.c','-Ithird_party/tinycc',
        '-DONE_SOURCE=1','-DCONFIG_TCCBOOT=1','-D__pollikos__=1',
        '-DPOLLIKOS_TCC_DETERMINISTIC_DATE=1','-o',"$userOutput/tcc.elf")
    Invoke-Pollikcc @('--allow-simd','--small-code-model','-c','third_party/tinycc/lib/libtcc1.c','-Ithird_party/tinycc',
        '-o',"$userOutput/libtcc1.o")
    Invoke-Pollikcc @('--small-code-model','-c','third_party/tinycc/lib/va_list.c','-Ithird_party/tinycc',
        '-o',"$userOutput/va_list.o")
    Invoke-Checked llvm-ar @('rcs',"$userOutput/libtcc1.a","$userOutput/libtcc1.o","$userOutput/va_list.o")
    $nativeLibcDir = "$userOutput/native-libc"
    New-Item -ItemType Directory -Force $nativeLibcDir | Out-Null
    $nativeLibcObjects = @()
    foreach ($source in @(Get-ChildItem -LiteralPath 'sdk/lib' -Filter '*.c' | Sort-Object Name)) {
        $object = "$nativeLibcDir/$($source.BaseName).o"
        Compile-PollikosObject -Source $source.FullName -Object $object -RuntimeDir $runtimeDir `
            -Optimization '-O2' -CodeModel 'small'
        $nativeLibcObjects += $object
    }
    Invoke-Checked llvm-ar (@('rcs',"$userOutput/libc-native.a") + $nativeLibcObjects)
    # Reproducibility: identical inputs and flags must produce identical ELF.
    Invoke-Pollikcc @('sdk/examples/hello.c','-o',"$userOutput/sdk_hello_repeat.elf")
    $first = (Get-FileHash -Algorithm SHA256 "$userOutput/sdk_hello.elf").Hash
    $second = (Get-FileHash -Algorithm SHA256 "$userOutput/sdk_hello_repeat.elf").Hash
    if ($first -ne $second) { throw 'SDK build is not reproducible for sdk_hello.elf' }
    Remove-Item -LiteralPath "$userOutput/sdk_hello_repeat.elf" -Force
    Invoke-Checked python @('assets/build_system_icons.py')
    Invoke-Checked python @('tools/build_x64_data.py',$output)
    if (!$SelfTest) { Invoke-Checked python @('tools/build_x64_system.py',$output) }
    # Install the SDK hello through the official tool, exercising it on every
    # build; the test image is disposable and never a user data image.
    Invoke-Checked python @('sdk/tools/pollikinstall.py',"$output/PollikData-test.img",
        '/bin/sdk_hello',"$userOutput/sdk_hello.elf",'--verbose')
    # Dedicated almost-full disposable image for the ENOSPC safety test.
    Invoke-Checked python @('tools/make_full_image.py',"$output/PollikData-test.img",
        "$output/PollikData-full.img")
    Invoke-Checked nasm @('-f','bin','boot/boot.asm','-o',"$output/boot.bin")
    Invoke-Checked nasm @('-f','bin','boot/stage2.asm','-o',"$output/stage2.bin")
    foreach ($module in @('entry','interrupts','user_payload')) {
        Invoke-Checked nasm (@('-f','elf64',"-I$output/", "kernel/arch/x86_64/$module.asm",'-o',"$output/$module.o") + $defines)
    }
    $bearSslArchive = "$output/libbearssl-x64.a"
    $bearSslStamp = "$output/bearssl-x64.stamp"
    $bearSslSources = Get-ChildItem third_party/bearssl/src -Filter '*.c' -Recurse
    $rebuildBearSsl = !(Test-Path $bearSslArchive) -or !(Test-Path $bearSslStamp)
    if (!$rebuildBearSsl) {
        $stampTime = (Get-Item $bearSslStamp).LastWriteTimeUtc
        $rebuildBearSsl = [bool]($bearSslSources | Where-Object { $_.LastWriteTimeUtc -gt $stampTime } | Select-Object -First 1)
    }
    if ($rebuildBearSsl) {
        $bearSslObjects = @()
        $bearSslIndex = 0
        foreach ($source in $bearSslSources) {
            $object = "$output/bearssl-x64-$('{0:D3}' -f $bearSslIndex).o"
            Invoke-Checked clang @('--target=x86_64-none-elf','-ffreestanding','-fno-pic','-fno-pie',
                '-fno-stack-protector','-mno-red-zone','-mgeneral-regs-only','-O2','-Wall','-Wextra','-Werror',
                '-DBR_AES_X86NI=0','-DBR_SSE2=0','-DBR_RDRAND=0','-DBR_USE_URANDOM=0',
                '-DBR_USE_WIN32_RAND=0','-DBR_USE_UNIX_TIME=0','-DBR_USE_WIN32_TIME=0',
                '-Ithird_party/bearssl/inc','-Ithird_party/bearssl/src','-Isdk/include','-c',$source.FullName,'-o',$object)
            $bearSslObjects += $object
            $bearSslIndex++
        }
        Invoke-Checked llvm-ar (@('rcs',$bearSslArchive) + $bearSslObjects)
        Set-Content -LiteralPath $bearSslStamp -Value 'BearSSL freestanding scalar x86_64'
    }
    $modules = @('kernel','auth64','physical','pmm','vmm','usercopy','process','scheduler','fpu','elf64','elf_demo','timer','scheduler_demo','fs_platform','net_platform','audio_platform','audio_stream','devices','network','launch','syscall','path','file','file_demo','stat_demo','dir_demo','runtime_demo','heap','rtc64_decode','rtc64','tty','mouse','console_fb','window','pipe','c3_demo','c4_demo','c5_demo','c6_demo','c7_demo')
    if($Production){$modules=@($modules | Where-Object {$_ -notlike '*_demo'})}
    if ($SelfTest) { $modules += @('test_memory','memory_test','user_test','elf_test','scheduler_test','path_test','file_test','stat_test','dir_test','runtime_test','c2_test','c3_test','c4_test','c5_test','c6_test','c7_test','selfhost_test') }
    $linkOptions = @()
    if($Production){$linkOptions+='--gc-sections'}
    if ($CrashWriteLog) {
        $defines += '-DPOLLIKFS_WRITELOG_TEST=1'
        $modules += 'crash_write_log'
        $linkOptions += '--wrap=ata_write_sector'
    }
    $objects = @()
    $modules += @('../../vfs','../../pollikfs','../../storage','../../hal','../../audio')
    foreach ($module in $modules) {
        $objectName = Split-Path $module -Leaf
        Invoke-Checked clang (@('--target=x86_64-none-elf','-ffreestanding','-fno-pic','-fno-pie',
        '-fno-stack-protector','-mno-red-zone','-mgeneral-regs-only','-O2','-Wall','-Wextra','-Werror',
        '-Ithird_party/bearssl/inc','-Isdk/include',
        '-c',"kernel/arch/x86_64/$module.c",'-o',"$output/$objectName.o") + $defines + $productionCompileFlags)
        $objects += "$output/$objectName.o"
    }
    foreach ($source in @('arp','dhcp','dns','icmp','ipv4','net_manager','net_util','rtl8139','tcp','udp','wifi_if')) {
        $objectName = "net_$source"
        Invoke-Checked clang @('--target=x86_64-none-elf','-ffreestanding','-fno-pic','-fno-pie',
            '-fno-stack-protector','-mno-red-zone','-mgeneral-regs-only','-O2','-Wall','-Wextra','-Werror',
            '-DPOLLIK_X64=1','-Ithird_party/bearssl/inc','-Isdk/include',
            '-c',"kernel/net/$source.c",'-o',"$output/$objectName.o")
        $objects += "$output/$objectName.o"
    }
    Invoke-Checked clang @('--target=x86_64-none-elf','-ffreestanding','-fno-pic','-fno-pie',
        '-fno-stack-protector','-mno-red-zone','-mgeneral-regs-only','-O2','-Wall','-Wextra','-Werror',
        '-DPOLLIK_X64=1','-Ithird_party/bearssl/inc','-Isdk/include',
        '-c','kernel/net/tls.c','-o',"$output/net_tls.o")
    $objects += "$output/net_tls.o"
    $objects += $bearSslArchive
    Invoke-Checked ld.lld (@('-m','elf_x86_64','-T','kernel/arch/x86_64/linker.ld',
        "$output/entry.o","$output/interrupts.o","$output/user_payload.o") + $objects + $linkOptions + @('-o',"$output/kernel.elf"))
    Invoke-Checked llvm-objcopy @('-O','binary',"$output/kernel.elf","$output/kernel.bin")
    $kernelBytes = [IO.File]::ReadAllBytes("$PSScriptRoot/$output/kernel.bin")
    $stage = [IO.File]::ReadAllBytes("$PSScriptRoot/$output/stage2.bin")
    $boot = [IO.File]::ReadAllBytes("$PSScriptRoot/$output/boot.bin")
    if ($boot.Length -ne 512 -or $stage.Length -ne 4096 -or $stage[4094] -ne 0 -or $stage[4095] -ne 0) {
        throw 'Unexpected BIOS loader layout'
    }
    if ($kernelBytes.Length -eq 0 -or $kernelBytes.Length -gt 1MB) { throw 'Invalid bootstrap image size' }
    $sectors = [int][Math]::Ceiling($kernelBytes.Length / 512)
    $stage[4094] = [byte]($sectors -band 255)
    $stage[4095] = [byte](($sectors -shr 8) -band 255)
    $destination = "$PSScriptRoot/$output/PollikOS-x86_64.img"
    $stream = [IO.File]::Open("$destination.pending", 'Create', 'Write', 'None')
    try {
        # Include complete final 32 KiB BIOS read chunk and conventional geometry.
        $stream.SetLength(16MB)
        $stream.Write($boot, 0, $boot.Length)
        $stream.Write($stage, 0, $stage.Length)
        $stream.Write($kernelBytes, 0, $kernelBytes.Length)
        $stream.Flush($true)
    } finally { $stream.Dispose() }
    # Host scanners can hold an older image briefly. Try the canonical name
    # first, then bounded alternates; the test harness boots the newest image.
    $installed = $false
    $lastError = ''
    foreach ($slot in @('', 'b', 'c')) {
        $candidate = "$PSScriptRoot/$output/PollikOS-x86_64$slot.img"
        for ($attempt = 0; $attempt -lt 10 -and -not $installed; ++$attempt) {
            try {
                if (Test-Path -LiteralPath $candidate) {
                    try { [IO.File]::Delete($candidate) } catch {}
                }
                if (Test-Path -LiteralPath $candidate) {
                    [IO.File]::Replace("$destination.pending", $candidate, "$candidate.previous")
                } else {
                    [IO.File]::Move("$destination.pending", $candidate)
                }
                $installed = $true
                if ($slot) { Write-Warning "installed as PollikOS-x86_64$slot.img (older name busy)" }
            } catch { $lastError = $_.Exception.Message; Start-Sleep -Milliseconds 300 }
        }
        if ($installed) { break }
    }
    if (-not $installed) { throw "Could not install $destination ($lastError)" }
    Write-Host "Built $output/PollikOS-x86_64.img ($($kernelBytes.Length) kernel bytes)"
    if ($PerturbSchedule) { Write-Host 'Self-test schedule perturbation: TIMER64_HZ=137' }
    if ($CrashWriteLog) { Write-Host 'Crash write log enabled: PKWL sector frames on debugcon port 0xe9' }
} finally { Pop-Location }
