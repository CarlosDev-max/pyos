/* runtime.c — el "libc" mínimo de pyos.
 * Estas son las únicas funciones que existen en el sistema: no hay malloc,
 * no hay filesystem todavía, no hay hilos. Todo lo que el código Python del
 * usuario transpilado a C invoca (pyos_draw, pyos_clear, etc.) vive acá.
 */

#include <stdint.h>
#include <stddef.h>

/* ---------- VGA texto (0xB8000), 80x25, el framebuffer real de la BIOS ---------- */
#define VGA_WIDTH  80
#define VGA_HEIGHT 25
#define VGA_MEM    ((uint16_t*)0xB8000)
#define VGA_COLOR  0x0F /* blanco sobre negro */

static size_t vga_row = 0;
static size_t vga_col = 0;

static inline uint16_t vga_entry(char c, uint8_t color) {
    return (uint16_t)c | (uint16_t)color << 8;
}

static void vga_scroll(void) {
    for (size_t y = 1; y < VGA_HEIGHT; y++) {
        for (size_t x = 0; x < VGA_WIDTH; x++) {
            VGA_MEM[(y - 1) * VGA_WIDTH + x] = VGA_MEM[y * VGA_WIDTH + x];
        }
    }
    for (size_t x = 0; x < VGA_WIDTH; x++) {
        VGA_MEM[(VGA_HEIGHT - 1) * VGA_WIDTH + x] = vga_entry(' ', VGA_COLOR);
    }
    vga_row = VGA_HEIGHT - 1;
}

void pyos_clear(void) {
    for (size_t y = 0; y < VGA_HEIGHT; y++)
        for (size_t x = 0; x < VGA_WIDTH; x++)
            VGA_MEM[y * VGA_WIDTH + x] = vga_entry(' ', VGA_COLOR);
    vga_row = 0;
    vga_col = 0;
}

void pyos_putc(char c) {
    if (c == '\n') {
        vga_col = 0;
        vga_row++;
    } else {
        VGA_MEM[vga_row * VGA_WIDTH + vga_col] = vga_entry(c, VGA_COLOR);
        if (++vga_col == VGA_WIDTH) {
            vga_col = 0;
            vga_row++;
        }
    }
    if (vga_row >= VGA_HEIGHT) vga_scroll();
}

void pyos_draw(const char* s) {
    while (*s) pyos_putc(*s++);
}

/* ---------- Puerto serie COM1 (0x3F8) — para logs y debug en QEMU ---------- */
static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}
static inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    __asm__ volatile ("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

void pyos_serial_init(void) {
    outb(0x3F8 + 1, 0x00);
    outb(0x3F8 + 3, 0x80);
    outb(0x3F8 + 0, 0x03);
    outb(0x3F8 + 1, 0x00);
    outb(0x3F8 + 3, 0x03);
    outb(0x3F8 + 2, 0xC7);
    outb(0x3F8 + 4, 0x0B);
}

static int serial_tx_empty(void) {
    return inb(0x3F8 + 5) & 0x20;
}

void pyos_log(const char* s) {
    while (*s) {
        while (!serial_tx_empty());
        outb(0x3F8, *s++);
    }
}

/* ---------- Control de CPU ---------- */
void pyos_halt(void) {
    __asm__ volatile ("cli");
    for (;;) __asm__ volatile ("hlt");
}

/* ---------- Entry point real, llamado desde boot.asm ---------- */
extern void pyos_entry(void); /* definida en generated.c, transpilada del Python del usuario */

void kernel_main(void) {
    pyos_serial_init();
    pyos_clear();
    pyos_log("pyos: kernel_main() arrancó, saltando a pyos_entry()\n");
    pyos_entry();
    pyos_halt(); /* si pyos_entry() vuelve, no hay a dónde ir: frenamos la CPU */
}
