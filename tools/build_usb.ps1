param([ValidateSet('i386','x86_64')][string]$Arch, [string]$Output, [switch]$Debug)
$ErrorActionPreference='Stop'
$sourceRoot='third_party/libpayload-usb'
$objectDir="$Output/usb"
New-Item -ItemType Directory -Force $objectDir | Out-Null
$flags=@("--target=$Arch-none-elf",'-ffreestanding','-fno-builtin','-fno-pic','-fno-pie','-fno-stack-protector','-ffunction-sections','-fdata-sections','-Oz',"-I$sourceRoot/include")
if($Debug){$flags+='-DUSB_DEBUG=1'}
if($Arch -eq 'x86_64'){$flags+=@('-DPOLLIK_X64=1','-mno-red-zone','-mgeneral-regs-only','-Isdk/include')}
else{$flags+=@('-march=i386','-mno-sse','-mno-mmx','-Ikernel/include')}
$objects=@()
foreach($source in (Get-ChildItem -LiteralPath "$sourceRoot/drivers" -Filter '*.c' | Sort-Object Name)){
    $object="$objectDir/$($source.BaseName).o"
    & clang @flags -c $source.FullName -o $object
    if($LASTEXITCODE -ne 0){throw "USB compile failed: $($source.Name)"}
    $objects+=$object
}
foreach($name in @('usb_input','usb_platform')){
    $object="$objectDir/$name.o"
    & clang @flags -Wall -Wextra -Werror -c "kernel/$name.c" -o $object
    if($LASTEXITCODE -ne 0){throw "USB adapter compile failed: $name"}
    $objects+=$object
}
& llvm-ar rcs "$Output/libusb-input.a" @objects
if($LASTEXITCODE -ne 0){throw 'USB archive failed'}
