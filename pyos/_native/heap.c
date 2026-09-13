/* heap.c — el heap más simple posible: un bump allocator sobre un arreglo
 * estático. No hay pyos_free() todavía (no es un free-list real) — para el
 * tamaño de programas que corren en pyos hoy, el heap crece hasta agotarse
 * y listo; es una limitación conocida, documentada en el README.
 */
#include <stdint.h>

#define HEAP_SIZE (256 * 1024) /* 256 KiB */
static uint8_t heap[HEAP_SIZE];
static uint32_t heap_used = 0;

extern void pyos_log(const char*);
extern void pyos_halt(void);

void* pyos_alloc(uint32_t size) {
    size = (size + 3) & ~3u; /* alinear a 4 bytes */
    if (heap_used + size > HEAP_SIZE) {
        pyos_log("*** heap de pyos agotado (256 KiB) ***\n");
        pyos_halt();
    }
    void* ptr = &heap[heap_used];
    heap_used += size;
    return ptr;
}

static uint32_t pyos_strlen(const char* s) {
    uint32_t n = 0;
    while (s[n]) n++;
    return n;
}

/* Concatena dos strings en un buffer nuevo del heap. Esto es lo que el
 * transpiler genera para `a + b` cuando a y b son str. */
const char* pyos_concat(const char* a, const char* b) {
    uint32_t la = pyos_strlen(a), lb = pyos_strlen(b);
    char* out = (char*)pyos_alloc(la + lb + 1);
    uint32_t i = 0;
    for (uint32_t j = 0; j < la; j++) out[i++] = a[j];
    for (uint32_t j = 0; j < lb; j++) out[i++] = b[j];
    out[i] = 0;
    return out;
}

/* Convierte un int a string decimal (con signo) en un buffer del heap.
 * Esto es lo que genera `str(x)` en el Python del usuario. */
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

    char* out = (char*)pyos_alloc(i + 1);
    for (int k = 0; k < i; k++) out[k] = tmp[i - 1 - k];
    out[i] = 0;
    return out;
}

/* Igualdad de strings. Esto es lo que genera `a == b` cuando a y b son str
 * — es lo que le permite a la shell hacer `if pyos.line() == "help":` en
 * vez de comparar carácter por carácter. */
int pyos_streq(const char* a, const char* b) {
    while (*a && *b) {
        if (*a != *b) return 0;
        a++;
        b++;
    }
    return *a == *b;
}
