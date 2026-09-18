; PollikOS Stage 2: loads the kernel image above 1 MiB, collects E820 and
; VBE data, then enters protected mode and jumps to the kernel entry.
;
; The kernel is read from LBA 9 in 64-sector (32 KiB) chunks into a real-mode
; buffer at 0x10000 and copied to 0x100000 + n*32 KiB with a flat "unreal"
; DS/ES (4 GiB limit). The image size in sectors is patched by build.ps1 into
; kernel_sectors (last word of this 4096-byte stage), so the kernel is no
; longer limited to the 512 KiB that fit below the EBDA.
bits 16
org 0x8000
    mov [drive], dl
    ; Fast A20 gate: required before writing above 1 MiB.
    in al, 0x92
    or al, 2
    and al, 0xfe
    out 0x92, al
    mov ax, [kernel_sectors]
    test ax, ax
    jz fail
    add ax, 63
    shr ax, 6
    mov [chunks], ax
    mov edi, KERNEL_LOAD
.load:
    mov dl, [drive]
    mov si, packet
    mov ah, 0x42
    int 0x13
    jc fail
    ; BIOS calls may reload segment descriptors; re-establish unreal DS/ES
    ; (base 0, 4 GiB limit) before every copy above 1 MiB.
    cli
    lgdt [gdt_descriptor]
    mov eax, cr0
    or al, 1
    mov cr0, eax
    jmp short .flush
.flush:
    mov bx, 0x10
    mov ds, bx
    mov es, bx
    and al, 0xfe
    mov cr0, eax
    xor bx, bx
    mov ds, bx
    mov es, bx
    mov esi, CHUNK_BUFFER
    mov ecx, CHUNK_BYTES / 4
    a32 rep movsd
    sti
    add dword [packet+8], 64
    dec word [chunks]
    jnz .load
    xor ax, ax
    mov es, ax
    mov di, 0x6004
    xor ebx, ebx
    xor bp, bp
.e820_loop:
    mov eax, 0xe820
    mov edx, 0x534d4150
    mov ecx, 24
    mov dword [es:di+20], 1
    int 0x15
    jc .e820_done
    cmp eax, 0x534d4150
    jne .e820_done
    cmp cl, 20
    jb .e820_skip
    cmp cl, 24
    jb .e820_valid
    test byte [es:di+20], 1
    jz .e820_skip
.e820_valid:
    inc bp
    add di, 24
.e820_skip:
    test ebx, ebx
    jz .e820_done
    cmp bp, 64
    jae .e820_done
    jmp .e820_loop
.e820_done:
    movzx eax, bp
    mov [0x6000], eax
    mov ax, 0x4f01
    mov cx, 0x118
    mov di, 0x7000
    int 0x10
    cmp ax, 0x004f
    jne fail
    mov ax, 0x4f02
    mov bx, 0x4118
    int 0x10
    cmp ax, 0x004f
    jne fail
    cli
    in al, 0x92
    or al, 2
    out 0x92, al
    lgdt [gdt_descriptor]
    mov eax, cr0
    or eax, 1
    mov cr0, eax
    jmp 0x08:protected
fail:
    mov ax, 0x0e45
    int 0x10
    cli
    hlt
    jmp fail
KERNEL_LOAD  equ 0x100000
CHUNK_BUFFER equ 0x10000
CHUNK_BYTES  equ 64 * 512
drive: db 0
chunks: dw 0
align 4
packet: db 16, 0
    dw 64
    dw 0, CHUNK_BUFFER >> 4
    dq 9
align 8
gdt:
    dq 0
    dq 0x00cf9a000000ffff
    dq 0x00cf92000000ffff
gdt_descriptor:
    dw $-gdt-1
    dd gdt
bits 32
protected:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov fs, ax
    mov gs, ax
    mov esp, 0x9fc00
    cld
    jmp KERNEL_LOAD
times 4094-($-$$) db 0
; Kernel image length in 512-byte sectors; written by build.ps1.
kernel_sectors: dw 0
