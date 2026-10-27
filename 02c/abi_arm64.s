.section .text
.global _start

_start:
  mov x0, #1
  adr x1, message
  mov x2, #18
  mov x8, #64
  svc #0

  mov x0, #0
  mov x8, #93
  svc #0

.section .rodata
message:
  .ascii "Hello from ARM64!\n"
