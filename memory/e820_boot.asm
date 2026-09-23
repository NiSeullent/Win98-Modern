; Autonomous BIOS E820 floppy boot sector. Prints firmware ranges without DOS,
; a guest keyboard, or touching the hard disk. This is not Win98 memory evidence.
bits 16
org 07C00h

start:
    cli
    xor ax, ax
    mov ss, ax
    mov sp, 07C00h
    mov ds, ax
    mov es, ax
    sti
    mov si, banner
    call print_string
    xor ebx, ebx
    xor bp, bp

.next:
    cmp bp, 32
    jae .limit
    mov di, entry
    mov dword [entry + 20], 1
    mov eax, 0E820h
    mov edx, 0534D4150h
    mov ecx, 24
    push ds
    push es
    int 15h
    pop es
    pop ds
    jc .carry
    cmp eax, 0534D4150h
    jne .bad
    cmp ecx, 20
    jb .bad
    mov [continuation], ebx
    inc bp

    mov si, base_text
    call print_string
    mov si, entry
    mov cx, 8
    call print_hex_reversed
    mov si, length_text
    call print_string
    mov si, entry + 8
    mov cx, 8
    call print_hex_reversed
    mov si, type_text
    call print_string
    mov si, entry + 16
    mov cx, 4
    call print_hex_reversed
    mov si, newline
    call print_string

    mov ebx, [continuation]
    test ebx, ebx
    jnz .next
    mov si, done_text
    jmp .end

.carry:
    cmp bp, 0
    jne .done
    mov si, unsupported_text
    jmp .end
.done:
    mov si, done_text
    jmp .end
.bad:
    mov si, bad_text
    jmp .end
.limit:
    mov si, limit_text
.end:
    call print_string
.halt:
    cli
    hlt
    jmp .halt

print_string:
    push ax
.char:
    lodsb
    test al, al
    jz .return
    call putc
    jmp .char
.return:
    pop ax
    ret

print_hex_reversed:
    push si
    push cx
    add si, cx
    dec si
.byte:
    mov al, [si]
    call print_hex_byte
    dec si
    loop .byte
    pop cx
    pop si
    ret

print_hex_byte:
    push ax
    mov ah, al
    shr al, 4
    call print_nibble
    mov al, ah
    and al, 0Fh
    call print_nibble
    pop ax
    ret

print_nibble:
    push ax
    cmp al, 9
    jbe .decimal
    add al, 'A' - 10
    jmp .write
.decimal:
    add al, '0'
.write:
    call putc
    pop ax
    ret

putc:
    push ax
    push bx
    mov ah, 0Eh
    xor bh, bh
    mov bl, 7
    int 10h
    pop bx
    pop ax
    ret

banner db 'BIOS E820 map (firmware only)', 13, 10, 0
base_text db 'B=', 0
length_text db ' L=', 0
type_text db ' T=', 0
newline db 13, 10, 0
done_text db 'E820 complete', 13, 10, 0
unsupported_text db 'E820 unavailable', 13, 10, 0
bad_text db 'E820 invalid reply', 13, 10, 0
limit_text db 'E820 entry limit', 13, 10, 0
continuation dd 0
entry times 24 db 0

times 510 - ($ - $$) db 0
dw 0AA55h
