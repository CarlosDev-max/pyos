/* timer.c — PIT (Programmable Interval Timer) canal 0, IRQ0 a ~100 Hz.
 * Provee el tick de tiempo para la multitarea preemptiva.
 */

#include <stdint.h>

static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}
static inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    __asm__ volatile ("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}
extern void pyos_scheduler_tick(uint32_t frame_esp);
#define PIT_BASE   1193182u
#define TIMER_HZ   100u

volatile uint32_t pyos_ticks_counter = 0;

void pyos_timer_irq(uint32_t frame_esp) {
    pyos_ticks_counter++;
    pyos_scheduler_tick(frame_esp);
}

void pyos_timer_init(void) {
    /* canal 0, rate generator (modo 2), lobyte/hibyte (modo 3 byte-access) */
    outb(0x43, 0x34);
    uint16_t div_ = (uint16_t)(PIT_BASE / TIMER_HZ);
    outb(0x40, (uint8_t)(div_ & 0xFF));
    outb(0x40, (uint8_t)((div_ >> 8) & 0xFF));
    /* desenmascarar IRQ0 (bit 0 del master 0x21) */
    outb(0x21, (uint8_t)(inb(0x21) & 0xFE));
}

/* uptime en ticks (1 tick = 10 ms) */
uint32_t pyos_uptime(void) {
    return pyos_ticks_counter;
}
