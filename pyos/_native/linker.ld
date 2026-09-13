/* linker.ld — coloca el kernel de pyos en 1MB, donde GRUB lo espera */
ENTRY(_start)

SECTIONS
{
    . = 1M;

    /* El header multiboot TIENE que caer en los primeros 8192 bytes del
     * archivo, así que va primero y sin nada de metadata del compilador
     * (build-id, eh_frame, comment) mezclada antes. */
    .text : ALIGN(4K)
    {
        *(.multiboot)
        *(.text)
    }

    .rodata : ALIGN(4K)
    {
        *(.rodata*)
    }

    .data : ALIGN(4K)
    {
        *(.data)
    }

    .bss : ALIGN(4K)
    {
        *(COMMON)
        *(.bss)
    }

    /DISCARD/ :
    {
        *(.note.gnu.build-id)
        *(.comment)
        *(.eh_frame)
    }
}
