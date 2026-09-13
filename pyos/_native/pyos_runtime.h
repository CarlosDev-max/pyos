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

/* paging.c — memoria virtual (identity map 16 MiB al boot) */
void pyos_paging_init(void);

/* runtime.c */
const char* pyos_substr(int start, int len);

/* ata.c + myfs.c — Fase 4: disco IDE y filesystem MYOSFS */
void pyos_ata_init(void);
int pyos_fs_init(void);
int pyos_fopen(const char* name, int mode);   /* 0=leer, 1=escribir(truncar) */
int pyos_fwrite(int fd, const char* data);
const char* pyos_fread(int fd, int max);
int pyos_fclose(int fd);
int pyos_fexists(const char* name);
int pyos_fdel(const char* name);
int pyos_fls(void);
int pyos_fsize(int fd);
void pyos_fs_status(void);

/* iso9660.c — lectura del volumen ISO9660 que viaja como módulo multiboot */
void pyos_iso_init(void);
int pyos_iso_status(void);
int pyos_iso_ls(void);
const char* pyos_iso_read(const char* name);
void pyos_iso_free(const char* p);

/* timer.c + proc.c — Fase 5: multitarea preemptiva (round-robin, PIT) */
void pyos_scheduler_init(void);
void pyos_idle_stack_setup(void);
void pyos_timer_init(void);
void pyos_spawn(const char* name, void (*fn)(void));
void pyos_exit_task(void);
void pyos_sleep(uint32_t ms);
void pyos_ps(void);
uint32_t pyos_uptime(void);
uint32_t pyos_ticks(void);

#endif