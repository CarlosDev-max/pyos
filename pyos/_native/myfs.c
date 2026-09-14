/* myfs.c — "MYOSFS v1": filesystem de escritura propio de MYOS (Fase 4).
 *
 * Un sistema de archivos plano y honesto sobre el disco ATA (primario
 * master). Layout fijo sobre bloques de 1024 B (2 sectores):
 *
 *   bloque 0    superbloque (magic "MYOSFS1", totales, offsets)
 *   bloques 1-4 tabla de inodos (64 inodos de 48 B → 16/bloque)
 *   bloque 5    bitmap de bloques de datos (1 bit/bloque, 1024 B)
 *   bloques 6.. datos (archivos = corrida contigua de bloques)
 *
 * Cada inodo guarda {type, size, start, nblocks, name[24]}: un archivo es
 * una corrida CONTIGUA de bloques — diseño deliberadamente simple, sin
 * tablas indirectas; la fragmentación se maneja devolviendo error de
 * "no hay corrida libre" (honesto > magia).
 *
 * Persistencia real: cada escritura se flushea al disco (cache flush del
 * ATA), así un `-drive` de QEMU mantiene los datos entre reinicios, igual
 * que un disco físico en una PC.
 *
 * Si el disco está sin formatear (o es otra cosa), MYOSFS lo formatea solo
 * en el primer boot y loguea qué pasó.
 */

#include <stdint.h>
#include <stddef.h>

#include "pyos_runtime.h"

#define FS_MAGIC        "MYOSFS1"
#define FS_VERSION      1u
#define FS_BLOCK        1024u
#define FS_SECT_PER_BLK 2u
#define FS_NINODES      64u
#define FS_INODE_CAP    24u   /* nombre máx, incluye '\0' */
#define FS_BITMAP_BLK   5u
#define FS_DATA_START   6u
#define FS_INODE_START  1u

#define TYPE_FREE 0u
#define TYPE_FILE 1u
#define TYPE_DIR  2u

#define FS_MAX_OPEN 8u
#define FS_RBUF     4096

#pragma pack(push, 1)
typedef struct {
    char     magic[8];
    uint32_t version;
    uint32_t total_blocks;
    uint32_t n_inodes;
    uint32_t inode_start;
    uint32_t bitmap_start;
    uint32_t data_start;
    uint32_t mounted;
} myfs_super_t;

typedef struct {
    uint32_t magic;    /* 0x4D4E4653 = 'MNFS' */
    uint32_t used;
    uint32_t type;
    uint32_t size;
    uint32_t start;    /* primer bloque de datos */
    uint32_t nblocks;
    char     name[FS_INODE_CAP];
} myfs_inode_t;

typedef struct {
    int      inode;    /* índice en la tabla, o -1 */
    uint32_t pos;
} myfs_fd_t;
#pragma pack(pop)

extern void pyos_log(const char* s);
extern void pyos_draw(const char* s);
extern void pyos_putdec(uint32_t n);
extern int ata_read_lba(int bus, int drive, uint32_t lba, void* buf, int sectors);
extern int ata_write_lba(int bus, int drive, uint32_t lba, const void* buf, int sectors);
extern void* pyos_alloc(uint32_t size);
extern void pyos_free(void* p);

static uint8_t fs_disk[FS_BLOCK];
static uint8_t fs_bitmap_bitmap[FS_BLOCK];
static uint8_t fs_inode_buf[FS_BLOCK];
static myfs_fd_t fs_open[FS_MAX_OPEN];
static uint32_t fs_blocks_total = 0;
static uint32_t fs_data_blocks = 0;
static int fs_mounted = 0;

static void myfs_draw_str(const char* s) { pyos_draw(s); }

static int blk_read(uint32_t blk, void* buf) {
    return ata_read_lba(0, 0, blk * FS_SECT_PER_BLK, buf, FS_SECT_PER_BLK);
}
static int blk_write(uint32_t blk, const void* buf) {
    return ata_write_lba(0, 0, blk * FS_SECT_PER_BLK, buf, FS_SECT_PER_BLK);
}

static int fs_bm_test(uint32_t blk) {
    uint32_t byte = blk / 8, bit = blk % 8;
    return (fs_bitmap_bitmap[byte] >> bit) & 1;
}
static void fs_bm_set(uint32_t blk) {
    uint32_t byte = blk / 8, bit = blk % 8;
    fs_bitmap_bitmap[byte] |= (uint8_t)(1u << bit);
}
static void fs_bm_clr(uint32_t blk) {
    uint32_t byte = blk / 8, bit = blk % 8;
    fs_bitmap_bitmap[byte] &= (uint8_t)~(1u << bit);
}
static void fs_bm_flush(void) {
    blk_write(FS_BITMAP_BLK, fs_bitmap_bitmap);
}

static int fs_inode_load(myfs_inode_t* ino, int idx) {
    int blk = FS_INODE_START + (idx / (FS_BLOCK / sizeof(myfs_inode_t)));
    if (blk_read((uint32_t)blk, fs_inode_buf) != 0) return -1;
    *ino = ((myfs_inode_t*)fs_inode_buf)[idx % (FS_BLOCK / sizeof(myfs_inode_t))];
    return 0;
}
static int fs_inode_store(myfs_inode_t* ino, int idx) {
    int blk = FS_INODE_START + (idx / (FS_BLOCK / sizeof(myfs_inode_t)));
    if (blk_read((uint32_t)blk, fs_inode_buf) != 0) return -1;
    ((myfs_inode_t*)fs_inode_buf)[idx % (FS_BLOCK / sizeof(myfs_inode_t))] = *ino;
    if (blk_write((uint32_t)blk, fs_inode_buf) != 0) return -1;
    return 0;
}

static int fs_find_name(const char* name) {
    myfs_inode_t ino;
    for (int i = 0; i < (int)FS_NINODES; i++) {
        if (fs_inode_load(&ino, i) != 0) break;
        if (ino.magic != 0x4D4E4653u || !ino.used) continue;
        int j = 0;
        while (j < FS_INODE_CAP && name[j] && ino.name[j]
               && name[j] == ino.name[j]) j++;
        if (j < FS_INODE_CAP && name[j] == 0 && ino.name[j] == 0) return i;
    }
    return -1;
}

static int fs_alloc_inode(void) {
    myfs_inode_t ino;
    for (int i = 0; i < (int)FS_NINODES; i++) {
        if (fs_inode_load(&ino, i) != 0) break;
        if (!(ino.magic == 0x4D4E4653u && ino.used)) return i;
    }
    return -1;
}

/* busca una corrida contigua de n bloques libres; devuelve el primero */
static int fs_alloc_run(uint32_t n) {
    uint32_t run = 0, last = -1;
    for (uint32_t b = FS_DATA_START; b < fs_blocks_total; b++) {
        if (!fs_bm_test(b)) {
            if (run == 0) last = b;
            run++;
            if (run == n) return (int)last;
        } else {
            run = 0;
        }
    }
    return -1;
}

static void fs_free_run(uint32_t start, uint32_t n) {
    for (uint32_t b = start; b < start + n; b++) fs_bm_clr(b);
    fs_bm_flush();
}

static void myfs_log(const char* s) { pyos_log(s); }

/* formatea el disco entero (usado cuando no hay superbloque válido) */
static int myfs_format(void) {
    myfs_super_t super = {0};
    __builtin_memcpy(super.magic, FS_MAGIC, 8);
    super.version = FS_VERSION;
    super.total_blocks = fs_blocks_total;
    super.n_inodes = FS_NINODES;
    super.inode_start = FS_INODE_START;
    super.bitmap_start = FS_BITMAP_BLK;
    super.data_start = FS_DATA_START;
    super.mounted = 1;

    if (blk_write(0, &super) != 0) return -1;

    myfs_inode_t empty;
    __builtin_memset(&empty, 0, sizeof(empty));
    for (uint32_t b = FS_INODE_START; b < FS_DATA_START; b++)
        if (blk_write(b, &empty) != 0) return -1;

    __builtin_memset(fs_bitmap_bitmap, 0, FS_BLOCK);
    fs_bm_flush();
    return 0;
}

/* monta; si no hay FS válido, formatea. devuelve 0 si NO hay disco */
int pyos_fs_init(void) {
    if (fs_mounted) return 1;

    uint32_t lba = 0;
    /* sin driver de tamaño acá: preguntamos al ATA con un sector dummy */
    (void)lba;
    extern uint32_t ata_lba_count(int, int);
    fs_blocks_total = ata_lba_count(0, 0) / FS_SECT_PER_BLK;
    if (fs_blocks_total < FS_DATA_START + 2) {
        myfs_log("myfs: no hay disco ATA, filesystem inactivo\n");
        return 0;
    }
    fs_data_blocks = fs_blocks_total - FS_DATA_START;

    if (blk_read(0, fs_disk) != 0) return 0;
    myfs_super_t* super = (myfs_super_t*)fs_disk;
    int ok = (__builtin_memcmp(super->magic, FS_MAGIC, 8) == 0
              && super->version == FS_VERSION);

    if (!ok) {
        myfs_log("myfs: disco sin MYOSFS, formateando...\n");
        if (myfs_format() != 0) return 0;
    } else {
        fs_blocks_total = super->total_blocks;
        if (blk_read(FS_BITMAP_BLK, fs_bitmap_bitmap) != 0) return 0;
    }

    for (int i = 0; i < FS_MAX_OPEN; i++) fs_open[i].inode = -1;

    /* bloqueamos los bloques de metadatos en el bitmap */
    for (uint32_t b = 0; b < FS_DATA_START; b++) fs_bm_set(b);
    fs_mounted = 1;
    myfs_log("myfs: MYOSFS v1 montado\n");
    return 1;
}

int pyos_fopen(const char* name, int mode) {
    if (!fs_mounted || !name) return -1;

    int idx;
    if (mode == 1) {
        idx = fs_find_name(name);
        if (idx < 0) {
            idx = fs_alloc_inode();
            if (idx < 0) { myfs_log("myfs: tabla de inodos llena\n"); return -1; }
            myfs_inode_t ino;
            __builtin_memset(&ino, 0, sizeof(ino));
            ino.magic = 0x4D4E4653u;
            ino.used = 1;
            ino.type = TYPE_FILE;
            ino.start = 0;
            ino.nblocks = 0;
            int k = 0;
            while (k < FS_INODE_CAP - 1 && name[k]) { ino.name[k] = name[k]; k++; }
            ino.name[k] = 0;
            if (fs_inode_store(&ino, idx) != 0) return -1;
        } else {
            /* truncar: liberar bloques y reiniciar */
            myfs_inode_t ino;
            fs_inode_load(&ino, idx);
            if (ino.nblocks) fs_free_run(ino.start, ino.nblocks);
            ino.size = 0; ino.start = 0; ino.nblocks = 0;
            fs_inode_store(&ino, idx);
        }
    } else {
        idx = fs_find_name(name);
        if (idx < 0) return -1;
    }

    for (int i = 0; i < FS_MAX_OPEN; i++) {
        if (fs_open[i].inode < 0) {
            fs_open[i].inode = idx;
            fs_open[i].pos = 0;
            return i;
        }
    }
    return -1; /* demasiados handles abiertos */
}

int pyos_fclose(int fd) {
    if (fd < 0 || fd >= FS_MAX_OPEN || fs_open[fd].inode < 0) return -1;
    fs_open[fd].inode = -1;
    return 0;
}

int pyos_fexists(const char* name) {
    return fs_mounted ? (fs_find_name(name) >= 0) : 0;
}

int pyos_fdel(const char* name) {
    int idx = fs_find_name(name);
    if (idx < 0) return -1;
    myfs_inode_t ino;
    fs_inode_load(&ino, idx);
    if (ino.nblocks) fs_free_run(ino.start, ino.nblocks);
    __builtin_memset(&ino, 0, sizeof(ino));
    fs_inode_store(&ino, idx);
    return 0;
}

/* reemplaza el contenido del archivo por «text» (un string) */
int pyos_fwrite(int fd, const char* text) {
    if (!fs_mounted || fd < 0 || fd >= FS_MAX_OPEN || fs_open[fd].inode < 0)
        return -1;
    if (!text) text = "";

    myfs_inode_t ino;
    fs_inode_load(&ino, fs_open[fd].inode);

    uint32_t len = 0;
    while (text[len]) len++;
    uint32_t need = (len + FS_BLOCK - 1) / FS_BLOCK;
    if (need == 0) need = 1;

    if (ino.nblocks) fs_free_run(ino.start, ino.nblocks);
    int start = fs_alloc_run(need);
    if (start < 0) {
        myfs_log("myfs: sin corrida libre para el archivo\n");
        ino.start = 0; ino.nblocks = 0; ino.size = 0;
        fs_inode_store(&ino, fs_open[fd].inode); /* persistir el reset: si no,
            el inodo en disco queda apuntando a los bloques que fs_free_run()
            ya marcó libres unas líneas arriba — corrupción latente */
        return -1;
    }

    for (uint32_t b = 0; b < need; b++) fs_bm_set((uint32_t)start + b);
    fs_bm_flush();

    /* escribir el contenido bloque por bloque */
    uint8_t zbuf[FS_BLOCK] = {0};
    for (uint32_t b = 0; b < need; b++) {
        __builtin_memset(zbuf, 0, FS_BLOCK);
        for (uint32_t o = 0; o < FS_BLOCK; o++) {
            uint32_t i = b * FS_BLOCK + o;
            zbuf[o] = i < len ? (uint8_t)text[i] : 0;
        }
        if (blk_write((uint32_t)start + b, zbuf) != 0) return -1;
    }

    ino.start = (uint32_t)start;
    ino.nblocks = need;
    ino.size = len;
    fs_inode_store(&ino, fs_open[fd].inode);
    fs_open[fd].pos = len;
    return (int)len;
}

/* lee hasta max bytes del archivo (desde la posición actual) y lo devuelve
 * como string en un búfer estático — "" si no hay más contenido */
static char fs_rbuf[FS_RBUF];
const char* pyos_fread(int fd, int max) {
    if (!fs_mounted || fd < 0 || fd >= FS_MAX_OPEN || fs_open[fd].inode < 0)
        return "";
    myfs_inode_t ino;
    fs_inode_load(&ino, fs_open[fd].inode);

    uint32_t pos = fs_open[fd].pos;
    if (pos >= ino.size || max <= 0) return "";

    uint32_t want = (uint32_t)max;
    if (want > ino.size - pos) want = ino.size - pos;
    if (want > (uint32_t)(FS_RBUF - 1)) want = (uint32_t)(FS_RBUF - 1);

    uint8_t blk[FS_BLOCK];
    for (uint32_t k = 0; k < want; k++) {
        uint32_t blkno = ino.start + ((pos + k) / FS_BLOCK);
        uint32_t off = (pos + k) % FS_BLOCK;
        if (off == 0 && k < FS_BLOCK) {
            if (blk_read(blkno, blk) != 0) return "";
        }
        fs_rbuf[k] = (char)blk[off];
    }
    fs_rbuf[want] = 0;
    fs_open[fd].pos += want;
    return fs_rbuf;
}

/* listado de archivos por pantalla (como `ls`) */
int pyos_fls(void) {
    if (!fs_mounted) return 0;
    myfs_inode_t ino;
    int files = 0;
    for (int i = 0; i < (int)FS_NINODES; i++) {
        if (fs_inode_load(&ino, i) != 0) break;
        if (ino.magic != 0x4D4E4653u || !ino.used) continue;
        files++;
        pyos_draw(ino.name);
        pyos_draw("  (");
        pyos_putdec(ino.size);
        pyos_draw(" B)\n");
    }
    return files;
}

int pyos_fsize(int fd) {
    if (fd < 0 || fd >= FS_MAX_OPEN || fs_open[fd].inode < 0) return -1;
    myfs_inode_t ino;
    fs_inode_load(&ino, fs_open[fd].inode);
    return (int)ino.size;
}

/* estado del FS para diagnóstico */
void pyos_fs_status(void) {
    if (!fs_mounted) { pyos_draw("FS: inactivo\n"); return; }
    pyos_draw("FS: MYOSFS v1, ");
    pyos_putdec(fs_blocks_total * FS_BLOCK / 1024);
    pyos_draw(" KiB totales\n");
}

/* 1 si el filesystem MYOSFS está montado (pyos.fs_mounted) */
int pyos_fs_mounted(void) { return fs_mounted ? 1 : 0; }

/* apéndice: agrega «text» al final del archivo; devuelve los bytes escritos,
 * -1 si el fd es inválido. Lee el contenido previo al heap (debe entrar: el
 * archivo final no puede superar la memoria disponible) y lo reescribe junto
 * con el texto nuevo, con la misma lógica de corrida contigua que fwrite. */
int pyos_fopen_append(int fd, const char* text) {
    if (!fs_mounted || fd < 0 || fd >= FS_MAX_OPEN || fs_open[fd].inode < 0)
        return -1;
    if (!text) text = "";

    myfs_inode_t ino;
    fs_inode_load(&ino, fs_open[fd].inode);
    uint32_t old = ino.size;
    uint32_t add = 0;
    while (text[add]) add++;
    if (add == 0) return 0;

    uint32_t total = old + add;
    char* buf = (char*)pyos_alloc(total);
    if (!buf) { myfs_log("myfs: sin heap para append\n"); return -1; }

    /* leer el contenido previo, bloque por bloque */
    uint8_t blk[FS_BLOCK];
    for (uint32_t k = 0; k < old; k++) {
        uint32_t blkno = ino.start + (k / FS_BLOCK);
        uint32_t off = k % FS_BLOCK;
        if (off == 0) {
            if (blk_read(blkno, blk) != 0) { pyos_free(buf); return -1; }
        }
        buf[k] = (char)blk[off];
    }
    for (uint32_t k = 0; k < add; k++) buf[old + k] = text[k];

    if (ino.nblocks) fs_free_run(ino.start, ino.nblocks);
    uint32_t need = (total + FS_BLOCK - 1u) / FS_BLOCK;
    if (need == 0) need = 1;
    int start = fs_alloc_run(need);
    if (start < 0) {
        pyos_free(buf);
        myfs_log("myfs: sin corrida libre para append\n");
        ino.start = 0; ino.nblocks = 0; ino.size = 0;
        fs_inode_store(&ino, fs_open[fd].inode);
        return -1;
    }

    for (uint32_t b = 0; b < need; b++) fs_bm_set((uint32_t)start + b);
    fs_bm_flush();

    uint8_t zbuf[FS_BLOCK] = {0};
    for (uint32_t b = 0; b < need; b++) {
        __builtin_memset(zbuf, 0, FS_BLOCK);
        for (uint32_t o = 0; o < FS_BLOCK; o++) {
            uint32_t i = b * FS_BLOCK + o;
            zbuf[o] = i < total ? (uint8_t)buf[i] : 0;
        }
        if (blk_write((uint32_t)start + b, zbuf) != 0) { pyos_free(buf); return -1; }
    }

    ino.start = (uint32_t)start;
    ino.nblocks = need;
    ino.size = total;
    fs_inode_store(&ino, fs_open[fd].inode);
    fs_open[fd].pos = total;
    pyos_free(buf);
    return (int)add;
}