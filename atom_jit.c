#include "atom.h"

void jit_init(JITContext *jit) {
    jit->fortran_lib = NULL;
    jit->process_file = NULL;
    jit->jit_enabled = false;
    jit->last_result = 0;
    jit->has_result = false;
#ifdef _WIN32
    strcpy(jit->temp_file, "atom_jit_temp.txt");
#else
    strcpy(jit->temp_file, "/tmp/atom_jit_temp.txt");
#endif
    
    #ifdef __linux__
    jit->fortran_lib = dlopen("./libatom_fortran.so", RTLD_LAZY);
    if (jit->fortran_lib) {
        jit->process_file = (void(*)(const char*))dlsym(jit->fortran_lib, "process_file_from_c");
        if (jit->process_file) {
            jit->jit_enabled = true;
            printf("[JIT] Fortran library loaded successfully!\n");
        } else {
            printf("[JIT] Warning: process_file_from_c not found\n");
            dlclose(jit->fortran_lib);
            jit->fortran_lib = NULL;
        }
    } else {
        printf("[JIT] Fortran library not found, using fallback\n");
    }
    #endif
}

void jit_cleanup(JITContext *jit) {
    #ifdef __linux__
    if (jit->fortran_lib) {
        dlclose(jit->fortran_lib);
        jit->fortran_lib = NULL;
    }
    #endif
    remove(jit->temp_file);
}

bool jit_compile_arithmetic(JITContext *jit, const char *operations) {
    if (!jit->jit_enabled || !jit->process_file) return false;
    FILE *f = fopen(jit->temp_file, "w");
    if (!f) return false;
    fprintf(f, "%s\n", operations);
    fclose(f);
    return true;
}

void jit_execute_arithmetic(JITContext *jit) {
    if (jit->jit_enabled && jit->process_file) {
        jit->process_file(jit->temp_file);
        jit->has_result = true;
    }
}

long jit_get_result(JITContext *jit) {
    if (jit->has_result) {
        jit->has_result = false;
        return jit->last_result;
    }
    return 0;
}

void generate_fortran_ops(AtomVM *vm, const char *cmd, long a, long b) {
    long result = 0;
    if (strcmp(cmd, "T1") == 0) result = a + b;
    else if (strcmp(cmd, "T2") == 0) result = a - b;
    else if (strcmp(cmd, "T3") == 0) result = a * b;
    else if (strcmp(cmd, "T4") == 0) {
        if (b != 0) result = a / b;
        else {
            fprintf(stderr, "[ERROR] Division by zero!\n");
            result = 0;
        }
    }
    push_number(vm, result);

    if (vm->jit.jit_enabled && vm->jit.process_file) {
        char ops[256];
        snprintf(ops, sizeof(ops), "%s %ld %ld", cmd, a, b);
        if (jit_compile_arithmetic(&vm->jit, ops)) {
            jit_execute_arithmetic(&vm->jit);
        }
    }
}
