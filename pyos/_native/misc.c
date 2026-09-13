/* misc.c — Tandas D (tiempo), E (máquina) y F (chars aleatorios / cajones)
 * del roadmap.
 *
 * Todo lo que no tenía un hogar natural terminó acá:
 *   - pyos.millis() / pyos.seconds()  → contador del PIT de timer.c
 *   - pyos.paging_enabled() / mem_total() / cpu_* / kernel_base()
 *   - pyos.version_string() / rand_str()
 *   - pyos.toupper_char() / tolower_char() / is_digit() / is_alpha()
 *
 * pyos.mem_heap_blocks() vive en heap.c (recorre la free-list) y
 * pyos.iso_count() en iso9660.c (recorre el directorio raíz del CD).
 */

#include <stdint.h>

#include "pyos_runtime.h"

extern void* pyos_alloc(uint32_t size);
extern uint32_t pyos_mem_total_mb;

/* ---------- Tanda D: tiempo ---------------------------------------------- */

/* millis desde el boot (1 tick del PIT = 10 ms), saturando a INT_MAX */
int pyos_millis(void) {
    uint32_t t = pyos_ticks();
    if (t > 0x7FFFFFFFu / 10u) return 0x7FFFFFFF;
    return (int)(t * 10u);
}

/* segundos desde el boot (100 ticks por segundo) */
int pyos_seconds(void) { return (int)(pyos_ticks() / 100u); }

/* ---------- Tanda E: máquina --------------------------------------------- */

/* paginación identity: se activa desde boot.asm apenas hay copia del kernel
 * al sitio alto; si hay una línea "paginas" en el log, está activa */
int pyos_paging_enabled(void) {
    uint32_t cr0;
    __asm__ volatile ("mov %%cr0, %0" : "=r"(cr0));
    return (cr0 & 0x80000000u) ? 1 : 0;
}

/* RAM total en MiB (multiboot); 32 si no hubo info multiboot válida */
int pyos_mem_total(void) {
    if (pyos_mem_total_mb > 0x7FFFFFFFu) return 0x7FFFFFFF;
    return (int)pyos_mem_total_mb;
}

/* CPUID por mayor: "GenuineIntel" / "AuthenticAMD" / "Genuine0l00" -> N/A */
const char* pyos_cpu_vendor(void) {
    uint32_t eax, ebx, ecx, edx;
    __asm__ volatile ("cpuid"
                      : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx)
                      : "a"(0u));
    static char vendor[16];
    vendor[0]  = (char)(ebx & 0xFFu);
    vendor[1]  = (char)((ebx >> 8) & 0xFFu);
    vendor[2]  = (char)((ebx >> 16) & 0xFFu);
    vendor[3]  = (char)((ebx >> 24) & 0xFFu);
    vendor[4]  = (char)(edx & 0xFFu);
    vendor[5]  = (char)((edx >> 8) & 0xFFu);
    vendor[6]  = (char)((edx >> 16) & 0xFFu);
    vendor[7]  = (char)((edx >> 24) & 0xFFu);
    vendor[8]  = (char)(ecx & 0xFFu);
    vendor[9]  = (char)((ecx >> 8) & 0xFFu);
    vendor[10] = (char)((ecx >> 16) & 0xFFu);
    vendor[11] = (char)((ecx >> 24) & 0xFFu);
    vendor[12] = 0;

    int i = 0, ok = 1;
    for (; i < 12; i++) {
        if (!((vendor[i] >= 'A' && vendor[i] <= 'Z') ||
              (vendor[i] >= 'a' && vendor[i] <= 'z') ||
              (vendor[i] >= '0' && vendor[i] <= '9')))
            ok = 0;
    }
    if (!ok) { vendor[0] = 'N'; vendor[1] = '/'; vendor[2] = 'A'; vendor[3] = 0; }
    return vendor;
}

/* CPUID mayor 1, EDX bit 0 = x87 FPU presente */
int pyos_cpu_has_fpu(void) {
    uint32_t eax = 0, ebx = 0, ecx = 0, edx = 0;
    __asm__ volatile ("cpuid"
                      : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx)
                      : "a"(1u));
    return (edx & 1u) ? 1 : 0;
}

/* dirección donde se mapea el kernel (linker.ld) */
int pyos_kernel_base(void) { return 0x00100000; }

const char* pyos_version_string(void) {
    char* out = (char*)pyos_alloc(12u);
    if (!out) return 0;
    char v[] = "MYOS v0.7-big";
    for (int i = 0; i <= 12; i++) out[i] = v[i];
    return out;
}

/* ---------- Tanda F: chars y azar ---------------------------------------- */

int pyos_toupper_char(int c) {
    return (c >= 'a' && c <= 'z') ? (c - 32) : c;
}

int pyos_tolower_char(int c) {
    return (c >= 'A' && c <= 'Z') ? (c + 32) : c;
}

int pyos_is_digit(int c) { return (c >= '0' && c <= '9') ? 1 : 0; }

int pyos_is_alpha(int c) {
    return ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')) ? 1 : 0;
}

/* n caracteres imprimibles (CP437 32..126) aleatorios; "" para n<=0 */
const char* pyos_rand_str(int n) {
    if (n > 0x7FFFFF00) n = 0x7FFFFF00; /* no te pases del heap */
    if (n <= 0) return "";
    char* out = (char*)pyos_alloc((uint32_t)n + 1u);
    if (!out) return 0;
    for (int i = 0; i < n; i++)
        out[i] = (char)(32 + pyos_random_int(95));
    out[n] = 0;
    return out;
}