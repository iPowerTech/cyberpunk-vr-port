option casemap:none
EXTERN UploadReservationObserved:BYTE
EXTERN UploadReservationCheck:PROC
EXTERN UploadReservationReturnVisited:QWORD
EXTERN UploadReservationRoute:QWORD
EXTERN CvrNativeUploadReservationXstateMask:QWORD
EXTERN CvrNativeUploadReservationXstateMode:DWORD
.data
ALIGN 16
InputXmm BYTE 16 DUP(11h),16 DUP(22h),16 DUP(33h),16 DUP(44h)
    BYTE 16 DUP(55h),16 DUP(66h),16 DUP(77h),16 DUP(88h)
    BYTE 16 DUP(99h),16 DUP(0AAh),16 DUP(0BBh),16 DUP(0CCh)
    BYTE 16 DUP(0DDh),16 DUP(0EEh),16 DUP(0FFh),16 DUP(10h)
ALIGN 16
FakeRing QWORD 0,0,4096
ChangedMxcsr DWORD 3F80h
.code
UploadReservationBaseline PROC
    jne BaselineFailed
    xor edx, edx
    mov rax, rcx
BaselineFailed:
    ret
UploadReservationBaseline ENDP

; The fixture callback deliberately violates all register preservation promises.
; The production thunk restores its complete saved native state afterward.
CvrNativeUploadReservationObserve PROC FRAME
    sub rsp, 28h
    .allocstack 28h
    .endprolog
    pushfq
    pop rdx
    call UploadReservationCheck
    add rsp, 28h
    jmp UploadReservationClobber
CvrNativeUploadReservationObserve ENDP
UploadReservationClobber PROC
    test QWORD PTR [CvrNativeUploadReservationXstateMask], 4
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
UploadReservationClobber ENDP

UploadReservationProbe PROC FRAME
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
    mov [rsp+20h], rcx
    mov [rsp+28h], edx
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
    test QWORD PTR [CvrNativeUploadReservationXstateMask], 4
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
    mov [rsp+78h], rax
    lea rbx, FakeRing
    mov rcx, 4096
    mov rdx, 4608
    mov rbp, 11111111h
    mov rsi, 22222222h
    mov rdi, 33333333h
    mov r8, 4096
    mov r9, 4000
    mov r10, 44444444h
    mov r11, 55555555h
    mov r12, 66666666h
    mov r13, 512
    mov r14, 77777777h
    mov r15, 88888888h
    cmp DWORD PTR [rsp+28h], 0
    mov rax, 99999999h
    stc
    std
    call QWORD PTR [rsp+20h]
UploadReservationFixtureReturn::
    mov QWORD PTR [UploadReservationReturnVisited], 1
    jne UploadReservationFixtureRetry
UploadReservationFixtureSuccess::
    mov QWORD PTR [UploadReservationRoute], 1
    jmp CaptureResult
UploadReservationFixtureRetry::
    mov QWORD PTR [UploadReservationRoute], 2
CaptureResult:
    mov QWORD PTR [UploadReservationObserved+00h], rax
    mov QWORD PTR [UploadReservationObserved+08h], rcx
    mov QWORD PTR [UploadReservationObserved+10h], rdx
    mov QWORD PTR [UploadReservationObserved+18h], rbx
    mov QWORD PTR [UploadReservationObserved+20h], rbp
    mov QWORD PTR [UploadReservationObserved+28h], rsi
    mov QWORD PTR [UploadReservationObserved+30h], rdi
    mov QWORD PTR [UploadReservationObserved+38h], r8
    mov QWORD PTR [UploadReservationObserved+40h], r9
    mov QWORD PTR [UploadReservationObserved+48h], r10
    mov QWORD PTR [UploadReservationObserved+50h], r11
    mov QWORD PTR [UploadReservationObserved+58h], r12
    mov QWORD PTR [UploadReservationObserved+60h], r13
    mov QWORD PTR [UploadReservationObserved+68h], r14
    mov QWORD PTR [UploadReservationObserved+70h], r15
    pushfq
    pop rax
    mov QWORD PTR [UploadReservationObserved+78h], rax
    mov QWORD PTR [UploadReservationObserved+80h], rsp
    mov rax, [rsp+78h]
    mov QWORD PTR [UploadReservationObserved+88h], rax
    lea rcx, [UploadReservationObserved+0EFh]
    and rcx, -40h
    mov QWORD PTR [UploadReservationObserved+90h], rcx
    mov rax, QWORD PTR [CvrNativeUploadReservationXstateMask]
    mov QWORD PTR [UploadReservationObserved+98h], rax
    mov QWORD PTR [UploadReservationObserved+0A0h], 4096
    mov eax, DWORD PTR [CvrNativeUploadReservationXstateMode]
    mov QWORD PTR [UploadReservationObserved+0A8h], rax
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
    mov rax, QWORD PTR [CvrNativeUploadReservationXstateMask]
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
UploadReservationProbe ENDP
PUBLIC UploadReservationFixtureRetry
PUBLIC UploadReservationFixtureReturn
PUBLIC UploadReservationFixtureSuccess
END
