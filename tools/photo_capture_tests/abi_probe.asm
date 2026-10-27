option casemap:none
EXTERN CvrPhotoTestBypass:DWORD
.const
LimitWidth REAL4 3840.0
LimitHeight REAL4 2160.0
ALIGN 16
Seed DWORD 012345678h,0abcdef01h,0fedcba98h,076543210h
.code
; Reproduce the original helper's leaf ABI, including untouched R8/R9/XMM4/5.
NativeClamp PROC
    cmp DWORD PTR [rcx], 3840
    ja Resize
    cmp DWORD PTR [rdx], 2160
    jbe Done
Resize:
    mov eax, [rcx]
    xorps xmm0, xmm0
    movss xmm3, LimitHeight
    xorps xmm2, xmm2
    movss xmm1, LimitWidth
    cvtsi2ss xmm2, rax
    mov eax, [rdx]
    cvtsi2ss xmm0, rax
    divss xmm1, xmm2
    divss xmm3, xmm0
    xorps xmm0, xmm0
    minss xmm3, xmm1
    mulss xmm2, xmm3
    cvttss2si rax, xmm2
    mov [rcx], eax
    mov eax, [rdx]
    cvtsi2ss xmm0, rax
    mulss xmm0, xmm3
    cvttss2si rax, xmm0
    mov [rdx], eax
Done:
    ret
NativeClamp ENDP
NativeReturn PROC
    ret
NativeReturn ENDP

; A legal Win64 callback may clobber all these registers. Force that case.
CvrPhotoClampSkip PROC
    mov rcx, 11111111h
    mov rdx, 22222222h
    mov r8, 33333333h
    mov r9, 44444444h
    mov r10, 55555555h
    mov r11, 66666666h
    pxor xmm0, xmm0
    pxor xmm1, xmm1
    pxor xmm2, xmm2
    pxor xmm3, xmm3
    pxor xmm4, xmm4
    pxor xmm5, xmm5
    mov eax, CvrPhotoTestBypass
    test eax, eax
    ret
CvrPhotoClampSkip ENDP

ProbeClamp PROC FRAME
    push rbp
    .pushreg rbp
    lea rsp, [rsp-40h]
    .allocstack 40h
    mov rbp, rsp
    .setframe rbp, 0
    .endprolog
    mov [rsp+20h], rcx
    mov [rsp+28h], r9
    mov rcx, rdx
    mov rdx, r8
    mov rax, 0102030405060708h
    mov r8, 01122334455667788h
    mov r9, 02132435465768798h
    mov r10, 031425364758697A8h
    mov r11, 0415263748596A7B8h
    movdqu xmm0, XMMWORD PTR [Seed]
    movdqu xmm1, XMMWORD PTR [Seed]
    movdqu xmm2, XMMWORD PTR [Seed]
    movdqu xmm3, XMMWORD PTR [Seed]
    movdqu xmm4, XMMWORD PTR [Seed]
    movdqu xmm5, XMMWORD PTR [Seed]
    test eax, eax
    call QWORD PTR [rsp+20h]
    pushfq
    pop QWORD PTR [rbp+30h]
    mov [rsp+38h], r11
    mov r11, [rsp+28h]
    mov [r11], rax
    mov [r11+8], rcx
    mov [r11+10h], rdx
    mov [r11+18h], r8
    mov [r11+20h], r9
    mov [r11+28h], r10
    mov rax, [rsp+38h]
    mov [r11+30h], rax
    mov rax, [rsp+30h]
    mov [r11+38h], rax
    movdqu [r11+40h], xmm0
    movdqu [r11+50h], xmm1
    movdqu [r11+60h], xmm2
    movdqu [r11+70h], xmm3
    movdqu [r11+80h], xmm4
    movdqu [r11+90h], xmm5
    lea rsp, [rbp+40h]
    pop rbp
    ret
ProbeClamp ENDP
END
