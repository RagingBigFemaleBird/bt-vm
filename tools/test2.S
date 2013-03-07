.data
 
msg:
    .ascii      "Starting test...\n"
len = . - msg

num:
    .ascii	"ffffffffffffffff\n"
numlen = 17

 
.text
 
.globl _start
_start:
    /* syscall write(int fd, const void *buf, size_t count) */
    mov     $1, %ebx     /* fd -> stdout */
    mov     $msg, %ecx   /* buf -> msg */
    mov     $len, %edx   /* count -> len(msg) */
    mov     $4, %eax     /* write is syscall #4 */
    int     $0x80          /* invoke syscall */

    call    output

//    mov     $24, %eax
//    int     $0x80
    mov     $10000, %ecx
    1:
    mov     $24, %eax
    int     $0x80
    dec     %ecx
    jnz     1b

    call    output
    mov     $1, %eax
    int     $0x80


output:
    rdtsc
    mov    $num, %esi
    mov    $8, %ecx
    1:
    mov    %edx, %ebx
    and    $0xf0000000, %ebx
    shr    $28, %ebx
    cmp    $0x9, %ebx
    ja     2f
    add    $48, %ebx 
    jmp    3f
    2:
    add    $87, %ebx
    3:
    movb   %bl, (%esi)
    inc    %esi
    shl    $4, %edx
    dec    %ecx
    jnz    1b

    mov    $8, %ecx
    1:
    mov    %eax, %ebx
    and    $0xf0000000, %ebx
    shr    $28, %ebx
    cmp    $0x9, %ebx
    ja     2f
    add    $48, %ebx 
    jmp    3f
    2:
    add    $87, %ebx
    3:
    movb   %bl, (%esi)
    inc    %esi
    shl    $4, %eax
    dec    %ecx
    jnz    1b
    /* syscall write(int fd, const void *buf, size_t count) */
    mov     $1, %ebx     /* fd -> stdout */
    mov     $num, %ecx   /* buf -> msg */
    mov     $numlen, %edx   /* count -> len(msg) */
    mov     $4, %eax     /* write is syscall #4 */
    int     $0x80          /* invoke syscall */
    ret
    
