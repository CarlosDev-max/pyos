/* iso9660.c — lector del ISO con el que se booteó, tal como lo inyecta GRUB.
 *
 * Parte de la Fase 4 del roadmap: poder leer datos que vienen EN la misma ISO
 * con la que se booteó. En vez de manejar un driver ATAPI completo (frágil y
 * dependiente del emulador), aprovechamos el mecanismo estándar multiboot:
 * GRUB carga /boot/data.iso como módulo y lo deja apuntado en la info
 * multiboot. El kernel solo parsea ISO9660 desde esa memoria.
 *
 *   - pyos.iso_read("archivo.txt") busca el archivo en la raíz del volumen y
 *     lo devuelve en el heap (liberar con pyos.iso_free()).
 *   - pyos.iso_ls() lista la raíz; pyos.iso_status() dice si hay volumen.
 *   - Si no hay módulo ISO (boot sin data.iso), las funciones devuelven
 *     0/"" sin colgarse.
 */

#include <stdint.h>
#include <stddef.h>

#include "pyos_runtime.h"

#define CD_SECTOR 2048u

extern void pyos_log(const char* s);
extern void pyos_draw(const char* s);
extern void pyos_putdec(uint32_t n);
extern void pyos_log_char(char c);
extern void* pyos_alloc(uint32_t size);
extern void pyos_free(void* p);

/* el volumen ISO9660 en memoria, puesto por runtime.c desde la info multiboot */
extern const uint8_t* pyos_cd_bytes;
extern uint32_t pyos_cd_len;

static int cd_ready(void) {
    return pyos_cd_bytes != 0 && pyos_cd_len >= 16u * CD_SECTOR;
}

static const uint8_t* cd_sector(uint32_t lba) {
    return pyos_cd_bytes + (size_t)lba * CD_SECTOR;
}

static uint32_t pyos_le32(const uint8_t* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8)
         | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

typedef struct {
    uint32_t lba;
    uint32_t len;
} iso_extent_t;

static void iso_extent_of(const uint8_t* rec, iso_extent_t* out) {
    out->lba = pyos_le32(rec + 2);
    out->len = pyos_le32(rec + 10);
}

/* directorio dentro del volumen (con sanity checks de límites) */
static const uint8_t* iso_dir_buf(uint32_t lba, uint32_t need, uint32_t off) {
    if (lba + (off / CD_SECTOR) > pyos_cd_len / CD_SECTOR) return 0;
    return cd_sector(lba + (off / CD_SECTOR)) + (off % CD_SECTOR);
}

static int iso_dir_ok(const iso_extent_t* d) {
    uint64_t end = (uint64_t)d->lba * CD_SECTOR + d->len;
    return d->len > 0 && end <= pyos_cd_len;
}

/* comparar dos nombres ignorando mayúsculas y el ";<version>" ISO9660 */
static int iso_namematch(const char* a, const char* b, int blen) {
    int off = 0;
    while (off < blen && b[off] != ';') {
        char ca = a[off];
        char cb = b[off];
        if (ca == 0) return 0;
        if (ca >= 'a' && ca <= 'z') ca -= 32;
        if (cb >= 'a' && cb <= 'z') cb -= 32;
        if (ca != cb) return 0;
        off++;
    }
    return a[off] == 0;
}

/* busca un nombre (sin "/") en el directorio raíz del PVD */
static int iso_root_find(const char* name, iso_extent_t* out) {
    if (!name || !cd_ready()) return 0;

    const uint8_t* pvd = cd_sector(16);
    if (pvd[0] != 1) return 0;
    iso_extent_t root;
    iso_extent_of(pvd + 156, &root);
    if (!iso_dir_ok(&root)) return 0;

    uint32_t pos = 0;
    while (pos < root.len) {
        const uint8_t* rec = iso_dir_buf(root.lba, 0, pos);
        if (!rec) return 0;
        uint8_t len = rec[0];
        if (len == 0) { pos = ((pos / CD_SECTOR) + 1) * CD_SECTOR; continue; }
        if (!(rec[25] & 0x02) && iso_namematch(name, (const char*)rec + 33, rec[32])) {
            iso_extent_of(rec, out);
            return 1;
        }
        pos += len;
    }
    return 0;
}

/* detecta el volumen en el módulo multiboot (puesto por runtime.c) */
void pyos_iso_init(void) {
    if (!cd_ready()) {
        pyos_log("iso9660: sin volumen ISO9660 en memoria\n");
        return;
    }
    const uint8_t* pvd = cd_sector(16);
    if (pvd[0] == 1) {
        pyos_log("iso9660: volumen ISO9660 en memoria, lector listo\n");
    } else {
        pyos_log("iso9660: el modulo no tiene PVD valido\n");
        pyos_cd_len = 0;
    }
}

int pyos_iso_status(void) { return cd_ready(); }

/* lista el contenido de la raíz (para `pyos.iso_ls()`) */
int pyos_iso_ls(void) {
    if (!cd_ready()) { pyos_draw("iso: sin CD\n"); return 0; }
    const uint8_t* pvd = cd_sector(16);
    iso_extent_t root;
    iso_extent_of(pvd + 156, &root);
    if (!iso_dir_ok(&root)) return 0;

    uint32_t pos = 0;
    int n = 0;
    while (pos < root.len) {
        const uint8_t* rec = iso_dir_buf(root.lba, 0, pos);
        if (!rec) break;
        uint8_t len = rec[0];
        if (len == 0) { pos = ((pos / CD_SECTOR) + 1) * CD_SECTOR; continue; }
        pos += len;
        if (rec[25] & 0x02) continue;
        uint8_t namelen = rec[32];
        const char* rname = (const char*)rec + 33;
        n++;
        int k;
        for (k = 0; k < (int)namelen && rname[k] != ';'; k++)
            pyos_putc(rname[k]);
        if (k == 0) pyos_putc('?');
        pyos_draw("  (");
        pyos_putdec(pyos_le32(rec + 10));
        pyos_draw(" B)\n");
    }
    return n;
}

/* lee un archivo de la raíz del CD al heap; "" si no existe / no hay CD */
const char* pyos_iso_read(const char* name) {
    if (!name || !cd_ready()) return "";
    iso_extent_t f;
    if (!iso_root_find(name, &f)) return "";
    if ((uint64_t)f.lba * CD_SECTOR + f.len > pyos_cd_len) return "";

    char* buf = (char*)pyos_alloc(f.len + 1);
    if (!buf) return "";

    uint32_t copied = 0;
    uint32_t pos = 0;
    while (pos < f.len) {
        const uint8_t* src = cd_sector(f.lba + pos / CD_SECTOR) + (pos % CD_SECTOR);
        uint32_t take = f.len - pos;
        if (take > CD_SECTOR) take = CD_SECTOR;
        for (uint32_t k = 0; k < take; k++) buf[copied++] = (char)src[k];
        pos += take;
    }
    buf[f.len] = 0;
    return buf;
}

/* libera el resultado de pyos_iso_read */
void pyos_iso_free(const char* p) { pyos_free((void*)(uintptr_t)p); }