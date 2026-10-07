param(
    [string]$ImageName = "PollikOS-Alpha.img",
    [string]$DataImagePath,
    [string]$Resolution = 'auto',
    [switch]$Fullscreen,
    [switch]$Headless,
    [ValidateSet('auto','tcg','whpx')][string]$Accel = 'auto',
    [ValidateSet('sdl','gtk')][string]$Display = 'gtk',
    [ValidateSet('max','qemu64')][string]$Cpu = 'max',
    [switch]$NoLaunch
)
$ErrorActionPreference = 'Stop'
Set-Location $PSScriptRoot
$dataPath = if ([string]::IsNullOrWhiteSpace($DataImagePath)) {
    Join-Path $PSScriptRoot 'build/PollikData.img'
} else {
    [IO.Path]::GetFullPath($DataImagePath)
}
$pendingImage = "$PSScriptRoot/build/$ImageName.pending"
$currentImage = "$PSScriptRoot/build/$ImageName"
if (Test-Path $pendingImage) {
    try {
        if(Test-Path $currentImage) { [IO.File]::Replace($pendingImage,$currentImage,"$currentImage.previous") }
        else { [IO.File]::Move($pendingImage,$currentImage) }
    } catch [IO.IOException] { throw 'Close the running QEMU window before launching the new build.' }
}
if (!(Get-Command qemu-system-x86_64 -ErrorAction SilentlyContinue)) {
    $bundledQemu = Join-Path $env:USERPROFILE 'scoop\apps\qemu\current'
    if (Test-Path (Join-Path $bundledQemu 'qemu-system-x86_64.exe')) { $env:PATH = "$bundledQemu;$env:PATH" }
    else { throw 'Nie znaleziono QEMU. Dodaj qemu-system-x86_64.exe do PATH.' }
}
function Select-PollikAccel {
    if ($Accel -ne 'auto') { return @($Accel, 'explicit -Accel request') }
    try {
        if (-not ('PollikHypervisor' -as [type])) {
            Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class PollikHypervisor {
    [DllImport("WinHvPlatform.dll")]
    public static extern int WHvGetCapability(uint code, out uint value, uint size, out uint written);
}
'@
        }
        [uint32]$present = 0; [uint32]$written = 0
        $hr = [PollikHypervisor]::WHvGetCapability(0, [ref]$present, 4, [ref]$written)
        if ($hr -ne 0 -or $written -ne 4 -or $present -eq 0) {
            return @('tcg', "Windows Hypervisor Platform unavailable (HRESULT=$hr, present=$present)")
        }
    } catch { return @('tcg', 'Windows Hypervisor Platform unavailable: ' + $_.Exception.Message) }
    $probeDir = Join-Path $env:TEMP ('pollikos-accel-' + [guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory $probeDir | Out-Null
    $probeErr = Join-Path $probeDir 'stderr.txt'
    $probeOut = Join-Path $probeDir 'stdout.txt'
    $probeCpu = if ($script:PSBoundParameters.ContainsKey('Cpu')) { $Cpu } else { 'qemu64' }
    if ($probeCpu -eq 'qemu64') { $probeCpu = 'qemu64,+rdrand' }
    $probe = Start-Process qemu-system-x86_64 -WindowStyle Hidden -PassThru -ArgumentList @(
        '-S','-display','none','-machine','pc','-accel','whpx','-cpu',$probeCpu,'-smp','1',
        '-m','64M','-nodefaults','-monitor','none','-serial','none') `
        -RedirectStandardError $probeErr -RedirectStandardOutput $probeOut
    if ($probe.WaitForExit(1200)) {
        $detail = (Get-Content $probeErr -Raw -ErrorAction SilentlyContinue).Trim()
        return @('tcg', "QEMU rejected WHPX (exit=$($probe.ExitCode)): $detail")
    }
    Stop-Process -Id $probe.Id -Force
    return @('whpx', 'Windows Hypervisor Platform present; QEMU WHPX initialization accepted')
}
$accelSelection = Select-PollikAccel
$Accel = $accelSelection[0]
if ($Accel -eq 'whpx' -and -not $PSBoundParameters.ContainsKey('Cpu')) {
    $Cpu = 'qemu64'
    $accelSelection[1] += '; default CPU qemu64 (WHPX-compatible); explicit -Cpu is preserved'
}
Write-Host "Acceleration selected: $Accel; reason: $($accelSelection[1])"
$qemuCpu = if ($Cpu -eq 'qemu64') { 'qemu64,+rdrand' } else { $Cpu }
if (!(Test-Path "build/$ImageName") -or !(Test-Path $dataPath)) {
    if ($Headless) { throw 'Headless measurement requires existing system and data images; refusing to build or format images.' }
    if (-not [string]::IsNullOrWhiteSpace($DataImagePath)) { throw "Requested data image not found: $dataPath" }
    & ./build.ps1 -ImageName $ImageName
}
if ($Resolution -eq 'auto') {
    if ($Headless) { $Resolution = '1920x1080' }
    else {
        Add-Type -AssemblyName System.Windows.Forms
        $monitor = [System.Windows.Forms.Screen]::FromPoint([System.Windows.Forms.Cursor]::Position)
        $bounds = $monitor.Bounds
        # Prefer standard 16:9 modes so a 27" (2560x1440) or 1080p panel looks native.
        $modes = @(@(2560,1440), @(1920,1080), @(1600,900), @(1280,720))
        $screenWidth = 0; $screenHeight = 0
        foreach ($m in $modes) {
            if ($m[0] -le $bounds.Width -and $m[1] -le $bounds.Height) { $screenWidth = $m[0]; $screenHeight = $m[1]; break }
        }
        if ($screenWidth -eq 0) {
            $screenWidth = [Math]::Min(3440, [Math]::Max(1024, [int]([Math]::Floor($bounds.Width / 8) * 8)))
            $screenHeight = [Math]::Min(1440, [Math]::Max(720, $bounds.Height - 70))
        }
        $Resolution = "${screenWidth}x${screenHeight}"
    }
}
if ($Resolution -notmatch '^(\d{4})x(\d{3,4})$') { throw 'Rozdzielczosc: auto lub np. 1920x1080.' }
$screenWidth = [int]$Matches[1]; $screenHeight = [int]$Matches[2]
if ($screenWidth -lt 1024 -or $screenWidth -gt 3440 -or $screenWidth % 8 -ne 0 -or $screenHeight -lt 720 -or $screenHeight -gt 1440) { throw 'Obslugiwany zakres: 1024x720 do 3440x1440; szerokosc podzielna przez 8.' }
# zoom-to-fit=off makes the QEMU window track the guest mode exactly, so a warm
# reboot (Restart) never leaves a stale, wrongly-scaled window behind.
$displayBackend = if ($Headless) { 'none' } elseif ($Display -eq 'gtk') { 'gtk,zoom-to-fit=off' } else { 'sdl' }
$displayOptions = @('-display', $displayBackend, '-fw_cfg', "name=opt/pollikos/display,string=$Resolution")
if ($Fullscreen -and -not $Headless) { $displayOptions += '-full-screen' }
# QEMU's dsound backend cannot always create the AC'97 capture voices
# (ac97.pi/ac97.mc) on Windows hosts, which floods the console with
# "Could not create a backend for voice" and leaves the guest silent.
# Probe candidates and use the first one that initializes cleanly:
# SDL (real audio) -> dsound -> none (silent, no errors).
function Test-PollikAudioBackend([string]$Backend) {
    $errFile = Join-Path $env:TEMP "pollikos_audio_$Backend.err"
    $outFile = Join-Path $env:TEMP "pollikos_audio_$Backend.out"
    Remove-Item $errFile, $outFile -ErrorAction SilentlyContinue
    $probe = @('-S', '-display', 'none', '-machine', 'pc',
               '-audiodev', "$Backend,id=probe", '-device', 'AC97,audiodev=probe',
               '-monitor', 'none', '-serial', 'none')
    $proc = Start-Process qemu-system-x86_64 -WindowStyle Hidden -ArgumentList $probe -PassThru `
        -RedirectStandardError $errFile -RedirectStandardOutput $outFile
    Start-Sleep -Milliseconds 900
    if (-not $proc.HasExited) { Stop-Process -Id $proc.Id -Force -ErrorAction SilentlyContinue }
    Start-Sleep -Milliseconds 200
    $text = Get-Content $errFile -Raw -ErrorAction SilentlyContinue
    return [string]::IsNullOrWhiteSpace($text) -or ($text -notmatch 'Could not|Can not open|backend for voice')
}
$audioBackend = 'none'
if (-not $Headless) {
    foreach ($candidate in @('sdl', 'dsound', 'none')) {
        if (Test-PollikAudioBackend $candidate) { $audioBackend = $candidate; break }
    }
}
Write-Host "PollikOS: $Resolution (120 Hz / 120 FPS), 2 GiB RAM; accel=$Accel; display=$displayBackend; cpu=$Cpu; smp=1; headless=$Headless"
Write-Host "Audio backend: $audioBackend"
$qmpOptions = @()
if ($Headless) {
    $listener = [Net.Sockets.TcpListener]::new([Net.IPAddress]::Loopback, 0)
    $listener.Start(); $qmpPort = $listener.LocalEndpoint.Port; $listener.Stop()
    $qmpAddress = "tcp:127.0.0.1:$qmpPort,server=on,wait=off"
    $qmpOptions = @('-qmp', $qmpAddress, '-snapshot')
    Write-Host "Headless QMP: $qmpAddress; all disks use transient snapshots"
}
if (-not $Headless) {
    Write-Host "System wallpaper sync: validating and installing stock files on $dataPath"
    & python tools/sync_system_files.py $dataPath
    if ($LASTEXITCODE -ne 0) {
        Write-Warning "Stock wallpaper sync refused (exit $LASTEXITCODE); continuing to QEMU. The guest uses its procedural fallback when files are unavailable."
    }
} else {
    Write-Host 'System wallpaper sync: skipped in headless snapshot mode; source data image remains untouched.'
}
$qemuArgs = @('-name','PollikOS v0.0.001','-machine','pc','-accel',$Accel,'-cpu',$qemuCpu,'-smp','1',
    '-rtc','base=utc','-m','2G','-device','VGA,vgamem_mb=32,refresh_rate=120') + $displayOptions + @(
    '-drive',"format=raw,file=build/$ImageName,if=ide,index=0",
    '-drive',"format=raw,file=$dataPath,if=ide,index=1",
    '-netdev','user,id=net0','-device','rtl8139,netdev=net0',
    '-audiodev',"$audioBackend,id=snd0",'-device','AC97,audiodev=snd0',
    '-serial','file:build/serial.log') + $qmpOptions
$quotedArgs = $qemuArgs | ForEach-Object { if ($_ -match '[\s,]') { '"' + $_ + '"' } else { $_ } }
Write-Host ('QEMU command: qemu-system-x86_64 ' + ($quotedArgs -join ' '))
if ($NoLaunch) { exit 0 }
& qemu-system-x86_64 @qemuArgs
if ($LASTEXITCODE -ne 0) { throw "QEMU zakonczyl prace z bledem $LASTEXITCODE. Zamknij inne okno PollikOS, jezeli dysk jest zajety." }
