option casemap:none
EXTERN StoryAbiError:DWORD
EXTERN StoryWrites:DWORD
.const
ALIGN 16
Seed DWORD 012345678h,0abcdef01h,0fedcba98h,076543210h
.code
NativeStoryReplay PROC
    mov rax, [rbp-50h]
    lea rdx, [rsp+58h]
    ret
NativeStoryReplay ENDP
CvrStoryAttentionAfter PROC
    cmp rcx, 12345678h
    jne BadCallback
    lea rax, [rbp-50h]
    cmp rax, rdx
    jne BadCallback
    mov rax, rsp
    and eax, 0fh
    cmp eax, 8
    jne BadCallback
    mov QWORD PTR [rsp+8], 1
    mov QWORD PTR [rsp+10h], 2
    mov QWORD PTR [rsp+18h], 3
    mov QWORD PTR [rsp+20h], 4
    mov DWORD PTR [rdx+10h], 03f800000h
    inc StoryWrites
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
BadCallback:
    mov StoryAbiError, 1
    ret
CvrStoryAttentionAfter ENDP
ProbeStory PROC FRAME
    push rbp
    .pushreg rbp
    push rbx
    .pushreg rbx
    lea rsp, [rsp-0B8h]
    .allocstack 0B8h
    lea rbp, [rsp+0B0h]
    .setframe rbp, 0B0h
    .endprolog
    mov [rsp+30h], rcx
    mov [rsp+38h], rdx
    mov rax, 0102030405060708h
    mov [rbp-50h], rax
    mov QWORD PTR [rbp-48h], 0
    mov QWORD PTR [rbp-40h], 0
    mov QWORD PTR [rbp-38h], 0
    mov rax, 01010101h
    mov rbx, 12345678h
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
    pushfq
    pop QWORD PTR [rsp+0A8h]
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
    cmp StoryWrites, 0
    je Done
    cmp DWORD PTR [rbp-40h], 03f800000h
    je Done
    mov StoryAbiError, 2
Done:
    lea rsp, [rsp+0B8h]
    pop rbx
    pop rbp
    ret
ProbeStory ENDP
END
