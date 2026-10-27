; Only the world UI mesh picker's triangle call is redirected here.
; Native callers retain R10 and other normally volatile registers across the leaf.
; Forward all seven arguments, then preserve ALL original post-call outputs.
option casemap:none
EXTERN CvrWorldTouchTriangleOriginal:QWORD
EXTERN CvrWorldTouchTriangleAfter:PROC
.code
CvrWorldTouchTriangleThunk PROC FRAME
    push rbp
    .pushreg rbp
    lea rsp,[rsp-0F0h]
    .allocstack 0F0h
    mov rbp,rsp
    .setframe rbp,0
    .endprolog
    mov rax,[rsp+120h]
    mov [rsp+20h],rax
    mov rax,[rsp+128h]
    mov [rsp+28h],rax
    mov [rsp+0E0h],rax
    mov rax,[rsp+130h]
    mov [rsp+30h],rax
    call QWORD PTR [CvrWorldTouchTriangleOriginal]
    mov [rsp+38h],rax
    mov [rsp+40h],rcx
    mov [rsp+48h],rdx
    mov [rsp+50h],r8
    mov [rsp+58h],r9
    mov [rsp+60h],r10
    mov [rsp+68h],r11
    pushfq
    pop rax
    mov [rsp+70h],rax
    movdqu [rsp+80h],xmm0
    movdqu [rsp+90h],xmm1
    movdqu [rsp+0A0h],xmm2
    movdqu [rsp+0B0h],xmm3
    movdqu [rsp+0C0h],xmm4
    movdqu [rsp+0D0h],xmm5
    mov rcx,[rsp+38h]
    mov rdx,[rsp+0E0h]
    call CvrWorldTouchTriangleAfter
    movdqu xmm0,[rsp+80h]
    movdqu xmm1,[rsp+90h]
    movdqu xmm2,[rsp+0A0h]
    movdqu xmm3,[rsp+0B0h]
    movdqu xmm4,[rsp+0C0h]
    movdqu xmm5,[rsp+0D0h]
    push QWORD PTR [rsp+70h]
    popfq
    mov rax,[rsp+38h]
    mov rcx,[rsp+40h]
    mov rdx,[rsp+48h]
    mov r8,[rsp+50h]
    mov r9,[rsp+58h]
    mov r10,[rsp+60h]
    mov r11,[rsp+68h]
    lea rsp,[rbp+0F0h]
    pop rbp
    ret
CvrWorldTouchTriangleThunk ENDP
END
