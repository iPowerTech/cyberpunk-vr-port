option casemap:none
EXTERN CvrNativeDrawOriginal:QWORD
EXTERN CvrNativeDrawBefore:PROC
EXTERN CvrNativeDrawAfter:PROC
EXTERN CyberpunkVR_EarlyDepthState:DWORD
EXTERN CyberpunkVR_GeometryOriginMode:DWORD
EXTERN CvrNativeDrawExpectedCaller:QWORD

SaveVolatile MACRO
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
ENDM
RestoreVolatile MACRO
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
ENDM
.code
CvrNativeDrawDetour PROC
    cmp DWORD PTR CyberpunkVR_GeometryOriginMode, 0
    je Passive
    cmp DWORD PTR CyberpunkVR_EarlyDepthState, 1
    jne Passive
    ; The native prologue overwrites flags and this caller-owned home slot.
    ; Keep the dispatcher a leaf with an unchanged RSP for stack sampling.
    mov [rsp+8], rax
    mov rax, CvrNativeDrawExpectedCaller
    cmp QWORD PTR [rsp], rax
    mov rax, [rsp+8]
    jne Passive
    jmp CvrNativeDrawScope
Passive:
    jmp QWORD PTR [CvrNativeDrawOriginal]
CvrNativeDrawDetour ENDP

CvrNativeDrawScope PROC FRAME
    sub rsp, 0F8h
    .allocstack 0F8h
    .endprolog
    SaveVolatile
    mov [rsp+0D0h], rdi
    mov [rsp+0D8h], r15
    mov [rsp+0E0h], r12
    mov [rsp+0E8h], rbx
    mov [rsp+0F0h], r14
    mov rcx, rsp
    call CvrNativeDrawBefore
    ; Copy the fifth argument. +28h stays scratch: the native routine uses
    ; its caller's sixth stack slot for a temporary IA binding result.
    mov rax, [rsp+120h]
    mov [rsp+20h], rax
    RestoreVolatile
    call QWORD PTR [CvrNativeDrawOriginal]
    SaveVolatile
    mov rcx, rsp
    call CvrNativeDrawAfter
    RestoreVolatile
    ; The verified caller immediately ANDs its batch count with zero, so
    ; return flags are dead. Use the canonical epilogue for Windows unwinding.
    add rsp, 0F8h
    ret
CvrNativeDrawScope ENDP
END
