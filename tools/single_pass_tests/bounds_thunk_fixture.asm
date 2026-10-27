option casemap:none
EXTERN CvrBoundsDetour:PROC
EXTERN CvrParticleDetour:PROC
EXTERN BoundsRegs:QWORD
EXTERN BoundsArgs:QWORD
EXTERN BoundsExpectedStack:QWORD
EXTERN BoundsExpectedReturn:QWORD
EXTERN BoundsExpectedFlags:QWORD
EXTERN BoundsVectors:BYTE
EXTERN BoundsPattern:BYTE
EXTERN BoundsStackArgument:QWORD
.code
RunBoundsThunk PROC FRAME
    sub rsp, 38h
    .allocstack 38h
    .endprolog
    mov [rsp+28h], rcx
    mov QWORD PTR [rsp+20h], 0A5h
    lea rax, [rsp-8]
    mov BoundsExpectedStack, rax
    lea rax, Finished
    mov BoundsExpectedReturn, rax
    movdqu xmm0, XMMWORD PTR BoundsPattern
    movdqu xmm1, xmm0
    movdqu xmm2, xmm0
    movdqu xmm3, xmm0
    movdqu xmm4, xmm0
    movdqu xmm5, xmm0
    mov rax, 11h
    mov rcx, 22h
    mov rdx, 33h
    mov r8, 44h
    mov r9, 55h
    mov r10, 66h
    mov r11, 77h
    cmp rax, rax
    pushfq
    pop QWORD PTR BoundsExpectedFlags
    call QWORD PTR [rsp+28h]
Finished:
    add rsp, 38h
    ret
RunBoundsThunk ENDP
CvrBoundsBefore PROC
    mov BoundsArgs, rcx
    mov BoundsArgs+8, rdx
    mov BoundsArgs+16, r8
    mov BoundsArgs+24, r9
    xor eax, eax
    mov rcx, rax
    mov rdx, rax
    mov r8, rax
    mov r9, rax
    mov r10, rax
    mov r11, rax
    pxor xmm0, xmm0
    pxor xmm1, xmm1
    pxor xmm2, xmm2
    pxor xmm3, xmm3
    pxor xmm4, xmm4
    pxor xmm5, xmm5
    or eax, -1
    ret
CvrBoundsBefore ENDP
CvrParticleBefore PROC
    jmp CvrBoundsBefore
CvrParticleBefore ENDP
CvrBoundsTarget PROC
    mov BoundsRegs, rax
    mov BoundsRegs+8, rcx
    mov BoundsRegs+16, rdx
    mov BoundsRegs+24, r8
    mov BoundsRegs+32, r9
    mov BoundsRegs+40, r10
    mov BoundsRegs+48, r11
    mov BoundsRegs+56, rsp
    mov rax, [rsp]
    mov BoundsRegs+64, rax
    pushfq
    pop rax
    mov BoundsRegs+72, rax
    mov rax, [rsp+28h]
    mov BoundsStackArgument, rax
    movdqu XMMWORD PTR BoundsVectors, xmm0
    movdqu XMMWORD PTR BoundsVectors+16, xmm1
    movdqu XMMWORD PTR BoundsVectors+32, xmm2
    movdqu XMMWORD PTR BoundsVectors+48, xmm3
    movdqu XMMWORD PTR BoundsVectors+64, xmm4
    movdqu XMMWORD PTR BoundsVectors+80, xmm5
    mov eax, 12345678h
    ret
CvrBoundsTarget ENDP
END
