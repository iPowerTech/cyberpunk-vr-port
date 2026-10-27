option casemap:none
EXTERN CvrWorldMarkerTextPublishDetour:PROC
EXTERN WorldMarkerTestGeometry:QWORD
EXTERN WorldMarkerTestXmm:DWORD
EXTERN RecordWorldMarkerStack:PROC
PUBLIC WorldMarkerTestEnd
.const
ones REAL4 1.0,1.0,1.0,1.0
.code
; Deliberately clobber all Win64 volatile state, like an arbitrary C++ helper.
CvrWorldMarkerTextTag PROC FRAME
    sub rsp,28h
    .allocstack 28h
    .endprolog
    call RecordWorldMarkerStack
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
    add rsp,28h
    clc
    ret
CvrWorldMarkerTextTag ENDP
RunWorldMarkerThunkTest PROC FRAME
    push rbx
    .pushreg rbx
    sub rsp,20h
    .allocstack 20h
    .endprolog
    mov rbx,rcx
    movups xmm0,ones
    movaps xmm1,xmm0
    movaps xmm2,xmm0
    movaps xmm3,xmm0
    movaps xmm4,xmm0
    movaps xmm5,xmm0
    mov eax,11111111h
    mov ecx,22222222h
    mov edx,33333333h
    mov r8d,44444444h
    mov r9d,55555555h
    mov r10d,66666666h
    mov r11d,77777777h
    stc
    call CvrWorldMarkerTextPublishDetour
continuation:
    jnc bad
    cmp rax,11111111h
    jne bad
    cmp rdx,33333333h
    jne bad
    cmp r8,44444444h
    jne bad
    cmp r9,55555555h
    jne bad
    cmp r10,66666666h
    jne bad
    cmp r11,77777777h
    jne bad
    lea rax,[rbx+1F8h]
    cmp rcx,rax
    jne bad
    movups XMMWORD PTR WorldMarkerTestXmm,xmm0
    movups XMMWORD PTR WorldMarkerTestXmm+10h,xmm1
    movups XMMWORD PTR WorldMarkerTestXmm+20h,xmm2
    movups XMMWORD PTR WorldMarkerTestXmm+30h,xmm3
    movups XMMWORD PTR WorldMarkerTestXmm+40h,xmm4
    movups XMMWORD PTR WorldMarkerTestXmm+50h,xmm5
    mov eax,1
    jmp done
bad:
    xor eax,eax
done:
    add rsp,20h
    pop rbx
    ret
RunWorldMarkerThunkTest ENDP
WorldMarkerTestEnd LABEL BYTE
END
