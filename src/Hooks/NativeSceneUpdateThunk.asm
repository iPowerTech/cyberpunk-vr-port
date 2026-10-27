option casemap:none
EXTERN CvrSceneUpdateQueueOriginal:QWORD
EXTERN CvrSceneUpdateProducerOriginal:QWORD
EXTERN CvrSceneUpdateGeneralOriginal:QWORD
EXTERN CvrSceneUpdatePreOriginal:QWORD
EXTERN CvrSceneUpdateMembershipOriginal:QWORD
EXTERN CvrSceneUpdatePostOriginal:QWORD
EXTERN CvrSceneUpdateGeneralRunnerOriginal:QWORD
EXTERN CvrSceneUpdateLaneRunnerOriginal:QWORD
EXTERN CvrSceneUpdateBefore:PROC
EXTERN CvrSceneUpdateAfter:PROC
.code

SAVE_STATE MACRO
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
RESTORE_STATE MACRO
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
FRAME_METADATA MACRO
    mov rax, [rsp+0F8h]
    mov [rsp+0D0h], rax
    lea rax, [rsp+0F8h]
    mov [rsp+0D8h], rax
ENDM

; The shared scheduler keeps its true native caller RA, original stack/fifth
; argument and every input/output register. There is no after-enqueue call.
CvrSceneUpdateQueueDetour PROC FRAME
    lea rsp, [rsp-0F8h]
    .allocstack 0F8h
    .endprolog
    SAVE_STATE
    FRAME_METADATA
    mov rax, [rsp+120h]
    mov [rsp+0E0h], rax
    mov ecx, 0
    lea rdx, [rsp+30h]
    call CvrSceneUpdateBefore
    RESTORE_STATE
    lea rsp, [rsp+0F8h]
    jmp QWORD PTR [CvrSceneUpdateQueueOriginal]
CvrSceneUpdateQueueDetour ENDP

; Audited bracketing entries take only two/three register arguments and do not
; inspect caller RA or undeclared stack arguments. Preserve all native outputs,
; not merely the ordinary C++ nonvolatile ABI, around the after observer.
SCOPED_THUNK MACRO name, original, event
name PROC FRAME
    lea rsp, [rsp-0F8h]
    .allocstack 0F8h
    .endprolog
    SAVE_STATE
    FRAME_METADATA
    mov QWORD PTR [rsp+0E0h], 0
    mov ecx, event
    lea rdx, [rsp+30h]
    call CvrSceneUpdateBefore
    mov [rsp+0E8h], rax
    RESTORE_STATE
    call QWORD PTR [original]
    SAVE_STATE
    mov ecx, event
    mov rdx, [rsp+0E8h]
    lea r8, [rsp+30h]
    call CvrSceneUpdateAfter
    RESTORE_STATE
    lea rsp, [rsp+0F8h]
    ret
name ENDP
ENDM
SCOPED_THUNK CvrSceneUpdateProducerDetour, CvrSceneUpdateProducerOriginal, 1
SCOPED_THUNK CvrSceneUpdateGeneralDetour, CvrSceneUpdateGeneralOriginal, 2
SCOPED_THUNK CvrSceneUpdatePreDetour, CvrSceneUpdatePreOriginal, 3
SCOPED_THUNK CvrSceneUpdateMembershipDetour, CvrSceneUpdateMembershipOriginal, 4
SCOPED_THUNK CvrSceneUpdatePostDetour, CvrSceneUpdatePostOriginal, 5
SCOPED_THUNK CvrSceneUpdateGeneralRunnerDetour, CvrSceneUpdateGeneralRunnerOriginal, 6
SCOPED_THUNK CvrSceneUpdateLaneRunnerDetour, CvrSceneUpdateLaneRunnerOriginal, 7
END
