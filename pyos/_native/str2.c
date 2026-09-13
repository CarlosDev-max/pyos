/* str2.c — strings avanzado (Tanda B del roadmap).
 *
 * Complementa heap.c con la API de strings del kernel. Convenciones:
 *   - las funciones "str" devuelven un string NUEVO alocado en el heap (con
 *     su '\0' final siempre), una cadena vacía estática "" si el resultado es
 *     vacío o la entrada es NULL, y 0 (NULL) si falla el alloc — chequeá el
 *     resultado contra 0 antes de usarlo.
 *   - las funciones "int" devuelven -1 cuando el dato no existe.
 * No se usa libc: los helpers están acá, en el mismo archivo.
 */

#include <stdint.h>

#include "pyos_runtime.h"

extern void* pyos_alloc(uint32_t size);

static int s2_len(const char* s) {
    int n = 0;
    if (s) while (s[n]) n++;
    return n;
}

int pyos_str_len(const char* s) { return s2_len(s); }

/* parsea decimal (con '-' inicial); cadena vacía -> 0; se detiene en el
 * primer carácter no dígito; satura al rango de int como los demás */
int pyos_str_to_int(const char* s) {
    if (!s) return 0;
    int i = 0, neg = 0;
    if (s[0] == '-') { neg = 1; i = 1; }
    long long acc = 0;
    while (s[i] >= '0' && s[i] <= '9') {
        acc = acc * 10 + (s[i] - '0');
        if (acc > 2147483647LL) acc = 2147483647LL;
        i++;
    }
    if (neg) acc = -acc;
    return (int)acc;
}

/* código CP437 en la posición i; -1 si i está fuera de rango */
int pyos_str_char_at(const char* s, int i) {
    if (!s || i < 0) return -1;
    int n = s2_len(s);
    if (i >= n) return -1;
    return (unsigned char)s[i];
}

/* posición de la primera aparición de sub en s (-1 si no está) */
static int s2_index_of(const char* s, const char* sub) {
    if (!s || !sub) return -1;
    int ls = s2_len(s), lb = s2_len(sub);
    if (lb == 0) return 0;
    if (lb > ls) return -1;
    for (int i = 0; i + lb <= ls; i++) {
        int j = 0;
        while (j < lb && s[i + j] == sub[j]) j++;
        if (j == lb) return i;
    }
    return -1;
}

int pyos_str_contains(const char* s, const char* sub) {
    return s2_index_of(s, sub) >= 0;
}

int pyos_str_index(const char* s, const char* sub) {
    return s2_index_of(s, sub);
}

/* apariciones de sub en s sin solaparse (sub vacío -> 0) */
int pyos_str_count(const char* s, const char* sub) {
    if (!s || !sub) return 0;
    int ls = s2_len(s), lb = s2_len(sub);
    if (lb == 0) return 0;
    int pos = 0, n = 0;
    while (pos + lb <= ls) {
        int j = 0;
        while (j < lb && s[pos + j] == sub[j]) j++;
        if (j == lb) { n++; pos += lb; }
        else pos++;
    }
    return n;
}

int pyos_starts_with(const char* s, const char* pref) {
    if (!s || !pref) return 0;
    int i = 0;
    while (pref[i]) {
        if (s[i] != pref[i]) return 0;
        i++;
    }
    return 1;
}

int pyos_ends_with(const char* s, const char* suf) {
    if (!s || !suf) return 0;
    int ls = s2_len(s), lf = s2_len(suf);
    if (lf > ls) return 0;
    for (int i = 0; i < lf; i++)
        if (s[ls - lf + i] != suf[i]) return 0;
    return 1;
}

const char* pyos_upper(const char* s) {
    if (!s) return "";
    int n = s2_len(s);
    char* out = (char*)pyos_alloc((uint32_t)n + 1u);
    if (!out) return 0;
    for (int i = 0; i < n; i++) {
        char c = s[i];
        out[i] = (c >= 'a' && c <= 'z') ? (char)(c - 32) : c;
    }
    out[n] = 0;
    return out;
}

const char* pyos_lower(const char* s) {
    if (!s) return "";
    int n = s2_len(s);
    char* out = (char*)pyos_alloc((uint32_t)n + 1u);
    if (!out) return 0;
    for (int i = 0; i < n; i++) {
        char c = s[i];
        out[i] = (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c;
    }
    out[n] = 0;
    return out;
}

const char* pyos_reverse(const char* s) {
    if (!s) return "";
    int n = s2_len(s);
    char* out = (char*)pyos_alloc((uint32_t)n + 1u);
    if (!out) return 0;
    for (int i = 0; i < n; i++) out[i] = s[n - 1 - i];
    out[n] = 0;
    return out;
}

/* sin espacios iniciales/finales (solo ' ', como el kernel) */
const char* pyos_trim(const char* s) {
    if (!s) return "";
    int n = s2_len(s);
    int a = 0, b = n;
    while (a < b && s[a] == ' ') a++;
    while (b > a && s[b - 1] == ' ') b--;
    int len = b - a;
    if (len == 0) return "";
    char* out = (char*)pyos_alloc((uint32_t)len + 1u);
    if (!out) return 0;
    for (int i = 0; i < len; i++) out[i] = s[a + i];
    out[len] = 0;
    return out;
}

/* s repetido n veces (n<=0 -> cadena vacía) */
const char* pyos_repeat(const char* s, int n) {
    if (n <= 0 || !s) return "";
    int ls = s2_len(s);
    if (ls == 0 || (uint32_t)n > 0xFFFFFFF0u / (uint32_t)ls) return "";
    char* out = (char*)pyos_alloc((uint32_t)n * (uint32_t)ls + 1u);
    if (!out) return 0;
    int pos = 0;
    for (int r = 0; r < n; r++)
        for (int i = 0; i < ls; i++) out[pos++] = s[i];
    out[pos] = 0;
    return out;
}

const char* pyos_left(const char* s, int n) {
    if (!s) return "";
    int ls = s2_len(s);
    if (n <= 0) return "";
    if (n > ls) n = ls;
    if (n == 0) return "";
    char* out = (char*)pyos_alloc((uint32_t)n + 1u);
    if (!out) return 0;
    for (int i = 0; i < n; i++) out[i] = s[i];
    out[n] = 0;
    return out;
}

const char* pyos_right(const char* s, int n) {
    if (!s) return "";
    int ls = s2_len(s);
    if (n <= 0) return "";
    if (n > ls) n = ls;
    if (n == 0) return "";
    char* out = (char*)pyos_alloc((uint32_t)n + 1u);
    if (!out) return 0;
    for (int i = 0; i < n; i++) out[i] = s[ls - n + i];
    out[n] = 0;
    return out;
}

/* rellena a la izquierda hasta longitud final n con el char ch (código
 * CP437); si n <= len(s) la copia queda igual */
const char* pyos_pad_left(const char* s, int n, int ch) {
    if (!s) return "";
    int ls = s2_len(s);
    int target = n < 0 ? 0 : n;
    int pad = target - ls;
    if (pad < 0) pad = 0;
    char* out = (char*)pyos_alloc((uint32_t)target + 1u);
    if (!out) return 0;
    int pos = 0;
    for (int i = 0; i < pad; i++) out[pos++] = (char)(ch & 0xFF);
    for (int i = 0; i < ls; i++) out[pos++] = s[i];
    out[pos] = 0;
    return out;
}

const char* pyos_pad_right(const char* s, int n, int ch) {
    if (!s) return "";
    int ls = s2_len(s);
    int target = n < 0 ? 0 : n;
    int pad = target - ls;
    if (pad < 0) pad = 0;
    char* out = (char*)pyos_alloc((uint32_t)target + 1u);
    if (!out) return 0;
    int pos = 0;
    for (int i = 0; i < ls; i++) out[pos++] = s[i];
    for (int i = 0; i < pad; i++) out[pos++] = (char)(ch & 0xFF);
    out[pos] = 0;
    return out;
}

/* reemplaza todas las apariciones del char old por new */
const char* pyos_replace_char(const char* s, int oldc, int newc) {
    if (!s) return "";
    int n = s2_len(s);
    char* out = (char*)pyos_alloc((uint32_t)n + 1u);
    if (!out) return 0;
    char o = (char)(oldc & 0xFF), v = (char)(newc & 0xFF);
    for (int i = 0; i < n; i++) out[i] = (s[i] == o) ? v : s[i];
    out[n] = 0;
    return out;
}

/* "0x" + hex minúsculas del patrón de 32 bits ("0x0" para 0) */
const char* pyos_int_to_hex(int value) {
    const char hexd[] = "0123456789abcdef";
    uint32_t v = (uint32_t)value;
    char buf[11];
    int pos = 0;
    buf[pos++] = '0';
    buf[pos++] = 'x';
    int started = 0;
    for (int shift = 28; shift >= 0; shift -= 4) {
        unsigned d = (v >> (unsigned)shift) & 0xFu;
        if (!started && d == 0 && shift > 0) continue;
        started = 1;
        buf[pos++] = hexd[d];
    }
    if (!started) buf[pos++] = '0';
    buf[pos] = 0;

    char* out = (char*)pyos_alloc((uint32_t)pos + 1u);
    if (!out) return 0;
    for (int i = 0; i < pos; i++) out[i] = buf[i];
    out[pos] = 0;
    return out;
}

/* "0b" + binario sin ceros a la izquierda ("0b0" para 0) */
const char* pyos_int_to_bin(int value) {
    uint32_t v = (uint32_t)value;
    char buf[34]; /* "0b" + 32 bits + '\0' */
    int pos = 0;
    buf[pos++] = '0';
    buf[pos++] = 'b';
    int started = 0;
    for (int shift = 31; shift >= 0; shift--) {
        unsigned d = (v >> (unsigned)shift) & 1u;
        if (!started && d == 0 && shift > 0) continue;
        started = 1;
        buf[pos++] = (char)('0' + d);
    }
    if (!started) buf[pos++] = '0';
    buf[pos] = 0;

    char* out = (char*)pyos_alloc((uint32_t)pos + 1u);
    if (!out) return 0;
    for (int i = 0; i < pos; i++) out[i] = buf[i];
    out[pos] = 0;
    return out;
}