/* proc.c — multitarea preemptiva (round-robin) sobre el PIT.
 *
 * Fase 5 del roadmap: varios procesos cooperando en la misma CPU. El tick
 * del PIT (timer.c) llama a pyos_scheduler_tick(), que decide si hay que
 * rotar el quantum y, de ser así, deja el ESP del próximo proceso en
 * scheduler_next_esp. boot.asm (irq_common) detecta ese valor al volver de
 * irq_handler y cambia el stack antes de restaurar registros: los procesos
 * se suspenden/retoman aprovechando el mismo frame de interrupción, así que
 * la conmutación es exacta y sin código extra de salvar estado.
 *
 * Cada proceso tiene su propio stack de kernel (del heap). Cuando el quantum
 * (1 tick) expira, el proceso se devuelve a la cola listos y el siguiente de
 * la cola pasa a correr. pyos.sleep() lo saca de la cola por N ticks.
 * pyos.ps() lista los procesos; pyos.spawn("nombre", funcion) crea uno nuevo.
 */

#include <stdint.h>
#include <stddef.h>

#include "pyos_runtime.h"

extern void pyos_log(const char* s);
extern void pyos_draw(const char* s);
extern void pyos_putdec(uint32_t n);
extern void* pyos_alloc(uint32_t size);
extern void pyos_free(void* p);

extern volatile uint32_t pyos_ticks_counter;
extern volatile uint32_t scheduler_next_esp;

#define MAX_PROCS 16

enum { PROC_UNUSED, PROC_READY, PROC_RUNNING, PROC_SLEEPING, PROC_EXITED };

typedef void (*proc_func)(void);

typedef struct proc {
    uint8_t       used;
    uint8_t       state;
    char          name[16];
    proc_func     func;
    uint32_t*     esp;          /* top del frame de interrupción de este proceso */
    uint32_t      sleep_left;   /* ticks restantes de sleep */
} proc_t;

static proc_t procs[MAX_PROCS];
static int current = -1;        /* índice del proceso corriendo (o -1) */

static uint32_t* idle_frame(void);   /* abajo: usa un stack estático */
static int next_ready_from(int start);
static void setup_frame(uint32_t* frame, uint32_t eip);

/* el proceso actual termina: lo marcamos y rotamos al siguiente listo */
static void finish_current(void) {
    int idx = current;
    if (idx < 0) return;
    procs[idx].state = PROC_EXITED;
    int next = next_ready_from(idx);
    if (next < 0) { current = -1; return; }
    current = next;
    procs[next].state = PROC_RUNNING;
    scheduler_next_esp = (uint32_t)(uintptr_t)procs[next].esp;
}

static void proc_entry(void) {
    procs[current].func();
    finish_current();
    for (;;) {
        __asm__ volatile ("hlt");
    }
}

/* el proceso 1 es el "idle": existe siempre, no hace nada, corre cuando no
 * hay nada más listo. Es lo que evita que la CPU se quede sin proceso. */
static void idle_task(void) {
    for (;;) {
        __asm__ volatile ("hlt");
    }
}

static int next_ready_from(int start) {
    for (int i = 1; i <= MAX_PROCS; i++) {
        int idx = (start + i) % MAX_PROCS;
        if (procs[idx].used && procs[idx].state == PROC_READY) return idx;
    }
    return -1;
}

/* cur_frame_esp es el frame real de interrupción del proceso en curso (el
 * puntero r que recibe irq_handler): lo guardamos en procs[current].esp para
 * que la próxima vez que el scheduler elija a ese proceso reanude EXACTAMENTE
 * donde fue interrumpido (por el PIT o por un yield), en vez de un frame
 * descartable de spawn. */
static void maybe_reschedule(uint32_t cur_frame_esp) {
    if (current >= 0 && cur_frame_esp)
        procs[current].esp = (uint32_t*)(uintptr_t)cur_frame_esp;
    int next = next_ready_from(current);
    if (next < 0) {           /* nada listo: queda el current (o idle) */
        return;
    }
    if (next == current) return;
    if (current >= 0) {
        if (procs[current].state == PROC_RUNNING)
            procs[current].state = PROC_READY;
    }
    procs[next].state = PROC_RUNNING;
    current = next;
    scheduler_next_esp = (uint32_t)(uintptr_t)procs[next].esp;
}

void pyos_scheduler_tick(uint32_t frame_esp) {
    /* avanzar sleeps */
    for (int i = 0; i < MAX_PROCS; i++) {
        if (procs[i].used && procs[i].state == PROC_SLEEPING) {
            if (procs[i].sleep_left > 0) {
                procs[i].sleep_left--;
                if (procs[i].sleep_left == 0)
                    procs[i].state = PROC_READY;
            }
        }
    }
    /* quantum de 1 tick: rotar si hay competencia */
    maybe_reschedule(frame_esp);
}

void pyos_scheduler_init(void) {
    extern void pyos_entry(void);  /* el main del usuario, definido en generated.c */

    /* proceso principal (el @pyos.entry del usuario): lo corre el scheduler */
    uint8_t* stk = (uint8_t*)pyos_alloc(8192);
    uint32_t* frame0 = (uint32_t*)((uintptr_t)stk + 8192 - 68);
    setup_frame(frame0, (uint32_t)(uintptr_t)proc_entry);
    procs[0].used = 1;
    procs[0].state = PROC_READY;
    procs[0].name[0] = 'm'; procs[0].name[1] = 'a';
    procs[0].name[2] = 'i'; procs[0].name[3] = 'n';
    procs[0].name[4] = 0;
    procs[0].func = pyos_entry;
    procs[0].esp = frame0;
    procs[0].sleep_left = 0;

    /* proceso idle */
    procs[1].used = 1;
    procs[1].state = PROC_READY;
    procs[1].name[0] = 'i'; procs[1].name[1] = 'd';
    procs[1].name[2] = 'l'; procs[1].name[3] = 'e';
    procs[1].name[4] = 0;
    procs[1].func = idle_task;
    procs[1].esp = idle_frame();
    procs[1].sleep_left = 0;

    current = -1;   /* el primer tick arranca main (slot 0) */
    pyos_log("proc: scheduler listo (main + idle + 14 slots)\n");
}

/* frame de interrupción para un proceso nuevo: exactamente lo que
 * irq_common en boot.asm consume al restaurar (segmentos, pusha, int_no,
 * err, eip, cs, eflags) — así el proceso aparece como si hubiera sido
 * suspendido una vez por el timer. */
static void setup_frame(uint32_t* frame, uint32_t eip) {
    for (int i = 0; i < 12; i++) frame[i] = 0;
    frame[12] = 0;                       /* int_no */
    frame[13] = 0;                       /* err */
    frame[14] = eip;                     /* eip: entra por proc_entry */
    frame[15] = 0x08;                    /* cs */
    frame[16] = 0x200;                   /* eflags: IF encendido */
    frame[0] = 0x10; frame[1] = 0x10;    /* gs, fs */
    frame[2] = 0x10; frame[3] = 0x10;    /* es, ds */
}

/* stack para el idle: estático, porque en el boot aún no hay heap cuando
 * create_scheduler... en realidad ya hay heap; pero estático igual (fijo). */
static uint8_t idle_stack[8192] __attribute__((aligned(16)));

static uint32_t* idle_frame(void) {
    uint32_t* frame = (uint32_t*)((uintptr_t)idle_stack + sizeof(idle_stack) - 68);
    setup_frame(frame, (uint32_t)(uintptr_t)proc_entry);
    return frame;
}

static int find_slot(void) {
    for (int i = 1; i < MAX_PROCS; i++) {
        if (!procs[i].used) return i;
    }
    return -1;
}

void pyos_spawn(const char* name, proc_func fn) {
    int slot = find_slot();
    if (slot < 0) { pyos_log("proc: sin slots libres\n"); return; }

    uint32_t size = 8192;
    uint8_t* stack = (uint8_t*)pyos_alloc(size);
    if (!stack) { pyos_log("proc: sin heap para stack\n"); return; }

    uint32_t* frame = (uint32_t*)((uintptr_t)stack + size - 68);
    for (int i = 0; i < 12; i++) frame[i] = 0;
    frame[12] = 0;                       /* int_no */
    frame[13] = 0;                       /* err */
    frame[14] = (uint32_t)(uintptr_t)proc_entry; /* eip */
    frame[15] = 0x08;                    /* cs */
    frame[16] = 0x200;                   /* eflags */
    frame[0] = 0x10; frame[1] = 0x10;    /* gs, fs */
    frame[2] = 0x10; frame[3] = 0x10;    /* es, ds */

    int n = 0;
    while (name[n] && n < 15) { procs[slot].name[n] = name[n]; n++; }
    procs[slot].name[n] = 0;

    procs[slot].used = 1;
    procs[slot].state = PROC_READY;
    procs[slot].func = fn;
    procs[slot].esp = frame;
    procs[slot].sleep_left = 0;

    pyos_log("proc: spawned '");
    pyos_log(procs[slot].name);
    pyos_log("' en slot ");
    pyos_putdec((uint32_t)slot);
    pyos_log("\n");
}

void pyos_exit_task(void) {
    pyos_log("proc: exit de '");
    pyos_log(procs[current].name);
    pyos_log("'\n");
    procs[current].state = PROC_EXITED;
    int next = next_ready_from(current);
    if (next < 0) { current = 0; procs[0].state = PROC_RUNNING; return; }
    current = next;
    procs[next].state = PROC_RUNNING;
    scheduler_next_esp = (uint32_t)(uintptr_t)procs[next].esp;
}

void pyos_scheduler_yield(uint32_t frame_esp) {
    maybe_reschedule(frame_esp);
}

void pyos_sleep(uint32_t ms) {
    if (current < 0) return;
    procs[current].sleep_left = (ms + 9) / 10; /* 100 Hz: ~1 tick = 10 ms */
    procs[current].state = PROC_SLEEPING;
    __asm__ volatile ("int $0x40");  /* bloquea: el switch ocurre en irq_common */
}

uint32_t pyos_ticks(void) { return pyos_ticks_counter; }

/* ps: lista de procesos */
void pyos_ps(void) {
    pyos_draw("PID  Estado      Nombre\n");
    for (int i = 0; i < MAX_PROCS; i++) {
        if (!procs[i].used) continue;
        pyos_putdec((uint32_t)i);
        pyos_draw("   ");
        switch (procs[i].state) {
            case PROC_READY: pyos_draw("listo      "); break;
            case PROC_RUNNING: pyos_draw("corriendo  "); break;
            case PROC_SLEEPING: pyos_draw("durmiendo  "); break;
            case PROC_EXITED: pyos_draw("terminado  "); break;
            default: pyos_draw("?          "); break;
        }
        pyos_draw(procs[i].name);
        pyos_draw("\n");
    }
}