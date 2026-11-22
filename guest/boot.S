/* Multiboot entry for the voxtalk guest kernel (flat binary, "a.out kludge"). */
    .text
    .globl _start
    .globl _mb_header
    .p2align 2
_mb_header:
    .long 0x1BADB002                 /* magic */
    .long 0x00010000                 /* flags: address fields valid */
    .long -(0x1BADB002 + 0x00010000) /* checksum */
    .long _mb_header                 /* header_addr */
    .long _mb_header                 /* load_addr (file starts at .text) */
    .long 0                          /* load_end_addr: whole file */
    .long 0                          /* bss_end_addr: kernel clears its own bss */
    .long _start                     /* entry_addr */

_start:
    cli
    movl $_boot_stack_top, %esp
    pushl %ebx                       /* multiboot info */
    pushl %eax                       /* magic */
    call _kmain
1:  hlt
    jmp 1b

    .bss
    .p2align 4
_boot_stack:
    .space 262144
_boot_stack_top:
