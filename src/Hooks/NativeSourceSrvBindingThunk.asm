option casemap:none
EXTERN CyberpunkVR_SourceSrvBindingEnable:DWORD
EXTERN CvrSourceSrvXstateReady:DWORD
EXTERN CvrSourceSrvXstateMask:QWORD
EXTERN CvrSourceSrvXstateBytes:DWORD
EXTERN CvrSourceSrvXstateMode:DWORD
EXTERN CvrSourceSrvIndividualOriginal:QWORD
EXTERN CvrSourceSrvCachedOriginal:QWORD
EXTERN CvrSourceSrvBefore:PROC
PUBLIC CvrSourceSrvIndividualAfterCallback
PUBLIC CvrSourceSrvCachedAfterCallback

Dispatcher MACRO procedureName, scopeName, originalName, route
LOCAL Passive
procedureName PROC FRAME
    push rbp
    .pushreg rbp
    pushfq
    .allocstack 8
    mov rbp, rsp
    .setframe rbp, 0
    .endprolog
    cmp DWORD PTR [CyberpunkVR_SourceSrvBindingEnable], 0
    je Passive
    cmp DWORD PTR [CvrSourceSrvXstateReady], 0
    je Passive
    IF route EQ 0
        cmp edx, 4
        jb Passive
        cmp edx, 6
        ja Passive
        cmp r8b, 1
        jne Passive
    ELSE
        cmp dl, 1
        jne Passive
    ENDIF
    push QWORD PTR [rbp]
    popfq
    lea rsp, [rbp+8]
    pop rbp
    jmp scopeName
Passive:
    push QWORD PTR [rbp]
    popfq
    lea rsp, [rbp+8]
    pop rbp
    jmp QWORD PTR [originalName]
procedureName ENDP
ENDM

BeforeScope MACRO procedureName, originalName, route, afterName
LOCAL SaveLegacy, SavedState, RestoreLegacy, RestoredState
procedureName PROC FRAME
    push rbp
    .pushreg rbp
    pushfq
    .allocstack 8
    mov [rsp-1000h], rax
    mov [rsp-2000h], rax
    mov [rsp-3000h], rax
    mov [rsp-4000h], rax
    lea rsp, [rsp-4118h]
    .allocstack 4118h
    mov rbp, rsp
    .setframe rbp, 0
    .endprolog
    mov [rsp+20h], rax
    mov [rsp+28h], rcx
    mov [rsp+30h], rdx
    mov [rsp+38h], rbx
    mov rax, [rsp+4120h]
    mov [rsp+40h], rax
    mov [rsp+48h], rsi
    mov [rsp+50h], rdi
    mov [rsp+58h], r8
    mov [rsp+60h], r9
    mov [rsp+68h], r10
    mov [rsp+70h], r11
    mov [rsp+78h], r12
    mov [rsp+80h], r13
    mov [rsp+88h], r14
    mov [rsp+90h], r15
    mov rax, [rsp+4118h]
    mov [rsp+98h], rax
    lea rax, [rsp+4128h]
    mov [rsp+0A0h], rax
    mov rax, [rax]
    mov [rsp+0A8h], rax
    lea rcx, [rsp+10Fh]
    and rcx, -40h
    mov [rsp+0B0h], rcx
    mov rax, QWORD PTR [CvrSourceSrvXstateMask]
    mov [rsp+0B8h], rax
    mov eax, DWORD PTR [CvrSourceSrvXstateBytes]
    mov [rsp+0C0h], rax
    mov eax, DWORD PTR [CvrSourceSrvXstateMode]
    mov [rsp+0C8h], rax
    test eax, eax
    jz SaveLegacy
    mov QWORD PTR [rcx+200h], 0
    mov QWORD PTR [rcx+208h], 0
    mov QWORD PTR [rcx+210h], 0
    mov QWORD PTR [rcx+218h], 0
    mov QWORD PTR [rcx+220h], 0
    mov QWORD PTR [rcx+228h], 0
    mov QWORD PTR [rcx+230h], 0
    mov QWORD PTR [rcx+238h], 0
    mov rax, [rsp+0B8h]
    mov rdx, rax
    shr rdx, 32
    db 48h
    xsave [rcx]
    jmp SavedState
SaveLegacy:
    db 48h
    fxsave [rcx]
SavedState:
    cld
    mov ecx, route
    lea rdx, [rsp+20h]
    call CvrSourceSrvBefore
afterName::
    mov rcx, [rsp+0B0h]
    cmp QWORD PTR [rsp+0C8h], 0
    je RestoreLegacy
    mov rax, [rsp+0B8h]
    mov rdx, rax
    shr rdx, 32
    db 48h
    xrstor [rcx]
    jmp RestoredState
RestoreLegacy:
    db 48h
    fxrstor [rcx]
RestoredState:
    push QWORD PTR [rsp+98h]
    popfq
    mov rax, [rsp+20h]
    mov rcx, [rsp+28h]
    mov rdx, [rsp+30h]
    mov rbx, [rsp+38h]
    mov rsi, [rsp+48h]
    mov rdi, [rsp+50h]
    mov r8, [rsp+58h]
    mov r9, [rsp+60h]
    mov r10, [rsp+68h]
    mov r11, [rsp+70h]
    mov r12, [rsp+78h]
    mov r13, [rsp+80h]
    mov r14, [rsp+88h]
    mov r15, [rsp+90h]
    mov rbp, rsp
    lea rsp, [rbp+4120h]
    pop rbp
    jmp QWORD PTR [originalName]
procedureName ENDP
ENDM
.code
Dispatcher CvrSourceSrvIndividualDetour, CvrSourceSrvIndividualScope, CvrSourceSrvIndividualOriginal, 0
Dispatcher CvrSourceSrvCachedDetour, CvrSourceSrvCachedScope, CvrSourceSrvCachedOriginal, 1
BeforeScope CvrSourceSrvIndividualScope, CvrSourceSrvIndividualOriginal, 0, CvrSourceSrvIndividualAfterCallback
BeforeScope CvrSourceSrvCachedScope, CvrSourceSrvCachedOriginal, 1, CvrSourceSrvCachedAfterCallback
END
