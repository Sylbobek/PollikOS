bits 16
org 0x7c00
start:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7c00
    sti
    mov [drive], dl
    mov si, packet
    mov ah, 0x42
    int 0x13
    jc error
    mov dl, [drive]
    jmp 0:0x8000
error:
    mov si, msg
.loop:
    lodsb
    test al, al
    jz $
    mov ah, 0x0e
    int 0x10
    jmp .loop
drive: db 0
msg: db 'PollikOS: disk read failed', 0
align 4
packet: db 16, 0
    dw 8
    dw 0x8000, 0
    dq 1
times 510-($-$$) db 0
dw 0xaa55
