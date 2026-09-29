#include <stddef.h>

long atom_sum(long a, long b) { return a + b; }

long atom_triple(long a) { return a * 3; }

long atom_negate(long a) { return -a; }

long atom_shift(long a, long b) { return a << b; }

long atom_peek8(const unsigned char *addr) { return (long)*addr; }

void atom_poke8(unsigned char *addr, long value) { *addr = (unsigned char)value; }

long atom_strlen(const char *text) {
    long n = 0;
    while (text[n]) n++;
    return n;
}
