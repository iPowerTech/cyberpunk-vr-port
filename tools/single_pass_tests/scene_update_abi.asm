option casemap:none
EXTERN SceneUpdateNativeInputs:BYTE
.data
ALIGN 16
InputXmm BYTE 16 DUP(11h),16 DUP(22h),16 DUP(33h),16 DUP(44h),16 DUP(55h),16 DUP(66h)
OutputXmm BYTE 16 DUP(0AAh),16 DUP(0BBh),16 DUP(0CCh),16 DUP(0DDh),16 DUP(0EEh),16 DUP(0FFh)
.code

STORE_VALUES MACRO target
    mov QWORD PTR [target+00h], rax
    mov QWORD PTR [target+08h], rcx
    mov QWORD PTR [target+10h], rdx
    mov QWORD PTR [target+18h], r8
    mov QWORD PTR [target+20h], r9
    mov QWORD PTR [target+28h], r10
    mov QWORD PTR [target+30h], r11
    pushfq
    pop rax
    mov QWORD PTR [target+38h], rax
    movdqu XMMWORD PTR [target+40h], xmm0
    movdqu XMMWORD PTR [target+50h], xmm1
    movdqu XMMWORD PTR [target+60h], xmm2
    movdqu XMMWORD PTR [target+70h], xmm3
    movdqu XMMWORD PTR [target+80h], xmm4
    movdqu XMMWORD PTR [target+90h], xmm5
ENDM

SceneUpdateNative PROC
    STORE_VALUES SceneUpdateNativeInputs
    mov rax, [rsp]
    mov QWORD PTR [SceneUpdateNativeInputs+0A0h], rax
    lea rax, [rsp]
    mov QWORD PTR [SceneUpdateNativeInputs+0A8h], rax
    mov rax, [rsp+28h]
    mov QWORD PTR [SceneUpdateNativeInputs+0B0h], rax
    ; Exercise the native callee-owned shadow area as well as output registers.
    mov QWORD PTR [rsp+8], 1234h
    mov QWORD PTR [rsp+10h], 2345h
    mov QWORD PTR [rsp+18h], 3456h
    mov QWORD PTR [rsp+20h], 4567h
    mov eax, 0AAAAAAAAh
    mov ecx, 0BBBBBBBBh
    mov edx, 0CCCCCCCCh
    mov r8d, 0DDDDDDDDh
    mov r9d, 0EEEEEEEEh
    mov r10d, 0FFFFFFFFh
    mov r11d, 12345678h
    movdqu xmm0, XMMWORD PTR [OutputXmm]
    movdqu xmm1, XMMWORD PTR [OutputXmm+10h]
    movdqu xmm2, XMMWORD PTR [OutputXmm+20h]
    movdqu xmm3, XMMWORD PTR [OutputXmm+30h]
    movdqu xmm4, XMMWORD PTR [OutputXmm+40h]
    movdqu xmm5, XMMWORD PTR [OutputXmm+50h]
    cmp eax, eax
    stc
    ret
SceneUpdateNative ENDP

SceneUpdateClobber PROC
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
    clc
    ret
SceneUpdateClobber ENDP

SceneUpdateProbe PROC FRAME
    push rbx
    .pushreg rbx
    lea rsp, [rsp-40h]
    .allocstack 40h
    .endprolog
    mov rbx, rdx
    mov [rsp+30h], rcx
    mov eax, 99999999h
    mov [rsp+20h], rax
    mov eax, 11111111h
    mov ecx, 22222222h
    mov edx, 33333333h
    mov r8d, 44444444h
    mov r9d, 55555555h
    mov r10d, 66666666h
    mov r11d, 77777777h
    movdqu xmm0, XMMWORD PTR [InputXmm]
    movdqu xmm1, XMMWORD PTR [InputXmm+10h]
    movdqu xmm2, XMMWORD PTR [InputXmm+20h]
    movdqu xmm3, XMMWORD PTR [InputXmm+30h]
    movdqu xmm4, XMMWORD PTR [InputXmm+40h]
    movdqu xmm5, XMMWORD PTR [InputXmm+50h]
    cmp eax, eax
    clc
    call QWORD PTR [rsp+30h]
    STORE_VALUES rbx
    lea rsp, [rsp+40h]
    pop rbx
    ret
SceneUpdateProbe ENDP
END
