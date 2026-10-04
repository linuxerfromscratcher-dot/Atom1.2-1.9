#include "atom.h"

static void usage(const char *program) {
    printf("atomc - Atom Fisher Uranium interpreter\n");
    printf("Usage: %s [options] <system.mh>\n", program);
    printf("       %s [options] -c <system.mh> -o <binary>\n", program);
    printf("       %s <native-binary>\n", program);
    printf("Options:\n");
    printf("  -c, --compile    pack the program into a native binary (with -o)\n");
    printf("  -o FILE          output of --compile, default: <source>.atom\n");
    printf("  --native-info    print the platform, the ABI layout and the payload\n");
    printf("  --trace          print every executed instruction\n");
    printf("  --no-aot         ignore and do not write the AOT cache\n");
    printf("  --lib DIR        add DIR to the HEADINCLUDE search path\n");
    printf("  --help           show this text\n");
}

static int run_program(AtomVM *vm, const char *title) {
    printf("===========================================================\n");
    printf("[ATOM SYSTEM] File: %s\n", title);
    printf("===========================================================\n\n");

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
    return 0;
}

int main(int argc, char *argv[]) {
    const char *source = NULL;
    const char *output = NULL;
    bool compile = false;
    bool trace = false;
    bool aot = true;
    bool info = false;

    for (int i = 1; i < argc; i++) {
        const char *arg = argv[i];
        if (strcmp(arg, "--help") == 0 || strcmp(arg, "-h") == 0) {
            usage(argv[0]);
            return 0;
        }
        if (strcmp(arg, "--native-info") == 0) {
            info = true;
            continue;
        }
        if (strcmp(arg, "--trace") == 0) {
            trace = true;
            continue;
        }
        if (strcmp(arg, "--no-aot") == 0) {
            aot = false;
            continue;
        }
        if (strcmp(arg, "--compile") == 0 || strcmp(arg, "-c") == 0) {
            compile = true;
            continue;
        }
        if (strcmp(arg, "-o") == 0 && i + 1 < argc) {
            output = argv[++i];
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

    set_trace_enabled(trace);
    set_aot_enabled(aot);

    if (info) {
        native_info(source);
        return 0;
    }

    if (compile) {
        if (!source) {
            fprintf(stderr, "[ERROR] --compile needs a source file\n");
            usage(argv[0]);
            return 1;
        }

        char default_output[512];
        if (!output) {
            snprintf(default_output, sizeof(default_output), "%s.atom", source);
            output = default_output;
        }

        set_aot_enabled(false);

        char self[1024];
        if (!atom_self_path(self, sizeof(self))) {
            fprintf(stderr, "[ERROR] Cannot locate the running executable, cannot pack it\n");
            return 1;
        }
        set_program_path(self);

        if (!parse_atom_system(source)) {
            fprintf(stderr, "[ERROR] Failed to parse.\n");
            return 1;
        }

        printf("\n[ATOM] Compiled: %d instructions, %d commands, %d libraries\n",
               program_length, code_word_count, code_lib_count);

        if (!native_compile(self, source, output)) {
            fprintf(stderr, "[ERROR] Cannot pack '%s'.\n", output);
            return 1;
        }
        return 0;
    }

    AtomNative image;
    memset(&image, 0, sizeof(image));
    NativeState native = NATIVE_NONE;
    char title[512];
    snprintf(title, sizeof(title), "%s", source ? source : "<none>");

    if (!source) {
        native = native_open_self(&image);
        if (native == NATIVE_FAULT) return 1;
        if (native == NATIVE_READY) {
            snprintf(title, sizeof(title), "%s (packed in %s)", image.source, argv[0]);
            printf("[NATIVE] Platform %s, ABI %s\n", atom_platform_text(), atom_abi_tag_text());
            printf("[NATIVE] Program '%s' is packed into this binary (%zu bytes)\n",
                   image.source, image.size);
            if (!native_restore(&image)) {
                fprintf(stderr, "[NATIVE] The packed program cannot be decoded\n");
                native_cleanup(&image);
                return 1;
            }
        }
    }

    if (!source && native != NATIVE_READY) {
        usage(argv[0]);
        return 1;
    }

    AtomVM *vm = (AtomVM *)calloc(1, sizeof(AtomVM));
    if (!vm) {
        fprintf(stderr, "[FATAL] Out of memory\n");
        native_cleanup(&image);
        return 1;
    }

    vm_init(vm, argv[0]);
    vm->trace = trace;

    int status = 0;

    if (source && !parse_atom_system(source)) {
        printf("[ERROR] Failed to parse.\n");
        status = 1;
    } else {
        run_program(vm, title);
    }

    vm_cleanup(vm);
    free(vm);
    native_cleanup(&image);
    return status;
}
