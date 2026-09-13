/* runtime.c — el "libc" mínimo de pyos.
 * Estas son las únicas funciones que existen en el sistema: no hay libc, el
 * filesystem y la multitarea viven en otros archivos (ata.c/myfs.c, timer.c).
 * Todo lo que el código Python del usuario transpilado a C invoca
 * (pyos_draw, pyos_clear, pyos_readline, pyos_heap_*, etc.) vive acá o en
 * heap.c, junto con la infraestructura de interrupciones (IDT, PIC 8259) y
 * el driver de teclado PS/2.
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

static void vga_backspace(void) {
    /* borra el carácter a la izquierda del cursor y retrocede */
    if (vga_col == 0) {
        if (vga_row > 0) {
            vga_row--;
            vga_col = VGA_WIDTH;
        }
    }
    if (vga_col > 0) vga_col--;
    VGA_MEM[vga_row * VGA_WIDTH + vga_col] = vga_entry(' ', VGA_COLOR);
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

void pyos_putdec(uint32_t n) {
    char buf[12];
    int i = 12;
    do {
        buf[--i] = (char)('0' + (n % 10));
        n /= 10;
    } while (n);
    while (i < 12) pyos_putc(buf[i++]);
}

/* ---------- Heap: área que heap.c recibe vía pyos_heap_init() ---------- */
#define KHEAP_AREA_SIZE (1u << 20)
static uint8_t kheap_area[KHEAP_AREA_SIZE] __attribute__((aligned(4096)));

/* ---------- Puerto serie COM1 (0x3F8) — para logs y debug en QEMU ---------- */
static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}
static inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    __asm__ volatile ("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}
static inline void io_wait(void) {
    /* darle tiempo al hardware: un write a un puerto sin usar (0x80) */
    outb(0x80, 0);
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

void pyos_log_char(char c) {
    while (!serial_tx_empty());
    outb(0x3F8, c);
}

static void serial_hex(uint32_t v) {
    for (int i = 28; i >= 0; i -= 4) {
        uint32_t d = (v >> i) & 0xF;
        pyos_log_char((char)(d < 10 ? '0' + d : 'a' + (d - 10)));
    }
}

/* ---------- IDT (32 bits, 256 vectores) ---------- */
struct idt_entry {
    uint16_t base_lo;
    uint16_t sel;
    uint8_t  zero;
    uint8_t  flags; /* 0x8E = present, ring 0, interrupt gate 32 bits */
    uint16_t base_hi;
};

struct idt_ptr {
    uint16_t limit;
    uint32_t base;
} __attribute__((packed));

static struct idt_entry idt[256] __attribute__((aligned(16)));
static struct idt_ptr idtp;

/* tablas de direcciones de los stubs, definidas en boot.asm */
extern uint32_t isr_stub_table[32];
extern uint32_t irq_stub_table[16];

static void idt_set_gate(int n, uint32_t base, uint16_t sel) {
    idt[n].base_lo = base & 0xFFFF;
    idt[n].sel = sel; /* selector de código donde viven los stubs */
    idt[n].zero = 0;
    idt[n].flags = 0x8E;
    idt[n].base_hi = (base >> 16) & 0xFFFF;
}

static uint32_t idt_current_cs(void) {
    uint32_t cs;
    __asm__ volatile ("mov %%cs, %0" : "=r"(cs));
    return cs;
}

/* ---------- PIC 8259 — remapeo a los vectores 32-47 ---------- */
static void pic_remap(void) {
    outb(0x20, 0x11); io_wait(); /* ICW1: init, edge triggered, cascade */
    outb(0xA0, 0x11); io_wait();
    outb(0x21, 0x20); io_wait(); /* ICW2: master empieza en el vector 32 */
    outb(0xA1, 0x28); io_wait(); /* ICW2: slave empieza en el vector 40 */
    outb(0x21, 0x04); io_wait(); /* ICW3: slave por IRQ2 */
    outb(0xA1, 0x02); io_wait();
    outb(0x21, 0x01); io_wait(); /* ICW4: modo 8086 */
    outb(0xA1, 0x01); io_wait();
    outb(0x21, 0xFD);            /* mascara: habilitar solo IRQ1 (teclado) */
    outb(0xA1, 0xFF);
}

/* ---------- Teclado PS/2 (scancode set 1) ---------- */
#define KB_IRQ 33

/* decodificación básica de scancodes: solo lo que el kernel necesita hoy
 * (letras, números, espacio, enter, backspace, shift, caps lock). Cualquier
 * otra tecla (esc, tab, flechas, E0 xx...) devuelve 0 y se ignora. */
static const char kb_plain[128] = {
    [0x02] = '1', [0x03] = '2', [0x04] = '3', [0x05] = '4', [0x06] = '5',
    [0x07] = '6', [0x08] = '7', [0x09] = '8', [0x0A] = '9', [0x0B] = '0',
    [0x0C] = '-', [0x0D] = '=',
    [0x0E] = '\b',
    [0x10] = 'q', [0x11] = 'w', [0x12] = 'e', [0x13] = 'r', [0x14] = 't',
    [0x15] = 'y', [0x16] = 'u', [0x17] = 'i', [0x18] = 'o', [0x19] = 'p',
    [0x1A] = '[', [0x1B] = ']',
    [0x1C] = '\n',
    [0x1E] = 'a', [0x1F] = 's', [0x20] = 'd', [0x21] = 'f', [0x22] = 'g',
    [0x23] = 'h', [0x24] = 'j', [0x25] = 'k', [0x26] = 'l',
    [0x27] = ';', [0x28] = '\'', [0x29] = '`',
    [0x2B] = '\\',
    [0x2C] = 'z', [0x2D] = 'x', [0x2E] = 'c', [0x2F] = 'v', [0x30] = 'b',
    [0x31] = 'n', [0x32] = 'm',
    [0x33] = ',', [0x34] = '.', [0x35] = '/',
    [0x39] = ' ',
};

static const char kb_shifted[128] = {
    [0x02] = '!', [0x03] = '@', [0x04] = '#', [0x05] = '$', [0x06] = '%',
    [0x07] = '^', [0x08] = '&', [0x09] = '*', [0x0A] = '(', [0x0B] = ')',
    [0x0C] = '_', [0x0D] = '+',
    [0x0E] = '\b',
    [0x10] = 'Q', [0x11] = 'W', [0x12] = 'E', [0x13] = 'R', [0x14] = 'T',
    [0x15] = 'Y', [0x16] = 'U', [0x17] = 'I', [0x18] = 'O', [0x19] = 'P',
    [0x1A] = '{', [0x1B] = '}',
    [0x1C] = '\n',
    [0x1E] = 'A', [0x1F] = 'S', [0x20] = 'D', [0x21] = 'F', [0x22] = 'G',
    [0x23] = 'H', [0x24] = 'J', [0x25] = 'K', [0x26] = 'L',
    [0x27] = ':', [0x28] = '"', [0x29] = '~',
    [0x2B] = '|',
    [0x2C] = 'Z', [0x2D] = 'X', [0x2E] = 'C', [0x2F] = 'V', [0x30] = 'B',
    [0x31] = 'N', [0x32] = 'M',
    [0x33] = '<', [0x34] = '>', [0x35] = '?',
    [0x39] = ' ',
};

static int kb_shift = 0;
static int kb_caps = 0;

/* ring buffer estático de caracteres ya decodificados (sin malloc) */
#define KB_RING_SIZE 256
#define KB_LINE_MAX  255

static volatile char kb_ring[KB_RING_SIZE];
static volatile size_t kb_ring_head = 0;
static volatile size_t kb_ring_tail = 0;

/* el carácter 0 nunca llega acá (los scancodes sin mapear se descartan),
 * así que sirve como "vacío" para pop. */
static void kb_ring_push(char c) {
    size_t next = (kb_ring_head + 1) % KB_RING_SIZE;
    if (next == kb_ring_tail) return; /* lleno: descartar (evitar overflow) */
    kb_ring[kb_ring_head] = c;
    kb_ring_head = next;
}

static char kb_ring_pop(void) {
    if (kb_ring_tail == kb_ring_head) return 0; /* vacío */
    char c = kb_ring[kb_ring_tail];
    kb_ring_tail = (kb_ring_tail + 1) % KB_RING_SIZE;
    return c;
}

static void kb_irq(void) {
    uint8_t sc = inb(0x60);

    if (sc == 0xE0) return; /* scancode extendido: no usado todavía */
    if (sc & 0x80) {        /* key-up (break): solo importa soltar shift */
        uint8_t kc = sc & 0x7F;
        if (kc == 0x2A || kc == 0x36) kb_shift = 0;
        return;
    }
    if (sc == 0x2A || sc == 0x36) { kb_shift = 1; return; } /* shift down */
    if (sc == 0x3A) { kb_caps = !kb_caps; return; }         /* caps lock */

    char plain = kb_plain[sc];
    if (!plain) return;

    int is_letter = (plain >= 'a' && plain <= 'z');
    int upper = (kb_shift != 0) ^ (kb_caps && is_letter);
    char c = upper ? kb_shifted[sc] : kb_plain[sc];
    if (c) kb_ring_push(c);
}

static char kb_wait_char(void) {
    for (;;) {
        char c = kb_ring_pop();
        if (c) return c;
        __asm__ volatile ("hlt"); /* dormir hasta el próximo IRQ1 */
    }
}

/* línea de entrada: echa al VGA mientras se escribe y se conserva estática */
static char kb_line[KB_LINE_MAX + 1];
static int kb_line_len = 0;

int pyos_readline(void) {
    kb_line_len = 0;
    for (;;) {
        char c = kb_wait_char();
        if (c == '\n') {
            kb_line[kb_line_len] = '\0';
            pyos_putc('\n');
            return kb_line_len;
        }
        if (c == '\b') {
            if (kb_line_len > 0) {
                kb_line_len--;
                vga_backspace();
            }
            continue;
        }
        if (kb_line_len < KB_LINE_MAX) {
            kb_line[kb_line_len++] = c;
            pyos_putc(c);
        }
    }
}

int pyos_kb_char(int i) {
    if (i < 0 || i >= kb_line_len) return -1;
    return (unsigned char)kb_line[i];
}

/* Devuelve la última línea leída por pyos_readline() como string
 * null-terminated real — esto es lo que le permite al Python del usuario
 * comparar comandos con == en vez de carácter por carácter. */
const char* pyos_line(void) {
    return kb_line;
}

/* ---------- Control de CPU ---------- */
void pyos_halt(void) {
    __asm__ volatile ("cli");
    for (;;) __asm__ volatile ("hlt");
}

void pyos_reboot(void) {
    /* Reset por el controller del teclado (8042): pulso la línea RESET de la
     * CPU con el comando 0xFE. En QEMU reinicia de inmediato; en hardware
     * real depende de la placa. Si la BIOS no responde, nos quedamos HLT. */
    __asm__ volatile ("cli");
    while (inb(0x64) & 0x02);   /* esperar a que el buffer de entrada se vacíe */
    outb(0x64, 0xFE);           /* pulso de reset del CPU */
    for (;;) __asm__ volatile ("hlt");
}

/* ---------- Handlers en C, llamados desde los stubs de boot.asm ---------- */
typedef struct {
    uint32_t gs, fs, es, ds;
    uint32_t edi, esi, ebp, esp, ebx, edx, ecx, eax;
    uint32_t int_no, err_code;
    uint32_t eip, cs, eflags, useresp, ss;
} int_regs_t;

void isr_handler(int_regs_t* r) {
    pyos_log("pyos: EXCEPCION #");
    serial_hex(r->int_no);
    pyos_log("\n");
    pyos_halt();
}

void irq_handler(int_regs_t* r) {
    if (r->int_no >= 40) outb(0xA0, 0x20); /* EOI al slave */
    outb(0x20, 0x20);                      /* EOI al master */
    if (r->int_no == KB_IRQ) kb_irq();
}

/* ---------- Inicialización de interrupciones y teclado ---------- */
void pyos_kb_init(void) {
    __asm__ volatile ("cli");

    idtp.limit = (uint16_t)(sizeof(idt) - 1);
    idtp.base = (uint32_t)&idt[0];
    uint16_t cs = (uint16_t)idt_current_cs();
    for (int i = 0; i < 32; i++) idt_set_gate(i, isr_stub_table[i], cs);
    for (int i = 0; i < 16; i++) idt_set_gate(32 + i, irq_stub_table[i], cs);

    pic_remap();

    /* descartar cualquier byte residual del controller PS/2 (p. ej. de GRUB) */
    while (inb(0x64) & 0x01) inb(0x60);
    outb(0x64, 0xAE); /* habilitar la interfaz del teclado */

    __asm__ volatile ("lidt %0" : : "m"(idtp));
    __asm__ volatile ("sti");
}

/* ---------- Entry point real, llamado desde boot.asm ---------- */
extern void pyos_entry(void); /* definida en generated.c, transpilada del Python del usuario */

void kernel_main(void) {
    pyos_heap_init((uint32_t)kheap_area, sizeof(kheap_area));
    pyos_paging_init();
    pyos_kb_init();
    pyos_serial_init();
    pyos_clear();
    pyos_log("pyos: kernel_main() arrancó, saltando a pyos_entry()\n");
    pyos_entry();
    pyos_halt(); /* si pyos_entry() vuelve, no hay a dónde ir: frenamos la CPU */
}