/* heap.c — memoria dinámica real del kernel.
 *
 * Allocador por "implicit free list": cada bloque lleva un header con tamaño,
 * magic (detección básica de corrupción) y flag de libre. Soporta splitting
 * (partir un bloque grande al allocar) y coalescing (fusionar bloques libres
 * adyacentes al liberar), alineación a 8 bytes y estadísticas.
 *
 * Este archivo es 100% freestanding y sin I/O: cada backend de boot le pasa
 * los límites del área de memoria que quiere usar como heap con
 * `pyos_heap_init(base, size)`:
 *   - runtime.c (target "iso"): un arreglo estático de 1 MiB en .bss
 *   - runtime_linux.c (target "linux-init"): 1 MiB pedido a Linux con brk()
 *
 * Cuando el heap se agota, pyos_alloc/pyos_concat/... devuelven 0 (NULL) y se
 * anota por COM1/stderr — nunca se cuelga la máquina: el código Python puede
 * chequear el resultado con `== 0`.
 *
 * API compatible con la anterior (bump allocator):
 *   pyos_alloc / pyos_concat / pyos_int_to_str / pyos_streq
 * API nueva (Fase 3 del roadmap):
 *   pyos_free / pyos_strdup / pyos_heap_total / pyos_heap_used / pyos_heap_free
 */

#include <stdint.h>
#include <stddef.h>

#include "pyos_runtime.h"

#define HEAP_MAGIC 0x6D794048UL /* "myH@" */
#define HEAP_ALIGN 8u
#define MIN_BLK    (sizeof(blk_t) + 8u)

typedef struct blk blk_t;
struct blk {
    uint32_t size;   /* tamaño total del bloque (header + payload) */
    uint32_t magic;
    int      free;
    blk_t*   next;
};
#define HDR_SIZE sizeof(blk_t)
#define DATA(b) ((uint8_t*)(b) + HDR_SIZE)
#define HDR(p)  ((blk_t*)((uint8_t*)(p) - HDR_SIZE))

static blk_t*    heap_head = 0;
static uint32_t  heap_total_bytes = 0;
static uint32_t  heap_used_bytes = 0;
static int       heap_ready = 0;

/* el sistema de boot le entrega el área de memoria (no la toma sola) */
extern void pyos_log(const char* s);

static uint32_t align8(uint32_t x) {
    return (x + (HEAP_ALIGN - 1)) & ~(HEAP_ALIGN - 1u);
}

void pyos_heap_init(uint32_t base, uint32_t size) {
    if (heap_ready) return;
    if (size < MIN_BLK) return;
    blk_t* b = (blk_t*)(uintptr_t)base;
    b->size = align8(size);
    b->magic = HEAP_MAGIC;
    b->free = 1;
    b->next = 0;
    heap_head = b;
    heap_total_bytes = b->size;
    heap_used_bytes = 0;
    heap_ready = 1;
}

void* pyos_alloc(uint32_t size) {
    if (!heap_ready || size > heap_total_bytes) return 0;
    uint32_t need = align8(size + HDR_SIZE);
    if (need < HDR_SIZE + 8u) need = HDR_SIZE + 8u;

    for (blk_t* b = heap_head; b; b = b->next) {
        if (!b->free) continue;
        if (b->size < need) continue;
        if (b->size - need >= MIN_BLK) {     /* splitting */
            blk_t* nb = (blk_t*)((uint8_t*)b + need);
            nb->size = b->size - need;
            nb->magic = HEAP_MAGIC;
            nb->free = 1;
            nb->next = b->next;
            b->next = nb;
            b->size = need;
        }
        b->free = 0;
        heap_used_bytes += b->size;
        return DATA(b);
    }
    pyos_log("pyos: heap agotado (devuelvo 0, chequea el resultado)\n");
    return 0;
}

void pyos_free(void* ptr) {
    if (!heap_ready || !ptr) return;
    blk_t* b = HDR(ptr);
    if (b->magic != HEAP_MAGIC) return;  /* corrupción / puntero inválido */
    if (b->free) return;                 /* double free */
    b->free = 1;
    heap_used_bytes -= b->size;

    blk_t* next = b->next;               /* coalescing con el siguiente */
    if (next && next->free && next->magic == HEAP_MAGIC
        && (uint8_t*)b + b->size == (uint8_t*)next) {
        b->size += next->size;
        b->next = next->next;
    }
    blk_t* prev = 0;                     /* … y con el anterior */
    for (blk_t* q = heap_head; q && q != b; q = q->next) prev = q;
    if (prev && prev->free && prev->magic == HEAP_MAGIC
        && (uint8_t*)prev + prev->size == (uint8_t*)b) {
        prev->size += b->size;
        prev->next = b->next;
    }
}

uint32_t pyos_heap_total(void) { return heap_total_bytes; }
uint32_t pyos_heap_used(void)  { return heap_used_bytes; }
uint32_t pyos_heap_free(void)  { return heap_total_bytes - heap_used_bytes; }

/* ---------- strings dinámicos sobre el heap --------------------------- */

static uint32_t hstrlen(const char* s) {
    uint32_t n = 0;
    if (!s) return 0;
    while (s[n]) n++;
    return n;
}

const char* pyos_strdup(const char* s) {
    if (!s) return 0;
    return pyos_concat(s, "");
}

/* Concatenación: lo que genera `a + b` entre strings en el transpiler. */
const char* pyos_concat(const char* a, const char* b) {
    if (!a || !b) return 0;
    uint32_t la = hstrlen(a), lb = hstrlen(b);
    char* out = (char*)pyos_alloc(la + lb + 1);
    if (!out) return 0;
    uint32_t i = 0;
    for (uint32_t j = 0; j < la; j++) out[i++] = a[j];
    for (uint32_t j = 0; j < lb; j++) out[i++] = b[j];
    out[i] = 0;
    return out;
}

/* Conversión int→str: lo que genera `str(x)` en el transpiler. */
const char* pyos_int_to_str(int value) {
    char tmp[12]; /* -2147483648 + '\0' = 12 */
    int i = 0;
    int neg = value < 0;
    unsigned int v = neg ? (unsigned int)(-(value + 1)) + 1u : (unsigned int)value;

    if (v == 0) tmp[i++] = '0';
    while (v > 0) {
        tmp[i++] = '0' + (v % 10);
        v /= 10;
    }
    if (neg) tmp[i++] = '-';

    char* out = (char*)pyos_alloc((uint32_t)i + 1);
    if (!out) return 0;
    for (int k = 0; k < i; k++) out[k] = tmp[i - 1 - k];
    out[i] = 0;
    return out;
}

/* Igualdad de strings: lo que genera `a == b` entre str. */
int pyos_streq(const char* a, const char* b) {
    if (!a || !b) return a == b;
    while (*a && *b) {
        if (*a != *b) return 0;
        a++;
        b++;
    }
    return *a == *b;
}