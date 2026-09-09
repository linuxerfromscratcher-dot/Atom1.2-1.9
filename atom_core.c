#include "atom.h"

void vm_init(AtomVM *vm) {
    vm->sp = -1;
    vm->r0 = vm->r1 = vm->r2 = vm->r3 = vm->r4 = vm->r5 = vm->r6 = vm->r7 = 0;
    vm->running = true;
    vm->heap_pointer = 0x1000;
    vm->cond_sp = 0;
    vm->loop_sp = -1;
    vm->line_number = 0;
    vm->current_context = 0;
    vm->execution_steps = 0;
    vm->word_count = 0;
    vm->pc = 0;
    
    memset(vm->stack, 0, sizeof(vm->stack));
    memset(vm->memory, 0, sizeof(vm->memory));
    memset(vm->condition_flag, 0, sizeof(vm->condition_flag));
    memset(vm->loop_stack, 0, sizeof(vm->loop_stack));
    memset(vm->port_values, 0, sizeof(vm->port_values));
    memset(vm->context_sp, 0, sizeof(vm->context_sp));
    memset(vm->saved_context, 0, sizeof(vm->saved_context));
    memset(vm->words, 0, sizeof(vm->words));
    
    for(int i = 0; i < MAX_FILES; i++) {
        vm->files[i].active = false;
        vm->files[i].fp = NULL;
        vm->files[i].position = 0;
    }
    
    jit_init(&vm->jit);
}

void vm_cleanup(AtomVM *vm) {
    for (int i = 0; i < MAX_FILES; i++) {
        if (vm->files[i].fp) {
            fclose(vm->files[i].fp);
            vm->files[i].fp = NULL;
            vm->files[i].active = false;
        }
    }
    jit_cleanup(&vm->jit);
}

void push_item(AtomVM *vm, StackItem item) {
    if (vm->sp < STACK_SIZE - 1) {
        vm->stack[++vm->sp] = item;
    } else {
        fprintf(stderr, "[FATAL] Stack overflow at line %d!\n", vm->line_number);
        vm->running = false;
    }
}

StackItem pop_item(AtomVM *vm) {
    if (vm->sp >= 0) {
        return vm->stack[vm->sp--];
    } else {
        fprintf(stderr, "[FATAL] Stack underflow at line %d!\n", vm->line_number);
        vm->running = false;
        StackItem empty = {TYPE_NUMBER, {0}};
        return empty;
    }
}

void push_number(AtomVM *vm, long val) {
    StackItem item = {TYPE_NUMBER, {.number = val}};
    push_item(vm, item);
}

void push_char(AtomVM *vm, char c) {
    StackItem item = {TYPE_CHAR, {.character = c}};
    push_item(vm, item);
}

void push_string(AtomVM *vm, const char *str) {
    StackItem item;
    item.type = TYPE_STRING;
    strncpy(item.value.string, str, 255);
    item.value.string[255] = '\0';
    push_item(vm, item);
}

void push_word(AtomVM *vm, const char *word) {
    StackItem item;
    item.type = TYPE_WORD;
    strncpy(item.value.word, word, MAX_WORD_LEN - 1);
    item.value.word[MAX_WORD_LEN - 1] = '\0';
    push_item(vm, item);
}

long pop_number(AtomVM *vm) {
    StackItem item = pop_item(vm);
    if (item.type == TYPE_NUMBER) return item.value.number;
    fprintf(stderr, "[ERROR] Expected number, got type %d\n", item.type);
    return 0;
}

char pop_char(AtomVM *vm) {
    StackItem item = pop_item(vm);
    if (item.type == TYPE_CHAR) return item.value.character;
    fprintf(stderr, "[ERROR] Expected char, got type %d\n", item.type);
    return 0;
}

const char* pop_string(AtomVM *vm) {
    static char buffer[256];
    StackItem item = pop_item(vm);
    if (item.type == TYPE_STRING) {
        strcpy(buffer, item.value.string);
        return buffer;
    }
    fprintf(stderr, "[ERROR] Expected string, got type %d\n", item.type);
    return "";
}

const char* pop_word(AtomVM *vm) {
    static char buffer[MAX_WORD_LEN];
    StackItem item = pop_item(vm);
    if (item.type == TYPE_WORD) {
        strcpy(buffer, item.value.word);
        return buffer;
    }
    fprintf(stderr, "[ERROR] Expected word, got type %d\n", item.type);
    return "";
}

void print_stack_item(StackItem item) {
    switch(item.type) {
        case TYPE_NUMBER: printf("%ld", item.value.number); break;
        case TYPE_CHAR: printf("'%c'", item.value.character); break;
        case TYPE_STRING: printf("\"%s\"", item.value.string); break;
        case TYPE_WORD: printf(":%s", item.value.word); break;
        case TYPE_INSTRUCTION:
            printf("[%c", item.value.instruction.cmd);
            if (item.value.instruction.arg) printf(" %d", item.value.instruction.arg);
            if (item.value.instruction.sub_arg) printf("(%d)", item.value.instruction.sub_arg);
            printf("]");
            break;
    }
}

void print_stack(AtomVM *vm) {
    printf("Stack [%d]: ", vm->sp + 1);
    for (int i = 0; i <= vm->sp && i < 10; i++) {
        print_stack_item(vm->stack[i]);
        printf(" ");
    }
    if (vm->sp >= 10) printf("...");
    printf("\n");
}

uint32_t read_port(AtomVM *vm, int port) {
    if (port >= 0 && port < MAX_PORTS) {
        switch(port) {
            case 0x60: return 0x1F;
            case 0x64: return 0x00;
            case 0x3F8: return 'A';
            case 0x3FD: return 0x20;
            case 0x378: return 0xFF;
            default: return vm->port_values[port];
        }
    }
    return 0;
}

void write_port(AtomVM *vm, int port, uint32_t value) {
    if (port >= 0 && port < MAX_PORTS) {
        vm->port_values[port] = value;
        switch(port) {
            case 0x3F8:
            case 0x378:
                if (value < 128) putchar((char)value);
                break;
        }
    }
}

long pack_data(long a, long b) {
    return (a << 16) | (b & 0xFFFF);
}

void unpack_data(long val, long *a, long *b) {
    *a = (val >> 16) & 0xFFFF;
    *b = val & 0xFFFF;
}

void save_context(AtomVM *vm, int context_id) {
    if (context_id < MAX_CONTEXTS) {
        vm->context_sp[context_id] = vm->sp;
        for (int i = 0; i <= vm->sp && i < STACK_SIZE; i++) {
            vm->saved_context[context_id][i] = vm->stack[i];
        }
    }
}

void restore_context(AtomVM *vm, int context_id) {
    if (context_id < MAX_CONTEXTS) {
        vm->sp = vm->context_sp[context_id];
        for (int i = 0; i <= vm->sp && i < STACK_SIZE; i++) {
            vm->stack[i] = vm->saved_context[context_id][i];
        }
    }
}

void load_raw_bytes(AtomVM *vm) {
    if (current_mh.raw_bytes_count > 0 && current_mh.target_address < MEMORY_SIZE) {
        int addr = current_mh.target_address;
        for (int i = 0; i < current_mh.raw_bytes_count && addr + i < MEMORY_SIZE; i++) {
            vm->memory[addr + i] = current_mh.raw_bytes[i];
        }
    }
}

void load_stack_context(AtomVM *vm) {
    for (int i = 0; i < current_mh.stack_size && i < STACK_SIZE; i++) {
        push_item(vm, current_mh.stack_context[i]);
    }
}

bool define_word(AtomVM *vm, const char *name, int start, int end) {
    if (vm->word_count >= MAX_WORDS) {
        fprintf(stderr, "[ERROR] Too many words!\n");
        return false;
    }
    strncpy(vm->words[vm->word_count].name, name, MAX_WORD_LEN - 1);
    vm->words[vm->word_count].start_pc = start;
    vm->words[vm->word_count].end_pc = end;
    vm->words[vm->word_count].defined = true;
    vm->word_count++;
    return true;
}

int find_word(AtomVM *vm, const char *name) {
    for (int i = 0; i < vm->word_count; i++) {
        if (strcmp(vm->words[i].name, name) == 0 && vm->words[i].defined) {
            return i;
        }
    }
    return -1;
}
