/* pyos_runtime.h — API que el código transpilado (generated.c) puede llamar.
 * Esta es la superficie real disponible para el Python del usuario: si una
 * función no está acá, no existe en el kernel.
 */
#ifndef PYOS_RUNTIME_H
#define PYOS_RUNTIME_H

#include <stdint.h>

void pyos_clear(void);
void pyos_putc(char c);
void pyos_draw(const char* s);
void pyos_log(const char* s);
void pyos_log_char(char c);
void pyos_putdec(uint32_t n);
void pyos_halt(void);
void pyos_reboot(void);
void pyos_kb_init(void);
int pyos_readline(void);
int pyos_kb_char(int i);
const char* pyos_line(void);

/* heap.c — memoria dinámica (free-list real) + strings dinámicos */
void pyos_heap_init(uint32_t base, uint32_t size);
void* pyos_alloc(uint32_t size);
void pyos_free(void* ptr);
uint32_t pyos_heap_total(void);
uint32_t pyos_heap_used(void);
uint32_t pyos_heap_free(void);
const char* pyos_strdup(const char* s);
const char* pyos_concat(const char* a, const char* b);
const char* pyos_int_to_str(int value);
int pyos_streq(const char* a, const char* b);

/* speaker.c / rng.c */
void pyos_beep(int freq_hz, int ms);
int pyos_random_int(int n);

#endif