.set TIMER_LO, 0x20003004

.globl PUT32
PUT32:
    str r1,[r0]
    bx lr

.globl GET32
GET32:
    ldr r0,[r0]
    bx lr

.globl dummy
dummy:
    bx lr

// not working for some reason
.global delay_usec
delay_usec:
  ldr r2, =TIMER_LO
  ldr r1, [r2]
  add r1, r0, r1
  dmb sy
loop:
  ldr r0, [r2]
  cmp r0, r1
  blt loop

