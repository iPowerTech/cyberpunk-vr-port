; Preserve the engine leaf routines' actual register contract, including flags
; and XMM outputs. The C++ callbacks are ordinary Win64 callees and may clobber
; every volatile register. Both original routines take register arguments only.
option casemap:none
EXTERN CvrPoseSetupDefaultOriginal:QWORD
EXTERN CvrPoseSetupCopyOriginal:QWORD
EXTERN CvrPoseSetupDefaultBefore:PROC
EXTERN CvrPoseSetupCopyBefore:PROC
EXTERN CvrPoseSetupCopyAfter:PROC
EXTERN CvrWorldBlendBeforeNotify:PROC

SaveVolatile MACRO
    mov [rsp+20h], rax
    mov [rsp+28h], rcx
    mov [rsp+30h], rdx
    mov [rsp+38h], r8
    mov [rsp+40h], r9
    mov [rsp+48h], r10
    mov [rsp+50h], r11
    pushfq
    pop rax
    mov [rsp+58h], rax
    movdqu [rsp+60h], xmm0
    movdqu [rsp+70h], xmm1
    movdqu [rsp+80h], xmm2
    movdqu [rsp+90h], xmm3
    movdqu [rsp+0A0h], xmm4
    movdqu [rsp+0B0h], xmm5
ENDM

RestoreVolatile MACRO
    movdqu xmm0, [rsp+60h]
    movdqu xmm1, [rsp+70h]
    movdqu xmm2, [rsp+80h]
    movdqu xmm3, [rsp+90h]
    movdqu xmm4, [rsp+0A0h]
    movdqu xmm5, [rsp+0B0h]
    push QWORD PTR [rsp+58h]
    popfq
    mov rax, [rsp+20h]
    mov rcx, [rsp+28h]
    mov rdx, [rsp+30h]
    mov r8, [rsp+38h]
    mov r9, [rsp+40h]
    mov r10, [rsp+48h]
    mov r11, [rsp+50h]
ENDM

.code
CvrPoseSetupDefaultDetour PROC FRAME
    ; A frame pointer gives Windows a standard LEA/POP epilogue while keeping
    ; incoming/result flags intact (ADD/SUB RSP would overwrite them).
    push rbp
    .pushreg rbp
    lea rsp, [rsp-0C0h]
    .allocstack 0C0h
    mov rbp, rsp
    .setframe rbp, 0
    .endprolog
    SaveVolatile
    call CvrPoseSetupDefaultBefore
    RestoreVolatile
    lea rsp, [rbp+0C0h]
    pop rbp
    jmp QWORD PTR [CvrPoseSetupDefaultOriginal]
CvrPoseSetupDefaultDetour ENDP

CvrPoseSetupCopyDetour PROC FRAME
    ; C0/C8 retain the arguments. D0..2CF is a per-call PoseReceipt (512 bytes),
    ; whose size/alignment/trivial destruction are checked in C++.
    push rbp
    .pushreg rbp
    lea rsp, [rsp-2D0h]
    .allocstack 2D0h
    mov rbp, rsp
    .setframe rbp, 0
    .endprolog
    SaveVolatile
    mov [rsp+0C0h], rcx
    mov [rsp+0C8h], rdx
    lea rcx, [rsp+0D0h]
    call CvrPoseSetupCopyBefore
    RestoreVolatile
    call QWORD PTR [CvrPoseSetupCopyOriginal]
    ; Save the ORIGINAL's outputs, not the incoming values: the copy updates
    ; RAX and XMM0/1. Commit ancestry only after the copied bytes exist.
    SaveVolatile
    lea rcx, [rsp+0D0h]
    mov rdx, [rsp+0C0h]
    call CvrPoseSetupCopyAfter
    RestoreVolatile
    lea rsp, [rbp+2D0h]
    pop rbp
    ret
CvrPoseSetupCopyDetour ENDP

; Replaces only WorldSpaceBlendCamera's call [rax+240h]. RCX/RDX and
; the vtable in RAX must reach the original notification unchanged.
CvrWorldBlendNotifyDetour PROC FRAME
    push rbp
    .pushreg rbp
    lea rsp, [rsp-0C0h]
    .allocstack 0C0h
    mov rbp, rsp
    .setframe rbp, 0
    .endprolog
    SaveVolatile
    call CvrWorldBlendBeforeNotify
    RestoreVolatile
    lea rsp, [rbp+0C0h]
    pop rbp
    jmp QWORD PTR [rax+240h]
CvrWorldBlendNotifyDetour ENDP
END
