;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
; K6INIT Low Level assembly functions
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

    .model small, c
    .586p

k6_getCPUIdentifier     PROTO C cpuidString:FAR PTR CHAR
k6_getCPUProcessorType  PROTO C

k6_setMTRR              PROTO C mtrrIndex:WORD, mtrrValue:DWORD

k6_getVBEInfoBlock      PROTO C vbeInfoBlockPtr:FAR PTR BYTE
k6_getVBEModeInfo       PROTO C videoMode:WORD, vbeModeInfoPtr:FAR PTR BYTE

    .CODE

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
; getCPUIdentifier
;
; Reads and returns the CPU identifier string (CPUID Function Level 2)
;
; Parameters: Pointer to 12 byte memory buffer for the string.

k6_getCPUIdentifier PROC  C USES eax ebx ecx edx esi,cpuidString:FAR PTR CHAR

    mov si, word ptr cpuidString

    xor eax, eax
    xor ebx, ebx
    xor ecx, ecx
    xor edx, edx

    cpuid

    mov [si+00h], ebx
    mov [si+04h], edx
    mov [si+08h], ecx

    ret 

k6_getCPUIdentifier ENDP

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
; getCPUProcessorType
;
; Reads and returns the processor type (CPUID Function Level 1)
;
; Returns: EAX output value from the call.

k6_getCPUProcessorType PROC C uses ebx ecx edx

    mov eax, 00000001h  ; Function Level 1: Get Processor Type, Family, etc.

    xor ebx, ebx
    xor ecx, ecx
    xor edx, edx

    cpuid

    ret

k6_getCPUProcessorType ENDP

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
; setMTRR
;
; Sets the MTRR of a given index to a given value. The value, including
; its bitfields must be calculated properly by the caller.
;
; Parameters: MTRR index (0 or 1)
;             MTRR value 

k6_setMTRR PROC C USES eax ebx ecx edx, mtrrIndex:WORD, mtrrValue:DWORD

k6_MSR_UWCCR    EQU 0C0000085h

    ; MSR register ID needs to go in ecx

    mov ecx, k6_MSR_UWCCR ; UC/WC Cacheability Control Register (UWCCR)
    xor ebx, ebx
    mov bx, mtrrIndex
    add ecx, ebx

    ; First read the MSR because we only change 1 dword

    rdmsr

    ; MTRR0 = eax
    ; MTRR1 = edx

    ; if mtrrIndex = 1, then we write into edx, else eax

    cmp ebx, 0
    jnz writeMTRR1

writeMTRR0:
    mov eax, mtrrValue
    jmp @f

writeMTRR1:
    mov edx, mtrrValue

@@:
    wrmsr
    ret

k6_setMTRR ENDP

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
; getVBEInfoBlock
;
; reads the VESA BIOS Extensions block from the graphics adapter
;
; Parameter: Pointer to a memory location to copy the VBE Info block to.
; Returns: Return value of the call

k6_getVBEInfoBlock PROC C USES es di,vbeInfoBlockPtr:FAR PTR BYTE

    les di, vbeInfoBlockPtr
    mov ax, 4f00h
    int 10h

    ; AX = Return value of the call
    ret 

k6_getVBEInfoBlock ENDP

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
; getVBEModeInfo
;
; reads the mode info for a given VESA mode from the VESA BIOS.
;
; Parameters: mode index 
;             pointer to a memory location to copy the VBE Mode Info to.
; Returns: Return value of the call


k6_getVBEModeInfo PROC C USES cx es di,videoMode:WORD,vbeModeInfoPtr:FAR PTR BYTE

    les di, vbeModeInfoPtr
    mov ax, 4f01h
    mov cx, videoMode
    int 10h

    ; AX = Return value of the call
    ret

k6_getVBEModeInfo ENDP

    END
