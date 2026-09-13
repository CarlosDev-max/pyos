/* rng.c — pyos.random_int(n): un LCG simple (no criptográfico, para juegos
 * y demos), sembrado con RDTSC (el contador de ciclos real de la CPU) la
 * primera vez que se usa, para no dar siempre la misma secuencia.
 */
#include <stdint.h>

static uint32_t rng_state = 0;
static int rng_seeded = 0;

static inline uint64_t read_tsc(void) {
    uint32_t lo, hi;
    __asm__ volatile ("rdtsc" : "=a"(lo), "=d"(hi));
    return ((uint64_t)hi << 32) | lo;
}

static void rng_seed_if_needed(void) {
    if (!rng_seeded) {
        rng_state = (uint32_t)read_tsc() | 1u; /* nunca 0 */
        rng_seeded = 1;
    }
}

int pyos_random_int(int n) {
    if (n <= 0) return 0;
    rng_seed_if_needed();
    /* LCG estilo Numerical Recipes */
    rng_state = rng_state * 1664525u + 1013904223u;
    uint32_t v = rng_state >> 8;
    return (int)(v % (uint32_t)n);
}
