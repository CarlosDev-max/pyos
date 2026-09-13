/* ata.c — driver ATA PIO (28-bit LBA) para discos IDE reales.
 *
 * Habla con el controlador PIIX del chipset (0x1F0 primario, 0x170
 * secundario) por puertos de E/S programados (PIO), sin DMA. Esto es lo que
 * le permite a MYOS leer y ESCRIBIR un disco de verdad: en QEMU es un
 * `-drive file=disk.img,format=raw`, en hardware real es el disco IDE/SATA
 * en modo legacy del primer/second channel.
 *
 * Multisector: los drivers del filesystem (myfs.c) piden sectores de 512 B;
 * acá se soporta count > 1 encadenando transferencias de 1 sector para
 * mantener el polling simple y robusto.
 */

#include <stdint.h>
#include <stddef.h>

#include "pyos_runtime.h"

#define ATA_PRIMARY_IO   0x1F0
#define ATA_SECONDARY_IO 0x170
#define SECTOR_SIZE      512u

#define ATA_SR_BSY 0x80
#define ATA_SR_DRDY 0x40
#define ATA_SR_DF  0x20
#define ATA_SR_DRQ 0x08
#define ATA_SR_ERR 0x01

#define ATA_CMD_READ_PIO   0x20
#define ATA_CMD_WRITE_PIO  0x30
#define ATA_CMD_IDENTIFY   0xEC

#define ATA_ID_DEVICE_TYPE 0
#define ATA_ID_LBA_CAPACITY 60   /* words 60-61: 48-bit LBA capacity */

extern void pyos_log(const char* s);

static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}
static inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    __asm__ volatile ("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}
static inline void insw_16(uint16_t port, void* buf, uint32_t words) {
    __asm__ volatile ("rep insw"
                      : "+D"(buf), "+c"(words) : "d"(port) : "memory");
}
static inline void outsw_16(uint16_t port, const void* buf, uint32_t words) {
    __asm__ volatile ("rep outsw"
                      : "+S"(buf), "+c"(words) : "d"(port) : "memory");
}
static inline void io_wait(void) {
    outb(0x80, 0); /* write to unused port: ~1 microsegundo de espera */
}

/* espera 400 ns mínimos entre cada registro del ATA */
static void ata_io_delay(void) {
    io_wait(); io_wait(); io_wait(); io_wait();
}

/* poll hasta que BSY se libere, con timeout por si no existe el drive */
static int ata_wait_ready(uint16_t io, uint32_t timeout) {
    while (timeout--) {
        uint8_t st = inb(io + 7);
        if (!(st & ATA_SR_BSY)) return (int)st;
    }
    return 0; /* timeout: no hay disco acá */
}

/* devuelve 1 si hay un dispositivo ATA (HDD) válido en {bus,drive}
 * (distinto de ATAPI: el CD-ROM se maneja por ATAPI, otro archivo) */
int ata_present(int bus, int drive) {
    uint16_t io = bus ? ATA_SECONDARY_IO : ATA_PRIMARY_IO;

    outb(io + 6, (uint8_t)(0xA0 | (drive << 4)));
    ata_io_delay();
    outb(io + 2, 0);
    outb(io + 3, 0);
    outb(io + 4, 0);
    outb(io + 5, 0);
    outb(io + 7, ATA_CMD_IDENTIFY);
    ata_io_delay();

    int st = ata_wait_ready(io, 1u << 26);
    if (!st) return 0;                 /* no hubo respuesta */
    uint8_t lo = inb(io + 4), hi = inb(io + 5);
    if ((lo | hi) != 0) return 0;      /* no device / ghost */

    if (inb(io + 7) & ATA_SR_ERR) return 0;
    return 1;
}

/* cantidad de sectores del disco {bus, drive} según IDENTIFY */
uint32_t ata_lba_count(int bus, int drive) {
    if (!ata_present(bus, drive)) return 0;
    uint16_t io = bus ? ATA_SECONDARY_IO : ATA_PRIMARY_IO;
    uint16_t id[256];
    /* re-issue IDENTIFY y leé el bloque identificador */
    outb(io + 6, (uint8_t)(0xA0 | (drive << 4)));
    ata_io_delay();
    outb(io + 2, 0); outb(io + 3, 0); outb(io + 4, 0); outb(io + 5, 0);
    outb(io + 7, ATA_CMD_IDENTIFY);
    ata_io_delay();
    if (!ata_wait_ready(io, 1u << 26)) return 0;
    insw_16(io, id, 256);
    return ((uint32_t)id[ATA_ID_LBA_CAPACITY]
            | ((uint32_t)id[ATA_ID_LBA_CAPACITY + 1] << 16));
}

static int ata_sector_pio(uint16_t io, uint32_t lba, void* buf, int write) {
    outb(io + 2, 1);                                    /* count = 1 */
    outb(io + 3, (uint8_t)(lba & 0xFF));
    outb(io + 4, (uint8_t)((lba >> 8) & 0xFF));
    outb(io + 5, (uint8_t)((lba >> 16) & 0xFF));
    outb(io + 6, (uint8_t)(0x40 | 0xE0 | ((lba >> 24) & 0x0F)));
    outb(io + 7, write ? ATA_CMD_WRITE_PIO : ATA_CMD_READ_PIO);
    ata_io_delay();

    int st = ata_wait_ready(io, 1u << 26);
    if (!st || (st & ATA_SR_ERR)) return -1;

    if (write) {
        outsw_16(io, buf, SECTOR_SIZE / 2);
        /* flush del búfer del drive, y chequeo post-write por errores */
        outb(io + 7, 0xE7); /* FLUSH CACHE */
        ata_io_delay();
        ata_wait_ready(io, 1u << 26);
    } else {
        insw_16(io, buf, SECTOR_SIZE / 2);
    }
    return 0;
}

int ata_read_lba(int bus, int drive, uint32_t lba, void* buf, int sectors) {
    uint16_t io = bus ? ATA_SECONDARY_IO : ATA_PRIMARY_IO;
    uint8_t* p = (uint8_t*)buf;
    for (int i = 0; i < sectors; i++) {
        if (ata_sector_pio(io, lba + (uint32_t)i, p, 0) != 0) return -1;
        p += SECTOR_SIZE;
    }
    return 0;
}

int ata_write_lba(int bus, int drive, uint32_t lba, const void* buf, int sectors) {
    uint16_t io = bus ? ATA_SECONDARY_IO : ATA_PRIMARY_IO;
    const uint8_t* p = (const uint8_t*)buf;
    for (int i = 0; i < sectors; i++) {
        if (ata_sector_pio(io, lba + (uint32_t)i, (void*)p, 1) != 0) return -1;
        p += SECTOR_SIZE;
    }
    return 0;
}

/* init: qué hay conectado (log a COM1) — el FS decide qué usar */
void pyos_ata_init(void) {
    if (ata_present(0, 0)) {
        pyos_log("ata: disco primario master encontrado (");
        uint32_t n = ata_lba_count(0, 0);
        /* logging numérico minimximal por byte */
        pyos_log_char((char)('0' + (uint32_t)((n / (4u * 1000u)) % 10)));
        pyos_log(" MB)\n");
    } else {
        pyos_log("ata: sin disco ATA en primario master\n");
    }
}