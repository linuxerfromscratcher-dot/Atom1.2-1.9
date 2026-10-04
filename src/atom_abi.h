#ifndef ATOM_ABI_H
#define ATOM_ABI_H

#include <stdint.h>
#include <stddef.h>
#include <limits.h>


#define ATOM_ABI_CAT_(a, b) a##b
#define ATOM_ABI_CAT(a, b) ATOM_ABI_CAT_(a, b)

#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
#define ATOM_ABI_ASSERT(cond, msg) _Static_assert(cond, msg)
#elif defined(__GNUC__)
#define ATOM_ABI_ASSERT(cond, msg) \
    typedef char ATOM_ABI_CAT(atom_abi_assert_, __LINE__)[(cond) ? 1 : -1]
#else
#define ATOM_ABI_ASSERT(cond, msg) \
    enum { ATOM_ABI_CAT(atom_abi_assert_, __LINE__) = 1 / (int)((cond) ? 1 : 2) }
#endif

#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
#define ATOM_ABI_ALIGNOF(type) _Alignof(type)
#elif defined(__GNUC__)
#define ATOM_ABI_ALIGNOF(type) __alignof__(type)
#else
#define ATOM_ABI_ALIGNOF(type) sizeof(type)
#endif

typedef int64_t atom_word;

typedef int32_t atom_index;

#define ATOM_ABI_WORD_SIZE 8
#define ATOM_ABI_WORD_DIGITS 63
#define ATOM_ABI_INDEX_SIZE 4

typedef struct {
    atom_word lo;
    atom_word hi;
} atom_pair;

typedef struct {
    atom_word words[4];
} atom_cell;

typedef struct {
    void *data;
    atom_word len;
} atom_buffer;

ATOM_ABI_ASSERT(CHAR_BIT == 8, "Atom is a byte machine: CHAR_BIT must be 8");
ATOM_ABI_ASSERT(sizeof(int8_t) == 1, "int8_t must be exactly one byte");
ATOM_ABI_ASSERT(sizeof(int32_t) == ATOM_ABI_INDEX_SIZE,
                "atom_index must match int32_t on both sides of the FFI");
ATOM_ABI_ASSERT(sizeof(int64_t) == ATOM_ABI_WORD_SIZE,
                "atom_word must match Fortran integer(c_int64_t): 8 bytes");
ATOM_ABI_ASSERT(sizeof(atom_word) == ATOM_ABI_WORD_SIZE, "atom_word width drift");
ATOM_ABI_ASSERT(sizeof(atom_index) == ATOM_ABI_INDEX_SIZE, "atom_index width drift");
ATOM_ABI_ASSERT(ATOM_ABI_ALIGNOF(atom_word) >= ATOM_ABI_INDEX_SIZE,
                "atom_word must be at least 4 byte aligned");
ATOM_ABI_ASSERT(ATOM_ABI_WORD_DIGITS == CHAR_BIT * (int)sizeof(atom_word) - 1,
                "atom_word must hold the -2^63..2^63-1 range the VM prints");

ATOM_ABI_ASSERT(sizeof(atom_pair) == 2 * ATOM_ABI_WORD_SIZE, "atom_pair must be 16 bytes");
ATOM_ABI_ASSERT(offsetof(atom_pair, lo) == 0, "atom_pair.lo must sit at offset 0");
ATOM_ABI_ASSERT(offsetof(atom_pair, hi) == ATOM_ABI_WORD_SIZE, "atom_pair.hi must sit at offset 8");
ATOM_ABI_ASSERT(sizeof(atom_cell) == 4 * ATOM_ABI_WORD_SIZE, "atom_cell must be 32 bytes");
ATOM_ABI_ASSERT(offsetof(atom_cell, words) == 0, "atom_cell.words must sit at offset 0");
ATOM_ABI_ASSERT(sizeof(atom_buffer) == 2 * ATOM_ABI_WORD_SIZE, "atom_buffer must be 16 bytes");
ATOM_ABI_ASSERT(offsetof(atom_buffer, data) == 0, "atom_buffer.data must sit at offset 0");
ATOM_ABI_ASSERT(offsetof(atom_buffer, len) == ATOM_ABI_WORD_SIZE,
                "atom_buffer.len must sit at offset 8, no padding in front of it");

#ifdef ATOM_ABI_STRICT_ALIGN
ATOM_ABI_ASSERT(ATOM_ABI_ALIGNOF(atom_word) == ATOM_ABI_WORD_SIZE,
                "the raw FFI trampoline requires 8 byte aligned atom_word");
ATOM_ABI_ASSERT(sizeof(long) == ATOM_ABI_WORD_SIZE,
                "the raw FFI trampoline requires an LP64 target (long is not 8 bytes here)");
#endif

#if defined(_WIN32)
#define ATOM_PLATFORM_OS_ID 1u
#define ATOM_PLATFORM_OS_TEXT "windows"
#elif defined(__APPLE__)
#define ATOM_PLATFORM_OS_ID 2u
#define ATOM_PLATFORM_OS_TEXT "macos"
#elif defined(__linux__)
#define ATOM_PLATFORM_OS_ID 3u
#define ATOM_PLATFORM_OS_TEXT "linux"
#elif defined(__FreeBSD__) || defined(__NetBSD__) || defined(__OpenBSD__)
#define ATOM_PLATFORM_OS_ID 4u
#define ATOM_PLATFORM_OS_TEXT "bsd"
#else
#define ATOM_PLATFORM_OS_ID 0u
#define ATOM_PLATFORM_OS_TEXT "unknown"
#endif

#if defined(__x86_64__) || defined(_M_X64)
#define ATOM_PLATFORM_ARCH_ID 1u
#define ATOM_PLATFORM_ARCH_TEXT "x86_64"
#elif defined(__aarch64__) || defined(_M_ARM64)
#define ATOM_PLATFORM_ARCH_ID 2u
#define ATOM_PLATFORM_ARCH_TEXT "aarch64"
#elif defined(__i386__) || defined(_M_IX86)
#define ATOM_PLATFORM_ARCH_ID 3u
#define ATOM_PLATFORM_ARCH_TEXT "x86"
#elif defined(__arm__) || defined(_M_ARM)
#define ATOM_PLATFORM_ARCH_ID 4u
#define ATOM_PLATFORM_ARCH_TEXT "arm"
#elif defined(__riscv)
#define ATOM_PLATFORM_ARCH_ID 5u
#define ATOM_PLATFORM_ARCH_TEXT "riscv64"
#elif defined(__powerpc64__)
#define ATOM_PLATFORM_ARCH_ID 6u
#define ATOM_PLATFORM_ARCH_TEXT "ppc64"
#else
#define ATOM_PLATFORM_ARCH_ID 0u
#define ATOM_PLATFORM_ARCH_TEXT "unknown"
#endif

#if defined(__BYTE_ORDER__) && (__BYTE_ORDER__ == __ORDER_BIG_ENDIAN__)
#define ATOM_PLATFORM_ENDIAN_ID 1u
#define ATOM_PLATFORM_ENDIAN_TEXT "be"
#else
#define ATOM_PLATFORM_ENDIAN_ID 0u
#define ATOM_PLATFORM_ENDIAN_TEXT "le"
#endif

uint64_t atom_platform_tag(void);
const char *atom_platform_text(void);
uint64_t atom_abi_tag(void);
const char *atom_abi_tag_text(void);

#endif
