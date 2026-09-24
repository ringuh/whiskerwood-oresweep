.intel_syntax noprefix
.text

/* dsound forwarders: jump through pointers filled in DllMain */
.macro FWD name
.globl p_\name
p_\name:
    jmp qword ptr [rip + real_\name]
.endm
FWD DirectSoundCreate
FWD DirectSoundEnumerateA
FWD DirectSoundEnumerateW
FWD DllCanUnloadNow
FWD DllGetClassObject
FWD DirectSoundCaptureCreate
FWD DirectSoundCaptureEnumerateA
FWD DirectSoundCaptureEnumerateW
FWD GetDeviceID
FWD DirectSoundFullDuplexCreate
FWD DirectSoundCreate8
FWD DirectSoundCaptureCreate8

/* Hook trampoline. Entered by an absolute jmp placed over the start of the
   per-cell body of the mine tool's "commit drag" loop.
   On entry: rax = sparse-set element index, rdx = pointer to set data ptr. */
.globl ore_tramp
ore_tramp:
    push rax
    push rcx
    push rdx
    push r8
    push r9
    push r10
    push r11
    sub  rsp, 0x88
    movdqu [rsp+0x20], xmm0
    movdqu [rsp+0x30], xmm1
    movdqu [rsp+0x40], xmm2
    movdqu [rsp+0x50], xmm3
    movdqu [rsp+0x60], xmm4
    movdqu [rsp+0x70], xmm5
    mov  rcx, [rdx]
    lea  rax, [rax+rax*4]
    lea  rcx, [rcx+rax*8]          /* element = data + idx*40 ; IntVector at +0 */
    call OreFilter_ShouldSkip
    mov  dword ptr [rsp+0x80], eax
    movdqu xmm0, [rsp+0x20]
    movdqu xmm1, [rsp+0x30]
    movdqu xmm2, [rsp+0x40]
    movdqu xmm3, [rsp+0x50]
    movdqu xmm4, [rsp+0x60]
    movdqu xmm5, [rsp+0x70]
    cmp  dword ptr [rsp+0x80], 0
    lea  rsp, [rsp+0x88]
    pop  r11
    pop  r10
    pop  r9
    pop  r8
    pop  rdx
    pop  rcx
    pop  rax
    jne  1f
    /* keep cell: replay the 14 overwritten bytes and continue */
    xor  ecx, ecx
    mov  qword ptr [rsp+0x74], 0
    xorps xmm0, xmm0
    jmp  qword ptr [rip + g_retKeep]
1:  /* skip cell: advance the set iterator exactly like the game does */
    mov  eax, dword ptr [rbp-0x34]
    not  eax
    and  dword ptr [rbp-0x28], eax
    lea  rcx, [rbp-0x38]
    call qword ptr [rip + g_iterNext]
    jmp  qword ptr [rip + g_loopHead]
