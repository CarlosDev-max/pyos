/* runtime_linux.c — backend "linux-init" de pyos.
 *
 * Implementa la MISMA API C que runtime.c (pyos_draw, pyos_putc,
 * pyos_readline, pyos_log...), pero sobre syscalls de Linux i386
 * (int $0x80) directamente, sin libc. Se linkea como binario estático y se
 * empaqueta en un initramfs como /init: tu kernel.py transpirado pasa a ser
 * el PID 1 de Linux.
 *
 * Así, el mismo código Python transpirado sirve para dos mundos: bare metal
 * (target "iso" con GRUB/QEMU) y un Linux mínimo (target "linux-init") —
 * para prototipar sin máquina virtual.
 *
 * heap.c, strings y conteo vienen en archivos compartidos (heap.c/rng.c);
 * speaker.c NO entra en este build (habla con el PC speaker, no tiene sentido
 * como init de Linux) — un kernel.py que use pyos.beep() fallará al linkear
 * con --target=linux-init.
 */

#include <stdint.h>
#include <stddef.h>

#include "pyos_runtime.h"

static inline long _sc(long n, long a, long b, long c) {
    long r;
    __asm__ volatile("int $0x80"
                     : "=a"(r)
                     : "a"(n), "b"(a), "c"(b), "d"(c)
                     : "memory");
    return r;
}
#define SYS_READ       3
#define SYS_WRITE      4
#define SYS_BRK        45
#define SYS_EXIT_GROUP 252

static void puts_fd(long fd, const char* s) {
    while (*s) {
        _sc(SYS_WRITE, fd, (long)s, 1);
        s++;
    }
}

void pyos_putc(char c) {
    char b = c;
    _sc(SYS_WRITE, 1, (long)&b, 1);
}

void pyos_draw(const char* s) { puts_fd(1, s); }
void pyos_log(const char* s)  { puts_fd(2, s); }

void pyos_log_char(char c) {
    char b = c;
    _sc(SYS_WRITE, 2, (long)&b, 1);
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

void pyos_clear(void) {
    puts_fd(1, "\x1b[2J\x1b[H");
}

static char line[256];
static int line_len = 0;

int pyos_readline(void) {
    line_len = 0;
    for (;;) {
        char c;
        long n = _sc(SYS_READ, 0, (long)&c, 1);
        if (n <= 0) {
            /* EOF (stdin cerrado): equivale a "hay que terminar" */
            _sc(SYS_EXIT_GROUP, 0, 0, 0);
            for (;;) {}
        }
        if (c == '\n') {
            line[line_len] = '\0';
            pyos_putc('\n');
            return line_len;
        }
        if (c == '\b' || c == 127) {
            if (line_len > 0) {
                line_len--;
                puts_fd(1, "\b \b");
            }
            continue;
        }
        if (c >= ' ' && line_len < 255) {
            line[line_len++] = c;
            pyos_putc(c);
        }
    }
}

int pyos_kb_char(int i) {
    if (i < 0 || i >= line_len) return -1;
    return (unsigned char)line[i];
}

const char* pyos_line(void) {
    return line;
}

void pyos_halt(void) {
    _sc(SYS_EXIT_GROUP, 0, 0, 0);
    for (;;) {}
}

void pyos_reboot(void) {
    _sc(SYS_EXIT_GROUP, 0, 0, 0);
    for (;;) {}
}

void pyos_kb_init(void) {
    /* no aplica: no hay PS/2 en un init de Linux */
}

/* ---------- heap sobre brk() de Linux ---------------------------------
 * heap.c (compartido con la ISO) espera que el sistema le pase el área de
 * memoria que puede usar; acá le pedimos 1 MB al kernel Linux con brk(). */

extern void pyos_heap_init(uint32_t base, uint32_t size);

static void linux_heap_init(void) {
    long base = _sc(SYS_BRK, 0, 0, 0);
    if (base <= 0) {
        for (;;) {}
    }
    long want = base + (1u << 20);
    long top = _sc(SYS_BRK, want, 0, 0);
    if (top < want) {
        for (;;) {}
    }
    pyos_heap_init((uint32_t)base, (uint32_t)(top - base));
}

/* ---------- Entry point (el ELF arranca acá, sin libc) ---------------- */

extern void pyos_entry(void);  /* en generated.c */

void __attribute__((noreturn)) _start(void) {
    linux_heap_init();
    pyos_clear();
    puts_fd(1, "pyos linux-init: saltando a pyos_entry()\n");
    pyos_entry();
    _sc(SYS_EXIT_GROUP, 0, 0, 0);
    for (;;) {}
}