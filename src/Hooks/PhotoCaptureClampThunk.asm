; Only the screenshot readback CALL site is redirected here. The shared native
; clamp entry remains untouched: world-widget callers depend on live R8/R9.
; Preserve all incoming volatile registers and flags around the C++ predicate.
option casemap:none
EXTERN CvrPhotoClampOriginal:QWORD
EXTERN CvrPhotoClampSkip:PROC

RestoreIncoming MACRO
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
ENDM

.code
CvrPhotoClampDetour PROC FRAME
    push rbp
    .pushreg rbp
    lea rsp, [rsp-0C0h]
    .allocstack 0C0h
    mov rbp, rsp
    .setframe rbp, 0
    .endprolog
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
    call CvrPhotoClampSkip
    test al, al
    jz NativeClamp
    RestoreIncoming
    lea rsp, [rbp+0C0h]
    pop rbp
    ret
NativeClamp:
    RestoreIncoming
    lea rsp, [rbp+0C0h]
    pop rbp
    jmp QWORD PTR [CvrPhotoClampOriginal]
CvrPhotoClampDetour ENDP
END
