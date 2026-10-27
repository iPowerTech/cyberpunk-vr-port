option casemap:none
EXTERN CvrSurveillanceArgs:QWORD
EXTERN CvrSurveillanceError:DWORD
EXTERN CvrSurveillanceFinishValues:DWORD
.const
ALIGN 16
Seed DWORD 012345678h,0abcdef01h,0fedcba98h,076543210h
Ten REAL4 10.0
Twenty REAL4 20.0
.code
TestNativeInput PROC
    mov CvrSurveillanceArgs, rcx
    mov CvrSurveillanceArgs+8, rdx
    mov CvrSurveillanceArgs+16, r8
    mov CvrSurveillanceArgs+24, r9
    mov rax, [rsp+28h]
    mov CvrSurveillanceArgs+32, rax
    mov rax, [rsp+30h]
    mov CvrSurveillanceArgs+40, rax
    mov DWORD PTR [rdx], 03f800000h
    mov DWORD PTR [rdx+4], 040000000h
    mov DWORD PTR [rdx+8], 040400000h
    mov rax, 1234567890abcdefh
    test eax, eax
    ret
TestNativeInput ENDP
CvrSurveillanceInputAfter PROC
    cmp rcx, 12345678h
    je GoodComponent
    mov CvrSurveillanceError, 1
GoodComponent:
    cmp rdx, CvrSurveillanceArgs+8
    je GoodOutput
    mov CvrSurveillanceError, 2
    ret
GoodOutput:
    movss xmm0, DWORD PTR [rdx+4]
    addss xmm0, Ten
    movss DWORD PTR [rdx+4], xmm0
    movss xmm0, DWORD PTR [rdx+8]
    addss xmm0, Twenty
    movss DWORD PTR [rdx+8], xmm0
    xor eax, eax
    xor ecx, ecx
    xor edx, edx
    xor r8d, r8d
    xor r9d, r9d
    xor r10d, r10d
    xor r11d, r11d
    pxor xmm0, xmm0
    pxor xmm1, xmm1
    pxor xmm2, xmm2
    pxor xmm3, xmm3
    pxor xmm4, xmm4
    pxor xmm5, xmm5
    ret
CvrSurveillanceInputAfter ENDP
ProbeInput PROC FRAME
    push rbp
    .pushreg rbp
    push rdi
    .pushreg rdi
    lea rsp, [rsp-58h]
    .allocstack 58h
    mov rbp, rsp
    .setframe rbp, 0
    .endprolog
    mov [rsp+30h], rcx
    mov [rsp+38h], r8
    mov rax, 1111222233334444h
    mov [rsp+20h], rax
    mov rax, 5555666677778888h
    mov [rsp+28h], rax
    mov rcx, 01020304h
    mov r8, 05060708h
    mov r9, 09101112h
    mov r10, 013141516h
    mov r11, 017181920h
    mov rdi, 12345678h
    movdqu xmm0, XMMWORD PTR [Seed]
    movdqu xmm1, XMMWORD PTR [Seed]
    movdqu xmm2, XMMWORD PTR [Seed]
    movdqu xmm3, XMMWORD PTR [Seed]
    movdqu xmm4, XMMWORD PTR [Seed]
    movdqu xmm5, XMMWORD PTR [Seed]
    call QWORD PTR [rsp+30h]
    pushfq
    pop QWORD PTR [rbp+48h]
    mov [rsp+40h], r11
    mov r11, [rsp+38h]
    mov [r11], rax
    mov [r11+8], rcx
    mov [r11+10h], rdx
    mov [r11+18h], r8
    mov [r11+20h], r9
    mov [r11+28h], r10
    mov rax, [rsp+40h]
    mov [r11+30h], rax
    mov rax, [rsp+48h]
    mov [r11+38h], rax
    movdqu [r11+40h], xmm0
    movdqu [r11+50h], xmm1
    movdqu [r11+60h], xmm2
    movdqu [r11+70h], xmm3
    movdqu [r11+80h], xmm4
    movdqu [r11+90h], xmm5
    lea rsp, [rbp+58h]
    pop rdi
    pop rbp
    ret
ProbeInput ENDP

NativeFinishRead PROC
    movss xmm0, DWORD PTR [rsp+4Ch]
    ret
NativeFinishRead ENDP
CvrSurveillanceFinishInput PROC
    cmp rcx, 12345678h
    jne BadFinish
    lea rax, [rdx+20h]
    cmp rax, r8
    jne BadFinish
    mov QWORD PTR [rdx], 0
    mov DWORD PTR [rdx+8], 0
    mov DWORD PTR [r8], 0
    mov DWORD PTR [r8+4], 03e4ccccdh
    mov DWORD PTR [r8+8], 0bdcccccdh
    xor eax, eax
    xor ecx, ecx
    xor edx, edx
    xor r8d, r8d
    xor r9d, r9d
    xor r10d, r10d
    xor r11d, r11d
    pxor xmm0, xmm0
    pxor xmm1, xmm1
    pxor xmm2, xmm2
    pxor xmm3, xmm3
    pxor xmm4, xmm4
    pxor xmm5, xmm5
    ret
BadFinish:
    mov CvrSurveillanceError, 3
    ret
CvrSurveillanceFinishInput ENDP
ProbeFinish PROC FRAME
    push rbp
    .pushreg rbp
    push rdi
    .pushreg rdi
    lea rsp, [rsp-0B8h]
    .allocstack 0B8h
    mov rbp, rsp
    .setframe rbp, 0
    .endprolog
    mov [rsp+30h], rcx
    mov [rsp+38h], rdx
    mov QWORD PTR [rsp+40h], 0
    mov DWORD PTR [rsp+48h], 4
    mov DWORD PTR [rsp+60h], 0
    mov DWORD PTR [rsp+64h], 042000000h
    mov DWORD PTR [rsp+68h], 042140000h
    mov rax, 01010101h
    mov rcx, 01020304h
    mov rdx, 02030405h
    mov r8, 05060708h
    mov r9, 09101112h
    mov r10, 013141516h
    mov r11, 017181920h
    mov rdi, 12345678h
    movdqu xmm0, XMMWORD PTR [Seed]
    movdqu xmm1, XMMWORD PTR [Seed]
    movdqu xmm2, XMMWORD PTR [Seed]
    movdqu xmm3, XMMWORD PTR [Seed]
    movdqu xmm4, XMMWORD PTR [Seed]
    movdqu xmm5, XMMWORD PTR [Seed]
    test eax, eax
    call QWORD PTR [rsp+30h]
    pushfq
    pop QWORD PTR [rbp+0A8h]
    mov [rsp+0A0h], r11
    mov r11, [rsp+38h]
    mov [r11], rax
    mov [r11+8], rcx
    mov [r11+10h], rdx
    mov [r11+18h], r8
    mov [r11+20h], r9
    mov [r11+28h], r10
    mov rax, [rsp+0A0h]
    mov [r11+30h], rax
    mov rax, [rsp+0A8h]
    mov [r11+38h], rax
    movdqu [r11+40h], xmm0
    movdqu [r11+50h], xmm1
    movdqu [r11+60h], xmm2
    movdqu [r11+70h], xmm3
    movdqu [r11+80h], xmm4
    movdqu [r11+90h], xmm5
    mov rax, [rsp+40h]
    mov QWORD PTR CvrSurveillanceFinishValues, rax
    mov eax, [rsp+48h]
    mov CvrSurveillanceFinishValues+8, eax
    mov rax, [rsp+60h]
    mov QWORD PTR CvrSurveillanceFinishValues+12, rax
    mov eax, [rsp+68h]
    mov CvrSurveillanceFinishValues+20, eax
    lea rsp, [rbp+0B8h]
    pop rdi
    pop rbp
    ret
ProbeFinish ENDP
END
