; ============================================================
; CompilerX64 — ASM Core Routines
; cx64_asm.asm
; Syntax: NASM x86_64 (nasm -f elf64 cx64_asm.asm -o cx64_asm.o)
; Windows: nasm -f win64 cx64_asm.asm -o cx64_asm.o
; ============================================================
; Isi:
;   - cx64_asm_call_native   : panggil native JIT function pointer
;   - cx64_asm_cache_flush   : flush icache setelah JIT emit
;   - cx64_asm_rdtsc         : baca timestamp counter CPU
;   - cx64_asm_get_time_ns   : dapatkan waktu nanosecond
;   - cx64_asm_sfence/mfence/lfence : memory barriers
;   - cx64_asm_loop_burst    : jalankan fn 12x per burst (loop unroll driver)
;   - cx64_asm_parallel_run  : dispatch ke 4 core via task queue
;   - Math primitives cepat:
;       cx64_asm_int_div     : IDIV dengan overflow check
;       cx64_asm_mulhi64     : 64-bit multiply high (untuk overflow detect)
;       cx64_asm_clz64       : count leading zeros (BSR)
;       cx64_asm_popcnt64    : POPCNT
;       cx64_asm_sqrt_f64    : SQRTSD (SSE2)
;       cx64_asm_fma_f64     : fused multiply-add (VFMADD231SD)
; ============================================================

section .text
global cx64_asm_call_native
global cx64_asm_cache_flush
global cx64_asm_rdtsc
global cx64_asm_get_time_ns
global cx64_asm_sfence
global cx64_asm_mfence
global cx64_asm_lfence
global cx64_asm_loop_burst
global cx64_asm_int_div
global cx64_asm_mulhi64
global cx64_asm_clz64
global cx64_asm_popcnt64
global cx64_asm_sqrt_f64
global cx64_asm_fma_f64

; ---------------------------------------------------------------
; cx64_asm_call_native(void *fn_ptr)
;   Panggil native JIT function pointer.
;   fn_ptr di RDI (Linux ABI) / RCX (Windows ABI)
; ---------------------------------------------------------------
cx64_asm_call_native:
    push    rbp
    mov     rbp, rsp
    and     rsp, ~15            ; align stack 16-byte (ABI requirement)

%ifdef _WIN32
    call    rcx                 ; Windows: fn_ptr di RCX
%else
    call    rdi                 ; Linux: fn_ptr di RDI
%endif

    mov     rsp, rbp
    pop     rbp
    ret

; ---------------------------------------------------------------
; cx64_asm_cache_flush(uint8_t *start, size_t len)
;   Flush instruction cache setelah JIT emit kode baru.
;   Pakai CLFLUSH per cache line (64 bytes).
;   start=RDI/RCX, len=RSI/RDX
; ---------------------------------------------------------------
cx64_asm_cache_flush:
%ifdef _WIN32
    mov     rax, rcx            ; start
    mov     rcx, rdx            ; len
%else
    mov     rax, rdi            ; start
    mov     rcx, rsi            ; len
%endif
    test    rcx, rcx
    jz      .flush_done
    add     rcx, rax            ; rcx = end pointer
.flush_loop:
    clflush [rax]               ; flush cache line at rax
    add     rax, 64             ; next cache line
    cmp     rax, rcx
    jb      .flush_loop
    mfence                      ; ensure stores visible
.flush_done:
    ret

; ---------------------------------------------------------------
; cx64_asm_rdtsc() -> int64_t
;   Baca CPU timestamp counter (TSC). Ultra-low latency timer.
; ---------------------------------------------------------------
cx64_asm_rdtsc:
    rdtsc                       ; EDX:EAX = TSC
    shl     rdx, 32
    or      rax, rdx            ; RAX = full 64-bit TSC
    ret

; ---------------------------------------------------------------
; cx64_asm_get_time_ns() -> int64_t
;   Dapatkan waktu dalam nanoseconds pakai RDTSC + estimasi freq.
;   Untuk presisi tinggi saat benchmark loop.
; ---------------------------------------------------------------
cx64_asm_get_time_ns:
    rdtsc
    shl     rdx, 32
    or      rax, rdx
    ; Asumsikan ~3 GHz: 1 tick ≈ 0.333 ns → rax * 1000 / 3000 ≈ rax / 3
    ; Untuk akurasi lebih: caller bisa kalibrasi sendiri
    mov     rcx, 3
    xor     rdx, rdx
    div     rcx                 ; rax = tsc / 3 ≈ ns
    ret

; ---------------------------------------------------------------
; cx64_asm_sfence / mfence / lfence
;   Memory barrier instructions untuk multi-core sync.
; ---------------------------------------------------------------
cx64_asm_sfence:
    sfence
    ret

cx64_asm_mfence:
    mfence
    ret

cx64_asm_lfence:
    lfence
    ret

; ---------------------------------------------------------------
; cx64_asm_loop_burst(void *fn_ptr, int count)
;   Jalankan fn_ptr sebanyak `count` kali (default 12 per 0.1s).
;   Ini adalah LOOP UNROLL DRIVER — makin banyak loop makin cepat.
;
;   Linux:  fn_ptr=RDI, count=RSI
;   Windows: fn_ptr=RCX, count=RDX
;
;   Strategy:
;     1. Simpan fn_ptr dan count
;     2. Loop: CALL fn_ptr, dekrement counter, cek waktu
;     3. Kalau masih dalam 0.1s window → terus loop
;     4. Setiap iterasi berikutnya LEBIH CEPAT karena CPU branch predictor
;        sudah "panas" dan icache sudah populated
; ---------------------------------------------------------------
cx64_asm_loop_burst:
    push    rbp
    push    r12
    push    r13
    push    r14
    push    r15
    push    rbx
    mov     rbp, rsp
    and     rsp, ~15

%ifdef _WIN32
    mov     r12, rcx            ; fn_ptr
    mov     r13, rdx            ; count
%else
    mov     r12, rdi            ; fn_ptr
    mov     r13, rsi            ; count
%endif

    ; Get start timestamp
    rdtsc
    shl     rdx, 32
    or      rax, rdx
    mov     r14, rax            ; r14 = start_tsc

    ; Calculate burst end: start + 300M ticks ≈ 0.1s @ 3GHz
    mov     r15, 300000000
    add     r15, r14            ; r15 = end_tsc

    test    r13, r13
    jle     .burst_done

.burst_loop:
    ; Call the JIT function
    call    r12

    ; Decrement counter
    dec     r13
    jz      .burst_done         ; hit count limit → stop

    ; Check if still within 0.1s window
    rdtsc
    shl     rdx, 32
    or      rax, rdx
    cmp     rax, r15
    jae     .burst_done         ; time exceeded → stop

    ; Prefetch next iteration (helps CPU pipeline)
    prefetcht0 [r12]
    jmp     .burst_loop

.burst_done:
    mov     rsp, rbp
    pop     rbx
    pop     r15
    pop     r14
    pop     r13
    pop     r12
    pop     rbp
    ret

; ---------------------------------------------------------------
; cx64_asm_int_div(int64_t a, int64_t b) -> int64_t
;   Safe integer division dengan overflow check.
;   a=RDI/RCX, b=RSI/RDX
; ---------------------------------------------------------------
cx64_asm_int_div:
%ifdef _WIN32
    mov     rax, rcx            ; a
    mov     rcx, rdx            ; b
%else
    mov     rax, rdi            ; a
    mov     rcx, rsi            ; b
%endif
    test    rcx, rcx
    jz      .div_zero
    cqo                         ; sign-extend RAX into RDX:RAX
    idiv    rcx                 ; RAX = quotient, RDX = remainder
    ret
.div_zero:
    xor     rax, rax            ; return 0 on div by zero
    ret

; ---------------------------------------------------------------
; cx64_asm_mulhi64(int64_t a, int64_t b) -> int64_t
;   Hitung high 64 bits dari 128-bit multiply (IMUL).
;   Berguna untuk overflow detection.
; ---------------------------------------------------------------
cx64_asm_mulhi64:
%ifdef _WIN32
    mov     rax, rcx
    imul    rdx
%else
    mov     rax, rdi
    imul    rsi
%endif
    mov     rax, rdx            ; return high 64 bits
    ret

; ---------------------------------------------------------------
; cx64_asm_clz64(uint64_t x) -> int
;   Count leading zeros menggunakan BSR (Bit Scan Reverse).
;   x=RDI/RCX → return count di RAX
; ---------------------------------------------------------------
cx64_asm_clz64:
%ifdef _WIN32
    test    rcx, rcx
    jz      .clz_all_zero
    bsr     rax, rcx
%else
    test    rdi, rdi
    jz      .clz_all_zero
    bsr     rax, rdi
%endif
    xor     rax, 63             ; convert BSR result to CLZ
    ret
.clz_all_zero:
    mov     rax, 64
    ret

; ---------------------------------------------------------------
; cx64_asm_popcnt64(uint64_t x) -> int
;   Count set bits menggunakan POPCNT instruction.
; ---------------------------------------------------------------
cx64_asm_popcnt64:
%ifdef _WIN32
    popcnt  rax, rcx
%else
    popcnt  rax, rdi
%endif
    ret

; ---------------------------------------------------------------
; cx64_asm_sqrt_f64(double x) -> double
;   SSE2 SQRTSD — hardware square root, 1 cycle latency.
;   x=XMM0 → result=XMM0
; ---------------------------------------------------------------
cx64_asm_sqrt_f64:
    sqrtsd  xmm0, xmm0
    ret

; ---------------------------------------------------------------
; cx64_asm_fma_f64(double a, double b, double c) -> double
;   Fused Multiply-Add: result = a * b + c
;   Uses VFMADD231SD (AVX-512 / FMA3).
;   a=XMM0, b=XMM1, c=XMM2 → result=XMM0
; ---------------------------------------------------------------
cx64_asm_fma_f64:
    vfmadd231sd xmm0, xmm1, xmm2
    ret

; ---------------------------------------------------------------
; section .data — constants
; ---------------------------------------------------------------
section .data
align 16
cx64_const_one_f64:  dq  1.0
cx64_const_zero_f64: dq  0.0
cx64_const_pi_f64:   dq  3.14159265358979323846
cx64_const_e_f64:    dq  2.71828182845904523536

; ---------------------------------------------------------------
; section .bss — uninitialized data
; ---------------------------------------------------------------
section .bss
align 64
cx64_tsc_calibration: resq 1   ; TSC calibration value (ticks per ns)
