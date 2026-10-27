; The engine action reader is a leaf with a stricter register contract than
; ordinary Win64 calls. Its callers retain R8/R9 and XMM1/2 across the call.
; Preserve the ORIGINAL's outputs, changing only the returned float in XMM0.
option casemap:none
EXTERN CvrSwimmingActionOriginal:QWORD
EXTERN CvrSwimmingActionAfter:PROC

.code
CvrSwimmingActionDetour PROC FRAME
    push rbp
    .pushreg rbp
    lea rsp, [rsp-0D0h]
    .allocstack 0D0h
    mov rbp, rsp
    .setframe rbp, 0
    .endprolog
    ; No incoming argument/register/flag is changed before the original leaf.
    mov [rsp+0C0h], rcx
    mov [rsp+0C8h], rdx
    call QWORD PTR [CvrSwimmingActionOriginal]

    mov [rsp+20h], rax
    mov [rsp+28h], rcx
    mov [rsp+30h], rdx
    mov [rsp+38h], r8
    mov [rsp+40h], r9
    mov [rsp+48h], r10
    mov [rsp+50h], r11
    pushfq
    pop rax
    mov [rsp+58h], rax
    movdqu [rsp+60h], xmm0
    movdqu [rsp+70h], xmm1
    movdqu [rsp+80h], xmm2
    movdqu [rsp+90h], xmm3
    movdqu [rsp+0A0h], xmm4
    movdqu [rsp+0B0h], xmm5

    mov rcx, [rsp+0C0h]
    mov rdx, [rsp+0C8h]
    movss xmm2, DWORD PTR [rsp+60h]
    call CvrSwimmingActionAfter
    movss DWORD PTR [rsp+60h], xmm0

    movdqu xmm0, [rsp+60h]
    movdqu xmm1, [rsp+70h]
    movdqu xmm2, [rsp+80h]
    movdqu xmm3, [rsp+90h]
    movdqu xmm4, [rsp+0A0h]
    movdqu xmm5, [rsp+0B0h]
    push QWORD PTR [rsp+58h]
    popfq
    mov rax, [rsp+20h]
    mov rcx, [rsp+28h]
    mov rdx, [rsp+30h]
    mov r8, [rsp+38h]
    mov r9, [rsp+40h]
    mov r10, [rsp+48h]
    mov r11, [rsp+50h]
    lea rsp, [rbp+0D0h]
    pop rbp
    ret
CvrSwimmingActionDetour ENDP
END
