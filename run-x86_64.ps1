param(
    [ValidateSet('tcg','whpx')][string]$Accel = 'tcg',
    [switch]$Headless,
    [switch]$NoLaunch,
    [string]$DataImage = '',
    [switch]$NoNetwork,
    [switch]$Audio,
    [int]$MemoryMiB = 8192
)
$ErrorActionPreference = 'Stop'
$image = Join-Path $PSScriptRoot 'build/x86_64/system/PollikOS-x86_64.img'
if (!$DataImage) { $DataImage = Join-Path $PSScriptRoot 'build/x86_64/system/PollikData-system-30g.img' }
foreach ($path in @($image,$DataImage)) {
    if (!(Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "Missing image: $path. Run build-x86_64.ps1 -Production first. This launcher never formats disks."
    }
}
if ($MemoryMiB -lt 64) { throw 'The native target needs at least 64 MiB.' }
$image = (Resolve-Path -LiteralPath $image).Path
$DataImage = (Resolve-Path -LiteralPath $DataImage).Path
$display = if ($Headless) { 'none' } else { 'gtk,zoom-to-fit=off' }
$nativeMachine = if($Audio){'pc,pcspk-audiodev=native_audio'}else{'pc'}
$qemuArgs = @('-name','PollikOS v0.0.001','-machine',$nativeMachine,'-accel',$Accel,
    '-cpu','qemu64,+rdrand','-rtc','base=utc','-smp','1','-m',"$MemoryMiB",'-vga','std','-display',$display,
    '-drive',"file=$image,format=raw,if=ide,index=0,snapshot=on",
    '-drive',"file=$DataImage,format=raw,if=ide,index=1,snapshot=on",
    '-serial','stdio')
if($NoNetwork){$qemuArgs+=@('-nic','none')}
else{$qemuArgs+=@('-netdev','user,id=native_net','-device','rtl8139,netdev=native_net')}
if($Audio){$qemuArgs+=@('-audiodev','dsound,id=native_audio','-device','AC97,audiodev=native_audio')}
Write-Host 'Native x86-64 kernel and .pol applications; desktop migration is incomplete.'
Write-Host 'Both disks use snapshots: changes in this session are temporary.'
Write-Host "Guest RAM: $MemoryMiB MiB. Disk profile: 30 GiB; PollikFS v2 usable filesystem remains about 32 MiB."
Write-Host ('qemu-system-x86_64 ' + (($qemuArgs | ForEach-Object { '"' + $_ + '"' }) -join ' '))
if (!$NoLaunch) { & qemu-system-x86_64 @qemuArgs }
