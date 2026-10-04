#include "atom.h"

#ifdef ATOM_USE_LIBFFI
#include <ffi.h>
#endif

#if defined(__x86_64__) && !defined(_WIN32) && !defined(ATOM_USE_LIBFFI)
#define ATOM_FFI_SYSV 1
#define ATOM_ABI_STRICT_ALIGN 1
#endif

#include "atom_abi.h"


static void *native_handle(void) {
#ifdef _WIN32
    return (void *)GetModuleHandleA(NULL);
#else
    return (void *)RTLD_DEFAULT;
#endif
}

const char *ffi_backend(void) {
#ifdef ATOM_USE_LIBFFI
    return "libffi";
#elif defined(ATOM_FFI_SYSV)
    return "sysv64";
#else
    return "none";
#endif
}

void *ffi_native_base(void) {
    return native_handle();
}

int ffi_module_count(FfiState *ffi) {
    int count = 0;
    for (int i = 0; i < FFI_MAX_MODULES; i++) {
        if (ffi->modules[i].loaded) count++;
    }
    return count;
}

static FfiBlock *block_of(FfiState *ffi, void *ptr) {
    for (FfiBlock *b = ffi->blocks; b; b = b->next) {
        if (b->ptr == ptr) return b;
    }
    return NULL;
}

static FfiBlock *block_new(FfiState *ffi, long size) {
    if (size <= 0) size = 1;
    FfiBlock *block = (FfiBlock *)calloc(1, sizeof(FfiBlock));
    if (!block) return NULL;
    block->ptr = calloc(1, (size_t)size);
    if (!block->ptr) {
        free(block);
        return NULL;
    }
    block->size = (size_t)size;
    block->next = ffi->blocks;
    ffi->blocks = block;
    return block;
}

void ffi_init(FfiState *ffi) {
    memset(ffi, 0, sizeof(FfiState));
    ffi->blocks = NULL;
    ffi->calls = 0;
    ffi->ready = true;
    printf("[FFI] backend: %s, max args: %d\n", ffi_backend(), FFI_MAX_ARGS);
}

void ffi_cleanup(FfiState *ffi) {
    for (int i = 0; i < FFI_MAX_MODULES; i++) {
        if (ffi->modules[i].loaded) {
            ffi_close(ffi, i + 1);
        }
    }
    FfiBlock *b = ffi->blocks;
    while (b) {
        FfiBlock *next = b->next;
        free(b->ptr);
        free(b);
        b = next;
    }
    ffi->blocks = NULL;
    ffi->ready = false;
}

static void *open_library(const char *path) {
#ifdef _WIN32
    return (void *)LoadLibraryA(path);
#else
    return dlopen(path, RTLD_NOW | RTLD_LOCAL);
#endif
}

static void close_library(void *handle) {
#ifdef _WIN32
    FreeLibrary((HMODULE)handle);
#else
    dlclose(handle);
#endif
}

static const char *open_error(void) {
#ifdef _WIN32
    static char buffer[256];
    DWORD code = GetLastError();
    snprintf(buffer, sizeof(buffer), "error %lu", (unsigned long)code);
    return buffer;
#else
    const char *err = dlerror();
    return err ? err : "unknown error";
#endif
}

bool ffi_open(FfiState *ffi, const char *path, int *handle, bool quiet) {
    if (!path || !*path) {
        if (!quiet) fprintf(stderr, "[FFI] Error: empty library path\n");
        return false;
    }

    void *lib = open_library(path);
    if (!lib) {
        if (!quiet) {
            fprintf(stderr, "[FFI] Error: cannot load '%s': %s\n", path, open_error());
        }
        return false;
    }

    for (int i = 0; i < FFI_MAX_MODULES; i++) {
        if (!ffi->modules[i].loaded) {
            ffi->modules[i].loaded = true;
            ffi->modules[i].handle = lib;
            snprintf(ffi->modules[i].path, sizeof(ffi->modules[i].path), "%s", path);
            if (handle) *handle = i + 1;
            printf("[FFI] Loaded: %s (handle %d)\n", path, i + 1);
            return true;
        }
    }

    close_library(lib);
    fprintf(stderr, "[FFI] Error: module table full\n");
    return false;
}

void ffi_close(FfiState *ffi, int handle) {
    if (handle <= 0 || handle > FFI_MAX_MODULES) {
        fprintf(stderr, "[FFI] Error: invalid handle %d\n", handle);
        return;
    }
    if (!ffi->modules[handle - 1].loaded) return;
    printf("[FFI] Unloaded: %s (handle %d)\n",
           ffi->modules[handle - 1].path, handle);
    close_library(ffi->modules[handle - 1].handle);
    ffi->modules[handle - 1].handle = NULL;
    ffi->modules[handle - 1].loaded = false;
    ffi->modules[handle - 1].path[0] = '\0';
}

static bool resolve_symbol(FfiState *ffi, int handle, const char *name, void **out, bool quiet);

bool ffi_resolve_quiet(FfiState *ffi, int handle, const char *name, void **out) {
    return resolve_symbol(ffi, handle, name, out, true);
}

bool ffi_resolve(FfiState *ffi, int handle, const char *name, void **out) {
    return resolve_symbol(ffi, handle, name, out, false);
}

static bool resolve_symbol(FfiState *ffi, int handle, const char *name, void **out, bool quiet) {
    if (!name || !*name) {
        if (!quiet) fprintf(stderr, "[FFI] Error: empty symbol name\n");
        return false;
    }

    void *lib = native_handle();
    if (handle != 0) {
        if (handle < 0 || handle > FFI_MAX_MODULES || !ffi->modules[handle - 1].loaded) {
            if (!quiet) fprintf(stderr, "[FFI] Error: invalid handle %d\n", handle);
            return false;
        }
        lib = ffi->modules[handle - 1].handle;
    }

    void *sym = NULL;
#ifdef _WIN32
    sym = (void *)GetProcAddress((HMODULE)lib, name);
#else
    dlerror();
    sym = dlsym(lib, name);
#endif

    if (!sym) {
        if (!quiet) fprintf(stderr, "[FFI] Error: symbol '%s' not found\n", name);
        return false;
    }

    bool known = false;
    for (int i = 0; i < ffi->symbol_count; i++) {
        if (ffi->symbols[i] == sym) {
            known = true;
            break;
        }
    }
    if (!known && ffi->symbol_count < FFI_MAX_SYMBOLS) {
        ffi->symbols[ffi->symbol_count++] = sym;
    }

    if (out) *out = sym;
    return true;
}

bool ffi_symbol_known(FfiState *ffi, void *sym) {
    if (!sym) return false;
    for (int i = 0; i < ffi->symbol_count; i++) {
        if (ffi->symbols[i] == sym) return true;
    }
    return false;
}

#ifdef ATOM_USE_LIBFFI
static bool call_word(FfiState *ffi, void *fn, const atom_word *args, int nargs, atom_word *result) {
    if (!fn || nargs < 0 || nargs > FFI_MAX_ARGS) return false;

    ffi_cif cif;
    ffi_type *atypes[FFI_MAX_ARGS];
    void *avalues[FFI_MAX_ARGS];
    atom_word slots[FFI_MAX_ARGS];

    for (int i = 0; i < nargs; i++) {
        slots[i] = args[i];
        atypes[i] = &ffi_type_sint64;
        avalues[i] = &slots[i];
    }

    if (ffi_prep_cif(&cif, FFI_DEFAULT_ABI, nargs, &ffi_type_sint64, atypes) != FFI_OK) {
        fprintf(stderr, "[FFI] Error: cannot prepare call with %d args\n", nargs);
        return false;
    }

    atom_word value = 0;
    ffi_call(&cif, FFI_FN(fn), &value, avalues);
    if (result) *result = value;
    ffi->calls++;
    return true;
}
#elif defined(ATOM_FFI_SYSV)
static bool call_word(FfiState *ffi, void *fn, const atom_word *args, int nargs, atom_word *result) {
    if (!fn) return false;
    if (nargs < 0 || nargs > 6) {
        fprintf(stderr, "[FFI] Error: the sysv64 backend supports 0..6 arguments, got %d\n", nargs);
        return false;
    }

    struct {
        atom_word slots[6];
        atom_word count;
        void *target;
        atom_word value;
    } frame;

    for (int i = 0; i < 6; i++) frame.slots[i] = (i < nargs) ? args[i] : 0;
    frame.count = (atom_word)nargs;
    frame.target = fn;
    frame.value = 0;

    __asm__ volatile (
        "movq %[a0], %%rdi\n\t"
        "movq %[a1], %%rsi\n\t"
        "movq %[a2], %%rdx\n\t"
        "movq %[a3], %%rcx\n\t"
        "movq %[a4], %%r8\n\t"
        "movq %[a5], %%r9\n\t"
        "movq %[n], %%rax\n\t"
        "call *%[fn]\n\t"
        "movq %%rax, %[ret]\n\t"
        : [ret] "=m" (frame.value)
        : [a0] "m" (frame.slots[0]), [a1] "m" (frame.slots[1]),
          [a2] "m" (frame.slots[2]), [a3] "m" (frame.slots[3]),
          [a4] "m" (frame.slots[4]), [a5] "m" (frame.slots[5]),
          [n] "m" (frame.count), [fn] "m" (frame.target)
        : "rax", "rdi", "rsi", "rdx", "rcx", "r8", "r9", "r10", "r11", "memory", "cc"
    );

    if (result) *result = frame.value;
    ffi->calls++;
    return true;
}
#else
static bool call_word(FfiState *ffi, void *fn, const atom_word *args, int nargs, atom_word *result) {
    (void)fn; (void)args; (void)nargs; (void)result; (void)ffi;
    fprintf(stderr, "[FFI] Error: no call backend for this architecture\n");
    return false;
}
#endif

bool ffi_call_long(FfiState *ffi, void *fn, const long *args, int nargs, long *result) {
    if (nargs < 0 || nargs > FFI_MAX_ARGS) return false;

    atom_word slots[FFI_MAX_ARGS];
    for (int i = 0; i < nargs; i++) slots[i] = (atom_word)args[i];

    atom_word value = 0;
    if (!call_word(ffi, fn, slots, nargs, &value)) return false;
    if (result) *result = (long)value;
    return true;
}

bool ffi_call_ptr(FfiState *ffi, void *fn, void *ptr, long *result) {
    atom_word slots[1];
    slots[0] = (atom_word)(uintptr_t)ptr;

    atom_word value = 0;
    if (!call_word(ffi, fn, slots, 1, &value)) return false;
    if (result) *result = (long)value;
    return true;
}

bool ffi_call_ptr_word(FfiState *ffi, void *fn, void *ptr, long count, long *result) {
    atom_word slots[2];
    slots[0] = (atom_word)(uintptr_t)ptr;
    slots[1] = (atom_word)count;

    atom_word value = 0;
    if (!call_word(ffi, fn, slots, 2, &value)) return false;
    if (result) *result = (long)value;
    return true;
}

bool ffi_call_void(FfiState *ffi, void *fn, long *result) {
    atom_word value = 0;
    if (!call_word(ffi, fn, NULL, 0, &value)) return false;
    if (result) *result = (long)value;
    return true;
}

void *ffi_alloc(FfiState *ffi, long size) {
    FfiBlock *block = block_new(ffi, size);
    if (!block) {
        fprintf(stderr, "[FFI] Error: allocation of %ld bytes failed\n", size);
        return NULL;
    }
    return block->ptr;
}

void ffi_free(FfiState *ffi, void *ptr) {
    if (!ptr) return;
    FfiBlock *block = block_of(ffi, ptr);
    if (!block) {
        fprintf(stderr, "[FFI] Warning: pointer %p is not a tracked block\n", ptr);
        return;
    }
    FfiBlock *prev = NULL;
    for (FfiBlock *b = ffi->blocks; b; b = b->next) {
        if (b == block) {
            if (prev) prev->next = b->next;
            else ffi->blocks = b->next;
            free(b->ptr);
            free(b);
            return;
        }
        prev = b;
    }
}

char *ffi_cstr(FfiState *ffi, const char *text) {
    if (!text) text = "";
    size_t len = strlen(text) + 1;
    FfiBlock *block = block_new(ffi, (long)len);
    if (!block) return NULL;
    memcpy(block->ptr, text, len);
    return (char *)block->ptr;
}

bool ffi_store8(FfiState *ffi, void *ptr, long value) {
    if (!ptr) return false;
    if (!block_of(ffi, ptr)) {
        fprintf(stderr, "[FFI] Error: store8 target %p is not tracked\n", ptr);
        return false;
    }
    *(unsigned char *)ptr = (unsigned char)(value & 0xFF);
    return true;
}

long ffi_load8(FfiState *ffi, void *ptr) {
    if (!ptr || !block_of(ffi, ptr)) return -1;
    return (long)(*(unsigned char *)ptr);
}

bool ffi_store32(FfiState *ffi, void *ptr, long value) {
    if (!ptr) return false;
    if (!block_of(ffi, ptr)) {
        fprintf(stderr, "[FFI] Error: store32 target %p is not tracked\n", ptr);
        return false;
    }
    uint32_t raw = (uint32_t)(value & 0xFFFFFFFFL);
    memcpy(ptr, &raw, sizeof(raw));
    return true;
}

long ffi_load32(FfiState *ffi, void *ptr) {
    if (!ptr || !block_of(ffi, ptr)) return -1;
    uint32_t raw = 0;
    memcpy(&raw, ptr, sizeof(raw));
    return (long)raw;
}

bool ffi_abi_check(FfiState *ffi, int handle, bool verbose) {
    void *fn_word_size = NULL;
    void *fn_pair_size = NULL;
    void *fn_buffer_size = NULL;
    void *fn_fill_pair = NULL;
    void *fn_fill_buffer = NULL;
    void *fn_fill_cells = NULL;

    const struct {
        const char *name;
        void **slot;
    } probes[] = {
        {"atom_abi_word_size", &fn_word_size},
        {"atom_abi_pair_size", &fn_pair_size},
        {"atom_abi_buffer_size", &fn_buffer_size},
        {"atom_abi_fill_pair", &fn_fill_pair},
        {"atom_abi_fill_buffer", &fn_fill_buffer},
        {"atom_abi_fill_cells", &fn_fill_cells},
    };

    for (size_t i = 0; i < sizeof(probes) / sizeof(probes[0]); i++) {
        if (!ffi_resolve_quiet(ffi, handle, probes[i].name, probes[i].slot)) {
            if (verbose) {
                fprintf(stderr, "[ABI] module does not export %s\n", probes[i].name);
            }
            return false;
        }
    }

    bool ok = true;

    long reported = -1;
    if (!ffi_call_void(ffi, fn_word_size, &reported)) ok = false;
    else if (reported != ATOM_ABI_WORD_SIZE) {
        fprintf(stderr, "[ABI] word width mismatch: C %d, module %ld\n",
                ATOM_ABI_WORD_SIZE, reported);
        ok = false;
    } else if (verbose) {
        printf("[ABI] word width %d bytes on both sides\n", ATOM_ABI_WORD_SIZE);
    }

    long pair_size = 0;
    if (!ffi_call_void(ffi, fn_pair_size, &pair_size)) ok = false;
    else if (pair_size != (long)sizeof(atom_pair)) {
        fprintf(stderr, "[ABI] atom_pair size mismatch: C %zu, module %ld\n",
                sizeof(atom_pair), pair_size);
        ok = false;
    }

    long buffer_size = 0;
    if (!ffi_call_void(ffi, fn_buffer_size, &buffer_size)) ok = false;
    else if (buffer_size != (long)sizeof(atom_buffer)) {
        fprintf(stderr, "[ABI] atom_buffer size mismatch: C %zu, module %ld\n",
                sizeof(atom_buffer), buffer_size);
        ok = false;
    }

    if (ok && verbose) {
        printf("[ABI] shared records: atom_pair %zu bytes (offsets %zu/%zu), "
               "atom_buffer %zu bytes (len at %zu)\n",
               sizeof(atom_pair), offsetof(atom_pair, lo), offsetof(atom_pair, hi),
               sizeof(atom_buffer), offsetof(atom_buffer, len));
    }

    if (ok) {
        atom_pair pair;
        memset(&pair, 0x5A, sizeof(pair));
        ffi_call_ptr(ffi, fn_fill_pair, &pair, NULL);
        if (pair.lo != 1111111111LL || pair.hi != 2222222222LL) {
            fprintf(stderr, "[ABI] atom_pair layout mismatch: lo=%lld hi=%lld\n",
                    (long long)pair.lo, (long long)pair.hi);
            ok = false;
        } else if (verbose) {
            printf("[ABI] atom_pair offsets verified through the module\n");
        }
    }

    if (ok) {
        atom_buffer buffer;
        memset(&buffer, 0x5A, sizeof(buffer));
        ffi_call_ptr(ffi, fn_fill_buffer, &buffer, NULL);
        if (buffer.data != NULL || buffer.len != 4242) {
            fprintf(stderr, "[ABI] atom_buffer layout mismatch: data=%p len=%lld\n",
                    buffer.data, (long long)buffer.len);
            ok = false;
        } else if (verbose) {
            printf("[ABI] atom_buffer offsets verified through the module\n");
        }
    }

    if (ok) {
        atom_cell cells[4];
        memset(cells, 0, sizeof(cells));
        ffi_call_ptr_word(ffi, fn_fill_cells, cells, 4, NULL);
        for (int row = 0; row < 4 && ok; row++) {
            for (int col = 0; col < 4; col++) {
                atom_word expect = (atom_word)(col * 1000 + row);
                if (cells[row].words[col] != expect) {
                    fprintf(stderr, "[ABI] array order mismatch at [%d][%d]: "
                                    "got %lld, expected %lld\n",
                            row, col, (long long)cells[row].words[col], (long long)expect);
                    ok = false;
                    break;
                }
            }
        }
        if (ok && verbose) {
            printf("[ABI] array order verified: the module writes in the C buffer order\n");
        }
    }

    if (ok && verbose) {
        printf("[ABI] C and module agree on %s\n", atom_abi_tag_text());
    }
    return ok;
}
