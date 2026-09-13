/* math.c — matemática entera freestanding (Tanda A del roadmap).
 *
 * Todas las funciones devuelven int de 32 bits; cuando un resultado no
 * existe (índices fuera de rango, entrada negativa inválida, etc.) o se sale
 * del rango de un int, se devuelve -1 (o 0, según lo documentado en el
 * prototype de pyos_runtime.h). No hay punto flotante acá: sqrt es entera
 * (bisección), no hay libm.
 */

#include <stdint.h>

#include "pyos_runtime.h"

extern int pyos_random_int(int n);

/* valor absoluto; INT_MIN no tiene espejo positivo -> 2147483647 */
int pyos_abs(int n) {
    if (n == -2147483647 - 1) return 2147483647;
    return n < 0 ? -n : n;
}

int pyos_sign(int n) {
    return n > 0 ? 1 : (n < 0 ? -1 : 0);
}

int pyos_is_even(int n) { return (n & 1) == 0; }
int pyos_is_odd(int n)  { return (n & 1) != 0; }

/* a^b con b >= 0 (0^0 = 1); si el resultado no entra en int, -1 */
int pyos_pow(int a, int b) {
    if (b < 0) return -1;
    long long r = 1, base = a;
    for (int i = 0; i < b; i++) {
        r *= base;
        if (r > 2147483647LL || r < -2147483647LL - 1) return -1;
    }
    return (int)r;
}

/* n! con n <= 12 (13! = 6227020800 ya desborda int); n<0 o n>12 -> -1 */
int pyos_factorial(int n) {
    if (n < 0 || n > 12) return -1;
    int r = 1;
    for (int i = 2; i <= n; i++) r *= i;
    return r;
}

/* enésimo Fibonacci (F0=0, F1=1); n<0 o n>40 -> -1 (fib(40) entra en int) */
int pyos_fib(int n) {
    if (n < 0 || n > 40) return -1;
    int a = 0, b = 1;
    for (int i = 0; i < n; i++) { int t = a + b; a = b; b = t; }
    return a;
}

/* |n| como unsigned (soporta INT_MIN sin desbordar) */
static unsigned m_uabs(int n) {
    return n < 0 ? (unsigned)(-(n + 1)) + 1u : (unsigned)n;
}

/* máximo común divisor (valor absoluto; gcd(0,0)=0, nunca divide por 0) */
int pyos_gcd(int a, int b) {
    unsigned ua = m_uabs(a), ub = m_uabs(b);
    while (ub) { unsigned t = ua % ub; ua = ub; ub = t; }
    return (int)ua;
}

/* mínimo común múltiplo; 0 si a==0 o b==0, -1 si no entra en int */
int pyos_lcm(int a, int b) {
    if (a == 0 || b == 0) return 0;
    unsigned ua = m_uabs(a), ub = m_uabs(b);
    unsigned x = ua, y = ub;
    while (y) { unsigned t = x % y; x = y; y = t; }
    unsigned g = x;
    long long m = (long long)(ua / g) * ub;
    if (m > 2147483647LL) return -1;
    return (int)m;
}

/* parte entera de la raíz cuadrada (bisección); n<=0 -> 0 */
int pyos_sqrt_int(int n) {
    if (n <= 0) return 0;
    unsigned u = (unsigned)n;
    unsigned lo = 0, hi = (u > 46341u) ? 46341u : u + 1u;
    while (lo < hi) {
        unsigned mid = lo + (hi - lo + 1u) / 2u;
        if (mid <= u / mid) lo = mid; else hi = mid - 1u;
    }
    return (int)lo;
}

/* primalidad: n<2 falso; prueba divisores impares hasta sqrt(n) */
int pyos_is_prime(int n) {
    if (n < 2) return 0;
    if (n % 2 == 0) return n == 2;
    for (int i = 3; i <= n / i; i += 2)
        if (n % i == 0) return 0;
    return 1;
}

/* próximo primo >= n; -1 si se llega al tope sin encontrar */
int pyos_next_prime(int n) {
    if (n < 2) n = 2;
    if (n == 2) return 2;
    if ((n & 1) == 0) n++;
    while (1) {
        if (pyos_is_prime(n)) return n;
        if (n > 2147483647 - 2) return -1;
        n += 2;
    }
}

/* cantidad de dígitos decimales (0 -> 1; negativo sin el signo) */
int pyos_digit_count(int n) {
    unsigned v = m_uabs(n);
    if (v == 0) return 1;
    int c = 0;
    while (v) { c++; v /= 10; }
    return c;
}

int pyos_sum_digits(int n) {
    unsigned v = m_uabs(n);
    int s = 0;
    while (v) { s += (int)(v % 10); v /= 10; }
    return s;
}

/* número con los dígitos invertidos (conserva el signo); si el resultado no
 * entra en int se devuelve 0 (no hay representación, evitamos el UB) */
int pyos_reverse_int(int n) {
    int neg = n < 0;
    unsigned long long v = m_uabs(n);
    unsigned long long r = 0;
    while (v) {
        r = r * 10ull + (v % 10ull);
        v /= 10ull;
    }
    if (r > 2147483648ull) return 0;
    if (r > 2147483647ull) return 0;
    return neg ? -(int)r : (int)r;
}

int pyos_is_palindrome_int(int n) {
    return pyos_reverse_int(n) == n;
}

int pyos_min(int a, int b) { return a < b ? a : b; }
int pyos_max(int a, int b) { return a > b ? a : b; }

int pyos_clamp(int n, int lo, int hi) {
    if (lo > hi) { int t = lo; lo = hi; hi = t; }
    if (n < lo) return lo;
    if (n > hi) return hi;
    return n;
}

int pyos_is_power_of_two(int n) {
    return n > 0 && (n & (n - 1)) == 0;
}

/* menor potencia de 2 >= n (n<=0 -> 1); si no entra en int, -1 */
int pyos_next_power_of_two(int n) {
    if (n <= 0) return 1;
    unsigned p = 1;
    while (p < (unsigned)n) p <<= 1;
    if (p > 2147483647u) return -1;
    return (int)p;
}

/* piso de log2 (n<=1 -> 0) */
int pyos_log2_floor(int n) {
    if (n <= 1) return 0;
    unsigned v = (unsigned)n;
    int r = 0;
    while (v > 1) { v >>= 1; r++; }
    return r;
}

/* aleatorio en [lo, hi] inclusive (normaliza si lo > hi) */
int pyos_rand_range(int lo, int hi) {
    if (hi < lo) { int t = lo; lo = hi; hi = t; }
    long long span = (long long)hi - lo + 1;
    if (span <= 0 || span > 2147483647LL) return lo;
    return lo + pyos_random_int((int)span);
}

int pyos_rand_bool(void) { return pyos_random_int(2); }

/* dado: 1..sides; sides<=1 -> 1 */
int pyos_roll(int sides) {
    if (sides <= 1) return 1;
    return 1 + pyos_random_int(sides);
}