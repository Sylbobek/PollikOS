param([string]$ImageName = "PollikOS-Alpha.img", [string]$Resolution = 'auto', [switch]$Fullscreen)
$ErrorActionPreference = 'Stop'
Set-Location $PSScriptRoot
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
if (!(Test-Path "build/$ImageName") -or !(Test-Path build/PollikData.img)) { & ./build.ps1 -ImageName $ImageName }
if ($Resolution -eq 'auto') {
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
if ($Resolution -notmatch '^(\d{4})x(\d{3,4})$') { throw 'Rozdzielczosc: auto lub np. 1920x1080.' }
$screenWidth = [int]$Matches[1]; $screenHeight = [int]$Matches[2]
if ($screenWidth -lt 1024 -or $screenWidth -gt 3440 -or $screenWidth % 8 -ne 0 -or $screenHeight -lt 720 -or $screenHeight -gt 1440) { throw 'Obslugiwany zakres: 1024x720 do 3440x1440; szerokosc podzielna przez 8.' }
# zoom-to-fit=off makes the QEMU window track the guest mode exactly, so a warm
# reboot (Restart) never leaves a stale, wrongly-scaled window behind.
$displayOptions = @('-display', 'gtk,zoom-to-fit=off', '-fw_cfg', "name=opt/pollikos/display,string=$Resolution")
if ($Fullscreen) { $displayOptions += '-full-screen' }
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
    $proc = Start-Process qemu-system-x86_64 -ArgumentList $probe -PassThru `
        -RedirectStandardError $errFile -RedirectStandardOutput $outFile
    Start-Sleep -Milliseconds 900
    if (-not $proc.HasExited) { Stop-Process -Id $proc.Id -Force -ErrorAction SilentlyContinue }
    Start-Sleep -Milliseconds 200
    $text = Get-Content $errFile -Raw -ErrorAction SilentlyContinue
    return [string]::IsNullOrWhiteSpace($text) -or ($text -notmatch 'Could not|Can not open|backend for voice')
}
$audioBackend = 'none'
foreach ($candidate in @('sdl', 'dsound', 'none')) {
    if (Test-PollikAudioBackend $candidate) { $audioBackend = $candidate; break }
}
Write-Host "PollikOS: $Resolution (120 Hz / 120 FPS), 2 GiB RAM"
Write-Host "Audio backend: $audioBackend"
& qemu-system-x86_64 -name 'Pollik OS v0.1 Alpha' -machine pc -cpu max -rtc base=utc -m 2G -device VGA,vgamem_mb=32,refresh_rate=120 @displayOptions -drive "format=raw,file=build/$ImageName,if=ide,index=0" -drive 'format=raw,file=build/PollikData.img,if=ide,index=1' -netdev 'user,id=net0' -device 'rtl8139,netdev=net0' -audiodev "$audioBackend,id=snd0" -device 'AC97,audiodev=snd0' -serial 'file:build/serial.log'
if ($LASTEXITCODE -ne 0) { throw "QEMU zakonczyl prace z bledem $LASTEXITCODE. Zamknij inne okno PollikOS, jezeli dysk jest zajety." }
