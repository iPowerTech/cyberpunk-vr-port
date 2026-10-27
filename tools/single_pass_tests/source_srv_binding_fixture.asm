option casemap:none
EXTERN SourceSrvNativeInputs:BYTE
EXTERN SourceSrvNativeStackArgs:QWORD
EXTERN SourceSrvReturnValue:QWORD
EXTERN SourceSrvCheck:PROC
EXTERN CvrSourceSrvXstateMask:QWORD
EXTERN CvrSourceSrvXstateBytes:DWORD
EXTERN CvrSourceSrvXstateMode:DWORD
.data
ALIGN 16
InputXmm BYTE 16 DUP(11h),16 DUP(22h),16 DUP(33h),16 DUP(44h)
    BYTE 16 DUP(55h),16 DUP(66h),16 DUP(77h),16 DUP(88h)
    BYTE 16 DUP(99h),16 DUP(0AAh),16 DUP(0BBh),16 DUP(0CCh)
    BYTE 16 DUP(0DDh),16 DUP(0EEh),16 DUP(0FFh),16 DUP(10h)
ChangedMxcsr DWORD 3F80h
.code
SourceSrvNative PROC
    mov QWORD PTR [SourceSrvNativeInputs+00h], rax
    mov QWORD PTR [SourceSrvNativeInputs+08h], rcx
    mov QWORD PTR [SourceSrvNativeInputs+10h], rdx
    mov QWORD PTR [SourceSrvNativeInputs+18h], rbx
    mov QWORD PTR [SourceSrvNativeInputs+20h], rbp
    mov QWORD PTR [SourceSrvNativeInputs+28h], rsi
    mov QWORD PTR [SourceSrvNativeInputs+30h], rdi
    mov QWORD PTR [SourceSrvNativeInputs+38h], r8
    mov QWORD PTR [SourceSrvNativeInputs+40h], r9
    mov QWORD PTR [SourceSrvNativeInputs+48h], r10
    mov QWORD PTR [SourceSrvNativeInputs+50h], r11
    mov QWORD PTR [SourceSrvNativeInputs+58h], r12
    mov QWORD PTR [SourceSrvNativeInputs+60h], r13
    mov QWORD PTR [SourceSrvNativeInputs+68h], r14
    mov QWORD PTR [SourceSrvNativeInputs+70h], r15
    pushfq
    pop rax
    mov QWORD PTR [SourceSrvNativeInputs+78h], rax
    mov QWORD PTR [SourceSrvNativeInputs+80h], rsp
    mov rax, [rsp]
    mov QWORD PTR [SourceSrvNativeInputs+88h], rax
    mov rax, [rsp+28h]
    mov QWORD PTR [SourceSrvNativeStackArgs], rax
    mov rax, [rsp+30h]
    mov QWORD PTR [SourceSrvNativeStackArgs+8], rax
    lea rcx, [SourceSrvNativeInputs+0EFh]
    and rcx, -40h
    mov QWORD PTR [SourceSrvNativeInputs+90h], rcx
    mov rax, QWORD PTR [CvrSourceSrvXstateMask]
    mov QWORD PTR [SourceSrvNativeInputs+98h], rax
    mov eax, DWORD PTR [CvrSourceSrvXstateBytes]
    mov QWORD PTR [SourceSrvNativeInputs+0A0h], rax
    mov eax, DWORD PTR [CvrSourceSrvXstateMode]
    mov QWORD PTR [SourceSrvNativeInputs+0A8h], rax
    test eax, eax
    jz NativeLegacy
    mov QWORD PTR [rcx+200h], 0
    mov QWORD PTR [rcx+208h], 0
    mov QWORD PTR [rcx+210h], 0
    mov QWORD PTR [rcx+218h], 0
    mov QWORD PTR [rcx+220h], 0
    mov QWORD PTR [rcx+228h], 0
    mov QWORD PTR [rcx+230h], 0
    mov QWORD PTR [rcx+238h], 0
    mov rax, QWORD PTR [CvrSourceSrvXstateMask]
    mov rdx, rax
    shr rdx, 32
    db 48h
    xsave [rcx]
    jmp NativeSaved
NativeLegacy:
    db 48h
    fxsave [rcx]
NativeSaved:
    ; Native owns its home slots and its return value; the observer tail-enters it.
    mov QWORD PTR [rsp+8], 1234h
    mov QWORD PTR [rsp+10h], 2345h
    mov QWORD PTR [rsp+18h], 3456h
    mov QWORD PTR [rsp+20h], 4567h
    mov rax, 0ABCDEF0011223344h
    cld
    ret
SourceSrvNative ENDP

CvrSourceSrvBefore PROC FRAME
    sub rsp, 28h
    .allocstack 28h
    .endprolog
    pushfq
    pop r8
    call SourceSrvCheck
    add rsp, 28h
    jmp SourceSrvClobber
CvrSourceSrvBefore ENDP
SourceSrvClobber PROC
    test QWORD PTR [CvrSourceSrvXstateMask], 4
    jz ClobberLegacy
    vzeroupper
ClobberLegacy:
    xor eax, eax
    xor ecx, ecx
    xor edx, edx
    xor ebx, ebx
    xor ebp, ebp
    xor esi, esi
    xor edi, edi
    xor r8d, r8d
    xor r9d, r9d
    xor r10d, r10d
    xor r11d, r11d
    xor r12d, r12d
    xor r13d, r13d
    xor r14d, r14d
    xor r15d, r15d
    pxor xmm0, xmm0
    pxor xmm1, xmm1
    pxor xmm2, xmm2
    pxor xmm3, xmm3
    pxor xmm4, xmm4
    pxor xmm5, xmm5
    pxor xmm6, xmm6
    pxor xmm7, xmm7
    pxor xmm8, xmm8
    pxor xmm9, xmm9
    pxor xmm10, xmm10
    pxor xmm11, xmm11
    pxor xmm12, xmm12
    pxor xmm13, xmm13
    pxor xmm14, xmm14
    pxor xmm15, xmm15
    fninit
    fldz
    ldmxcsr ChangedMxcsr
    stc
    ret
SourceSrvClobber ENDP

SourceSrvProbe PROC FRAME
    push rbx
    .pushreg rbx
    push rbp
    .pushreg rbp
    push rsi
    .pushreg rsi
    push rdi
    .pushreg rdi
    push r12
    .pushreg r12
    push r13
    .pushreg r13
    push r14
    .pushreg r14
    push r15
    .pushreg r15
    sub rsp, 408h
    .allocstack 408h
    .endprolog
    mov [rsp+30h], rcx
    mov [rsp+38h], edx
    mov eax, 99999999h
    mov QWORD PTR [rsp+20h], rax
    mov QWORD PTR [rsp+28h], 12345678h
    db 48h
    fxsave [rsp+1F0h]
    fninit
    fld1
    fldpi
    movdqu xmm0, XMMWORD PTR [InputXmm]
    movdqu xmm1, XMMWORD PTR [InputXmm+10h]
    movdqu xmm2, XMMWORD PTR [InputXmm+20h]
    movdqu xmm3, XMMWORD PTR [InputXmm+30h]
    movdqu xmm4, XMMWORD PTR [InputXmm+40h]
    movdqu xmm5, XMMWORD PTR [InputXmm+50h]
    movdqu xmm6, XMMWORD PTR [InputXmm+60h]
    movdqu xmm7, XMMWORD PTR [InputXmm+70h]
    movdqu xmm8, XMMWORD PTR [InputXmm+80h]
    movdqu xmm9, XMMWORD PTR [InputXmm+90h]
    movdqu xmm10, XMMWORD PTR [InputXmm+0A0h]
    movdqu xmm11, XMMWORD PTR [InputXmm+0B0h]
    movdqu xmm12, XMMWORD PTR [InputXmm+0C0h]
    movdqu xmm13, XMMWORD PTR [InputXmm+0D0h]
    movdqu xmm14, XMMWORD PTR [InputXmm+0E0h]
    movdqu xmm15, XMMWORD PTR [InputXmm+0F0h]
    test QWORD PTR [CvrSourceSrvXstateMask], 4
    jz SeededState
    vinsertf128 ymm0, ymm0, XMMWORD PTR [InputXmm+0F0h], 1
    vinsertf128 ymm1, ymm1, XMMWORD PTR [InputXmm+0E0h], 1
    vinsertf128 ymm2, ymm2, XMMWORD PTR [InputXmm+0D0h], 1
    vinsertf128 ymm3, ymm3, XMMWORD PTR [InputXmm+0C0h], 1
    vinsertf128 ymm4, ymm4, XMMWORD PTR [InputXmm+0B0h], 1
    vinsertf128 ymm5, ymm5, XMMWORD PTR [InputXmm+0A0h], 1
    vinsertf128 ymm6, ymm6, XMMWORD PTR [InputXmm+90h], 1
    vinsertf128 ymm7, ymm7, XMMWORD PTR [InputXmm+80h], 1
    vinsertf128 ymm8, ymm8, XMMWORD PTR [InputXmm+70h], 1
    vinsertf128 ymm9, ymm9, XMMWORD PTR [InputXmm+60h], 1
    vinsertf128 ymm10, ymm10, XMMWORD PTR [InputXmm+50h], 1
    vinsertf128 ymm11, ymm11, XMMWORD PTR [InputXmm+40h], 1
    vinsertf128 ymm12, ymm12, XMMWORD PTR [InputXmm+30h], 1
    vinsertf128 ymm13, ymm13, XMMWORD PTR [InputXmm+20h], 1
    vinsertf128 ymm14, ymm14, XMMWORD PTR [InputXmm+10h], 1
    vinsertf128 ymm15, ymm15, XMMWORD PTR [InputXmm], 1
SeededState:
    mov rax, 11111111h
    mov rcx, 22222222h
    mov rbx, 44444444h
    mov rbp, 55555555h
    mov rsi, 66666666h
    mov rdi, 77777777h
    mov r9, 99999999h
    mov r10, 0AAAAAAAAh
    mov r11, 0BBBBBBBBh
    mov r12, 0CCCCCCCCh
    mov r13, 0DDDDDDDDh
    mov r14, 0EEEEEEEEh
    mov r15, 0FFFFFFFFh
    cmp DWORD PTR [rsp+38h], 0
    jne SeedCached
    mov edx, 5
    mov r8d, 1
    jmp SeededArguments
SeedCached:
    cmp DWORD PTR [rsp+38h], 1
    jne SeedUnrelated
    mov edx, 1
    mov r8d, 1
    jmp SeededArguments
SeedUnrelated:
    cmp DWORD PTR [rsp+38h], 4
    je SeedRaw
    cmp DWORD PTR [rsp+38h], 5
    je SeedStructured64
    mov edx, 3
    mov r8d, 3
    jmp SeededArguments
SeedRaw:
    mov edx, 4
    mov r8d, 1
    jmp SeededArguments
SeedStructured64:
    mov edx, 6
    mov r8d, 1
SeededArguments:
    cmp eax, eax
    stc
    std
    call QWORD PTR [rsp+30h]
SourceSrvProbeReturn::
    mov QWORD PTR [SourceSrvReturnValue], rax
    db 48h
    fxrstor [rsp+1F0h]
    cld
    add rsp, 408h
    pop r15
    pop r14
    pop r13
    pop r12
    pop rdi
    pop rsi
    pop rbp
    pop rbx
    ret
SourceSrvProbe ENDP
PUBLIC SourceSrvProbeReturn
END
