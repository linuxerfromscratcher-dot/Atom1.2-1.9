#include "atom.h"
#include "atom_abi.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/stat.h>
#include <unistd.h>
#if defined(__APPLE__)
#include <mach-o/dyld.h>
#endif
#endif


#define NATIVE_FORMAT_VERSION 1u
#define NATIVE_PAYLOAD_MAGIC 0x4E415449u /* "NATI" */
#define NATIVE_TRAILER_MAGIC 0x4E415442u /* "NATB" */
#define NATIVE_TRAILER_SIZE 48u
#define NATIVE_HEADER_SIZE 40u
#define NATIVE_FNV_OFFSET 2166136261u
#define NATIVE_FNV_PRIME 16777619u

const char *atom_platform_text(void) {
    static char text[64];
    if (!text[0]) {
        snprintf(text, sizeof(text), "%s-%s-%s-%s",
                 ATOM_PLATFORM_OS_TEXT, ATOM_PLATFORM_ARCH_TEXT,
                 ATOM_PLATFORM_ENDIAN_TEXT, sizeof(long) == 8 ? "lp64" : "llp64");
    }
    return text;
}

const char *atom_abi_tag_text(void) {
    static char text[64];
    if (!text[0]) {
        snprintf(text, sizeof(text), "w%d/i%d pair%zu cell%zu buf%zu+%zu",
                 (int)sizeof(atom_word), (int)sizeof(atom_index), sizeof(atom_pair),
                 sizeof(atom_cell), sizeof(atom_buffer), offsetof(atom_buffer, len));
    }
    return text;
}

static uint64_t mix64(uint64_t hash, uint64_t value) {
    for (int i = 0; i < 8; i++) {
        hash ^= (value >> (8 * i)) & 0xFFu;
        hash *= 1099511628211ULL;
    }
    return hash;
}

uint64_t atom_platform_tag(void) {
    static uint64_t cached = 0;
    if (cached) return cached;

    uint64_t tag = 1469598103934665603ULL;
    tag = mix64(tag, ATOM_PLATFORM_OS_ID);
    tag = mix64(tag, ATOM_PLATFORM_ARCH_ID);
    tag = mix64(tag, ATOM_PLATFORM_ENDIAN_ID);
    tag = mix64(tag, sizeof(long));
    tag = mix64(tag, sizeof(int));
    tag = mix64(tag, sizeof(void *));
    tag = mix64(tag, sizeof(size_t));
    cached = tag;
    return cached;
}

uint64_t atom_abi_tag(void) {
    uint64_t tag = 1469598103934665603ULL;
    tag = mix64(tag, ATOM_ABI_WORD_SIZE);
    tag = mix64(tag, ATOM_ABI_INDEX_SIZE);
    tag = mix64(tag, CHAR_BIT);
    tag = mix64(tag, ATOM_ABI_ALIGNOF(atom_word));
    tag = mix64(tag, ATOM_ABI_ALIGNOF(atom_index));
    tag = mix64(tag, offsetof(atom_pair, lo));
    tag = mix64(tag, offsetof(atom_pair, hi));
    tag = mix64(tag, offsetof(atom_cell, words));
    tag = mix64(tag, offsetof(atom_buffer, data));
    tag = mix64(tag, offsetof(atom_buffer, len));
    tag = mix64(tag, sizeof(atom_buffer));
    tag = mix64(tag, sizeof(StackItem));
    tag = mix64(tag, sizeof(AtomInstruction));
    return tag;
}

static uint32_t crc32_bytes(const void *data, size_t size) {
    const unsigned char *p = (const unsigned char *)data;
    uint32_t crc = NATIVE_FNV_OFFSET;
    for (size_t i = 0; i < size; i++) {
        crc ^= p[i];
        crc *= NATIVE_FNV_PRIME;
    }
    return crc;
}

static uint32_t put_u32(unsigned char *dst, uint32_t value) {
    dst[0] = (unsigned char)(value & 0xFF);
    dst[1] = (unsigned char)((value >> 8) & 0xFF);
    dst[2] = (unsigned char)((value >> 16) & 0xFF);
    dst[3] = (unsigned char)((value >> 24) & 0xFF);
    return 4;
}

static uint32_t put_u64(unsigned char *dst, uint64_t value) {
    for (int i = 0; i < 8; i++) dst[i] = (unsigned char)((value >> (8 * i)) & 0xFF);
    return 8;
}

static uint32_t get_u32(const unsigned char *src) {
    return (uint32_t)src[0] | ((uint32_t)src[1] << 8) |
           ((uint32_t)src[2] << 16) | ((uint32_t)src[3] << 24);
}

static uint64_t get_u64(const unsigned char *src) {
    uint64_t value = 0;
    for (int i = 0; i < 8; i++) value |= (uint64_t)src[i] << (8 * i);
    return value;
}

bool atom_self_path(char *out, size_t size) {
    if (!out || size == 0) return false;
    out[0] = '\0';

#ifdef _WIN32
    DWORD written = GetModuleFileNameA(NULL, out, (DWORD)size);
    return written > 0 && written < size;
#elif defined(__APPLE__)
    char buffer[1024];
    uint32_t written = (uint32_t)sizeof(buffer);
    if (_NSGetExecutablePath(buffer, &written) != 0) return false;
    char *real = realpath(buffer, NULL);
    if (real) {
        snprintf(out, size, "%s", real);
        free(real);
    } else {
        snprintf(out, size, "%s", buffer);
    }
    return out[0] != '\0';
#else
    ssize_t written = readlink("/proc/self/exe", out, size - 1);
    if (written <= 0) return false;
    out[written] = '\0';
    return true;
#endif
}

static bool read_file_blob(const char *path, unsigned char **out, size_t *out_size) {
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return false;
    }
    long size = ftell(f);
    if (size <= 0 || fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return false;
    }
    unsigned char *data = (unsigned char *)malloc((size_t)size);
    if (!data) {
        fclose(f);
        return false;
    }
    size_t got = fread(data, 1, (size_t)size, f);
    fclose(f);
    if (got != (size_t)size) {
        free(data);
        return false;
    }
    *out = data;
    *out_size = got;
    return true;
}

static size_t trailer_payload_offset(const unsigned char *blob, size_t size,
                                     uint64_t *platform_tag, uint64_t *abi_tag,
                                     uint64_t *payload_size, uint32_t *payload_crc) {
    if (size < NATIVE_TRAILER_SIZE) return 0;

    const unsigned char *trailer = blob + size - NATIVE_TRAILER_SIZE;
    if (get_u32(trailer) != NATIVE_TRAILER_MAGIC) return 0;
    if (get_u32(trailer + 4) != NATIVE_FORMAT_VERSION) return 0;

    uint32_t trailer_crc = get_u32(trailer + NATIVE_TRAILER_SIZE - 4);
    if (crc32_bytes(trailer, NATIVE_TRAILER_SIZE - 4) != trailer_crc) return 0;

    uint64_t tag = get_u64(trailer + 8);
    uint64_t abi = get_u64(trailer + 16);
    uint64_t offset = get_u64(trailer + 24);
    uint64_t length = get_u64(trailer + 32);

    if (offset < NATIVE_HEADER_SIZE || length > offset) return 0;
    if (offset + length > size - NATIVE_TRAILER_SIZE) return 0;

    *platform_tag = tag;
    *abi_tag = abi;
    *payload_size = length;
    *payload_crc = get_u32(trailer + 40);
    return (size_t)offset;
}

static void native_free(AtomNative *img) {
    if (!img) return;
    free(img->data);
    img->data = NULL;
    img->size = 0;
}

static NativeState native_open(const char *exe, AtomNative *img) {
    memset(img, 0, sizeof(*img));

    unsigned char *blob = NULL;
    size_t blob_size = 0;
    if (!read_file_blob(exe, &blob, &blob_size)) {
        fprintf(stderr, "[NATIVE] Cannot read the VM image: %s\n", exe);
        return NATIVE_FAULT;
    }

    uint64_t platform_tag = 0, abi_tag = 0, payload_size = 0;
    uint32_t payload_crc = 0;
    size_t offset = trailer_payload_offset(blob, blob_size, &platform_tag, &abi_tag,
                                           &payload_size, &payload_crc);
    if (offset == 0) {
        free(blob);
        return NATIVE_NONE;
    }

    if (platform_tag != atom_platform_tag()) {
        fprintf(stderr, "[NATIVE] Payload was packed for another platform\n");
        fprintf(stderr, "[NATIVE] this VM runs on %s (tag 0x%016llX)\n",
                atom_platform_text(), (unsigned long long)atom_platform_tag());
        free(blob);
        return NATIVE_FAULT;
    }

    if (abi_tag != atom_abi_tag()) {
        fprintf(stderr, "[NATIVE] Payload ABI does not match this VM: %s (tag 0x%016llX)\n",
                atom_abi_tag_text(), (unsigned long long)atom_abi_tag());
        free(blob);
        return NATIVE_FAULT;
    }

    unsigned char *payload = blob + offset;
    if (get_u32(payload) != NATIVE_PAYLOAD_MAGIC || get_u32(payload + 4) != NATIVE_FORMAT_VERSION) {
        fprintf(stderr, "[NATIVE] Payload header is damaged\n");
        free(blob);
        return NATIVE_FAULT;
    }

    uint32_t source_size = get_u32(payload + 32);
    uint32_t image_size = get_u32(payload + 24);
    if ((size_t)NATIVE_HEADER_SIZE + source_size + image_size > payload_size) {
        fprintf(stderr, "[NATIVE] Payload is truncated\n");
        free(blob);
        return NATIVE_FAULT;
    }

    if (crc32_bytes(payload, payload_size) != payload_crc) {
        fprintf(stderr, "[NATIVE] Payload checksum mismatch, the binary is damaged\n");
        free(blob);
        return NATIVE_FAULT;
    }

    img->data = (unsigned char *)malloc(payload_size);
    if (!img->data) {
        free(blob);
        return NATIVE_FAULT;
    }
    memcpy(img->data, payload, payload_size);
    img->size = payload_size;
    img->platform_tag = platform_tag;
    img->abi_tag = abi_tag;
    if (source_size > 0 && source_size < sizeof(img->source)) {
        memcpy(img->source, payload + NATIVE_HEADER_SIZE, source_size);
        img->source[source_size] = '\0';
    } else {
        snprintf(img->source, sizeof(img->source), "<embedded>");
    }
    free(blob);
    return NATIVE_READY;
}

bool native_restore(const AtomNative *img) {
    if (!img || !img->data) return false;

    const unsigned char *source = img->data + NATIVE_HEADER_SIZE;
    uint32_t source_size = get_u32(img->data + 32);
    uint32_t image_size = get_u32(img->data + 24);

    return pack_deserialize_program(source + source_size, image_size, false, NULL, NULL);
}

NativeState native_open_self(AtomNative *img) {
    char self[1024];
    if (!atom_self_path(self, sizeof(self))) {
        fprintf(stderr, "[NATIVE] Cannot locate the running executable\n");
        return NATIVE_FAULT;
    }
    return native_open(self, img);
}

bool native_compile(const char *exe, const char *source, const char *output) {
    if (!exe || !output || !source) return false;

    unsigned char *vm_image = NULL;
    size_t vm_size = 0;
    if (!read_file_blob(exe, &vm_image, &vm_size)) {
        fprintf(stderr, "[NATIVE] Cannot read the VM image: %s\n", exe);
        return false;
    }

    uint64_t platform_tag = 0, abi_tag = 0, payload_size = 0;
    uint32_t payload_crc = 0;
    size_t existing = trailer_payload_offset(vm_image, vm_size, &platform_tag, &abi_tag,
                                              &payload_size, &payload_crc);
    size_t vm_only = existing ? existing : vm_size;

    size_t image_size = 0;
    unsigned char *image = pack_serialize_program(source, &image_size);
    if (!image) {
        free(vm_image);
        return false;
    }

    size_t name_size = strlen(source) + 1;
    size_t total = NATIVE_HEADER_SIZE + name_size + image_size;

    unsigned char *payload = (unsigned char *)malloc(total);
    if (!payload) {
        fprintf(stderr, "[NATIVE] Out of memory while packing %zu bytes\n", total);
        free(image);
        free(vm_image);
        return false;
    }
    memset(payload, 0, NATIVE_HEADER_SIZE);

    uint32_t at = 0;
    at += put_u32(payload + at, NATIVE_PAYLOAD_MAGIC);
    at += put_u32(payload + at, NATIVE_FORMAT_VERSION);
    at += put_u64(payload + at, atom_platform_tag());
    at += put_u64(payload + at, atom_abi_tag());
    at += put_u32(payload + at, (uint32_t)image_size);
    at += put_u32(payload + at, crc32_bytes(image, image_size));
    at += put_u32(payload + at, (uint32_t)name_size);
    at += put_u32(payload + at, 0);
    memcpy(payload + at, source, name_size);
    memcpy(payload + at + name_size, image, image_size);

    unsigned char trailer[NATIVE_TRAILER_SIZE];
    memset(trailer, 0, sizeof(trailer));
    at = 0;
    at += put_u32(trailer + at, NATIVE_TRAILER_MAGIC);
    at += put_u32(trailer + at, NATIVE_FORMAT_VERSION);
    at += put_u64(trailer + at, atom_platform_tag());
    at += put_u64(trailer + at, atom_abi_tag());
    at += put_u64(trailer + at, (uint64_t)vm_only);
    at += put_u64(trailer + at, (uint64_t)total);
    at += put_u32(trailer + at, crc32_bytes(payload, total));
    put_u32(trailer + NATIVE_TRAILER_SIZE - 4, crc32_bytes(trailer, NATIVE_TRAILER_SIZE - 4));

    FILE *out = fopen(output, "wb");
    if (!out) {
        fprintf(stderr, "[NATIVE] Cannot create '%s'\n", output);
        free(payload);
        free(image);
        free(vm_image);
        return false;
    }

    bool ok = fwrite(vm_image, 1, vm_only, out) == vm_only;
    if (ok) ok = fwrite(payload, 1, total, out) == total;
    if (ok) ok = fwrite(trailer, 1, sizeof(trailer), out) == sizeof(trailer);
    if (fclose(out) != 0) ok = false;

#ifndef _WIN32
    if (ok) chmod(output, 0755);
#endif

    if (ok) {
        printf("[NATIVE] Platform: %s\n", atom_platform_text());
        printf("[NATIVE] ABI: %s\n", atom_abi_tag_text());
        printf("[NATIVE] Payload: %zu bytes at offset %zu, source '%s'\n", total, vm_only, source);
        printf("[NATIVE] Built: %s\n", output);
    } else {
        fprintf(stderr, "[NATIVE] Cannot write '%s'\n", output);
    }

    free(payload);
    free(image);
    free(vm_image);
    return ok;
}

void native_info(const char *exe) {
    char self[1024];
    if (!exe || !*exe) {
        if (!atom_self_path(self, sizeof(self))) {
            fprintf(stderr, "[NATIVE] Cannot locate the running executable\n");
            return;
        }
        exe = self;
    }

    printf("platform : %s (tag 0x%016llX)\n", atom_platform_text(),
           (unsigned long long)atom_platform_tag());
    printf("abi      : %s (tag 0x%016llX)\n", atom_abi_tag_text(),
           (unsigned long long)atom_abi_tag());
    printf("format   : native v%u, trailer %u bytes\n", NATIVE_FORMAT_VERSION,
           NATIVE_TRAILER_SIZE);

    AtomNative img;
    NativeState state = native_open(exe, &img);
    switch (state) {
        case NATIVE_READY:
            printf("file     : %s\n", exe);
            printf("payload  : %s (%zu bytes)\n", img.source, img.size);
            native_free(&img);
            break;
        case NATIVE_FAULT:
            printf("file     : %s\n", exe);
            printf("payload  : damaged or foreign\n");
            break;
        default:
            printf("file     : %s\n", exe);
            printf("payload  : none (plain VM)\n");
            break;
    }
}

void native_cleanup(AtomNative *img) {
    native_free(img);
}
