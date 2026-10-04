#include "atom.h"

static bool try_load(AtomVM *vm, const char *path) {
    size_t len = strlen(path);
    if (len == 0 || len >= sizeof(vm->jit.lib_path)) return false;
    int handle = 0;
    if (!ffi_open(&vm->ffi, path, &handle, true)) return false;
    memcpy(vm->jit.lib_path, path, len + 1);
    return true;
}

static bool try_load_fortran(AtomVM *vm) {
    const char *env = getenv("ATOM_FORTRAN_LIB");
    if (env && *env) {
        if (try_load(vm, env)) return true;
        printf("[JIT] ATOM_FORTRAN_LIB='%s' cannot be loaded, using C arithmetic\n", env);
        return false;
    }

    const char *dir = get_self_dir();
    char path[512];

    if (dir && *dir) {
        snprintf(path, sizeof(path), "%s/../%s", dir, FORTRAN_LIB_NAME);
        if (try_load(vm, path)) return true;
        snprintf(path, sizeof(path), "%s/%s", dir, FORTRAN_LIB_NAME);
        if (try_load(vm, path)) return true;
    }

    snprintf(path, sizeof(path), "./%s", FORTRAN_LIB_NAME);
    if (try_load(vm, path)) return true;

    return try_load(vm, FORTRAN_LIB_NAME);
}

void jit_init(AtomVM *vm) {
    JITContext *jit = &vm->jit;
    memset(jit, 0, sizeof(*jit));

    if (!try_load_fortran(vm)) {
        printf("[JIT] Fortran module not found, using C arithmetic\n");
        return;
    }

    int handle = 0;
    for (int i = 0; i < FFI_MAX_MODULES; i++) {
        if (vm->ffi.modules[i].loaded) {
            handle = i + 1;
            break;
        }
    }

    if (!ffi_resolve(&vm->ffi, handle, "atom_add", &jit->sym_add) ||
        !ffi_resolve(&vm->ffi, handle, "atom_sub", &jit->sym_sub) ||
        !ffi_resolve(&vm->ffi, handle, "atom_mul", &jit->sym_mul) ||
        !ffi_resolve(&vm->ffi, handle, "atom_div", &jit->sym_div)) {
        printf("[JIT] Fortran module has no atom_* symbols, using C arithmetic\n");
        jit->sym_add = NULL;
        jit->sym_sub = NULL;
        jit->sym_mul = NULL;
        jit->sym_div = NULL;
        return;
    }

    if (!ffi_abi_check(&vm->ffi, handle, true)) {
        printf("[JIT] Fortran module does not match the VM ABI, using C arithmetic\n");
        jit->enabled = false;
        jit->sym_add = NULL;
        jit->sym_sub = NULL;
        jit->sym_mul = NULL;
        jit->sym_div = NULL;
        return;
    }

    jit->enabled = true;
    printf("[JIT] Fortran arithmetic module ready: %s\n", jit->lib_path);
}

void jit_cleanup(AtomVM *vm) {
    vm->jit.enabled = false;
}

long jit_get_result(JITContext *jit) {
    if (!jit->has_result) return 0;
    jit->has_result = false;
    return jit->last_result;
}

void generate_fortran_ops(AtomVM *vm, const char *cmd, long a, long b) {
    JITContext *jit = &vm->jit;
    int op = cmd[1] - '1';
    long result = 0;
    bool done = false;

    if (op < 0 || op > 3) {
        fprintf(stderr, "[ERROR] Line %d: unknown subcommand %s\n", vm->line_number, cmd);
        return;
    }

    if (jit->enabled) {
        void *fn = NULL;
        switch (op) {
            case 0: fn = jit->sym_add; break;
            case 1: fn = jit->sym_sub; break;
            case 2: fn = jit->sym_mul; break;
            default: fn = jit->sym_div; break;
        }
        if (fn) {
            long args[2];
            args[0] = a;
            args[1] = b;
            if (ffi_call_long(&vm->ffi, fn, args, 2, &result)) done = true;
        }
    }

    if (!done) {
        switch (op) {
            case 0: result = a + b; break;
            case 1: result = a - b; break;
            case 2: result = a * b; break;
            default: result = (b != 0) ? a / b : 0; break;
        }
    }

    if (op == 3 && b == 0) {
        fprintf(stderr, "[ERROR] Division by zero!\n");
        result = 0;
    }

    jit->last_result = result;
    jit->has_result = true;
    push_number(vm, result);
}
