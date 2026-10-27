option casemap:none
EXTERN CvrNativeUploadReservationObserve:PROC
EXTERN CvrNativeUploadReservationXstateMask:QWORD
EXTERN CvrNativeUploadReservationXstateMode:DWORD
PUBLIC CvrNativeUploadReservationAfterCallback
.code
; Entered by a real CALL replacing CE..D4, never by an inherited-frame JMP.
; Native RSP is 16-byte aligned; CALL makes the normal Win64 entry alignment.
CvrNativeUploadReservationThunk PROC FRAME
    push rbp
    .pushreg rbp
    pushfq
    .allocstack 8
    ; Touch each intervening stack page without modifying registers or FLAGS.
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
    lea rax, [rsp+4130h]
    mov [rsp+0A0h], rax
    mov rax, [rax+78h]
    mov [rsp+0A8h], rax
    test QWORD PTR [rsp+98h], 40h
    jz CapacityCaptured
    mov rax, [rbx+10h]
    mov [rsp+0C0h], rax
CapacityCaptured:
    lea rcx, [rsp+10Fh]
    and rcx, -40h
    mov [rsp+0B0h], rcx
    mov rax, QWORD PTR [CvrNativeUploadReservationXstateMask]
    mov [rsp+0B8h], rax
    mov eax, DWORD PTR [CvrNativeUploadReservationXstateMode]
    mov [rsp+0C8h], rax
    test eax, eax
    jz SaveLegacy
    ; XSAVE does not initialize the reserved standard-format header bytes.
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
    test QWORD PTR [rsp+98h], 40h
    jz Restore
    lea rcx, [rsp+20h]
    cld
    call CvrNativeUploadReservationObserve
CvrNativeUploadReservationAfterCallback::
Restore:
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
    jne Complete
    ; Exact success-only instructions overwritten at 7745D0 and 7745D2.
    xor edx, edx
    mov rax, rcx
Complete:
    ; Both paths RET to 7745D3. Its relocated JNE selects 7745F4 on failure.
    ; Keeping the real CALL return address also preserves the CET shadow stack.
    mov rbp, rsp
    lea rsp, [rbp+4120h]
    pop rbp
    ret
CvrNativeUploadReservationThunk ENDP
END
