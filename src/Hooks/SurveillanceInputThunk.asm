option casemap:none
EXTERN CvrSurveillanceInputOriginal:QWORD
EXTERN CvrSurveillanceInputAfter:PROC
EXTERN CvrSurveillanceFinishInput:PROC
.code
; Replaces only DeviceCameraControlComponent's input-reader call. The native
; reader has six arguments; RDI is the live component in that verified caller.
CvrSurveillanceInputDetour PROC FRAME
    push rbp
    .pushreg rbp
    lea rsp, [rsp-0E0h]
    .allocstack 0E0h
    mov rbp, rsp
    .setframe rbp, 0
    .endprolog
    mov [rsp+0D8h], rax
    mov [rsp+0D0h], rdx
    mov rax, [rsp+110h]
    mov [rsp+20h], rax
    mov rax, [rsp+118h]
    mov [rsp+28h], rax
    mov rax, [rsp+0D8h]
    call QWORD PTR [CvrSurveillanceInputOriginal]
    mov [rsp+30h], rax
    mov [rsp+38h], rcx
    mov [rsp+40h], rdx
    mov [rsp+48h], r8
    mov [rsp+50h], r9
    mov [rsp+58h], r10
    mov [rsp+60h], r11
    pushfq
    pop rax
    mov [rsp+68h], rax
    movdqu [rsp+70h], xmm0
    movdqu [rsp+80h], xmm1
    movdqu [rsp+90h], xmm2
    movdqu [rsp+0A0h], xmm3
    movdqu [rsp+0B0h], xmm4
    movdqu [rsp+0C0h], xmm5
    mov rcx, rdi
    mov rdx, [rsp+0D0h]
    call CvrSurveillanceInputAfter
    movdqu xmm0, [rsp+70h]
    movdqu xmm1, [rsp+80h]
    movdqu xmm2, [rsp+90h]
    movdqu xmm3, [rsp+0A0h]
    movdqu xmm4, [rsp+0B0h]
    movdqu xmm5, [rsp+0C0h]
    push QWORD PTR [rsp+68h]
    popfq
    mov rax, [rsp+30h]
    mov rcx, [rsp+38h]
    mov rdx, [rsp+40h]
    mov r8, [rsp+48h]
    mov r9, [rsp+50h]
    mov r10, [rsp+58h]
    mov r11, [rsp+60h]
    lea rsp, [rbp+0E0h]
    pop rbp
    ret
CvrSurveillanceInputDetour ENDP

; Replaces movss xmm0,[rsp+44h] after CameraSystem's automatic modifiers.
; Native input Euler is [caller rsp+60h], alternative auto Euler is +40h.
; Preserve the complete original register state, then replay that movss.
CvrSurveillanceFinishDetour PROC FRAME
    lea rsp, [rsp-0E8h]
    .allocstack 0E8h
    .endprolog
    mov [rsp+30h], rax
    mov [rsp+38h], rcx
    mov [rsp+40h], rdx
    mov [rsp+48h], r8
    mov [rsp+50h], r9
    mov [rsp+58h], r10
    mov [rsp+60h], r11
    pushfq
    pop rax
    mov [rsp+68h], rax
    movdqu [rsp+70h], xmm0
    movdqu [rsp+80h], xmm1
    movdqu [rsp+90h], xmm2
    movdqu [rsp+0A0h], xmm3
    movdqu [rsp+0B0h], xmm4
    movdqu [rsp+0C0h], xmm5
    mov rcx, rdi
    lea rdx, [rsp+130h]
    lea r8, [rsp+150h]
    call CvrSurveillanceFinishInput
    movdqu xmm0, [rsp+70h]
    movdqu xmm1, [rsp+80h]
    movdqu xmm2, [rsp+90h]
    movdqu xmm3, [rsp+0A0h]
    movdqu xmm4, [rsp+0B0h]
    movdqu xmm5, [rsp+0C0h]
    push QWORD PTR [rsp+68h]
    popfq
    mov rax, [rsp+30h]
    mov rcx, [rsp+38h]
    mov rdx, [rsp+40h]
    mov r8, [rsp+48h]
    mov r9, [rsp+50h]
    mov r10, [rsp+58h]
    mov r11, [rsp+60h]
    lea rsp, [rsp+0E8h]
    movss xmm0, DWORD PTR [rsp+4Ch]
    ret
CvrSurveillanceFinishDetour ENDP
END
