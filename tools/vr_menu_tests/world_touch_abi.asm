option casemap:none
EXTERN TouchError:DWORD
EXTERN TouchCalls:DWORD
.const
ALIGN 16
Seed DWORD 012345678h,0abcdef01h,0fedcba98h,076543210h
.code
TouchOriginal PROC
    cmp rcx,11h
    jne BadOriginal
    cmp rdx,22h
    jne BadOriginal
    cmp r8,33h
    jne BadOriginal
    cmp r9,44h
    jne BadOriginal
    cmp QWORD PTR [rsp+28h],55h
    jne BadOriginal
    cmp QWORD PTR [rsp+30h],66h
    jne BadOriginal
    cmp QWORD PTR [rsp+38h],77h
    jne BadOriginal
    mov eax,1
    test eax,eax
    ret
BadOriginal:
    mov TouchError,1
    xor eax,eax
    ret
TouchOriginal ENDP
CvrWorldTouchTriangleAfter PROC
    cmp cl,1
    jne BadAfter
    cmp rdx,66h
    jne BadAfter
    mov rax,rsp
    and eax,0fh
    cmp eax,8
    jne BadAfter
    mov QWORD PTR [rsp+8],1
    mov QWORD PTR [rsp+10h],2
    mov QWORD PTR [rsp+18h],3
    mov QWORD PTR [rsp+20h],4
    inc TouchCalls
    xor eax,eax
    xor ecx,ecx
    xor edx,edx
    xor r8d,r8d
    xor r9d,r9d
    xor r10d,r10d
    xor r11d,r11d
    pxor xmm0,xmm0
    pxor xmm1,xmm1
    pxor xmm2,xmm2
    pxor xmm3,xmm3
    pxor xmm4,xmm4
    pxor xmm5,xmm5
    ret
BadAfter:
    mov TouchError,2
    ret
CvrWorldTouchTriangleAfter ENDP
TouchProbe PROC FRAME
    push rbp
    .pushreg rbp
    lea rsp,[rsp-0C0h]
    .allocstack 0C0h
    mov rbp,rsp
    .setframe rbp,0
    .endprolog
    mov [rsp+40h],rcx
    mov [rsp+48h],rdx
    mov QWORD PTR [rsp+20h],55h
    mov QWORD PTR [rsp+28h],66h
    mov QWORD PTR [rsp+30h],77h
    mov rcx,11h
    mov rdx,22h
    mov r8,33h
    mov r9,44h
    mov r10,013141516h
    mov r11,017181920h
    movdqu xmm0,XMMWORD PTR [Seed]
    movdqu xmm1,XMMWORD PTR [Seed]
    movdqu xmm2,XMMWORD PTR [Seed]
    movdqu xmm3,XMMWORD PTR [Seed]
    movdqu xmm4,XMMWORD PTR [Seed]
    movdqu xmm5,XMMWORD PTR [Seed]
    call QWORD PTR [rsp+40h]
    pushfq
    pop QWORD PTR [rsp+0B8h]
    mov [rsp+0B0h],r11
    mov r11,[rsp+48h]
    mov [r11],rax
    mov [r11+8],rcx
    mov [r11+10h],rdx
    mov [r11+18h],r8
    mov [r11+20h],r9
    mov [r11+28h],r10
    mov rax,[rsp+0B0h]
    mov [r11+30h],rax
    mov rax,[rsp+0B8h]
    mov [r11+38h],rax
    movdqu [r11+40h],xmm0
    movdqu [r11+50h],xmm1
    movdqu [r11+60h],xmm2
    movdqu [r11+70h],xmm3
    movdqu [r11+80h],xmm4
    movdqu [r11+90h],xmm5
    lea rsp,[rbp+0C0h]
    pop rbp
    ret
TouchProbe ENDP
END
