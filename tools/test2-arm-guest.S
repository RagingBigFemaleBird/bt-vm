.data
 
msg:
    .ascii      "Starting test...\n"
len = . - msg

num:
    .ascii	"ffffffff\n"
numlen = 9

 
.text
 
.globl _start
_start:
    
    /* syscall write(int fd, const void *buf, size_t count) */
    mov %r0, $1
    ldr %r1, =msg
    ldr %r2, =len
    mov %r7, $4
    swi $4

    bl    output

    ldr %r8, =10000
    1:
    push {%r8}
    ldr %r6, =0xdeadbeef
    mov %r7, $24
    swi $24
    pop {%r8}
    subs %r8, %r8, $1
    bne 1b

    bl    output
    mov %r7, $1
    swi $1



output:
    mrc p15, 0, %r0, c9, c13, 0
    ldr %r1, =num
    mov %r2, $8
    1:
    mov %r3, %r0
    mov %r4, $0xf0000000
    and %r3, %r3, %r4
    mov %r3, %r3, lsr #28
    cmp %r3, $9
    ble 2f
    add %r3, %r3, $87
    b 3f
    2:
    add %r3, %r3, $48
    3:
    strb %r3, [%r1]
    add %r1, %r1, $1
    mov %r0, %r0, lsl #4
    subs %r2, %r2, $1
    bne 1b

    /* syscall write(int fd, const void *buf, size_t count) */
    mov %r0, $1
    ldr %r1, =num
    ldr %r2, =numlen
    mov %r7, $4
    swi $4
    mov pc, lr

