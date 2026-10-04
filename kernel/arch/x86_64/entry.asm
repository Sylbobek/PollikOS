; Entered in 32-bit protected mode by the unchanged PollikOS stage 2.
bits 32
section .entry
global _start
extern __bss_start, __bss_end, __text_start, __text_end
extern __rodata_start, __rodata_end, kernel64_main
global boot_pml4, boot_pt, boot_stack_top, boot_stack_guard
global df_stack_top, df_stack_guard, nmi_stack_top, nmi_stack_guard
global gdt64
_start:
    cli
    cld
    ; COM1 diagnostics also work on CPUs rejected before long mode.
    mov dx, 0x3f9
    xor al, al
    out dx, al
    mov dx, 0x3fb
    mov al, 0x80
    out dx, al
    mov dx, 0x3f8
    mov al, 1
    out dx, al
    inc dx
    xor al, al
    out dx, al
    mov dx, 0x3fb
    mov al, 3
    out dx, al
    pushfd
    pop eax
    mov ecx, eax
    xor eax, 1 << 21
    push eax
    popfd
    pushfd
    pop eax
    push ecx
    popfd
    xor eax, ecx
    test eax, 1 << 21
    jz bad_cpu
    xor eax, eax
    cpuid
    cmp eax, 1
    jb bad_cpu
    mov eax, 1
    cpuid
    and edx, (1 << 5) | (1 << 6) ; MSR and PAE
    cmp edx, (1 << 5) | (1 << 6)
    jne bad_cpu
    mov eax, 0x80000000
    cpuid
    cmp eax, 0x80000001
    jb bad_cpu
    mov eax, 0x80000001
    cpuid
    and edx, (1 << 29) | (1 << 20) | (1 << 11) ; long mode, NX and SYSCALL are mandatory
    cmp edx, (1 << 29) | (1 << 20) | (1 << 11)
    jne bad_cpu
    ; Require BIOS-confirmed usable RAM for image, tables and stacks.
    ; Reject overlapping reserved entries; never invent a RAM fallback.
    mov ecx, [0x6000]
    test ecx, ecx
    jz bad_memory
    cmp ecx, 64
    ja bad_memory
    mov esi, 0x6004
    xor ebp, ebp
.memory:
    test dword [esi+20], 1
    jz .next
    cmp dword [esi+4], 0
    jne .next
    mov eax, [esi]
    cmp eax, __bss_end
    jae .next
    mov edx, [esi+12]
    mov ebx, [esi+8]
    add ebx, eax
    adc edx, 0
    jc bad_memory
    test edx, edx
    jnz .overlap
    cmp ebx, 0x100000
    jbe .next
.overlap:
    cmp dword [esi+16], 1
    jne bad_memory
    cmp eax, 0x100000
    ja .next
    test edx, edx
    jnz .usable
    cmp ebx, __bss_end
    jb .next
.usable:
    mov ebp, 1
.next:
    add esi, 24
    loop .memory
    test ebp, ebp
    jz bad_memory
    mov edi, __bss_start
    mov ecx, __bss_end
    sub ecx, edi
    xor eax, eax
    rep stosb
    mov esp, boot_stack_top
    mov eax, boot_pdpt
    or eax, 3
    mov [boot_pml4], eax
    mov eax, boot_pd
    or eax, 3
    mov [boot_pdpt], eax
    mov eax, boot_pt
    or eax, 3
    mov [boot_pd], eax
    mov edi, boot_pt + 8 ; leave page zero unmapped
    mov eax, 0x1000
.pages:
    mov ebx, eax
    or ebx, 3           ; supervisor, writable
    mov edx, 0x80000000 ; NX
    cmp eax, __text_start
    jb .rodata
    cmp eax, __text_end
    jae .rodata
    and ebx, ~2
    xor edx, edx       ; text: executable, read-only
.rodata:
    cmp eax, __rodata_start
    jb .guard
    cmp eax, __rodata_end
    jae .guard
    and ebx, ~2        ; constants: read-only, NX
.guard:
    cmp eax, boot_stack_guard
    je .absent
    cmp eax, df_stack_guard
    je .absent
    cmp eax, nmi_stack_guard
    jne .store
.absent:
    xor ebx, ebx
    xor edx, edx
.store:
    mov [edi], ebx
    mov [edi+4], edx
    add edi, 8
    add eax, 4096
    cmp eax, 0x200000
    jb .pages
    lgdt [gdt_pointer32]
    mov eax, cr4
    or eax, 1 << 5
    mov cr4, eax
    mov eax, boot_pml4
    mov cr3, eax
    mov ecx, 0xc0000080
    rdmsr
    or eax, (1 << 8) | (1 << 11) ; LME, NXE; SCE deliberately disabled
    wrmsr
    mov eax, cr0
    or eax, (1 << 31) | (1 << 16) ; PG and WP
    mov cr0, eax
    jmp 0x08:long_entry
bad_cpu:
    mov esi, cpu_message
    jmp boot_error
bad_memory:
    mov esi, memory_message
boot_error:
    lodsb
    test al, al
    jz .halt
    mov bl, al
    mov dx, 0x3fd
.wait:
    in al, dx
    test al, 0x20
    jz .wait
    mov al, bl
    mov dx, 0x3f8
    out dx, al
    jmp boot_error
.halt:
    cli
    hlt
    jmp .halt
bits 64
long_entry:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov ss, ax
    xor eax, eax
    mov fs, ax
    mov gs, ax
    mov rsp, boot_stack_top
    xor ebp, ebp
    call kernel64_main
.halt:
    cli
    hlt
    jmp .halt
section .rodata
cpu_message: db '[X64] UNSUPPORTED CPU: CPUID/MSR/PAE/LM/NX required', 10, 0
memory_message: db '[X64] UNSUPPORTED MEMORY: usable E820 bootstrap range required', 10, 0
gdt_pointer32:
    dw 7*8-1
    dd gdt64
section .data
align 8
gdt64:
    dq 0
    dq 0x00af9b000000ffff ; 64-bit code, accessed
    dq 0x00cf93000000ffff ; data, accessed
    dq 0, 0              ; 16-byte TSS descriptor populated by C
    dq 0x00cff3000000ffff ; Ring 3 data, accessed (SYSRET SS)
    dq 0x00affb000000ffff ; Ring 3 64-bit code, accessed (SYSRET CS)
section .bss
align 4096
boot_pml4: resb 4096
boot_pdpt: resb 4096
boot_pd: resb 4096
boot_pt: resb 4096
boot_stack_guard: resb 4096
    resb 16384
boot_stack_top:
df_stack_guard: resb 4096
    resb 16384
df_stack_top:
nmi_stack_guard: resb 4096
    resb 16384
nmi_stack_top:
