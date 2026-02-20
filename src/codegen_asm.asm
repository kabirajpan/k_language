; ============================================================
; codegen_asm.asm - K Language Codegen Hot Path
; ============================================================
; All functions follow System V AMD64 ABI
; rdi = arg1, rsi = arg2, rdx = arg3
; rax = return value
; rbx, r12-r15 are callee-saved (we preserve them)
; ============================================================

section .text

; ============================================================
; buf_write_str(char *buf, size_t *cursor, const char *str)
;   rdi = output buffer base
;   rsi = pointer to cursor (current write position)
;   rdx = null-terminated string to write
;
; Copies str into buf[*cursor], advances *cursor by length
; ============================================================
global buf_write_str
buf_write_str:
    push    rbx
    push    r12
    push    r13

    mov     r12, rdi            ; r12 = buf base
    mov     r13, rsi            ; r13 = cursor ptr
    mov     rbx, rdx            ; rbx = str ptr

    mov     rax, [r13]          ; rax = current cursor value

.copy_loop:
    movzx   ecx, byte [rbx]    ; load next char
    test    cl, cl
    jz      .done               ; null terminator

    mov     [r12 + rax], cl    ; buf[cursor] = char
    inc     rax
    inc     rbx
    jmp     .copy_loop

.done:
    mov     [r13], rax          ; update cursor

    pop     r13
    pop     r12
    pop     rbx
    ret

; ============================================================
; buf_write_int(char *buf, size_t *cursor, long val)
;   rdi = output buffer base
;   rsi = pointer to cursor
;   rdx = integer value to write as decimal string
;
; Converts val to decimal and appends to buf
; ============================================================
global buf_write_int
buf_write_int:
    push    rbx
    push    r12
    push    r13
    push    r14
    push    r15

    mov     r12, rdi            ; r12 = buf base
    mov     r13, rsi            ; r13 = cursor ptr
    mov     r14, rdx            ; r14 = value

    ; use stack as temp digit buffer (max 20 digits for 64-bit)
    sub     rsp, 24
    lea     r15, [rsp + 23]     ; r15 = end of temp buffer
    mov     byte [r15], 0
    dec     r15

    ; handle zero
    test    r14, r14
    jnz     .convert

    mov     byte [r15], '0'
    dec     r15
    jmp     .write

.convert:
    test    r14, r14
    jz      .write

    mov     rax, r14
    xor     edx, edx
    mov     rcx, 10
    div     rcx                 ; rax = quotient, rdx = remainder
    mov     r14, rax

    add     dl, '0'
    mov     [r15], dl
    dec     r15
    jmp     .convert

.write:
    inc     r15                 ; r15 now points to first digit

    mov     rax, [r13]          ; rax = cursor

.write_loop:
    movzx   ecx, byte [r15]
    test    cl, cl
    jz      .int_done

    mov     [r12 + rax], cl
    inc     rax
    inc     r15
    jmp     .write_loop

.int_done:
    mov     [r13], rax          ; update cursor

    add     rsp, 24
    pop     r15
    pop     r14
    pop     r13
    pop     r12
    pop     rbx
    ret

; ============================================================
; buf_flush(char *buf, size_t len, const char *filename)
;   rdi = buffer
;   rsi = length
;   rdx = filename (null-terminated)
;
; Opens file, writes buffer in one syscall, closes.
; Returns 0 on success, -1 on error.
; ============================================================
global buf_flush
buf_flush:
    push    rbx
    push    r12
    push    r13
    push    r14

    mov     r12, rdi            ; r12 = buffer
    mov     r13, rsi            ; r13 = length
    mov     r14, rdx            ; r14 = filename

    ; sys_open(filename, O_WRONLY|O_CREAT|O_TRUNC, 0644)
    mov     rax, 2              ; sys_open
    mov     rdi, r14
    mov     rsi, 0x241          ; O_WRONLY | O_CREAT | O_TRUNC
    mov     rdx, 0644o
    syscall

    cmp     rax, 0
    jl      .open_fail

    mov     rbx, rax            ; rbx = fd

    ; sys_write(fd, buf, len)
    mov     rax, 1
    mov     rdi, rbx
    mov     rsi, r12
    mov     rdx, r13
    syscall

    ; sys_close(fd)
    mov     rax, 3
    mov     rdi, rbx
    syscall

    xor     eax, eax            ; return 0 = success
    jmp     .done

.open_fail:
    mov     eax, -1

.done:
    pop     r14
    pop     r13
    pop     r12
    pop     rbx
    ret


section .note.GNU-stack noalloc noexec nowrite progbits
