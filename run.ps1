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
    $bounds = if ($Fullscreen) { $monitor.Bounds } else { $monitor.WorkingArea }
    $screenWidth = [Math]::Min(3440, [Math]::Max(1024, $bounds.Width))
    $screenHeight = [Math]::Min(1440, [Math]::Max(720, $bounds.Height - $(if ($Fullscreen) { 0 } else { 70 })))
    $screenWidth = [int]([Math]::Floor($screenWidth / 8) * 8)
    $Resolution = "${screenWidth}x${screenHeight}"
}
if ($Resolution -notmatch '^(\d{4})x(\d{3,4})$') { throw 'Rozdzielczosc: auto lub np. 1920x1080.' }
$screenWidth = [int]$Matches[1]; $screenHeight = [int]$Matches[2]
if ($screenWidth -lt 1024 -or $screenWidth -gt 3440 -or $screenWidth % 8 -ne 0 -or $screenHeight -lt 720 -or $screenHeight -gt 1440) { throw 'Obslugiwany zakres: 1024x720 do 3440x1440; szerokosc podzielna przez 8.' }
$displayOptions = @('-fw_cfg', "name=opt/pollikos/display,string=$Resolution")
if ($Fullscreen) { $displayOptions += '-full-screen' }
Write-Host "PollikOS: $Resolution (120 Hz / 120 FPS), 2 GiB RAM"
& qemu-system-x86_64 -name 'Pollik OS v0.1 Alpha' -machine pc -cpu max -rtc base=utc -m 2G -device VGA,vgamem_mb=32,refresh_rate=120 @displayOptions -drive "format=raw,file=build/$ImageName,if=ide,index=0" -drive 'format=raw,file=build/PollikData.img,if=ide,index=1' -netdev 'user,id=net0' -device 'rtl8139,netdev=net0' -serial 'file:build/serial.log'
if ($LASTEXITCODE -ne 0) { throw "QEMU zakonczyl prace z bledem $LASTEXITCODE. Zamknij inne okno PollikOS, jezeli dysk jest zajety." }
