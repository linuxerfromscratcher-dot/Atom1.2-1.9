#include "atom.h"

static char s_self_dir[512];

static StackItem make_item(StackType type) {
    StackItem item;
    memset(&item, 0, sizeof(item));
    item.type = type;
    return item;
}

void set_self_dir(const char *path) {
    const char *slash = NULL;
    if (path) {
        slash = strrchr(path, '/');
        if (!slash) slash = strrchr(path, '\\');
    }
    if (!slash) {
        snprintf(s_self_dir, sizeof(s_self_dir), ".");
        return;
    }
    size_t len = (size_t)(slash - path);
    if (len == 0) len = 1;
    if (len >= sizeof(s_self_dir)) len = sizeof(s_self_dir) - 1;
    memcpy(s_self_dir, path, len);
    s_self_dir[len] = '\0';
}

const char *get_self_dir(void) {
    return s_self_dir;
}

void vm_init(AtomVM *vm, const char *self_path) {
    vm->sp = -1;
    vm->r0 = vm->r1 = vm->r2 = vm->r3 = 0;
    vm->r4 = vm->r5 = vm->r6 = vm->r7 = 0;
    vm->running = true;
    vm->heap_pointer = 0x1000;
    vm->pack_shadow_pointer = 0;
    vm->pack_shadow_used = 0;
    vm->condition_flag = false;
    vm->call_sp = -1;
    vm->line_number = 0;
    vm->pc_origin = 0;
    vm->current_context = 0;
    vm->execution_steps = 0;
    vm->pc = 0;
    vm->trace = false;

    memset(vm->stack, 0, sizeof(vm->stack));
    memset(vm->memory, 0, sizeof(vm->memory));
    memset(vm->vector_labels, -1, sizeof(vm->vector_labels));
    memset(vm->pack_shadow, 0, sizeof(vm->pack_shadow));
    memset(vm->call_stack, 0, sizeof(vm->call_stack));
    memset(vm->port_values, 0, sizeof(vm->port_values));
    memset(vm->context_sp, 0, sizeof(vm->context_sp));
    memset(vm->saved_context, 0, sizeof(vm->saved_context));

    for (int i = 0; i < MAX_FILES; i++) {
        vm->files[i].active = false;
        vm->files[i].fp = NULL;
        vm->files[i].position = 0;
        memset(vm->files[i].name, 0, sizeof(vm->files[i].name));
    }

    if (self_path) set_program_path(self_path);

    hal_init(vm);
    ffi_init(&vm->ffi);
    jit_init(vm);
}

void vm_cleanup(AtomVM *vm) {
    for (int i = 0; i < MAX_FILES; i++) {
        if (vm->files[i].fp) {
            fclose(vm->files[i].fp);
            vm->files[i].fp = NULL;
            vm->files[i].active = false;
        }
    }
    ffi_cleanup(&vm->ffi);
    jit_cleanup(vm);
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
    }
    fprintf(stderr, "[FATAL] Stack underflow at line %d!\n", vm->line_number);
    vm->running = false;
    return make_item(TYPE_NUMBER);
}

void push_number(AtomVM *vm, long val) {
    StackItem item = make_item(TYPE_NUMBER);
    item.value.number = val;
    push_item(vm, item);
}

void push_char(AtomVM *vm, char c) {
    StackItem item = make_item(TYPE_CHAR);
    item.value.character = c;
    push_item(vm, item);
}

void push_string(AtomVM *vm, const char *str) {
    StackItem item = make_item(TYPE_STRING);
    snprintf(item.value.string, sizeof(item.value.string), "%s", str ? str : "");
    push_item(vm, item);
}

void push_word(AtomVM *vm, const char *word) {
    StackItem item = make_item(TYPE_WORD);
    snprintf(item.value.word, sizeof(item.value.word), "%s", word ? word : "");
    push_item(vm, item);
}

long pop_number(AtomVM *vm) {
    StackItem item = pop_item(vm);
    switch (item.type) {
        case TYPE_NUMBER: return item.value.number;
        case TYPE_CHAR: return (long)item.value.character;
        default: break;
    }
    fprintf(stderr, "[ERROR] Expected number at line %d, got type %d\n",
            vm->line_number, (int)item.type);
    return 0;
}

char pop_char(AtomVM *vm) {
    StackItem item = pop_item(vm);
    if (item.type == TYPE_CHAR) return item.value.character;
    if (item.type == TYPE_NUMBER) return (char)item.value.number;
    fprintf(stderr, "[ERROR] Expected char at line %d, got type %d\n",
            vm->line_number, (int)item.type);
    return 0;
}

const char *pop_string(AtomVM *vm) {
    static char buffer[256];
    StackItem item = pop_item(vm);
    if (item.type == TYPE_STRING) {
        snprintf(buffer, sizeof(buffer), "%s", item.value.string);
        return buffer;
    }
    if (item.type == TYPE_WORD) {
        snprintf(buffer, sizeof(buffer), "%s", item.value.word);
        return buffer;
    }
    if (item.type == TYPE_NUMBER) {
        snprintf(buffer, sizeof(buffer), "%ld", item.value.number);
        return buffer;
    }
    fprintf(stderr, "[ERROR] Expected string at line %d, got type %d\n",
            vm->line_number, (int)item.type);
    return "";
}

const char *pop_word(AtomVM *vm) {
    static char buffer[MAX_WORD_LEN];
    StackItem item = pop_item(vm);
    if (item.type == TYPE_WORD) {
        snprintf(buffer, sizeof(buffer), "%s", item.value.word);
        return buffer;
    }
    if (item.type == TYPE_STRING) {
        snprintf(buffer, sizeof(buffer), "%s", item.value.string);
        return buffer;
    }
    fprintf(stderr, "[ERROR] Expected word at line %d, got type %d\n",
            vm->line_number, (int)item.type);
    return "";
}

void print_stack_item(StackItem item) {
    switch (item.type) {
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

void save_context(AtomVM *vm, int context_id) {
    if (context_id < 0 || context_id >= MAX_CONTEXTS) return;
    vm->context_sp[context_id] = vm->sp;
    for (int i = 0; i <= vm->sp && i < STACK_SIZE; i++) {
        vm->saved_context[context_id][i] = vm->stack[i];
    }
}

void restore_context(AtomVM *vm, int context_id) {
    if (context_id < 0 || context_id >= MAX_CONTEXTS) return;
    vm->sp = vm->context_sp[context_id];
    for (int i = 0; i <= vm->sp && i < STACK_SIZE; i++) {
        vm->stack[i] = vm->saved_context[context_id][i];
    }
}

void load_raw_bytes(AtomVM *vm) {
    if (current_mh.raw_bytes_count > 0 && current_mh.target_address < MEMORY_SIZE) {
        int addr = (int)current_mh.target_address;
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

int item_compare(const StackItem *left, const StackItem *right, int *order) {
    if (left->type == TYPE_STRING && right->type == TYPE_STRING) {
        int cmp = strcmp(left->value.string, right->value.string);
        *order = (cmp > 0) - (cmp < 0);
        return 0;
    }
    if (left->type == TYPE_WORD && right->type == TYPE_WORD) {
        int cmp = strcmp(left->value.word, right->value.word);
        *order = (cmp > 0) - (cmp < 0);
        return 0;
    }
    if ((left->type == TYPE_NUMBER || left->type == TYPE_CHAR) &&
        (right->type == TYPE_NUMBER || right->type == TYPE_CHAR)) {
        long a = (left->type == TYPE_NUMBER) ? left->value.number : (long)left->value.character;
        long b = (right->type == TYPE_NUMBER) ? right->value.number : (long)right->value.character;
        *order = (a > b) - (a < b);
        return 0;
    }
    *order = (int)left->type - (int)right->type;
    return 0;
}

void pack_write_record(AtomVM *vm, long addr, long cluster, long size) {
    unsigned char *rec = &vm->memory[addr];
    rec[0] = 0xF3;
    rec[1] = 0x0A;
    rec[2] = (unsigned char)(cluster & 0xFF);
    rec[3] = (unsigned char)((cluster >> 8) & 0xFF);
    rec[4] = (unsigned char)(size & 0xFF);
    rec[5] = (unsigned char)((size >> 8) & 0xFF);
    rec[6] = (unsigned char)((size >> 16) & 0xFF);
    rec[7] = (unsigned char)((size >> 24) & 0xFF);
}

bool pack_read_record(AtomVM *vm, long addr, long *cluster, long *size) {
    const unsigned char *rec = &vm->memory[addr];
    if (rec[0] != 0xF3 || rec[1] != 0x0A) return false;

    *cluster = (long)rec[2] | ((long)rec[3] << 8);
    *size = (long)rec[4] | ((long)rec[5] << 8) |
            ((long)rec[6] << 16) | ((long)rec[7] << 24);

    if (vm->pack_shadow_used + PACK_RECORD_SIZE > PACK_SHADOW_SIZE) {
        vm->pack_shadow_used = 0;
        printf("[PACK] shadow buffer recycled\n");
    }
    memcpy(&vm->pack_shadow[vm->pack_shadow_used], rec, PACK_RECORD_SIZE);
    vm->pack_shadow_pointer += PACK_RECORD_SIZE;
    vm->pack_shadow_used += PACK_RECORD_SIZE;
    return true;
}
