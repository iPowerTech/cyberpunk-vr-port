option casemap:none
EXTERN CvrWorldBlendNotifyDetour:PROC
.code
; Probe(function, destination, source, CpuState*). All 7 volatile GPRs,
; flags, and XMM0..5 are compared against the unhooked game routine.
PoseLeafProbe PROC FRAME
    push rbx
    .pushreg rbx
    push rsi
    .pushreg rsi
    push rdi
    .pushreg rdi
    push r12
    .pushreg r12
    sub rsp, 28h
    .allocstack 28h
    .endprolog
    mov rsi, rcx
    mov rdi, rdx
    mov r12, r8
    mov rbx, r9
    mov rcx, rdi
    mov rdx, r12
    mov r8, 8182838485868788h
    mov r9, 9192939495969798h
    mov r10, 0A1A2A3A4A5A6A7A8h
    mov r11, 0B1B2B3B4B5B6B7B8h
    pcmpeqd xmm0, xmm0
    movdqa xmm1, xmm0
    movdqa xmm2, xmm0
    movdqa xmm3, xmm0
    movdqa xmm4, xmm0
    movdqa xmm5, xmm0
    mov rax, 1122334455667788h
    cmp rax, rax
    call rsi
    mov [rbx], rax
    mov [rbx+8], rcx
    mov [rbx+10h], rdx
    mov [rbx+18h], r8
    mov [rbx+20h], r9
    mov [rbx+28h], r10
    mov [rbx+30h], r11
    pushfq
    pop rax
    mov [rbx+38h], rax
    movdqu [rbx+40h], xmm0
    movdqu [rbx+50h], xmm1
    movdqu [rbx+60h], xmm2
    movdqu [rbx+70h], xmm3
    movdqu [rbx+80h], xmm4
    movdqu [rbx+90h], xmm5
    add rsp, 28h
    pop r12
    pop rdi
    pop rsi
    pop rbx
    ret
PoseLeafProbe ENDP

PoseLeafClobber PROC
    mov rcx, 011111111h
    mov rdx, 022222222h
    mov r8, 088888888h
    mov r9, 099999999h
    mov r10, 0AAAAAAAAh
    mov r11, 0BBBBBBBBh
    pxor xmm0, xmm0
    pxor xmm1, xmm1
    pxor xmm2, xmm2
    pxor xmm3, xmm3
    pxor xmm4, xmm4
    pxor xmm5, xmm5
    xor eax, eax
    or eax, 1
    stc
    ret
PoseLeafClobber ENDP

WorldBlendNotifyReturn PROC
    ret
WorldBlendNotifyReturn ENDP
WorldBlendProbeOriginal PROC
    mov rax, [rcx]
    jmp QWORD PTR [rax+240h]
WorldBlendProbeOriginal ENDP
WorldBlendProbeHook PROC
    mov rax, [rcx]
    jmp CvrWorldBlendNotifyDetour
WorldBlendProbeHook ENDP
END
