option casemap:none
EXTERN InstanceWitnessObserved:BYTE
EXTERN InstanceWitnessCheck:PROC
EXTERN InstanceWitnessReturnVisited:QWORD
EXTERN InstanceWitnessRoute:QWORD
EXTERN InstanceWitnessFinalResult:QWORD
EXTERN CvrNativeInstanceReservationXstateMask:QWORD
EXTERN CvrNativeInstanceReservationXstateMode:DWORD
.data
ALIGN 16
InputXmm BYTE 16 DUP(11h),16 DUP(22h),16 DUP(33h),16 DUP(44h)
    BYTE 16 DUP(55h),16 DUP(66h),16 DUP(77h),16 DUP(88h)
    BYTE 16 DUP(99h),16 DUP(0AAh),16 DUP(0BBh),16 DUP(0CCh)
    BYTE 16 DUP(0DDh),16 DUP(0EEh),16 DUP(0FFh),16 DUP(10h)
ALIGN 16
FakeRing QWORD 0,0,0000003000020000h
ChangedMxcsr DWORD 3F80h
.code
InstanceWitnessBaseline PROC
    mov rbx, [rsp+10h]
    ret
InstanceWitnessBaseline ENDP

; The fixture callback deliberately violates all register preservation promises.
; The production thunk restores its complete saved native state afterward.
CvrNativeInstanceReservationObserve PROC FRAME
    sub rsp, 28h
    .allocstack 28h
    .endprolog
    pushfq
    pop rdx
    call InstanceWitnessCheck
    add rsp, 28h
    jmp InstanceWitnessClobber
CvrNativeInstanceReservationObserve ENDP
InstanceWitnessClobber PROC
    test QWORD PTR [CvrNativeInstanceReservationXstateMask], 4
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
InstanceWitnessClobber ENDP

InstanceWitnessProbe PROC FRAME
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
    sub rsp, 410h
    .allocstack 410h
    .endprolog
    mov [rsp+20h], rcx
    mov [rsp+28h], edx
    db 48h
    fxsave [rsp+1F8h]
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
    test QWORD PTR [CvrNativeInstanceReservationXstateMask], 4
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
    mov rax, 0ABCDEF0011223344h
    mov [rsp], rax
    mov rax, 0CAFEBABEC0DE1234h
    mov [rsp+8], rax
    mov ebx, 128
    mov rcx, 135296
    mov rdx, 4608
    mov rbp, 11111111h
    mov rsi, 22222222h
    mov rdi, 33333333h
    mov r8, 4096
    mov r9, 135168
    lea r10, FakeRing
    mov r11, 135168
    mov r12, 66666666h
    mov r13, 512
    mov r14, 77777777h
    mov r15, 88888888h
    test DWORD PTR [rsp+28h], 2
    jz ReservationSeeded
    mov r9, 131050
    mov r11, 131072
    mov rcx, 131200
    mov r8, 0
ReservationSeeded:
    cmp DWORD PTR [rsp+28h], 0
    mov rax, 135168
    mov rax, r9
    stc
    std
    call QWORD PTR [rsp+20h]
InstanceWitnessFixtureReturn::
    mov QWORD PTR [InstanceWitnessReturnVisited], 1
    jne InstanceWitnessFixtureRetry
InstanceWitnessFixtureSuccess::
    mov QWORD PTR [InstanceWitnessRoute], 1
    jmp CaptureResult
InstanceWitnessFixtureRetry::
    mov QWORD PTR [InstanceWitnessRoute], 2
CaptureResult:
    mov QWORD PTR [InstanceWitnessObserved+00h], rax
    mov QWORD PTR [InstanceWitnessObserved+08h], rcx
    mov QWORD PTR [InstanceWitnessObserved+10h], rdx
    mov QWORD PTR [InstanceWitnessObserved+18h], rbx
    mov QWORD PTR [InstanceWitnessObserved+20h], rbp
    mov QWORD PTR [InstanceWitnessObserved+28h], rsi
    mov QWORD PTR [InstanceWitnessObserved+30h], rdi
    mov QWORD PTR [InstanceWitnessObserved+38h], r8
    mov QWORD PTR [InstanceWitnessObserved+40h], r9
    mov QWORD PTR [InstanceWitnessObserved+48h], r10
    mov QWORD PTR [InstanceWitnessObserved+50h], r11
    mov QWORD PTR [InstanceWitnessObserved+58h], r12
    mov QWORD PTR [InstanceWitnessObserved+60h], r13
    mov QWORD PTR [InstanceWitnessObserved+68h], r14
    mov QWORD PTR [InstanceWitnessObserved+70h], r15
    pushfq
    pop rax
    mov QWORD PTR [InstanceWitnessObserved+78h], rax
    mov QWORD PTR [InstanceWitnessObserved+80h], rsp
    mov rax, [rsp]
    mov QWORD PTR [InstanceWitnessObserved+88h], rax
    mov eax, r8d
    mov QWORD PTR [InstanceWitnessFinalResult], rax
    lea rcx, [InstanceWitnessObserved+0EFh]
    and rcx, -40h
    mov QWORD PTR [InstanceWitnessObserved+90h], rcx
    mov rax, QWORD PTR [CvrNativeInstanceReservationXstateMask]
    mov QWORD PTR [InstanceWitnessObserved+98h], rax
    mov QWORD PTR [InstanceWitnessObserved+0A0h], 131072
    mov eax, DWORD PTR [CvrNativeInstanceReservationXstateMode]
    mov QWORD PTR [InstanceWitnessObserved+0A8h], rax
    test eax, eax
    jz CaptureLegacy
    mov QWORD PTR [rcx+200h], 0
    mov QWORD PTR [rcx+208h], 0
    mov QWORD PTR [rcx+210h], 0
    mov QWORD PTR [rcx+218h], 0
    mov QWORD PTR [rcx+220h], 0
    mov QWORD PTR [rcx+228h], 0
    mov QWORD PTR [rcx+230h], 0
    mov QWORD PTR [rcx+238h], 0
    mov rax, QWORD PTR [CvrNativeInstanceReservationXstateMask]
    mov rdx, rax
    shr rdx, 32
    db 48h
    xsave [rcx]
    jmp CapturedState
CaptureLegacy:
    db 48h
    fxsave [rcx]
CapturedState:
    db 48h
    fxrstor [rsp+1F8h]
    cld
    add rsp, 410h
    pop r15
    pop r14
    pop r13
    pop r12
    pop rdi
    pop rsi
    pop rbp
    pop rbx
    ret
InstanceWitnessProbe ENDP
PUBLIC InstanceWitnessFixtureRetry
PUBLIC InstanceWitnessFixtureReturn
PUBLIC InstanceWitnessFixtureSuccess
END
