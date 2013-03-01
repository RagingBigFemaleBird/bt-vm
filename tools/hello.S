.data
 
msg:
    .ascii      "Hello, ARM!\n"
len = . - msg
 
 
.text
 
.globl _start
_start:
    /* syscall write(int fd, const void *buf, size_t count) */
    mov     $1, %ebx     /* fd -> stdout */
    mov     $msg, %ecx   /* buf -> msg */
    mov     $len, %edx   /* count -> len(msg) */
    mov     $4, %eax     /* write is syscall #4 */
    int     $0x80          /* invoke syscall */

    mov     $1, %eax
    int     $0x80
