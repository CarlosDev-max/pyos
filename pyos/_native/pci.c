/* pci.c — enumeración PCI mínima (Fase 6 del roadmap).
 *
 * Acceso al config space vía el "mecanismo #1": los puertos 0xCF8
 * (dirección) y 0xCFC (datos). Es el mecanismo que soporta QEMU y
 * cualquier chipset x86 desde los 90s — no hace falta MMIO/ACPI para esto.
 * Solo implementamos lo que necesita la NIC: buscar por vendor/device id,
 * leer un BAR, y habilitar bus mastering (para que la tarjeta pueda hacer
 * DMA hacia nuestros buffers de recepción/transmisión).
 */
#include <stdint.h>

static inline void outl(uint16_t port, uint32_t val) {
    __asm__ volatile ("outl %0, %1" : : "a"(val), "Nd"(port));
}
static inline uint32_t inl(uint16_t port) {
    uint32_t ret;
    __asm__ volatile ("inl %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

#define PCI_CONFIG_ADDR 0xCF8
#define PCI_CONFIG_DATA 0xCFC

static uint32_t pci_addr(uint8_t bus, uint8_t dev, uint8_t fn, uint8_t off) {
    return 0x80000000u
         | ((uint32_t)bus << 16)
         | ((uint32_t)dev << 11)
         | ((uint32_t)fn  << 8)
         | (off & 0xFCu);
}

uint32_t pci_read32(uint8_t bus, uint8_t dev, uint8_t fn, uint8_t off) {
    outl(PCI_CONFIG_ADDR, pci_addr(bus, dev, fn, off));
    return inl(PCI_CONFIG_DATA);
}

void pci_write32(uint8_t bus, uint8_t dev, uint8_t fn, uint8_t off, uint32_t val) {
    outl(PCI_CONFIG_ADDR, pci_addr(bus, dev, fn, off));
    outl(PCI_CONFIG_DATA, val);
}

/* busca un dispositivo por vendor/device id. Recorre bus 0-3 (alcanza para
 * QEMU y para la inmensa mayoría de PCs reales, donde la NIC vive en el
 * bus principal); si lo encuentra, deja bus/dev/fn en los punteros. */
int pci_find(uint16_t vendor, uint16_t device,
             uint8_t* out_bus, uint8_t* out_dev, uint8_t* out_fn) {
    for (int bus = 0; bus < 4; bus++) {
        for (int dev = 0; dev < 32; dev++) {
            for (int fn = 0; fn < 8; fn++) {
                uint32_t id = pci_read32((uint8_t)bus, (uint8_t)dev, (uint8_t)fn, 0);
                if (id == 0xFFFFFFFFu) {
                    if (fn == 0) break; /* sin función 0: no hay nada en este dev */
                    continue;
                }
                uint16_t vid = (uint16_t)(id & 0xFFFFu);
                uint16_t did = (uint16_t)(id >> 16);
                if (vid == vendor && did == device) {
                    *out_bus = (uint8_t)bus;
                    *out_dev = (uint8_t)dev;
                    *out_fn  = (uint8_t)fn;
                    return 1;
                }
            }
        }
    }
    return 0;
}

uint32_t pci_bar(uint8_t bus, uint8_t dev, uint8_t fn, int n) {
    return pci_read32(bus, dev, fn, (uint8_t)(0x10 + n * 4));
}

/* habilita I/O space + bus mastering (bits 0 y 2 del command register) */
void pci_enable_bus_master(uint8_t bus, uint8_t dev, uint8_t fn) {
    uint32_t cmd = pci_read32(bus, dev, fn, 0x04);
    cmd |= 0x0005u;
    pci_write32(bus, dev, fn, 0x04, cmd);
}
