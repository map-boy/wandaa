# ============================================================================
#  Wandaa fixed runtime  --  tools/runtime/runtime.s
# ============================================================================
#  This file is the ONE piece of Wandaa's output that never varies between
#  compiled programs. It is assembled exactly once, at Wandaa-compiler build
#  time, by tools/extract_blob.cpp, which turns it into include/runtime_blob.hpp
#  (a byte array + fixup tables). End users never run an assembler.
#
#  Two rules make the resulting blob relocatable by plain memcpy:
#
#    1. Code AND data both live in .text. PEWriter emits a single
#       RWX section (characteristics 0xE0000020), so writable globals in
#       .text are legal. Because every internal reference is RIP-relative and
#       both ends move together, internal references need NO fixup at all --
#       GNU as resolves them at assembly time and emits no relocation.
#       The extractor asserts this (zero internal relocations).
#
#    2. Every Windows API call goes through `call [rip+__imp_NAME]`.
#       __imp_NAME is left undefined, so `as` emits exactly one
#       IMAGE_REL_AMD64_REL32 relocation naming it. The extractor records the
#       site; the PE builder repoints it at PEWriter's own IAT slot. This is
#       what the old linker did for us, done explicitly so it is checkable.
#
#  Anything else undefined is a bug and the extractor hard-fails on it --
#  except `wandaa_main`, the single intentional blob -> generated-code edge.
# ============================================================================

.intel_syntax noprefix
.text

# ---- exported entry points (the dynamic codegen calls into these) ----
.globl _start
.globl wandaa_init_stdout
.globl wandaa_print_str
.globl wandaa_crash_handler
.globl wandaa_print_int
.globl wandaa_str_len
.globl wandaa_print_strval
.globl wandaa_str_concat
.globl wandaa_str_eq
.globl wandaa_str_from_c
.globl wandaa_str_at
.globl wandaa_substr
.globl wandaa_int_to_str
.globl wandaa_str_to_int
.globl wandaa_array_new
.globl wandaa_read_file
.globl wandaa_write_file
# ---- exported data slots (codegen writes wandaa_current_line) ----
.globl wandaa_current_line
.globl wandaa_empty_str

# ---- the generated program's entry function, emitted by codegen.cpp ----
.extern wandaa_main

# ============================================================================
#  Runtime data. Deliberately in .text: PEWriter's single section is RWX, and
#  keeping data in the same section is what makes the blob memcpy-relocatable
#  (every reference is RIP-relative and internal, so `as` resolves them all
#  at assembly time and emits no relocation).
#
#  Data comes FIRST, before the code, for one specific reason: GAS resolves a
#  forward-referenced absolute symbol as an ADDRESS rather than an immediate,
#  which silently turns `mov edx, OFFSET wandaa_crash_msg_len` into a memory load
#  from 0x12. Defining it up here keeps it an immediate. Nothing ever falls
#  through into this region -- the PE entry point is the _start label offset,
#  and every routine below is reached by call.
# ============================================================================
.p2align 3
wandaa_hStdOut:       .quad 0
.p2align 3
wandaa_bytesWritten:  .quad 0
.p2align 3
wandaa_current_line:  .quad 0
.p2align 3
wandaa_intbuf:        .space 32, 0

.p2align 3
wandaa_empty_str_hdr: .quad 0
wandaa_empty_str:     .byte 0

wandaa_nl_char:       .byte 10

wandaa_crash_msg:     .ascii "Ikosa ku murongo: "
.set wandaa_crash_msg_len, . - wandaa_crash_msg

.p2align 4

# ============================ init_stdout ===================================
wandaa_init_stdout:
  push rbp
  mov rbp, rsp
  sub rsp, 32
  mov ecx, -11                      # STD_OUTPUT_HANDLE
  call qword ptr [rip+__imp_GetStdHandle]
  mov [rip+wandaa_hStdOut], rax
  add rsp, 32
  pop rbp
  ret

# ======================== wandaa_print_str(buf, len) ========================
wandaa_print_str:
  push rbp
  mov rbp, rsp
  sub rsp, 48
  mov r8d, edx                      # nNumberOfBytesToWrite
  mov rdx, rcx                      # lpBuffer
  mov rcx, [rip+wandaa_hStdOut]     # hFile
  lea r9, [rip+wandaa_bytesWritten]
  mov qword ptr [rsp+32], 0         # lpOverlapped = NULL
  call qword ptr [rip+__imp_WriteFile]
  add rsp, 48
  pop rbp
  ret

# ========================= wandaa_crash_handler =============================
#  Registered with AddVectoredExceptionHandler. Prints the source line that
#  was executing and exits 1, instead of letting Windows report a raw code.
wandaa_crash_handler:
  push rbp
  mov rbp, rsp
  lea rcx, [rip+wandaa_crash_msg]
  mov edx, OFFSET wandaa_crash_msg_len
  sub rsp, 32
  call wandaa_print_str
  add rsp, 32
  mov rcx, [rip+wandaa_current_line]
  sub rsp, 32
  call wandaa_print_int
  add rsp, 32
  mov ecx, 1
  sub rsp, 32
  call qword ptr [rip+__imp_ExitProcess]
  pop rbp
  ret

# ========================== wandaa_print_int(n) =============================
wandaa_print_int:
  push rbp
  mov rbp, rsp
  sub rsp, 48
  push rbx
  sub rsp, 8
  mov rax, rcx
  xor r10, r10                      # digit count
  xor r11, r11                      # negative flag
  cmp rax, 0
  jge wandaa_pi_pos
  mov r11, 1
  neg rax
wandaa_pi_pos:
  lea rbx, [rip+wandaa_intbuf]
  add rbx, 30
  mov byte ptr [rbx], 10            # trailing newline
wandaa_pi_loop:
  xor rdx, rdx
  mov rcx, 10
  div rcx
  add dl, 48
  dec rbx
  mov [rbx], dl
  inc r10
  cmp rax, 0
  jne wandaa_pi_loop
  cmp r11, 0
  je wandaa_pi_nosign
  dec rbx
  mov byte ptr [rbx], 45            # '-'
  inc r10
wandaa_pi_nosign:
  mov rcx, rbx
  mov rdx, r10
  inc rdx                           # + the newline
  call wandaa_print_str
  add rsp, 8
  pop rbx
  add rsp, 48
  pop rbp
  ret

# ========================== wandaa_str_len(s) ===============================
#  Wandaa strings are a pointer to the bytes, with an 8-byte length header
#  immediately before them.
wandaa_str_len:
  mov rax, [rcx-8]
  ret

# ======================== wandaa_print_strval(s) ============================
wandaa_print_strval:
  push rbp
  mov rbp, rsp
  sub rsp, 48
  push rbx
  sub rsp, 8
  mov rbx, rcx
  mov rdx, [rbx-8]
  mov rcx, rbx
  call wandaa_print_str
  lea rcx, [rip+wandaa_nl_char]
  mov rdx, 1
  call wandaa_print_str
  add rsp, 8
  pop rbx
  add rsp, 48
  pop rbp
  ret

# ======================= wandaa_str_concat(a, b) ============================
wandaa_str_concat:
  push rbp
  mov rbp, rsp
  sub rsp, 64
  push rbx
  push rsi
  push rdi
  push r12
  push r13
  push r14
  push r15
  sub rsp, 8
  mov r12, rcx
  mov r13, rdx
  mov rax, [r12-8]
  mov rbx, rax
  add rbx, [r13-8]                  # rbx = total length
  sub rsp, 32
  call qword ptr [rip+__imp_GetProcessHeap]
  add rsp, 32
  mov r14, rax
  mov rcx, r14
  xor rdx, rdx
  lea r8, [rbx+9]                   # 8-byte header + bytes + NUL
  sub rsp, 32
  call qword ptr [rip+__imp_HeapAlloc]
  add rsp, 32
  mov r15, rax
  mov [r15], rbx                    # length header
  mov rsi, r12
  lea rdi, [r15+8]
  mov rcx, [r12-8]
  rep movsb
  mov rsi, r13
  mov rcx, [r13-8]
  rep movsb
  mov byte ptr [rdi], 0
  lea rax, [r15+8]
  add rsp, 8
  pop r15
  pop r14
  pop r13
  pop r12
  pop rdi
  pop rsi
  pop rbx
  add rsp, 64
  pop rbp
  ret

# ========================= wandaa_str_eq(a, b) ==============================
wandaa_str_eq:
  push rbx
  push rsi
  push rdi
  mov rax, [rcx-8]
  cmp rax, [rdx-8]
  jne wandaa_streq_false
  mov rsi, rcx
  mov rdi, rdx
  mov rcx, rax
  repe cmpsb
  jne wandaa_streq_false
  mov rax, 1
  jmp wandaa_streq_done
wandaa_streq_false:
  xor rax, rax
wandaa_streq_done:
  pop rdi
  pop rsi
  pop rbx
  ret

# ======================== wandaa_read_file(path) ============================
wandaa_read_file:
  push rbp
  mov rbp, rsp
  sub rsp, 64
  push rbx
  push r12
  push r13
  sub rsp, 8
  mov r12, rcx
  mov rcx, r12
  mov edx, 0x80000000               # GENERIC_READ
  mov r8, 1                         # FILE_SHARE_READ
  xor r9, r9
  mov qword ptr [rsp+32], 3         # OPEN_EXISTING
  mov qword ptr [rsp+40], 0x80      # FILE_ATTRIBUTE_NORMAL
  mov qword ptr [rsp+48], 0
  call qword ptr [rip+__imp_CreateFileA]
  cmp rax, -1
  je wandaa_rf_fail
  mov rbx, rax
  mov rcx, rbx
  xor rdx, rdx
  call qword ptr [rip+__imp_GetFileSize]
  mov r13, rax
  call qword ptr [rip+__imp_GetProcessHeap]
  mov rcx, rax
  xor rdx, rdx
  lea r8, [r13+9]
  call qword ptr [rip+__imp_HeapAlloc]
  mov [rax], r13
  mov r12, rax
  mov rcx, rbx
  lea rdx, [r12+8]
  mov r8, r13
  lea r9, [rip+wandaa_bytesWritten]
  mov qword ptr [rsp+32], 0
  call qword ptr [rip+__imp_ReadFile]
  test eax, eax
  jnz wandaa_rf_readok
  call qword ptr [rip+__imp_GetLastError]
  mov rcx, rax
  sub rsp, 32
  call wandaa_print_int
  add rsp, 32
wandaa_rf_readok:
  mov rcx, rbx
  call qword ptr [rip+__imp_CloseHandle]
  lea rbx, [r12+8]
  add rbx, r13
  mov byte ptr [rbx], 0
  lea rax, [r12+8]
  jmp wandaa_rf_done
wandaa_rf_fail:
  lea rax, [rip+wandaa_empty_str]
wandaa_rf_done:
  add rsp, 8
  pop r13
  pop r12
  pop rbx
  add rsp, 64
  pop rbp
  ret

# ===================== wandaa_write_file(path, data) ========================
wandaa_write_file:
  push rbp
  mov rbp, rsp
  sub rsp, 64
  push rbx
  push r12
  push r13
  sub rsp, 8
  mov r12, rcx
  mov r13, rdx
  mov rcx, r12
  mov edx, 0x40000000               # GENERIC_WRITE
  xor r8, r8
  xor r9, r9
  mov qword ptr [rsp+32], 2         # CREATE_ALWAYS
  mov qword ptr [rsp+40], 0x80
  mov qword ptr [rsp+48], 0
  call qword ptr [rip+__imp_CreateFileA]
  cmp rax, -1
  je wandaa_wf_fail
  mov rbx, rax
  mov rcx, rbx
  mov rdx, r13
  mov r8, [r13-8]
  lea r9, [rip+wandaa_bytesWritten]
  mov qword ptr [rsp+32], 0
  call qword ptr [rip+__imp_WriteFile]
  mov rcx, rbx
  call qword ptr [rip+__imp_CloseHandle]
  mov rax, 1
  jmp wandaa_wf_done
wandaa_wf_fail:
  xor rax, rax
wandaa_wf_done:
  add rsp, 8
  pop r13
  pop r12
  pop rbx
  add rsp, 64
  pop rbp
  ret

# ====================== wandaa_str_from_c(char*) ============================
#  Turn a NUL-terminated C string handed back by a DLL into a Wandaa string.
#
#  This is what makes FFI return values usable. A Wandaa string is a pointer to
#  its bytes with an 8-byte length header immediately before them; a raw char*
#  from a foreign library has no such header, so it has to be measured and
#  copied onto our heap rather than aliased.
#
#  A null pointer yields the empty string rather than faulting.
wandaa_str_from_c:
  push rbp
  mov rbp, rsp
  sub rsp, 64
  push rbx
  push rsi
  push rdi
  push r12
  push r13
  sub rsp, 8

  test rcx, rcx
  jz wandaa_sfc_null
  mov r12, rcx

  xor rbx, rbx                      # rbx = strlen(src)
wandaa_sfc_len:
  cmp byte ptr [r12+rbx], 0
  je wandaa_sfc_have_len
  inc rbx
  jmp wandaa_sfc_len
wandaa_sfc_have_len:

  sub rsp, 32
  call qword ptr [rip+__imp_GetProcessHeap]
  add rsp, 32
  mov rcx, rax
  xor rdx, rdx
  lea r8, [rbx+9]                   # header + bytes + NUL
  sub rsp, 32
  call qword ptr [rip+__imp_HeapAlloc]
  add rsp, 32

  mov r13, rax
  mov [r13], rbx                    # length header
  mov rsi, r12
  lea rdi, [r13+8]
  mov rcx, rbx
  rep movsb
  mov byte ptr [rdi], 0
  lea rax, [r13+8]
  jmp wandaa_sfc_done

wandaa_sfc_null:
  lea rax, [rip+wandaa_empty_str]

wandaa_sfc_done:
  add rsp, 8
  pop r13
  pop r12
  pop rdi
  pop rsi
  pop rbx
  add rsp, 64
  pop rbp
  ret

# ========================== wandaa_str_at(s, i) =============================
#  Byte at index i, or -1 if the index is outside the string. The unsigned
#  compare catches negative indices as well as ones past the end.
wandaa_str_at:
  mov rax, [rcx-8]
  cmp rdx, rax
  jae wandaa_sat_oob
  movzx rax, byte ptr [rcx+rdx]
  ret
wandaa_sat_oob:
  mov rax, -1
  ret

# ====================== wandaa_substr(s, start, len) ========================
#  A fresh string holding len bytes from start. Both are clamped to the string
#  rather than trusted, so an out-of-range slice yields a shorter string (or an
#  empty one) instead of reading past the allocation.
wandaa_substr:
  push rbp
  mov rbp, rsp
  sub rsp, 64
  push rbx
  push rsi
  push rdi
  push r12
  push r13
  sub rsp, 8

  mov r12, rcx
  mov r13, [rcx-8]                  # r13 = length of the source

  cmp rdx, 0                        # clamp start into [0, len]
  jge wandaa_sub_s1
  xor rdx, rdx
wandaa_sub_s1:
  cmp rdx, r13
  jle wandaa_sub_s2
  mov rdx, r13
wandaa_sub_s2:

  mov rbx, r13                      # rbx = bytes available from start
  sub rbx, rdx
  cmp r8, 0                         # clamp len into [0, available]
  jge wandaa_sub_l1
  xor r8, r8
wandaa_sub_l1:
  cmp r8, rbx
  jle wandaa_sub_l2
  mov r8, rbx
wandaa_sub_l2:

  add r12, rdx                      # r12 = first byte to copy
  mov rbx, r8                       # rbx = final length

  sub rsp, 32
  call qword ptr [rip+__imp_GetProcessHeap]
  add rsp, 32
  mov rcx, rax
  xor rdx, rdx
  lea r8, [rbx+9]
  sub rsp, 32
  call qword ptr [rip+__imp_HeapAlloc]
  add rsp, 32

  mov r13, rax
  mov [r13], rbx
  mov rsi, r12
  lea rdi, [r13+8]
  mov rcx, rbx
  rep movsb
  mov byte ptr [rdi], 0
  lea rax, [r13+8]

  add rsp, 8
  pop r13
  pop r12
  pop rdi
  pop rsi
  pop rbx
  add rsp, 64
  pop rbp
  ret

# ======================== wandaa_int_to_str(n) ==============================
#  Same digit loop as wandaa_print_int, but the result is copied onto the heap
#  as a Wandaa string instead of written to stdout.
wandaa_int_to_str:
  push rbp
  mov rbp, rsp
  sub rsp, 64
  push rbx
  push rsi
  push rdi
  push r12
  push r13
  sub rsp, 8

  mov rax, rcx
  xor r10, r10                      # digit count
  xor r11, r11                      # negative flag
  cmp rax, 0
  jge wandaa_its_pos
  mov r11, 1
  neg rax
wandaa_its_pos:
  lea rbx, [rip+wandaa_intbuf]
  add rbx, 31                       # fill backwards; print_int uses 30 and down
wandaa_its_loop:
  xor rdx, rdx
  mov rcx, 10
  div rcx
  add dl, 48
  dec rbx
  mov [rbx], dl
  inc r10
  cmp rax, 0
  jne wandaa_its_loop
  cmp r11, 0
  je wandaa_its_nosign
  dec rbx
  mov byte ptr [rbx], 45            # '-'
  inc r10
wandaa_its_nosign:

  mov r12, rbx                      # r12 = first digit, r13 = length
  mov r13, r10

  sub rsp, 32
  call qword ptr [rip+__imp_GetProcessHeap]
  add rsp, 32
  mov rcx, rax
  xor rdx, rdx
  lea r8, [r13+9]
  sub rsp, 32
  call qword ptr [rip+__imp_HeapAlloc]
  add rsp, 32

  mov rbx, rax
  mov [rbx], r13
  mov rsi, r12
  lea rdi, [rbx+8]
  mov rcx, r13
  rep movsb
  mov byte ptr [rdi], 0
  lea rax, [rbx+8]

  add rsp, 8
  pop r13
  pop r12
  pop rdi
  pop rsi
  pop rbx
  add rsp, 64
  pop rbp
  ret

# ========================= wandaa_str_to_int(s) =============================
#  Parses an optional '-' followed by decimal digits, stopping at the first
#  character that is not a digit. Anything unparseable yields 0.
wandaa_str_to_int:
  push rbx
  xor rax, rax                      # accumulated value
  xor r10, r10                      # index
  mov r11, [rcx-8]                  # length
  xor r9, r9                        # negative flag
  cmp r11, 0
  je wandaa_sti_done
  cmp byte ptr [rcx], 45            # '-'
  jne wandaa_sti_loop
  mov r9, 1
  inc r10
wandaa_sti_loop:
  cmp r10, r11
  jae wandaa_sti_end
  movzx rbx, byte ptr [rcx+r10]
  cmp rbx, 48
  jb wandaa_sti_end
  cmp rbx, 57
  ja wandaa_sti_end
  sub rbx, 48
  imul rax, rax, 10
  add rax, rbx
  inc r10
  jmp wandaa_sti_loop
wandaa_sti_end:
  cmp r9, 0
  je wandaa_sti_done
  neg rax
wandaa_sti_done:
  pop rbx
  ret

# =========================== wandaa_array_new(n) ============================
#  A zero-filled array of n elements, laid out exactly like an array literal:
#  an 8-byte count header followed by the elements, with the value pointing at
#  the first element. Without this, arrays could only ever be literals.
#
#  HeapAlloc is asked for HEAP_ZERO_MEMORY (8) so the elements start at 0.
wandaa_array_new:
  push rbp
  mov rbp, rsp
  sub rsp, 64
  push rbx
  push r12
  sub rsp, 8

  mov r12, rcx
  cmp r12, 0                        # a negative count would compute a huge size
  jge wandaa_an_ok
  xor r12, r12
wandaa_an_ok:

  sub rsp, 32
  call qword ptr [rip+__imp_GetProcessHeap]
  add rsp, 32
  mov rcx, rax
  mov rdx, 8                        # HEAP_ZERO_MEMORY
  mov r8, r12
  shl r8, 3
  add r8, 8                         # header + n*8
  sub rsp, 32
  call qword ptr [rip+__imp_HeapAlloc]
  add rsp, 32

  mov rbx, rax
  mov [rbx], r12
  lea rax, [rbx+8]

  add rsp, 8
  pop r12
  pop rbx
  add rsp, 64
  pop rbp
  ret

# ================================ _start ====================================
_start:
  sub rsp, 40
  call wandaa_init_stdout
  add rsp, 40
  sub rsp, 40
  mov rcx, 1                        # FIRST handler
  lea rdx, [rip+wandaa_crash_handler]
  call qword ptr [rip+__imp_AddVectoredExceptionHandler]
  add rsp, 40
  sub rsp, 40
  call wandaa_main                  # <- the only blob -> generated-code edge
  add rsp, 40
  mov ecx, eax
  sub rsp, 40
  call qword ptr [rip+__imp_ExitProcess]
  hlt
