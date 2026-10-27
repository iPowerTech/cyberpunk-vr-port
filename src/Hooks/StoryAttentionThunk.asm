option casemap:none
EXTERN CvrStoryAttentionAfter:PROC
.code
; Inserted CALL at +3F90F7 (caller RSP aligned). Preserve all volatile state;
; the original source virtual call can leave registers live past this site.
CvrStoryAttentionDetour PROC FRAME
    lea rsp, [rsp-0E8h]
    .allocstack 0E8h
    .endprolog
    mov [rsp+30h], rax
    mov [rsp+38h], rcx
    mov [rsp+40h], rdx
    mov [rsp+48h], r8
    mov [rsp+50h], r9
    mov [rsp+58h], r10
    mov [rsp+60h], r11
    pushfq
    pop rax
    mov [rsp+68h], rax
    movdqu [rsp+70h], xmm0
    movdqu [rsp+80h], xmm1
    movdqu [rsp+90h], xmm2
    movdqu [rsp+0A0h], xmm3
    movdqu [rsp+0B0h], xmm4
    movdqu [rsp+0C0h], xmm5
    mov rcx, rbx
    lea rdx, [rbp-50h]
    call CvrStoryAttentionAfter
    movdqu xmm0, [rsp+70h]
    movdqu xmm1, [rsp+80h]
    movdqu xmm2, [rsp+90h]
    movdqu xmm3, [rsp+0A0h]
    movdqu xmm4, [rsp+0B0h]
    movdqu xmm5, [rsp+0C0h]
    push QWORD PTR [rsp+68h]
    popfq
    mov rcx, [rsp+38h]
    mov r8, [rsp+48h]
    mov r9, [rsp+50h]
    mov r10, [rsp+58h]
    mov r11, [rsp+60h]
    lea rsp, [rsp+0E8h]
    ; Replay exactly: mov rax,[rbp-50]; lea rdx,[caller rsp+50].
    mov rax, [rbp-50h]
    lea rdx, [rsp+58h]
    ret
CvrStoryAttentionDetour ENDP
END
