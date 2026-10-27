option casemap:none
EXTERN CvrRenderGraphCacheOriginal:QWORD
EXTERN CvrRenderGraphKey:PROC
EXTERN CvrRenderGraphCacheResultOriginal:QWORD
EXTERN CvrRenderGraphCacheResult:PROC
EXTERN CvrResourceContextOriginal:QWORD
EXTERN CvrResourceContextAfter:PROC
.code
; Internal engine calls can carry volatile state beyond the ordinary Win64
; contract. Only R8 (the graph hash) may change before the native lookup. Tail
; jump on the original stack also preserves its fifth argument and all outputs.
CvrRenderGraphCacheDetour PROC FRAME
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
    mov ecx, edx
    mov rdx, r8
    mov r8, [rsp+0E8h]
    mov r9, [rsp+38h]
    mov eax, DWORD PTR [rsp+50h]
    mov DWORD PTR [rsp+20h], eax
    mov rax, [rsp+110h]
    mov [rsp+28h], rax
    call CvrRenderGraphKey
    mov [rsp+48h], rax
    movdqu xmm0, [rsp+70h]
    movdqu xmm1, [rsp+80h]
    movdqu xmm2, [rsp+90h]
    movdqu xmm3, [rsp+0A0h]
    movdqu xmm4, [rsp+0B0h]
    movdqu xmm5, [rsp+0C0h]
    push QWORD PTR [rsp+68h]
    popfq
    mov rax, [rsp+30h]
    mov rcx, [rsp+38h]
    mov rdx, [rsp+40h]
    mov r8, [rsp+48h]
    mov r9, [rsp+50h]
    mov r10, [rsp+58h]
    mov r11, [rsp+60h]
    lea rsp, [rsp+0E8h]
    jmp QWORD PTR [CvrRenderGraphCacheOriginal]
CvrRenderGraphCacheDetour ENDP
CvrRenderGraphCacheResultDetour PROC FRAME
    lea rsp, [rsp-0E0h]
    .allocstack 0E0h
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
    mov rcx, [rsp+30h]
    call CvrRenderGraphCacheResult
    movdqu xmm0, [rsp+70h]
    movdqu xmm1, [rsp+80h]
    movdqu xmm2, [rsp+90h]
    movdqu xmm3, [rsp+0A0h]
    movdqu xmm4, [rsp+0B0h]
    movdqu xmm5, [rsp+0C0h]
    push QWORD PTR [rsp+68h]
    popfq
    mov rax, [rsp+30h]
    mov rcx, [rsp+38h]
    mov rdx, [rsp+40h]
    mov r8, [rsp+48h]
    mov r9, [rsp+50h]
    mov r10, [rsp+58h]
    mov r11, [rsp+60h]
    lea rsp, [rsp+0E0h]
    jmp QWORD PTR [CvrRenderGraphCacheResultOriginal]
CvrRenderGraphCacheResultDetour ENDP
; The native copy leaf preserves volatile inputs. Observe after the actual copy,
; retaining every native output and flag for interprocedurally optimized callers.
CvrResourceContextDetour PROC FRAME
    lea rsp, [rsp-0E8h]
    .allocstack 0E8h
    .endprolog
    call QWORD PTR [CvrResourceContextOriginal]
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
    mov r8, [rsp+0E8h]
    call CvrResourceContextAfter
    movdqu xmm0, [rsp+70h]
    movdqu xmm1, [rsp+80h]
    movdqu xmm2, [rsp+90h]
    movdqu xmm3, [rsp+0A0h]
    movdqu xmm4, [rsp+0B0h]
    movdqu xmm5, [rsp+0C0h]
    push QWORD PTR [rsp+68h]
    popfq
    mov rax, [rsp+30h]
    mov rcx, [rsp+38h]
    mov rdx, [rsp+40h]
    mov r8, [rsp+48h]
    mov r9, [rsp+50h]
    mov r10, [rsp+58h]
    mov r11, [rsp+60h]
    lea rsp, [rsp+0E8h]
    ret
CvrResourceContextDetour ENDP
END
