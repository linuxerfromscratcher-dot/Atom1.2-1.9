#include "atom.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

typedef enum {
    FILE_MODE_READ = 0,
    FILE_MODE_READ_WRITE_CREATE = 4
} FileMode;

static void execute_file(AtomVM *vm, int arg) {
    switch (arg) {
        case 1: {
            StackItem name_item = pop_item(vm);
            char filename[256];
            filename[0] = '\0';

            if (name_item.type == TYPE_STRING || name_item.type == TYPE_WORD) {
                snprintf(filename, sizeof(filename), "%s", name_item.value.string);
            } else if (name_item.type == TYPE_NUMBER) {
                long name_addr = name_item.value.number;
                if (name_addr < 0 || name_addr >= MEMORY_SIZE) {
                    fprintf(stderr, "[FILE] Error: invalid filename address %ld\n", name_addr);
                    push_number(vm, -1);
                    break;
                }
                snprintf(filename, sizeof(filename), "%s", (const char *)&vm->memory[name_addr]);
            } else {
                fprintf(stderr, "[FILE] Error: expected string or address for filename\n");
                push_number(vm, -1);
                break;
            }

            if (!*filename) {
                fprintf(stderr, "[FILE] Error: empty filename\n");
                push_number(vm, -1);
                break;
            }

            FILE *probe = fopen(filename, "r");
            bool exists = probe != NULL;
            if (probe) fclose(probe);

            int fd = -1;
            for (int i = 0; i < MAX_FILES; i++) {
                if (!vm->files[i].active) {
                    fd = i;
                    break;
                }
            }

            if (fd == -1) {
                fprintf(stderr, "[FILE] Error: no free descriptors for '%s'\n", filename);
                push_number(vm, -1);
                break;
            }

            const char *mode = exists ? "r+" : "w+";
            vm->files[fd].fp = fopen(filename, mode);
            if (!vm->files[fd].fp) {
                fprintf(stderr, "[FILE] Error: cannot open '%s'\n", filename);
                push_number(vm, -1);
                break;
            }

            vm->files[fd].active = true;
            snprintf(vm->files[fd].name, sizeof(vm->files[fd].name), "%s", filename);
            vm->files[fd].position = 0;
            printf("[FILE] Opened: '%s' (fd=%d, mode=%s)\n", filename, fd, mode);
            push_number(vm, fd);
            break;
        }

        case 2: {
            int fd = (int)pop_number(vm);
            if (fd < 0 || fd >= MAX_FILES || !vm->files[fd].active || !vm->files[fd].fp) {
                fprintf(stderr, "[FILE] Error: invalid descriptor %d\n", fd);
                push_number(vm, -1);
                break;
            }
            int ch = fgetc(vm->files[fd].fp);
            if (ch == EOF) {
                push_number(vm, -1);
                printf("[FILE] EOF fd=%d at pos %ld\n", fd, vm->files[fd].position);
            } else {
                vm->files[fd].position++;
                push_number(vm, ch);
                printf("[FILE] Read fd=%d: 0x%02X at pos %ld\n", fd, ch, vm->files[fd].position);
            }
            break;
        }

        case 3: {
            long value = pop_number(vm);
            int fd = (int)pop_number(vm);
            if (fd < 0 || fd >= MAX_FILES || !vm->files[fd].active || !vm->files[fd].fp) {
                fprintf(stderr, "[FILE] Error: invalid descriptor %d\n", fd);
                break;
            }
            if (fputc((char)value, vm->files[fd].fp) == EOF) {
                fprintf(stderr, "[FILE] Error: write failed on fd=%d\n", fd);
                break;
            }
            vm->files[fd].position++;
            fflush(vm->files[fd].fp);
            printf("[FILE] Write fd=%d: 0x%02lX at pos %ld\n", fd, value & 0xFF, vm->files[fd].position);
            break;
        }

        case 4: {
            int fd = (int)pop_number(vm);
            if (fd < 0 || fd >= MAX_FILES || !vm->files[fd].active) {
                fprintf(stderr, "[FILE] Error: invalid descriptor %d\n", fd);
                break;
            }
            if (vm->files[fd].fp) {
                fclose(vm->files[fd].fp);
                vm->files[fd].fp = NULL;
            }
            printf("[FILE] Closed fd=%d: '%s'\n", fd, vm->files[fd].name);
            vm->files[fd].active = false;
            vm->files[fd].position = 0;
            memset(vm->files[fd].name, 0, sizeof(vm->files[fd].name));
            break;
        }

        default:
            fprintf(stderr, "[FILE] Error: unknown subcommand F%d\n", arg);
            break;
    }
}

static void execute_get(AtomVM *vm, const AtomInstruction *inst) {
    if (!inst->has_arg) {
        push_number(vm, vm->r7);
        printf("[GET_PORT] system register R7 -> %u\n", vm->r7);
        return;
    }

    int device = inst->arg;
    if (device == 9 && inst->sub_arg > 0) device = 9 + inst->sub_arg;

    if (device >= 1 && device <= 9) {
        push_number(vm, hal_read(vm, device));
        return;
    }

    if (device > 9 && device <= hal_device_count()) {
        push_number(vm, hal_read(vm, device));
        return;
    }

    push_number(vm, (long)read_port(vm, inst->arg));
}

static void execute_head_include(const AtomInstruction *inst) {
    const char *name = get_name(inst);
    if (name) {
        printf("[HEADINCLUDE] %s (commands are linked at compile time)\n", name);
    }

    int count = 0;
    for (int i = 0; i < code_lib_count; i++) {
        if (!code_libs[i].head_include) continue;
        count++;
        printf("[HEADINCLUDE] lib %d: %s -> %s\n", count, code_libs[i].name, code_libs[i].path);
    }
    printf("[HEADINCLUDE] total %d\n", count);
}

static void call_command(AtomVM *vm, const char *name) {
    int index = find_word(name);
    if (index < 0) {
        fprintf(stderr, "[ERROR] Line %d: unknown command ':%s'\n", vm->line_number, name);
        push_number(vm, -1);
        return;
    }

    if (vm->call_sp >= MAX_CALL_STACK - 1) {
        fprintf(stderr, "[FATAL] Call stack overflow at line %d!\n", vm->line_number);
        vm->running = false;
        return;
    }

    WordDefinition *word = &code_words[index];
    printf("[EXEC] :%s [%d..%d] from %s\n",
           word->name, word->start_pc, word->end_pc, code_file_name(word->library));
    vm->call_stack[++vm->call_sp] = vm->pc + 1;
    vm->pc = word->start_pc;
}

static void execute_exec(AtomVM *vm, const AtomInstruction *inst) {
    const char *name = get_name(inst);
    if (name) {
        call_command(vm, name);
        return;
    }

    if (vm->sp < 0) {
        fprintf(stderr, "[ERROR] Line %d: EXEC needs a command name or address\n", vm->line_number);
        return;
    }

    StackItem item = pop_item(vm);
    if (item.type == TYPE_WORD) {
        call_command(vm, item.value.word);
        return;
    }
    if (item.type == TYPE_STRING) {
        call_command(vm, item.value.string);
        return;
    }
    if (item.type == TYPE_NUMBER) {
        fprintf(stderr, "[ERROR] Line %d: EXEC needs a command name, "
                "the numeric jump of Atom 1.x is gone (use J or a command block)\n",
                vm->line_number);
        push_number(vm, -1);
        return;
    }

    fprintf(stderr, "[ERROR] Line %d: EXEC cannot handle type %d\n", vm->line_number, (int)item.type);
}

static void read_input_line(const char *name, char *buffer, size_t size) {
    if (name) printf("%s", name);
    fflush(stdout);
    if (!fgets(buffer, (int)size, stdin)) buffer[0] = '\0';
    size_t len = strlen(buffer);
    if (len > 0 && buffer[len - 1] == '\n') buffer[--len] = '\0';
    if (len > 0 && buffer[len - 1] == '\r') buffer[--len] = '\0';
}

static void execute_input(AtomVM *vm, const AtomInstruction *inst) {
    const char *name = get_name(inst);
    char buffer[256];

    if (inst->arg == 1) {
        read_input_line(name, buffer, sizeof(buffer));
        push_string(vm, buffer);
        printf("[INPUT_BUF] %ld bytes -> stack\n", (long)strlen(buffer));
        return;
    }

    if (inst->arg == 2) {
        read_input_line(name, buffer, sizeof(buffer));
        char *end = NULL;
        long value = strtol(buffer, &end, 10);
        if (!buffer[0] || (end && *end != '\0')) {
            value = buffer[0] ? (unsigned char)buffer[0] : 0;
        }
        vm->memory[2] = (unsigned char)(value & 0xFF);
        printf("[INPUT_RAM] %ld -> service byte 2\n", value & 0xFF);
        return;
    }

    if (inst->arg == 3) {
        read_input_line(name, buffer, sizeof(buffer));
        char *end = NULL;
        long value = strtol(buffer, &end, 10);
        if (!buffer[0] || (end && *end != '\0')) {
            value = buffer[0] ? (unsigned char)buffer[0] : 0;
        }
        vm->r7 = (uint8_t)(value & 0xFF);
        printf("[INPUT_REG] R7 <- %ld\n", value & 0xFF);
        return;
    }

    fprintf(stderr, "[ERROR] Line %d: unknown subcommand I%d\n", vm->line_number, inst->arg);
}

static long ffi_arg_value(AtomVM *vm, StackItem item) {
    switch (item.type) {
        case TYPE_NUMBER: return item.value.number;
        case TYPE_CHAR: return (long)item.value.character;
        case TYPE_STRING: return (long)ffi_cstr(&vm->ffi, item.value.string);
        case TYPE_WORD: return (long)ffi_cstr(&vm->ffi, item.value.word);
        default: return 0;
    }
}

static void execute_ffi(AtomVM *vm, const AtomInstruction *inst) {
    char literal[MAX_WORD_LEN];
    literal[0] = '\0';
    const char *name = get_name(inst);
    if (name) snprintf(literal, sizeof(literal), "%s", name);

    switch (inst->arg) {
        case 1: {
            const char *path = literal[0] ? literal : pop_string(vm);
            char resolved[512];
            if (!resolve_runtime_file(path, resolved, sizeof(resolved))) {
                snprintf(resolved, sizeof(resolved), "%s", path);
            }
            int handle = 0;
            if (ffi_open(&vm->ffi, resolved, &handle, false)) push_number(vm, handle);
            else push_number(vm, -1);
            break;
        }

        case 2: {
            char target[MAX_WORD_LEN];
            if (literal[0]) {
                snprintf(target, sizeof(target), "%s", literal);
            } else {
                snprintf(target, sizeof(target), "%s", pop_string(vm));
            }
            int handle = (int)pop_number(vm);
            void *symbol = NULL;
            if (ffi_resolve(&vm->ffi, handle, target, &symbol)) {
                printf("[FFI] Symbol %s @ %p (handle %d)\n", target, symbol, handle);
                push_number(vm, (long)(intptr_t)symbol);
            } else {
                push_number(vm, -1);
            }
            break;
        }

        case 3: {
            int nargs = inst->sub_arg > 0 ? inst->sub_arg : 0;
            if (nargs > FFI_MAX_ARGS) {
                fprintf(stderr, "[ERROR] Line %d: FFI call with %d args exceeds %d\n",
                        vm->line_number, nargs, FFI_MAX_ARGS);
                push_number(vm, -1);
                return;
            }
            long args[FFI_MAX_ARGS];
            for (int i = nargs - 1; i >= 0; i--) {
                args[i] = ffi_arg_value(vm, pop_item(vm));
            }
            long target = pop_number(vm);
            void *fn = (void *)(intptr_t)target;
            if (target <= 0 || !ffi_symbol_known(&vm->ffi, fn)) {
                fprintf(stderr, "[ERROR] Line %d: FFI call target %ld is not a resolved symbol "
                        "(K2 must be called before K3)\n", vm->line_number, target);
                push_number(vm, -1);
                return;
            }
            long result = 0;
            if (ffi_call_long(&vm->ffi, fn, args, nargs, &result)) {
                printf("[FFI] Call %ld(%d args) -> %ld\n", target, nargs, result);
                push_number(vm, result);
            } else {
                push_number(vm, -1);
            }
            break;
        }

        case 4: {
            int handle = (int)pop_number(vm);
            ffi_close(&vm->ffi, handle);
            break;
        }

        case 5: {
            long size = pop_number(vm);
            void *ptr = ffi_alloc(&vm->ffi, size);
            if (!ptr) {
                push_number(vm, -1);
                return;
            }
            printf("[FFI] Alloc %ld bytes @ %p\n", size, ptr);
            push_number(vm, (long)(intptr_t)ptr);
            break;
        }

        case 6: {
            pop_number(vm);
            const char *text = literal[0] ? literal : pop_string(vm);
            char *ptr = ffi_cstr(&vm->ffi, text);
            if (!ptr) {
                push_number(vm, -1);
                return;
            }
            printf("[FFI] C string '%s' @ %p\n", text, (void *)ptr);
            push_number(vm, (long)(intptr_t)ptr);
            break;
        }

        case 7: {
            void *ptr = (void *)(intptr_t)pop_number(vm);
            ffi_free(&vm->ffi, ptr);
            break;
        }

        case 8: {
            long value = pop_number(vm);
            void *ptr = (void *)(intptr_t)pop_number(vm);
            if (!ffi_store32(&vm->ffi, ptr, value)) {
                push_number(vm, -1);
                return;
            }
            printf("[FFI] Store32 %ld -> %p\n", value, ptr);
            break;
        }

        case 9: {
            void *ptr = (void *)(intptr_t)pop_number(vm);
            long value = ffi_load8(&vm->ffi, ptr);
            if (value < 0) {
                push_number(vm, -1);
                return;
            }
            printf("[FFI] Load8 %p -> %ld\n", ptr, value);
            push_number(vm, value);
            break;
        }

        default:
            fprintf(stderr, "[ERROR] Line %d: unknown FFI subcommand K%d\n", vm->line_number, inst->arg);
            break;
    }
}

static void execute_kernel(AtomVM *vm, const AtomInstruction *inst) {
    if (!inst->has_arg) {
        printf("[KERNEL] Interrupt at line %d (pid %d, context %d)\n",
               inst->line, 0, vm->current_context);
        return;
    }

    if (inst->arg >= 1 && inst->arg <= 9) {
        execute_ffi(vm, inst);
        return;
    }

    printf("[KERNEL] Syscall %d at line %d\n", inst->arg, inst->line);
}

static void execute_setup(AtomVM *vm, const AtomInstruction *inst) {
    if (inst->arg == 1) {
        vm->heap_pointer = 0x1000;
        vm->pack_shadow_pointer = 0;
        vm->pack_shadow_used = 0;
        vm->r0 = vm->r1 = vm->r2 = vm->r3 = 0;
        vm->r4 = vm->r5 = vm->r6 = vm->r7 = 0;
        vm->condition_flag = false;
        vm->call_sp = -1;
        hal_reset(vm);
        printf("[SETUP] System initialized.\n");
        return;
    }

    if (inst->arg == 2) {
        long addr = pop_number(vm);
        if (addr < 0 || addr + 32 >= MEMORY_SIZE) {
            fprintf(stderr, "[ERROR] Line %d: STORE address %ld out of range\n", vm->line_number, addr);
            return;
        }
        int count = (vm->sp + 1 < 16) ? vm->sp + 1 : 16;
        for (int i = 0; i < count; i++) {
            if (vm->stack[i].type == TYPE_NUMBER) {
                vm->memory[addr + i] = (unsigned char)(vm->stack[i].value.number & 0xFF);
            }
        }
        vm->memory[addr + 16] = vm->r0;
        vm->memory[addr + 17] = vm->r1;
        vm->memory[addr + 18] = vm->r2;
        vm->memory[addr + 19] = vm->r3;
        vm->memory[addr + 20] = vm->r4;
        vm->memory[addr + 21] = vm->r5;
        vm->memory[addr + 22] = vm->r6;
        vm->memory[addr + 23] = vm->r7;
        vm->memory[addr + 24] = (unsigned char)(vm->sp & 0xFF);
        vm->memory[addr + 25] = (unsigned char)(vm->call_sp & 0xFF);
        vm->memory[addr + 26] = (unsigned char)(vm->condition_flag ? 1 : 0);
        push_number(vm, addr);
        printf("[STORE] Saved state to 0x%04lX\n", addr);
        return;
    }

    if (inst->arg == 3) {
        long addr = pop_number(vm);
        if (addr < 0 || addr + 32 >= MEMORY_SIZE) {
            fprintf(stderr, "[ERROR] Line %d: RESTORE address %ld out of range\n", vm->line_number, addr);
            return;
        }
        for (int i = 0; i < 16; i++) push_number(vm, vm->memory[addr + i]);
        vm->r0 = vm->memory[addr + 16];
        vm->r1 = vm->memory[addr + 17];
        vm->r2 = vm->memory[addr + 18];
        vm->r3 = vm->memory[addr + 19];
        vm->r4 = vm->memory[addr + 20];
        vm->r5 = vm->memory[addr + 21];
        vm->r6 = vm->memory[addr + 22];
        vm->r7 = vm->memory[addr + 23];
        vm->sp = (int)vm->memory[addr + 24] - 1;
        vm->call_sp = (int)vm->memory[addr + 25] - 1;
        vm->condition_flag = vm->memory[addr + 26] != 0;
        printf("[RESTORE] Restored state from 0x%04lX\n", addr);
        return;
    }

    fprintf(stderr, "[ERROR] Line %d: unknown subcommand S%d\n", vm->line_number, inst->arg);
}

static void execute_output(AtomVM *vm) {
    if (vm->sp < 0) {
        fprintf(stderr, "[ERROR] Line %d: WRITE on empty stack\n", vm->line_number);
        return;
    }

    StackItem item = vm->stack[vm->sp];

    if (item.type == TYPE_NUMBER && item.value.number >= 0 &&
        item.value.number < MEMORY_SIZE - 1) {
        const char *text = (const char *)&vm->memory[item.value.number];
        if (text[0] == '/') {
            text++;
            while (*text && *text != '/') putchar(*text++);
            putchar('\n');
            pop_item(vm);
            return;
        }
    }

    switch (item.type) {
        case TYPE_NUMBER: printf("%ld\n", item.value.number); break;
        case TYPE_CHAR: printf("%c\n", item.value.character); break;
        case TYPE_STRING: printf("%s\n", item.value.string); break;
        case TYPE_WORD: printf(":%s\n", item.value.word); break;
        default: printf("Unknown type\n"); break;
    }

    pop_item(vm);
}

static void trace_instruction(const AtomInstruction *inst) {
    const char *name = get_name(inst);
    printf("[TRACE] %s:%d %s", code_file_name(inst->file_idx), inst->line,
           inst->is_ret ? "RET" : "");
    if (!inst->is_ret) {
        printf("%c", inst->cmd);
        if (inst->has_arg) printf(" %d", inst->arg);
        if (inst->sub_arg) printf("(%d)", inst->sub_arg);
        if (name) printf(" \"%s\"", name);
    }
    printf("\n");
}

void vm_execute(AtomVM *vm) {
    vm->pc = 0;
    vm->execution_steps = 0;

    load_raw_bytes(vm);
    load_stack_context(vm);

    if (vm->trace) dump_program();

    while (vm->pc < program_length && vm->running) {
        vm->execution_steps++;

        if (vm->execution_steps > MAX_EXECUTION_STEPS) {
            fprintf(stderr, "[FATAL] Execution limit exceeded (%d steps)!\n", MAX_EXECUTION_STEPS);
            vm->running = false;
            break;
        }

        if (vm->pc < 0 || vm->pc >= program_length) {
            fprintf(stderr, "[ERROR] Invalid PC: %d\n", vm->pc);
            vm->running = false;
            break;
        }

        int current_pc = vm->pc;
        AtomInstruction inst = program[vm->pc];
        vm->line_number = inst.line;

        if (vm->trace) trace_instruction(&inst);

        if (inst.is_ret) {
            if (vm->call_sp >= 0) {
                vm->pc = vm->call_stack[vm->call_sp--];
            } else {
                fprintf(stderr, "[ERROR] Line %d: return without a call\n", inst.line);
            }
            continue;
        }

        switch (inst.cmd) {
            case 'A': {
                long size = pop_number(vm);
                if (size < 0) {
                    fprintf(stderr, "[ERROR] Line %d: ALLOC negative size %ld\n", inst.line, size);
                    push_number(vm, -1);
                    break;
                }
                long addr = vm->heap_pointer;
                vm->heap_pointer += size;
                if (vm->heap_pointer >= MEMORY_SIZE) {
                    fprintf(stderr, "[FATAL] Out of RAM at line %d!\n", inst.line);
                    vm->running = false;
                } else {
                    printf("[ALLOC] %ld bytes at 0x%04lX\n", size, addr);
                    push_number(vm, addr);
                }
                break;
            }

            case 'C': {
                StackItem right = pop_item(vm);
                StackItem left = pop_item(vm);
                int order;
                if (item_compare(&left, &right, &order) != 0) {
                    push_number(vm, 0);
                    break;
                }
                int result;
                switch (inst.arg) {
                    case 0:
                    case 1: result = (order == 0); break;
                    case 2: result = (order != 0); break;
                    case 3: result = (order > 0); break;
                    case 4: result = (order < 0); break;
                    case 5: result = (order >= 0); break;
                    case 6: result = (order <= 0); break;
                    default:
                        fprintf(stderr, "[ERROR] Line %d: unknown subcommand C%d\n", inst.line, inst.arg);
                        push_number(vm, 0);
                        return;
                }
                push_number(vm, result);
                if (vm->trace) printf("[CMP] %d\n", result);
                break;
            }

            case 'D': {
                push_number(vm, inst.arg);
                if (vm->trace) printf("[LOAD] %d\n", inst.arg);
                break;
            }

            case 'E': {
                execute_exec(vm, &inst);
                break;
            }

            case 'F': {
                execute_file(vm, inst.arg);
                break;
            }

            case 'G': {
                execute_get(vm, &inst);
                break;
            }

            case 'H': {
                execute_head_include(&inst);
                break;
            }

            case 'I': {
                execute_input(vm, &inst);
                break;
            }

            case 'J': {
                if (!inst.has_arg) {
                    fprintf(stderr, "[ERROR] Line %d: JUMP without label\n", inst.line);
                    break;
                }
                int target = find_label(inst.arg);
                if (target < 0) {
                    fprintf(stderr, "[ERROR] Line %d: JUMP label %d not found\n", inst.line, inst.arg);
                    break;
                }
                if (target >= program_length) {
                    fprintf(stderr, "[ERROR] Line %d: JUMP label %d out of range\n", inst.line, inst.arg);
                    break;
                }
                int taken = 1;
                if (inst.sub_arg == 1) {
                    long condition = pop_number(vm);
                    taken = (condition != 0);
                    printf("[JUMP] to label %d if true (instruction %d): %s\n",
                           inst.arg, target, taken ? "taken" : "not taken");
                } else if (inst.sub_arg == 2) {
                    long condition = pop_number(vm);
                    taken = (condition == 0);
                    printf("[JUMP] to label %d if false (instruction %d): %s\n",
                           inst.arg, target, taken ? "taken" : "not taken");
                } else if (inst.sub_arg == 0) {
                    printf("[JUMP] to label %d (instruction %d)\n", inst.arg, target);
                } else {
                    fprintf(stderr, "[ERROR] Line %d: unknown subcommand J%d\n", inst.line, inst.sub_arg);
                    break;
                }
                if (taken) vm->pc = target;
                break;
            }

            case 'K': {
                execute_kernel(vm, &inst);
                break;
            }

            case 'M': {
                if (inst.has_arg) {
                    long value = pop_number(vm);
                    if (inst.arg < 0 || inst.arg >= MEMORY_SIZE) {
                        fprintf(stderr, "[ERROR] Line %d: MEM address %d out of range\n", inst.line, inst.arg);
                        break;
                    }
                    vm->memory[inst.arg] = (unsigned char)(value & 0xFF);
                    printf("[MEM] Write 0x%02lX to 0x%04X (%s)\n", value & 0xFF, inst.arg,
                           code_file_name(inst.file_idx));
                } else {
                    long addr = pop_number(vm);
                    if (addr < 0 || addr >= MEMORY_SIZE) {
                        fprintf(stderr, "[ERROR] Line %d: MEM address %ld out of range\n", inst.line, addr);
                        push_number(vm, -1);
                        break;
                    }
                    push_number(vm, vm->memory[addr]);
                    printf("[MEM] Read 0x%04lX -> 0x%02X\n", addr, vm->memory[addr]);
                }
                break;
            }

            case 'N': {
                if (vm->sp < 0) {
                    fprintf(stderr, "[ERROR] Line %d: INC on empty stack\n", inst.line);
                    break;
                }
                if (vm->stack[vm->sp].type == TYPE_NUMBER) {
                    vm->stack[vm->sp].value.number++;
                } else if (vm->stack[vm->sp].type == TYPE_CHAR) {
                    vm->stack[vm->sp].value.character++;
                } else {
                    fprintf(stderr, "[ERROR] Line %d: INC needs a number\n", inst.line);
                }
                break;
            }

            case 'O': {
                execute_output(vm);
                break;
            }

            case 'P': {
                if (inst.arg == 2) {
                    if (vm->sp < 0) {
                        fprintf(stderr, "[ERROR] Line %d: POP on empty stack\n", inst.line);
                        break;
                    }
                    pop_item(vm);
                    break;
                }
                if (vm->sp < 0) {
                    fprintf(stderr, "[ERROR] Line %d: DUP on empty stack\n", inst.line);
                    break;
                }
                push_item(vm, vm->stack[vm->sp]);
                if (vm->trace) printf("[%s]\n", inst.arg == 1 ? "PUSH" : "DUP");
                break;
            }

            case 'Q': {
                vm->running = false;
                printf("[SYS] Terminated at line %d.\n", inst.line);
                break;
            }

            case 'R': {
                if (!inst.has_arg) {
                    push_number(vm, vm->r7);
                    break;
                }
                long value = pop_number(vm);
                uint8_t byte = (uint8_t)(value & 0xFF);
                switch (inst.arg) {
                    case 0: vm->r0 = byte; break;
                    case 1: vm->r1 = byte; break;
                    case 2: vm->r2 = byte; break;
                    case 3: vm->r3 = byte; break;
                    case 4: vm->r4 = byte; break;
                    case 5: vm->r5 = byte; break;
                    case 6: vm->r6 = byte; break;
                    case 7: vm->r7 = byte; break;
                    default:
                        fprintf(stderr, "[ERROR] Line %d: unknown register R%d\n", inst.line, inst.arg);
                        break;
                }
                if (inst.arg >= 0 && inst.arg <= 7) {
                    printf("[REG] R%d <- %u\n", inst.arg, (unsigned)byte);
                }
                break;
            }

            case 'S': {
                execute_setup(vm, &inst);
                break;
            }

            case 'T': {
                long b = pop_number(vm);
                long a = pop_number(vm);
                char cmd[3];
                snprintf(cmd, sizeof(cmd), "T%d", inst.arg);
                generate_fortran_ops(vm, cmd, a, b);
                break;
            }

            case 'U': {
                if (inst.arg == 1) {
                    long addr = pop_number(vm);
                    long b = pop_number(vm);
                    long a = pop_number(vm);
                    if (addr < 0 || addr + PACK_RECORD_SIZE > MEMORY_SIZE) {
                        fprintf(stderr, "[ERROR] Line %d: PACK address %ld out of range\n", inst.line, addr);
                        push_number(vm, -1);
                        break;
                    }
                    pack_write_record(vm, addr, a, b);
                    printf("[PACK] %ld,%ld -> 0x%04lX (%d bytes)\n", a, b, addr, PACK_RECORD_SIZE);
                    push_number(vm, addr);
                } else if (inst.arg == 2) {
                    long addr = pop_number(vm);
                    if (addr < 0 || addr + PACK_RECORD_SIZE > MEMORY_SIZE) {
                        fprintf(stderr, "[ERROR] Line %d: UNPACK address %ld out of range\n", inst.line, addr);
                        push_number(vm, -1);
                        break;
                    }
                    long a = 0, b = 0;
                    if (!pack_read_record(vm, addr, &a, &b)) {
                        fprintf(stderr, "[ERROR] Line %d: UNPACK record at 0x%04lX is not packed\n",
                                inst.line, addr);
                        push_number(vm, -1);
                        break;
                    }
                    printf("[UNPACK] 0x%04lX -> %ld,%ld (shadow 0x%04lX)\n",
                           addr, a, b, (long)vm->pack_shadow_pointer);
                    push_number(vm, a);
                    push_number(vm, b);
                } else {
                    fprintf(stderr, "[ERROR] Line %d: unknown subcommand U%d\n", inst.line, inst.arg);
                }
                break;
            }

            case 'V': {
                const char *target = get_name(&inst);
                if (!inst.has_arg && !target) {
                    printf("[VECTOR] Configured: %d handlers\n", MAX_PORTS / 4);
                    break;
                }
                if (target) {
                    int label_id = atoi(target);
                    int label_pc = find_label(label_id);
                    if (label_pc < 0) {
                        fprintf(stderr, "[ERROR] Line %d: VECTOR label %s not found\n", inst.line, target);
                        break;
                    }
                    printf("[VECTOR] Redirect: label %d -> handler %d at 0x%04X\n",
                           label_id, inst.arg, label_pc);
                    vm->vector_labels[inst.arg] = label_id;
                    vm->memory[inst.arg * 4] = (unsigned char)(label_pc & 0xFF);
                    vm->memory[inst.arg * 4 + 1] = (unsigned char)((label_pc >> 8) & 0xFF);
                    vm->memory[inst.arg * 4 + 2] = 0;
                    vm->memory[inst.arg * 4 + 3] = 0;
                    break;
                }
                long handler = pop_number(vm);
                int vector = inst.arg * 4;
                if (inst.arg < 0 || vector < 0 || vector + 3 >= MEMORY_SIZE) {
                    fprintf(stderr, "[ERROR] Line %d: VECTOR %d out of range\n", inst.line, inst.arg);
                    push_number(vm, -1);
                    break;
                }
                vm->memory[vector] = (unsigned char)((handler >> 24) & 0xFF);
                vm->memory[vector + 1] = (unsigned char)((handler >> 16) & 0xFF);
                vm->memory[vector + 2] = (unsigned char)((handler >> 8) & 0xFF);
                vm->memory[vector + 3] = (unsigned char)(handler & 0xFF);
                printf("[VECTOR] Int %d -> 0x%04lX at 0x%04X\n", inst.arg, handler & 0xFFFFFFFFL, vector);
                break;
            }

            case 'W': {
                break;
            }

            case 'X': {
                long b = pop_number(vm);
                long a = pop_number(vm);
                if (inst.arg == 1) push_number(vm, a & b);
                else if (inst.arg == 2) push_number(vm, a | b);
                else if (inst.arg == 3) push_number(vm, a ^ b);
                else fprintf(stderr, "[ERROR] Line %d: unknown subcommand X%d\n", inst.line, inst.arg);
                break;
            }

            case '"': {
                const char *text = get_name(&inst);
                push_string(vm, text ? text : "");
                if (vm->trace) printf("[PUSH] \"%s\"\n", text ? text : "");
                break;
            }

            case '\'': {
                const char *text = get_name(&inst);
                if (text && text[0]) {
                    push_char(vm, (char)text[0]);
                    if (vm->trace) printf("[PUSH] '%c'\n", text[0]);
                }
                break;
            }

            case 'Y': {
                if (inst.has_arg && inst.arg >= 1) {
                    save_context(vm, vm->current_context);
                    vm->current_context = (vm->current_context + 1) % MAX_CONTEXTS;
                    restore_context(vm, vm->current_context);
                    printf("[YIELD] Switched to context %d\n", vm->current_context);
                } else {
                    printf("[YIELD] Yield at line %d.\n", inst.line);
                }
                break;
            }

            case 'Z': {
                if (inst.arg == 1) {
                    if (vm->sp < 0) {
                        push_number(vm, 1);
                        break;
                    }
                    StackItem top = vm->stack[vm->sp];
                    long value = (top.type == TYPE_NUMBER) ? top.value.number : (long)top.value.character;
                    push_number(vm, (value == 0) ? 1 : 0);
                    if (vm->trace) printf("[CLEAR] top is %s zero\n", (value == 0) ? "" : "not");
                    break;
                }
                if (inst.arg != 0 && inst.has_arg) {
                    fprintf(stderr, "[ERROR] Line %d: unknown subcommand Z%d\n", inst.line, inst.arg);
                    break;
                }
                vm->sp = -1;
                vm->call_sp = -1;
                vm->condition_flag = false;
                break;
            }

            default: {
                fprintf(stderr, "[WARNING] Line %d (%s): unknown instruction '%c'\n",
                        inst.line, code_file_name(inst.file_idx), inst.cmd);
                break;
            }
        }

        if (vm->pc == current_pc) vm->pc++;
    }

    if (vm->execution_steps >= MAX_EXECUTION_STEPS) {
        printf("\n[WARNING] Execution stopped by the step limit.\n");
    }
}
