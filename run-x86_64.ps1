param(
    [ValidateSet('tcg','whpx')][string]$Accel = 'tcg',
    [switch]$Headless,
    [switch]$NoLaunch,
    [string]$DataImage = '',
    [int]$MemoryMiB = 256
)
$ErrorActionPreference = 'Stop'
$image = Join-Path $PSScriptRoot 'build/x86_64/system/PollikOS-x86_64.img'
if (!$DataImage) { $DataImage = Join-Path $PSScriptRoot 'build/x86_64/system/PollikData-system.img' }
foreach ($path in @($image,$DataImage)) {
    if (!(Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "Missing image: $path. Run build-x86_64.ps1 -Production first. This launcher never formats disks."
    }
}
if ($MemoryMiB -lt 64) { throw 'The native target needs at least 64 MiB.' }
$image = (Resolve-Path -LiteralPath $image).Path
$DataImage = (Resolve-Path -LiteralPath $DataImage).Path
$display = if ($Headless) { 'none' } else { 'gtk,zoom-to-fit=off' }
$qemuArgs = @('-name','PollikOS native x86-64','-machine','pc','-accel',$Accel,
    '-cpu','qemu64','-smp','1','-m',"$MemoryMiB",'-vga','std','-display',$display,
    '-drive',"file=$image,format=raw,if=ide,index=0,snapshot=on",
    '-drive',"file=$DataImage,format=raw,if=ide,index=1,snapshot=on",
    '-nic','none','-serial','stdio')
Write-Host 'Native x86-64 kernel and .pol applications; desktop migration is incomplete.'
Write-Host 'Both disks use snapshots: changes in this session are temporary.'
Write-Host ('qemu-system-x86_64 ' + (($qemuArgs | ForEach-Object { '"' + $_ + '"' }) -join ' '))
if (!$NoLaunch) { & qemu-system-x86_64 @qemuArgs }
