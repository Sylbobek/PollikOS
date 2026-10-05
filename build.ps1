param(
    [string]$ImageName = "PollikOS-Alpha.img",
    [switch]$FormatData = $false,
    [switch]$LegacyTsc = $false,
    [switch]$LegacyDamage = $false,
    [switch]$LegacyAtaRead = $false,
    [switch]$GfxReference = $false,
    [switch]$NoSync = $false
)
$ErrorActionPreference = 'Stop'
Set-Location $PSScriptRoot
New-Item -ItemType Directory -Force build | Out-Null
if (Get-Command python -ErrorAction SilentlyContinue) {
    try { python tools/gen_compile_commands.py | Out-Null } catch {}
}
function Invoke-Checked { param([string]$Program, [string[]]$Arguments) & $Program @Arguments; if ($LASTEXITCODE -ne 0) { throw "$Program failed ($LASTEXITCODE)" } }
Invoke-Checked python @('assets/build_cursor.py','--output','kernel/cursor_sprites.h')
$tscCompileFlags = @()
if ($LegacyTsc) { $tscCompileFlags += '-DPOLLIK_TSC_FORCE_CPUID=1' }
if ($LegacyTsc) { Write-Host 'TSC reader: legacy CPUID serialization' }
else { Write-Host 'TSC reader: LFENCE serialization with CPUID fallback' }
# Build the vendored freestanding TLS library, without SIMD (no FPU context switching).
if (!(Test-Path build/bearssl-freestanding.stamp)) {
    New-Item -ItemType Directory -Force build/bearssl-obj | Out-Null
    $tlsObjects = @()
    foreach ($source in (Get-ChildItem third_party/bearssl/src -Filter '*.c' -Recurse)) {
        $object = "build/bearssl-obj/$($source.BaseName).o"
        Invoke-Checked clang @('--target=i386-none-elf','-march=i386','-ffreestanding','-fno-pic','-fno-stack-protector','-mno-sse','-mno-mmx','-Os','-DBR_AES_X86NI=0','-DBR_SSE2=0','-DBR_RDRAND=0','-DBR_USE_URANDOM=0','-DBR_USE_WIN32_RAND=0','-DBR_USE_UNIX_TIME=0','-DBR_USE_WIN32_TIME=0','-Ikernel/include','-Ithird_party/bearssl/inc','-Ithird_party/bearssl/src','-c',$source.FullName,'-o',$object)
        $tlsObjects += $object
    }
    Invoke-Checked llvm-ar (@('rcs','build/libbearssl.a') + $tlsObjects)
    Set-Content build/bearssl-freestanding.stamp 'BearSSL 0.6; freestanding scalar i386'
}
Invoke-Checked nasm @('-f','bin','boot/boot.asm','-o','build/boot.bin')
Invoke-Checked nasm @('-f','bin','boot/stage2.asm','-o','build/stage2.bin')
Invoke-Checked nasm @('-f','elf32','kernel/entry.asm','-o','build/entry.o')
Invoke-Checked nasm @('-f','elf32','kernel/interrupts.asm','-o','build/interrupts.o')
$netModules = @('net_util','rtl8139','wifi_if','arp','ipv4','icmp','udp','dhcp','dns','tcp','tls','http','net_manager')
foreach ($module in @('kernel','desktop','compositor','graphics','gfx_device','soft3d','input_dispatch','wm','hw','hal','mem','pmm','vmm','klog','ahci','storage','pollikfs','vfs','process','syscall','elf','network','framebuffer','ui','trash','ui_animation','desktop_items','auth','media','pollikgl','audio')) {
    $optimization = if ($module -eq 'wm') { '-Oz' } else { '-Os' }
    $moduleFlags = @()
    if ($module -eq 'compositor' -and $LegacyDamage) { $moduleFlags += '-DPOLLIK_COMPOSITOR_LEGACY_DAMAGE=1' }
    if ($module -eq 'storage' -and $LegacyAtaRead) { $moduleFlags += '-DPOLLIK_ATA_SCALAR_READ=1' }
    Invoke-Checked clang (@('--target=i386-none-elf','-m32','-march=i386','-ffreestanding','-fno-pic','-fno-pie','-fno-stack-protector','-mno-sse','-mno-mmx',$optimization,'-Wall','-Wextra','-Werror') + $tscCompileFlags + $moduleFlags + @('-Ikernel/include','-Ikernel/gfx','-c',"kernel/$module.c",'-o',"build/$module.o"))
}
$gfxFlags = @()
if ($GfxReference) { $gfxFlags += '-DGFX_REFERENCE=1'; Write-Host 'Graphics primitives: reference scalar path' }
else { Write-Host 'Graphics primitives: optimized x86 path' }
Invoke-Checked clang (@('--target=i386-none-elf','-m32','-march=i386','-ffreestanding','-fno-pic','-fno-pie','-fno-stack-protector','-mno-sse','-mno-mmx','-Os','-Wall','-Wextra','-Werror') + $gfxFlags + @('-Ikernel/include','-Ikernel/gfx','-c','kernel/gfx/gfx_primitives.c','-o','build/gfx_primitives.o'))
foreach ($m in $netModules) {
    Invoke-Checked clang @('--target=i386-none-elf','-m32','-march=i386','-ffreestanding','-fno-pic','-fno-pie','-fno-stack-protector','-mno-sse','-mno-mmx','-Os','-Wall','-Wextra','-Werror','-Ikernel/include','-c',"kernel/net/$m.c",'-o',"build/$m.o")
}
$browserModules = @('browser_app','html_parser','css_engine','layout','render','js_engine','js_compat','images')
foreach ($m in $browserModules) {
    Invoke-Checked clang @('--target=i386-none-elf','-m32','-march=i386','-ffreestanding','-fno-pic','-fno-pie','-fno-stack-protector','-mno-sse','-mno-mmx','-Os','-Wall','-Wextra','-Werror','-Ikernel/include','-c',"kernel/browser/$m.c",'-o',"build/$m.o")
}
# Built-in GUI clients remain Ring0, but are independent translation units.
$guiModules = @('apps','app_edit','welcome','files','notes','terminal','settings','browser_client','pollikmark')
foreach ($m in $guiModules) {
    Invoke-Checked clang @('--target=i386-none-elf','-m32','-march=i386','-ffreestanding','-fno-pic','-fno-pie','-fno-stack-protector','-mno-sse','-mno-mmx','-Os','-Wall','-Wextra','-Werror','-Ikernel/include','-c',"kernel/gui/$m.c",'-o',"build/gui_$m.o")
}
$guiObjs = @($guiModules | ForEach-Object { "build/gui_$_.o" })
Invoke-Checked clang @('--target=i386-none-elf','-march=i386','-ffreestanding','-fno-pic','-fno-stack-protector','-mno-sse','-mno-mmx','-Os','-DJS_OPT','-Ikernel/include','-c','third_party/elk/elk.c','-o','build/elk.o')
# Build userspace applications
$userApps = @('hello', 'fault_test', 'fault_kernel', 'fault_stack')
$userElfObjs = @()
foreach ($app in $userApps) {
    Invoke-Checked clang @('--target=i386-none-elf','-m32','-march=i386','-ffreestanding','-fno-pic','-fno-pie','-fno-stack-protector','-mno-sse','-mno-mmx','-O2','-Iinclude','-c',"apps/$app/$app.c",'-o',"build/$app.o")
    Invoke-Checked ld.lld @('-m','elf_i386','-N','-s','-T','apps/user.ld',"build/$app.o",'-o',"build/$app.elf")
    Invoke-Checked llvm-objcopy @('-I','binary','-O','elf32-i386','-B','i386',"build/$app.elf","build/${app}_elf.o")
    $userElfObjs += "build/${app}_elf.o"
}
$browserObjs = @($browserModules | ForEach-Object { "build/$_.o" })
$netObjs = @($netModules | ForEach-Object { "build/$_.o" })
$linkArgs = @('-m','elf_i386','-T','kernel/linker.ld','build/entry.o','build/interrupts.o','build/kernel.o','build/desktop.o','build/compositor.o','build/graphics.o','build/gfx_primitives.o','build/gfx_device.o','build/soft3d.o','build/input_dispatch.o','build/wm.o','build/ui.o','build/hw.o','build/hal.o','build/mem.o','build/pmm.o','build/vmm.o','build/klog.o','build/ahci.o','build/storage.o','build/pollikfs.o','build/vfs.o','build/process.o','build/syscall.o','build/elf.o','build/trash.o','build/ui_animation.o','build/desktop_items.o','build/auth.o','build/media.o','build/pollikgl.o','build/audio.o') + $userElfObjs + @('build/network.o','build/framebuffer.o') + $netObjs + $browserObjs + $guiObjs + @('build/elk.o') + @('build/libbearssl.a','-o','build/kernel.elf')
Invoke-Checked ld.lld $linkArgs
Invoke-Checked llvm-objcopy @('-O','binary','build/kernel.elf','build/kernel.bin')
$kernelBytes = [IO.File]::ReadAllBytes("$PSScriptRoot/build/kernel.bin")
# Stage 2 loads the image to 1 MiB in 32 KiB chunks; the linker script bounds
# image + BSS. Keep an explicit cap so a runaway image cannot reach 0x800000.
$kernelSectors = [int][Math]::Ceiling($kernelBytes.Length / 512)
if ($kernelBytes.Length -gt 4194304) { throw "Kernel image $($kernelBytes.Length) B exceeds the 4 MiB load cap" }
$stage2Bytes = [IO.File]::ReadAllBytes("$PSScriptRoot/build/stage2.bin")
if ($stage2Bytes.Length -ne 4096) { throw "stage2.bin must be exactly 4096 bytes (got $($stage2Bytes.Length))" }
if ($stage2Bytes[4094] -ne 0 -or $stage2Bytes[4095] -ne 0) { throw 'stage2.bin kernel_sectors placeholder is not zero' }
$stage2Bytes[4094] = [byte]($kernelSectors -band 0xff)
$stage2Bytes[4095] = [byte](($kernelSectors -shr 8) -band 0xff)
$destinationPath = "$PSScriptRoot/build/$ImageName"
$imagePath = "$destinationPath.pending"
# A sparse raw image reports 10 GiB to the guest but consumes only sectors
# actually written by PollikOS on the Windows host.
$imageFile = [IO.File]::Open($imagePath, [IO.FileMode]::OpenOrCreate, [IO.FileAccess]::ReadWrite, [IO.FileShare]::Read)
$imageFile.Dispose()
& fsutil sparse setflag $imagePath
if ($LASTEXITCODE -ne 0) { throw 'Failed to create sparse disk image.' }
$image = [IO.File]::Open($imagePath, [IO.FileMode]::Open, [IO.FileAccess]::ReadWrite, [IO.FileShare]::Read)
try {
    $image.SetLength(0)
    $image.SetLength(10GB)
    $bootBytes = [IO.File]::ReadAllBytes("$PSScriptRoot/build/boot.bin")
    $image.Position = 0
    $image.Write($bootBytes, 0, $bootBytes.Length)
    $image.Position = 512
    $image.Write($stage2Bytes, 0, $stage2Bytes.Length)
    $image.Position = 4608
    $image.Write($kernelBytes, 0, $kernelBytes.Length)
    $image.Flush($true)
} finally {
    $image.Dispose()
}

# Build a separate removable-media installer. Its kernel embeds only the
# bootable prefix of the normal runtime image, then writes that prefix to the
# supported internal ATA target and creates PollikFS at the fixed 8 MiB offset.
New-Item -ItemType Directory -Force build/install | Out-Null
$wallpaperPackage = 'build/install/wallpapers_pkg.bin'
Invoke-Checked python @('tools/build_wallpaper_package.py','--output',$wallpaperPackage)
Invoke-Checked llvm-objcopy @('-I','binary','-O','elf32-i386','-B','i386',$wallpaperPackage,'build/install/wallpapers_pkg.o')
$runtimePrefix = 'build/install/runtime-prefix.bin'
$prefixStream = [IO.File]::Open($runtimePrefix, [IO.FileMode]::Create, [IO.FileAccess]::Write, [IO.FileShare]::Read)
try {
    $prefixStream.Write($bootBytes, 0, $bootBytes.Length)
    $prefixStream.Write($stage2Bytes, 0, $stage2Bytes.Length)
    $prefixStream.Write($kernelBytes, 0, $kernelBytes.Length)
} finally { $prefixStream.Dispose() }
Invoke-Checked llvm-objcopy @('-I','binary','-O','elf32-i386','-B','i386',$runtimePrefix,'build/install/runtime-prefix.o')
$commonCompile = @('--target=i386-none-elf','-m32','-march=i386','-ffreestanding','-fno-pic','-fno-pie','-fno-stack-protector','-mno-sse','-mno-mmx','-Oz','-Wall','-Wextra','-Werror','-Ikernel/include')
Invoke-Checked clang ($commonCompile + @('-DPOLLIK_INSTALL_MEDIA=1','-c','kernel/auth.c','-o','build/install/auth.o'))
Invoke-Checked clang ($commonCompile + @('-DPOLLIK_INSTALL_MEDIA=1','-c','kernel/desktop.c','-o','build/install/desktop.o'))
Invoke-Checked clang ($commonCompile + @('-DPOLLIK_INSTALL_MEDIA=1','-c','kernel/installer.c','-o','build/install/installer.o'))
foreach ($module in @('compositor','input_dispatch','wm')) {
    Invoke-Checked clang ($commonCompile + @('-DPOLLIK_INSTALL_MEDIA=1','-c',"kernel/$module.c","-o","build/install/$module.o"))
}
$installerLinkArgs = @()
for ($i = 0; $i -lt $linkArgs.Count; $i++) {
    if ($linkArgs[$i] -eq 'build/auth.o') { $installerLinkArgs += 'build/install/auth.o'; continue }
    if ($linkArgs[$i] -eq 'build/desktop.o') { $installerLinkArgs += 'build/install/desktop.o'; continue }
    if ($linkArgs[$i] -eq 'build/compositor.o') { $installerLinkArgs += 'build/install/compositor.o'; continue }
    if ($linkArgs[$i] -eq 'build/input_dispatch.o') { $installerLinkArgs += 'build/install/input_dispatch.o'; continue }
    if ($linkArgs[$i] -eq 'build/wm.o') { $installerLinkArgs += 'build/install/wm.o'; continue }
    if ($linkArgs[$i] -eq '-o') { $i++; continue }
    $installerLinkArgs += $linkArgs[$i]
}
$installerLinkArgs += @('build/install/installer.o','build/install/runtime-prefix.o',
                        'build/install/wallpapers_pkg.o','-o','build/install/kernel.elf')
Invoke-Checked ld.lld $installerLinkArgs
Invoke-Checked llvm-objcopy @('-O','binary','build/install/kernel.elf','build/install/kernel.bin')
$installerKernel = [IO.File]::ReadAllBytes("$PSScriptRoot/build/install/kernel.bin")
if ($installerKernel.Length -gt 4194304) { throw "Installer kernel exceeds the 4 MiB load cap" }
$installerSectors = [int][Math]::Ceiling($installerKernel.Length / 512)
$installerStage2 = [IO.File]::ReadAllBytes("$PSScriptRoot/build/stage2.bin")
$installerStage2[4094] = [byte]($installerSectors -band 0xff)
$installerStage2[4095] = [byte](($installerSectors -shr 8) -band 0xff)
$installerImagePath = "$PSScriptRoot/build/PollikOS-USB-Installer.img"
$installerPending = "$installerImagePath.pending"
$installerImage = [IO.File]::Open($installerPending, [IO.FileMode]::Create, [IO.FileAccess]::ReadWrite, [IO.FileShare]::Read)
try {
    $installerImage.SetLength(64MB)
    $installerImage.Position = 0; $installerImage.Write($bootBytes, 0, $bootBytes.Length)
    $installerImage.Position = 512; $installerImage.Write($installerStage2, 0, $installerStage2.Length)
    $installerImage.Position = 4608; $installerImage.Write($installerKernel, 0, $installerKernel.Length)
    $installerImage.Flush($true)
} finally { $installerImage.Dispose() }
if (Test-Path $installerImagePath) { [IO.File]::Replace($installerPending,$installerImagePath,"$installerImagePath.previous") }
else { [IO.File]::Move($installerPending,$installerImagePath) }
Write-Host "USB installer built: build/PollikOS-USB-Installer.img ($($installerKernel.Length) kernel bytes)"

$dataPath = "$PSScriptRoot/build/PollikData.img"
if (!(Test-Path $dataPath)) {
    Write-Host "Tworzenie nowego pustego dysku danych: build/PollikData.img (10 GiB sparse)..."
    $dataFile = [IO.File]::Open($dataPath, [IO.FileMode]::OpenOrCreate, [IO.FileAccess]::ReadWrite, [IO.FileShare]::Read)
    $dataFile.Dispose()
    & fsutil sparse setflag $dataPath
    if ($LASTEXITCODE -ne 0) { throw 'Failed to create sparse data disk.' }
    Invoke-Checked python @('-c', 'import sys; sys.path.insert(0,"tests"); from format_pollikfs2 import format_disk; format_disk(sys.argv[1], total_size_mb=40)', $dataPath)
    $dataImage = [IO.File]::Open($dataPath, [IO.FileMode]::OpenOrCreate, [IO.FileAccess]::ReadWrite, [IO.FileShare]::Read)
    try {
        if ($dataImage.Length -lt 10GB) { $dataImage.SetLength(10GB); $dataImage.Flush($true) }
    } finally { $dataImage.Dispose() }
    Write-Host "Utworzono i zainicjalizowano nowy czysty obraz PollikData.img."
} elseif ($FormatData) {
    Write-Host "UWAGA: Jawne formatowanie dysku danych (-FormatData)." -ForegroundColor Yellow
    $bakDate = Get-Date -Format "yyyyMMdd_HHmmss"
    $bakPath = "$dataPath.bak_$bakDate"
    Copy-Item $dataPath $bakPath
    Write-Host "Kopia zapasowa przed jawnym formatowaniem: $bakPath"
    Invoke-Checked python @('-c', 'import sys; sys.path.insert(0,"tests"); from format_pollikfs2 import format_disk; format_disk(sys.argv[1], total_size_mb=40)', $dataPath)
    Write-Host "Dysk PollikData.img zostal sformatowany na jawne zadanie uzytkownika."
} else {
    # Existing PollikData.img: strictly protect user data.
    # 1. Detect filesystem version and geometry
    $inspectOut = & python "$PSScriptRoot/tests/migrate_pollikfs.py" inspect $dataPath
    $fsStatus = "unsupported"
    foreach ($line in $inspectOut) {
        if ($line -match '^STATUS:(.+)$') {
            $fsStatus = $matches[1].Trim()
        }
    }

    if ($fsStatus -eq "current") {
        # Header inspection is read-only; the sync tool additionally validates
        # the complete filesystem, file length and references under an exclusive lock.
        Write-Host "PollikFS v2: Wykryto aktualny format i geometrie [31, 36]. Dane uzytkownika nienaruszone."
    } else {
        # 5. If migration is not supported, halt build with a clear message
        Write-Host "================================================================================" -ForegroundColor Red
        Write-Host "BLAD BEZPIECZENSTWA: Wykryto nieobslugiwany format lub uszkodzona geometrie PollikFS!" -ForegroundColor Red
        Write-Host "Sciezka: $dataPath" -ForegroundColor Yellow
        Write-Host "Szczegoly inspekcji:" -ForegroundColor Yellow
        foreach ($line in $inspectOut) { Write-Host "  $line" -ForegroundColor Gray }
        Write-Host ""
        Write-Host "Build NIGDY nie usuwa ani nie formatuje danych uzytkownika automatycznie!" -ForegroundColor Yellow
        Write-Host "Automatyczne formatowanie zostalo zablokowane w celu ochrony plikow." -ForegroundColor Yellow
        Write-Host ""
        Write-Host "Jesli chcesz JAWNIE sformatowac dysk na nowo (BEZPOWROTNA UTRATA DANYCH), uruchom:" -ForegroundColor Cyan
        Write-Host "   .\build.ps1 -FormatData" -ForegroundColor Green
        Write-Host "================================================================================" -ForegroundColor Red
        throw "Build zatrzymany w celu ochrony danych uzytkownika w PollikData.img."
    }
}

if ($NoSync) { Write-Host 'System sync: skipped (-NoSync); data image unchanged.' }
else { Invoke-Checked python @('tools/sync_system_files.py', $dataPath) }

try {
    if (Test-Path $destinationPath) { [IO.File]::Replace($imagePath,$destinationPath,"$destinationPath.previous") }
    else { [IO.File]::Move($imagePath,$destinationPath) }
    Write-Host "PollikOS built: build/$ImageName ($($kernelBytes.Length) kernel bytes, $kernelSectors sectors loaded at 1 MiB)"
} catch [IO.IOException] {
    Write-Host "Build ready: $imagePath. Close QEMU; run.ps1 will install it at next launch."
}

