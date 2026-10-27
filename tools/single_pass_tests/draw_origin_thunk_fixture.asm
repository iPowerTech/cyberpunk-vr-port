option casemap:none
EXTERN CvrNativeDrawDetour:PROC
EXTERN OriginInputs:QWORD
EXTERN OriginOutputs:QWORD
EXTERN OriginBefore:QWORD
EXTERN OriginBeforeCount:QWORD
EXTERN OriginAfterCount:QWORD
EXTERN OriginExpectedFlags:QWORD
EXTERN OriginReturnFlags:QWORD
EXTERN OriginArgument:QWORD
EXTERN OriginInputVectors:BYTE
EXTERN OriginOutputVectors:BYTE
EXTERN OriginPattern:BYTE
EXTERN CvrNativeDrawExpectedCaller:QWORD
EXTERN OriginWrongCaller:QWORD
EXTERN OriginAfterFlags:QWORD
.code
RunOriginThunk PROC FRAME
    push rbx
    .pushreg rbx
    push rsi
    .pushreg rsi
    push rdi
    .pushreg rdi
    push r12
    .pushreg r12
    push r14
    .pushreg r14
    push r15
    .pushreg r15
    push rbp
    .pushreg rbp
    sub rsp, 40h
    .allocstack 40h
    .endprolog
    lea rax, Returned
    cmp OriginWrongCaller, 0
    je Expected
    xor eax, eax
Expected:
    mov CvrNativeDrawExpectedCaller, rax
    mov QWORD PTR [rsp+20h], 0A5h
    mov rbx, 88h
    mov rsi, 99h
    mov rdi, 0AAh
    mov r12, 0BBh
    mov r14, 0CCh
    mov r15, 0DDh
    mov rbp, 0EEh
    movdqu xmm0, XMMWORD PTR OriginPattern
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
    pop QWORD PTR OriginExpectedFlags
    call CvrNativeDrawDetour
Returned:
    mov OriginOutputs, rax
    mov OriginOutputs+8, rcx
    mov OriginOutputs+16, rdx
    mov OriginOutputs+24, r8
    mov OriginOutputs+32, r9
    mov OriginOutputs+40, r10
    mov OriginOutputs+48, r11
    pushfq
    pop QWORD PTR OriginOutputs+56
    movdqu XMMWORD PTR OriginOutputVectors, xmm0
    movdqu XMMWORD PTR OriginOutputVectors+16, xmm1
    movdqu XMMWORD PTR OriginOutputVectors+32, xmm2
    movdqu XMMWORD PTR OriginOutputVectors+48, xmm3
    movdqu XMMWORD PTR OriginOutputVectors+64, xmm4
    movdqu XMMWORD PTR OriginOutputVectors+80, xmm5
    add rsp, 40h
    pop rbp
    pop r15
    pop r14
    pop r12
    pop rdi
    pop rsi
    pop rbx
    ret
RunOriginThunk ENDP

ClobberVolatile PROC
    xor eax, eax
    mov rcx, rax
    mov rdx, rax
    mov r8, rax
    mov r9, rax
    mov r10, rax
    mov r11, rax
    pcmpeqb xmm0, xmm0
    movdqu xmm1, xmm0
    movdqu xmm2, xmm0
    movdqu xmm3, xmm0
    movdqu xmm4, xmm0
    movdqu xmm5, xmm0
    ret
ClobberVolatile ENDP
CvrNativeDrawBefore PROC
    inc OriginBeforeCount
    mov rax, [rcx+38h]
    mov OriginBefore, rax
    mov rax, [rcx+40h]
    mov OriginBefore+8, rax
    mov rax, [rcx+48h]
    mov OriginBefore+16, rax
    mov rax, [rcx+0D0h]
    mov OriginBefore+24, rax
    mov rax, [rcx+0D8h]
    mov OriginBefore+32, rax
    mov rax, [rcx+0E0h]
    mov OriginBefore+40, rax
    jmp ClobberVolatile
CvrNativeDrawBefore ENDP
CvrNativeDrawAfter PROC
    mov rax, [rcx+68h]
    mov OriginAfterFlags, rax
    inc OriginAfterCount
    jmp ClobberVolatile
CvrNativeDrawAfter ENDP
OriginTarget PROC
    mov OriginInputs, rax
    mov OriginInputs+8, rcx
    mov OriginInputs+16, rdx
    mov OriginInputs+24, r8
    mov OriginInputs+32, r9
    mov OriginInputs+40, r10
    mov OriginInputs+48, r11
    mov OriginInputs+56, rbx
    mov OriginInputs+64, rsi
    mov OriginInputs+72, rdi
    mov OriginInputs+80, r12
    mov OriginInputs+88, r14
    mov OriginInputs+96, r15
    mov OriginInputs+104, rbp
    ; Like the native routine, establish flags before consuming them.
    cmp rax, rax
    pushfq
    pop QWORD PTR OriginInputs+112
    mov rax, [rsp+28h]
    mov OriginArgument, rax
    mov QWORD PTR [rsp+30h], 0DEADh
    movdqu XMMWORD PTR OriginInputVectors, xmm0
    movdqu XMMWORD PTR OriginInputVectors+16, xmm1
    movdqu XMMWORD PTR OriginInputVectors+32, xmm2
    movdqu XMMWORD PTR OriginInputVectors+48, xmm3
    movdqu XMMWORD PTR OriginInputVectors+64, xmm4
    movdqu XMMWORD PTR OriginInputVectors+80, xmm5
    pxor xmm0, xmm0
    pxor xmm1, xmm1
    pxor xmm2, xmm2
    pxor xmm3, xmm3
    pxor xmm4, xmm4
    pxor xmm5, xmm5
    mov eax, 12345678h
    mov ecx, 101h
    mov edx, 102h
    mov r8d, 103h
    mov r9d, 104h
    mov r10d, 105h
    mov r11d, 106h
    cmp rax, rax
    pushfq
    pop QWORD PTR OriginReturnFlags
    ret
OriginTarget ENDP
END
