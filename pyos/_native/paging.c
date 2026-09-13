/* paging.c — memoria virtual real de pyos (Fase 3 del roadmap).
 *
 * Habilita paginación x86 (CR0.PG) con un directorio de páginas y las tablas
 * necesarias para mapear los primeros 16 MiB de forma identity (VA == PA).
 * Es un mapeo de arranque pensado para que el kernel, su stack, el heap y la
 * IDT sigan funcionando en exactamente las mismas direcciones — el primer
 * paso honesto hacia aislamiento de procesos con MMU (map/unmap sobre estas
 * tablas vendrá junto con la multitarea de Fase 5).
 *
 * Los 4 MiB intermedios se dejan sin mapear adrede: 0x300000..0x7FFFFF.
 * El kernel ocupa 1 MiB, el resto de la RAM es territorio del heap *real*
 * (para cuando la Fase 5 pida frame allocator). Hoy no se usa, pero no mapear
 * todo de golpe deja a la vista dónde termina lo "nuestro".
 */

#include <stdint.h>

#include "pyos_runtime.h"

/* un directorio y 4 tablas cubren 16 MiB (4 MiB por tabla, 4 KiB por página) */
#define PAGES_PER_TABLE 1024u
#define MAPPED_MB       16u

static uint32_t page_directory[PAGES_PER_TABLE] __attribute__((aligned(4096)));
static uint32_t page_tables[MAPPED_MB / 4u][PAGES_PER_TABLE]
    __attribute__((aligned(4096)));

#define PAGE_PRESENT (1u << 0)
#define PAGE_WRITABLE (1u << 1)

extern void pyos_log(const char* s);

void pyos_paging_init(void) {
    for (unsigned int i = 0; i < PAGES_PER_TABLE; i++) page_directory[i] = 0;

    for (unsigned int t = 0; t < MAPPED_MB / 4u; t++) {
        for (unsigned int p = 0; p < PAGES_PER_TABLE; p++) {
            uint32_t addr = (t * 4u * 1024u * 1024u) + (p * 4096u);
            unsigned int top = (MAPPED_MB * 1024u * 1024u);
            uint32_t entry = addr | PAGE_PRESENT | PAGE_WRITABLE;
            page_tables[t][p] = addr < top ? entry : 0;
        }
        page_directory[t] =
            (uint32_t)&page_tables[t] | PAGE_PRESENT | PAGE_WRITABLE;
    }

    __asm__ volatile (
        "mov %0, %%cr3\n"          /* cargar el directorio de páginas */
        "mov %%cr0, %%eax\n"
        "or  $0x80000000, %%eax\n" /* CR0.PG = paginación on */
        "mov %%eax, %%cr0\n"
        "jmp 1f\n"
        "1:\n"
        : : "r"((uint32_t)page_directory) : "eax", "memory");

    pyos_log("pyos: paginacion identity habilitada (16 MiB)\n");
}