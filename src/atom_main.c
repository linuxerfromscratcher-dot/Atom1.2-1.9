#include "atom.h"

static void usage(const char *program) {
    printf("atomc - Atom Fisher Uranium interpreter\n");
    printf("Usage: %s [options] <system.mh>\n", program);
    printf("Options:\n");
    printf("  --trace        print every executed instruction\n");
    printf("  --no-aot       ignore and do not write the AOT cache\n");
    printf("  --lib DIR      add DIR to the HEADINCLUDE search path\n");
    printf("  --help         show this text\n");
}

int main(int argc, char *argv[]) {
    const char *source = NULL;
    bool trace = false;
    bool aot = true;

    for (int i = 1; i < argc; i++) {
        const char *arg = argv[i];
        if (strcmp(arg, "--help") == 0 || strcmp(arg, "-h") == 0) {
            usage(argv[0]);
            return 0;
        }
        if (strcmp(arg, "--trace") == 0) {
            trace = true;
            continue;
        }
        if (strcmp(arg, "--no-aot") == 0) {
            aot = false;
            continue;
        }
        if (strcmp(arg, "--lib") == 0 && i + 1 < argc) {
            add_search_path(argv[++i]);
            continue;
        }
        if (arg[0] == '-') {
            fprintf(stderr, "[ERROR] Unknown option: %s\n", arg);
            usage(argv[0]);
            return 1;
        }
        if (!source) source = arg;
    }

    if (!source) {
        usage(argv[0]);
        return 1;
    }

    set_trace_enabled(trace);
    set_aot_enabled(aot);

    AtomVM *vm = (AtomVM *)calloc(1, sizeof(AtomVM));
    if (!vm) {
        fprintf(stderr, "[FATAL] Out of memory\n");
        return 1;
    }

    vm_init(vm, argv[0]);
    vm->trace = trace;

    printf("===========================================================\n");
    printf("[ATOM SYSTEM] File: %s\n", source);
    printf("===========================================================\n\n");

    int status = 0;

    if (!parse_atom_system(source)) {
        printf("[ERROR] Failed to parse.\n");
        status = 1;
    } else {
        printf("\n[ATOM] Loaded: %d instructions, %d commands, %d libraries\n",
               program_length, code_word_count, code_lib_count);
        printf("Container: %s | Sector: %d | Address: 0x%04X%s\n",
               current_mh.container_name, current_mh.sector_mapping,
               current_mh.target_address,
               current_mh.tiny_ram_fallback ? " (nullified)" : "");

        if (current_mh.raw_bytes_count > 0) {
            printf("Raw bytes: %d\n", current_mh.raw_bytes_count);
        }
        if (current_mh.stack_size > 0) {
            printf("Stack context: %d values\n", current_mh.stack_size);
        }

        printf("\n[VM] Execution start...\n");
        printf("-----------------------------------------------------------\n\n");

        vm_execute(vm);

        printf("\n-----------------------------------------------------------\n");
        printf("[VM] Finished.\n");
        printf("Steps: %d\n", vm->execution_steps);
        printf("Stack size: %d\n", vm->sp + 1);
        if (vm->sp >= 0) {
            printf("Top: ");
            print_stack_item(vm->stack[vm->sp]);
            printf("\n");
            printf("Stack dump:\n");
            for (int i = 0; i <= vm->sp && i < 20; i++) {
                printf("  [%d] ", i);
                print_stack_item(vm->stack[i]);
                printf("\n");
            }
            if (vm->sp > 19) printf("  ...\n");
        }
        printf("Registers: R0=%d R1=%d R2=%d R3=%d R4=%d R5=%d R6=%d R7=%d\n",
               vm->r0, vm->r1, vm->r2, vm->r3, vm->r4, vm->r5, vm->r6, vm->r7);
        printf("FFI: backend %s, calls %d, modules %d\n",
               ffi_backend(), vm->ffi.calls, ffi_module_count(&vm->ffi));
    }

    vm_cleanup(vm);
    free(vm);
    return status;
}
