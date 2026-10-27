option casemap:none
EXTERN CacheRegisters:BYTE
EXTERN CacheFifth:QWORD
EXTERN CacheExpectedReturn:QWORD
EXTERN CacheReturn:QWORD
EXTERN CacheError:DWORD
EXTERN CacheModify:DWORD
.const
ALIGN 16
Seed DWORD 012345678h,0abcdef01h,0fedcba98h,076543210h
.code
NativeCacheProbe PROC
    mov QWORD PTR CacheRegisters, rax
    mov QWORD PTR CacheRegisters+8, rcx
    mov QWORD PTR CacheRegisters+10h, rdx
    mov QWORD PTR CacheRegisters+18h, r8
    mov QWORD PTR CacheRegisters+20h, r9
    mov QWORD PTR CacheRegisters+28h, r10
    mov QWORD PTR CacheRegisters+30h, r11
    pushfq
    pop QWORD PTR CacheRegisters+38h
    movdqu XMMWORD PTR CacheRegisters+40h, xmm0
    movdqu XMMWORD PTR CacheRegisters+50h, xmm1
    movdqu XMMWORD PTR CacheRegisters+60h, xmm2
    movdqu XMMWORD PTR CacheRegisters+70h, xmm3
    movdqu XMMWORD PTR CacheRegisters+80h, xmm4
    movdqu XMMWORD PTR CacheRegisters+90h, xmm5
    mov rax, [rsp+28h]
    mov CacheFifth, rax
    mov rax, [rsp]
    mov CacheReturn, rax
    ret
NativeCacheProbe ENDP
CvrRenderGraphKey PROC
    cmp ecx, 02030405h
    jne Bad
    cmp rdx, 05060708h
    jne Bad
    cmp r8, CacheExpectedReturn
    jne Bad
    cmp r9, 01020304h
    jne Bad
    cmp DWORD PTR [rsp+28h], 09101112h
    jne Bad
    mov rax, 0fedcba9876543210h
    cmp [rsp+30h], rax
    jne Bad
    mov rax, rsp
    and eax, 0fh
    cmp eax, 8
    jne Bad
    mov rax, rdx
    cmp CacheModify, 0
    je ClearVolatiles
    xor rax, 55h
ClearVolatiles:
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
Bad:
    mov CacheError, 1
    ret
CvrRenderGraphKey ENDP
CvrRenderGraphCacheResult PROC
    cmp rcx, 01010101h
    jne BadResult
    mov rax, rsp
    and eax, 0fh
    cmp eax, 8
    jne BadResult
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
BadResult:
    mov CacheError, 1
    ret
CvrRenderGraphCacheResult ENDP
CacheResultProbe PROC FRAME
    lea rsp, [rsp-38h]
    .allocstack 38h
    .endprolog
    mov [rsp+30h], rcx
    mov rax, 0fedcba9876543210h
    mov [rsp+28h], rax
    lea rax, ResultHere
    mov CacheExpectedReturn, rax
    mov [rsp], rax
    mov rax, 01010101h
    mov rcx, 01020304h
    mov rdx, 02030405h
    mov r8, 05060708h
    mov r9, 09101112h
    mov r10, 013141516h
    mov r11, 017181920h
    movdqu xmm0, XMMWORD PTR Seed
    movdqu xmm1, xmm0
    movdqu xmm2, xmm0
    movdqu xmm3, xmm0
    movdqu xmm4, xmm0
    movdqu xmm5, xmm0
    cmp rax, rax
    jmp QWORD PTR [rsp+30h]
ResultHere:
    lea rsp, [rsp+30h]
    ret
CacheResultProbe ENDP
CacheProbe PROC FRAME
    lea rsp, [rsp-38h]
    .allocstack 38h
    .endprolog
    mov [rsp+30h], rcx
    mov rax, 0fedcba9876543210h
    mov [rsp+20h], rax
    lea rax, ReturnHere
    mov CacheExpectedReturn, rax
    mov rax, 01010101h
    mov rcx, 01020304h
    mov rdx, 02030405h
    mov r8, 05060708h
    mov r9, 09101112h
    mov r10, 013141516h
    mov r11, 017181920h
    movdqu xmm0, XMMWORD PTR [Seed]
    movdqu xmm1, XMMWORD PTR [Seed]
    movdqu xmm2, XMMWORD PTR [Seed]
    movdqu xmm3, XMMWORD PTR [Seed]
    movdqu xmm4, XMMWORD PTR [Seed]
    movdqu xmm5, XMMWORD PTR [Seed]
    test eax, eax
    call QWORD PTR [rsp+30h]
ReturnHere:
    lea rsp, [rsp+38h]
    ret
CacheProbe ENDP
NativeCopyProbe PROC
    mov rax, rcx
    cmp rax, rax
    ret
NativeCopyProbe ENDP
CvrResourceContextAfter PROC
    cmp rcx, 01020304h
    jne CopyBad
    cmp rdx, 02030405h
    jne CopyBad
    cmp r8, CacheExpectedReturn
    jne CopyBad
    mov rax, rsp
    and eax, 0fh
    cmp eax, 8
    jne CopyBad
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
CopyBad:
    mov CacheError, 1
    ret
CvrResourceContextAfter ENDP
ResourceProbe PROC FRAME
    lea rsp, [rsp-38h]
    .allocstack 38h
    .endprolog
    mov [rsp+30h], rcx
    lea rax, CopyReturn
    mov CacheExpectedReturn, rax
    mov rax, 01010101h
    mov rcx, 01020304h
    mov rdx, 02030405h
    mov r8, 05060708h
    mov r9, 09101112h
    mov r10, 013141516h
    mov r11, 017181920h
    movdqu xmm0, XMMWORD PTR Seed
    movdqu xmm1, xmm0
    movdqu xmm2, xmm0
    movdqu xmm3, xmm0
    movdqu xmm4, xmm0
    movdqu xmm5, xmm0
    call QWORD PTR [rsp+30h]
CopyReturn:
    call NativeCacheProbe
    lea rsp, [rsp+38h]
    ret
ResourceProbe ENDP
END
