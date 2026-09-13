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
    ; instalar nuestra GDT plana: la de GRUB no garantiza que 0x08 sea un
    ; selector de código (de hecho ejecutamos con cs=0x10), y el iret de la
    ; multitarea necesita selectores propios y coherentes.
    lgdt [gdt_ptr]
    push dword 0x08                ; selector de código de nuestra GDT
    push dword reload_cs
    retf
reload_cs:
    mov ax, 0x10                   ; selector de datos de nuestra GDT
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov esp, stack_top             ; stack real, propio de pyos
    push ebx                       ; puntero a la info multiboot (por si el kernel la usa)
    push eax                       ; magic number devuelto por GRUB

    call kernel_main               ; salto real a C: acá arranca "pyos"

    sti                            ; multitarea: el primer tick del PIT arranca main
.hang:
    hlt
    jmp .hang

; ---------------------------------------------------------------------------
; stubs de interrupciones (ISR 0-31 y IRQ 0-15 remapeadas a los vectores
; 32-47). La CPU no puede llamar a C directamente desde un handler: cada
; vector pasa por acá, se preserva el estado completo y se delega en un
; handler C común (isr_handler / irq_handler, definidos en runtime.c).
; El layout del struct que reciben esos handlers (int_regs_t) es exactamente
; el orden de estos push, desde la dirección más baja:
;   gs fs es ds | edi esi ebp esp ebx edx ecx eax | int_no err | (eip cs eflags)
; ---------------------------------------------------------------------------

extern isr_handler
extern irq_handler
extern scheduler_next_esp

isr_common:
    pusha
    push ds
    push es
    push fs
    push gs
    mov ax, 0x10                   ; segmento de datos del kernel
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov eax, esp
    push eax                       ; puntero al struct int_regs_t
    call isr_handler
    add esp, 4
    pop gs
    pop fs
    pop es
    pop ds
    popa
    add esp, 8                     ; descartar int_no + error code
    iret

irq_common:
    pusha
    push ds
    push es
    push fs
    push gs
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov eax, esp
    push eax
    call irq_handler
    add esp, 4
    ; si el scheduler pidió conmutar, cambiamos de stack ahora: el frame que
    ; hay que retaurar (el del proceso siguiente, completo: segmentos, pusha,
    ; int_no+err y eip/cs/eflags) ya vive en la pila del proceso destino.
    cmp dword [scheduler_next_esp], 0
    je .no_switch
    mov esp, [scheduler_next_esp]
    mov dword [scheduler_next_esp], 0
.no_switch:
    pop gs
    pop fs
    pop es
    pop ds
    popa
    add esp, 8
    iret

; ISR sin error code propio: pushear un dummy para mantener el stack uniforme
%macro ISR_NOERR 1
isr_%1:
    push dword 0
    push dword %1
    jmp isr_common
%endmacro

; ISR con error code: ya lo pusheó la CPU
%macro ISR_ERR 1
isr_%1:
    push dword %1
    jmp isr_common
%endmacro

; IRQ: %1 = número de IRQ (para el label), %2 = vector de la IDT después del remapeo
%macro IRQ 2
irq_%1:
    push dword 0
    push dword %2
    jmp irq_common
%endmacro

ISR_NOERR 0
ISR_NOERR 1
ISR_NOERR 2
ISR_NOERR 3
ISR_NOERR 4
ISR_NOERR 5
ISR_NOERR 6
ISR_NOERR 7
ISR_ERR 8
ISR_NOERR 9
ISR_ERR 10
ISR_ERR 11
ISR_ERR 12
ISR_ERR 13
ISR_ERR 14
ISR_NOERR 15
ISR_NOERR 16
ISR_ERR 17
ISR_NOERR 18
ISR_NOERR 19
ISR_NOERR 20
ISR_NOERR 21
ISR_NOERR 22
ISR_NOERR 23
ISR_NOERR 24
ISR_NOERR 25
ISR_NOERR 26
ISR_NOERR 27
ISR_NOERR 28
ISR_NOERR 29
ISR_NOERR 30
ISR_NOERR 31

IRQ 0, 32
IRQ 1, 33
IRQ 2, 34
IRQ 3, 35
IRQ 4, 36
IRQ 5, 37
IRQ 6, 38
IRQ 7, 39
IRQ 8, 40
IRQ 9, 41
IRQ 10, 42
IRQ 11, 43
IRQ 12, 44
IRQ 13, 45
IRQ 14, 46
IRQ 15, 47

; Yield por software: int $0x40 — pyos_sleep() lo invoca para bloquearse.
; Entra por el mismo irq_common (que hace el switch si el scheduler lo pidió)
; pero no toca el PIC: la CPU ya se encargó de empujar eip/cs/eflags.
global irq_40_stub
irq_40_stub:
    push dword 0
    push dword 0x40
    jmp irq_common

; Tablas de direcciones de los stubs, para que runtime.c pueda cargarlas en la IDT.
section .rodata
global isr_stub_table
isr_stub_table:
    dd isr_0, isr_1, isr_2, isr_3, isr_4, isr_5, isr_6, isr_7
    dd isr_8, isr_9, isr_10, isr_11, isr_12, isr_13, isr_14, isr_15
    dd isr_16, isr_17, isr_18, isr_19, isr_20, isr_21, isr_22, isr_23
    dd isr_24, isr_25, isr_26, isr_27, isr_28, isr_29, isr_30, isr_31

global irq_stub_table
irq_stub_table:
    dd irq_0, irq_1, irq_2, irq_3, irq_4, irq_5, irq_6, irq_7
    dd irq_8, irq_9, irq_10, irq_11, irq_12, irq_13, irq_14, irq_15

; GDT plana del kernel: null | code (0x08) | data (0x10), base 0, 4 GiB,
; DPL 0. Suficiente para la multitarea en ring 0; GRUB no deja selectores
; predecibles (el boot entra con cs=0x10), así que la instalamos nosotros.
align 8
gdt_start:
    dq 0x0000000000000000                    ; null
    db 0xff,0xff,0x00,0x00,0x00,0x9a,0xcf,0x00  ; 0x08 código
    db 0xff,0xff,0x00,0x00,0x00,0x92,0xcf,0x00  ; 0x10 datos
gdt_end:

gdt_ptr:
    dw gdt_end - gdt_start - 1
    dd gdt_start
