; Only the screenshot readback CALL site is redirected here. The shared native
; clamp entry remains untouched: world-widget callers depend on live R8/R9.
; Preserve all incoming volatile registers and flags around the C++ predicate.
option casemap:none
EXTERN CvrPhotoClampOriginal:QWORD
EXTERN CvrPhotoClampSkip:PROC
EXTERN CvrPhotoSizeOriginal:QWORD
EXTERN CvrPhotoSizePolicy:PROC

RestoreIncoming MACRO
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
CvrPhotoClampDetour PROC FRAME
    push rbp
    .pushreg rbp
    lea rsp, [rsp-0C0h]
    .allocstack 0C0h
    mov rbp, rsp
    .setframe rbp, 0
    .endprolog
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
    call CvrPhotoClampSkip
    test al, al
    jz NativeClamp
    RestoreIncoming
    lea rsp, [rbp+0C0h]
    pop rbp
    ret
NativeClamp:
    RestoreIncoming
    lea rsp, [rbp+0C0h]
    pop rbp
    jmp QWORD PTR [CvrPhotoClampOriginal]
CvrPhotoClampDetour ENDP

; The capture-size query (2.5 sub_1420B5AE0). Its LTCG callers keep values in registers the native function
; leaves alone: the savegame thumbnail's caller (capture type 3) holds a zero in R11 across it and builds an object
; from it afterwards; a C++ detour that called into the engine-stereo switch returned a stack address in R11, and the
; autosave crashed at exe+0x269E06A (call [rax+18h] -> 1). So: the native function first, every volatile register,
; the flags and xmm0-5 exactly as it returned them, the C++ policy (which only rewrites the size it was given),
; then that state back.
CvrPhotoSizeDetour PROC FRAME
    sub rsp, 0D8h
    .allocstack 0D8h
    .endprolog
    mov [rsp+20h], rcx
    mov [rsp+28h], rdx
    call QWORD PTR [CvrPhotoSizeOriginal]
    mov [rsp+30h], rax
    mov [rsp+38h], rcx
    mov [rsp+40h], rdx
    mov [rsp+48h], r8
    mov [rsp+50h], r9
    mov [rsp+58h], r10
    mov [rsp+60h], r11
    pushfq
    pop QWORD PTR [rsp+68h]
    movdqu [rsp+70h], xmm0
    movdqu [rsp+80h], xmm1
    movdqu [rsp+90h], xmm2
    movdqu [rsp+0A0h], xmm3
    movdqu [rsp+0B0h], xmm4
    movdqu [rsp+0C0h], xmm5
    mov rcx, [rsp+20h]
    mov rdx, [rsp+28h]
    movzx r8d, al
    call CvrPhotoSizePolicy
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
    add rsp, 0D8h
    ret
CvrPhotoSizeDetour ENDP
END
