; Real-mode DOS firmware memory map probe for the disposable VM.
; This reports the BIOS E820 map, not RAM accepted by Windows 98 VMM.
bits 16
org 100h

start:
    push cs
    pop ds
    mov si, banner
    call print_string
    xor ebx, ebx
    mov byte [entries], 0

.next:
    cmp byte [entries], 64
    jae .limit
    mov ax, cs
    mov es, ax
    mov di, entry
    mov dword [entry + 20], 1
    mov eax, 0E820h
    mov edx, 0534D4150h       ; 'SMAP'
    mov ecx, 24
    push ds
    push es
    int 15h
    pop es
    pop ds
    jc .carry
    cmp eax, 0534D4150h
    jne .invalid
    cmp ecx, 20
    jb .invalid
    mov [entry_size], ecx
    mov [continuation], ebx
    inc byte [entries]

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
    cmp dword [entry_size], 24
    jb .legacy_entry
    mov si, attr_text
    call print_string
    mov si, entry + 20
    mov cx, 4
    call print_hex_reversed
    jmp .entry_end
.legacy_entry:
    mov si, legacy_text
    call print_string
.entry_end:
    mov si, newline
    call print_string

    mov ebx, [continuation]
    test ebx, ebx
    jnz .next
    mov si, done_text
    jmp .exit

.carry:
    cmp byte [entries], 0
    jne .done
    mov si, unsupported_text
    jmp .exit_failure
.done:
    mov si, done_text
    jmp .exit
.invalid:
    mov si, invalid_text
    jmp .exit_failure
.limit:
    mov si, limit_text
    jmp .exit_failure
.exit:
    call print_string
    mov ax, 4C00h
    int 21h
.exit_failure:
    call print_string
    mov ax, 4C02h
    int 21h

; DS:SI points to a $-terminated string.
print_string:
    push ax
    push dx
    mov dx, si
    mov ah, 09h
    int 21h
    pop dx
    pop ax
    ret

; Print CX bytes ending at DS:SI in big-endian hexadecimal order.
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
    push dx
    cmp al, 9
    jbe .decimal
    add al, 'A' - 10
    jmp .output
.decimal:
    add al, '0'
.output:
    mov dl, al
    mov ah, 02h
    int 21h
    pop dx
    pop ax
    ret

banner db 'BIOS E820 memory map (firmware, not Windows usable RAM)', 13, 10, '$'
base_text db 'base=0x$'
length_text db ' length=0x$'
type_text db ' type=0x$'
attr_text db ' attrs=0x$'
legacy_text db ' attrs=legacy20$'
newline db 13, 10, '$'
done_text db 'E820 complete', 13, 10, '$'
unsupported_text db 'E820 unavailable', 13, 10, '$'
invalid_text db 'E820 invalid reply', 13, 10, '$'
limit_text db 'E820 exceeded 64 entries', 13, 10, '$'
entries db 0
continuation dd 0
entry_size dd 0
entry times 24 db 0
