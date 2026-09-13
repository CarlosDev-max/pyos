; boot.asm — Multiboot2 header + entrypoint real de pyos
; GRUB carga este binario en modo protegido de 32 bits y salta a _start.
; No hay Linux ni ningún OS debajo: esto es lo primero que ejecuta la CPU
; después del bootloader.

MBALIGN     equ  1<<0
MEMINFO     equ  1<<1
FLAGS       equ  MBALIGN | MEMINFO
MAGIC       equ  0x1BADB002        ; Multiboot1 magic (simple y ampliamente soportado por GRUB)
CHECKSUM    equ -(MAGIC + FLAGS)

section .multiboot
align 4
    dd MAGIC
    dd FLAGS
    dd CHECKSUM

section .bss
align 16
stack_bottom:
    resb 16384                     ; 16 KiB de stack para el kernel
stack_top:

section .text
global _start
extern kernel_main

_start:
    cli                            ; nada de interrupciones hasta que el kernel las configure
    mov esp, stack_top             ; stack real, propio de pyos
    push ebx                       ; puntero a la info multiboot (por si el kernel la usa)
    push eax                       ; magic number devuelto por GRUB

    call kernel_main               ; salto real a C: acá arranca "pyos"

    cli
.hang:
    hlt
    jmp .hang
