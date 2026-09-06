#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdbool.h>
#include <stdint.h>
#include <time.h>

#define STACK_SIZE 256
#define MAX_CODE_LEN 4096
#define MAX_LINE_LEN 512
#define MEMORY_SIZE 8192
#define MAX_FILES 16
#define MAX_LOOP_STACK 64
#define MAX_CONTEXTS 16
#define MAX_PORTS 256
#define MAX_EXECUTION_STEPS 1000000  // Защита от бесконечных циклов

typedef struct {
    char cmd;          // A - Z
    int arg;         
    int sub_arg;      
    bool has_arg;
} AtomInstruction;

typedef struct {
    char container_name[64];
    int sector_mapping;
    unsigned int target_address;
    bool tiny_ram_fallback;
    unsigned char raw_bytes[256];
    int raw_bytes_count;
    long stack_context[STACK_SIZE];
    int stack_size;
} MhContainer;

typedef struct {
    char name[64];
    FILE *fp;
    bool active;
    long position;
} VirtualFile;

typedef struct {
    long stack[STACK_SIZE];
    int sp;                            
    unsigned char memory[MEMORY_SIZE]; 
    uint8_t r7;                       
    VirtualFile files[MAX_FILES];     
    long heap_pointer;                
    bool running;
    bool condition_flag[16];          
    int cond_sp;
    int loop_stack[MAX_LOOP_STACK];
    int loop_sp;
    int line_number;
    int execution_steps;              // Счетчик шагов для защиты от бесконечных циклов
    
    uint8_t r0, r1, r2, r3, r4, r5, r6;
    uint32_t port_values[MAX_PORTS];
    long saved_context[MAX_CONTEXTS][STACK_SIZE];
    int context_sp[MAX_CONTEXTS];
    int current_context;
} AtomVM;

AtomInstruction program[MAX_CODE_LEN];
int program_length = 0;
MhContainer current_mh;

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
    
    memset(vm->stack, 0, sizeof(vm->stack));
    memset(vm->memory, 0, sizeof(vm->memory));
    memset(vm->condition_flag, 0, sizeof(vm->condition_flag));
    memset(vm->loop_stack, 0, sizeof(vm->loop_stack));
    memset(vm->port_values, 0, sizeof(vm->port_values));
    memset(vm->context_sp, 0, sizeof(vm->context_sp));
    memset(vm->saved_context, 0, sizeof(vm->saved_context));
    
    for(int i = 0; i < MAX_FILES; i++) {
        vm->files[i].active = false;
        vm->files[i].fp = NULL;
        vm->files[i].position = 0;
    }
}

void push(AtomVM *vm, long val) {
    if (vm->sp < STACK_SIZE - 1) {
        vm->stack[++vm->sp] = val;
    } else {
        fprintf(stderr, "[FATAL ERROR] Stack overflow at line %d! Stack size: %d\n", 
                vm->line_number, vm->sp + 1);
        // Выводим содержимое стека для отладки
        fprintf(stderr, "Stack dump (last 10 entries):\n");
        int start = (vm->sp - 9 > 0) ? vm->sp - 9 : 0;
        for (int i = start; i <= vm->sp; i++) {
            fprintf(stderr, "  [%d] %ld\n", i, vm->stack[i]);
        }
        vm->running = false;
    }
}

long pop(AtomVM *vm) {
    if (vm->sp >= 0) {
        return vm->stack[vm->sp--];
    } else {
        fprintf(stderr, "[FATAL ERROR] Stack underflow at line %d!\n", vm->line_number);
        vm->running = false;
        return 0;
    }
}

long peek(AtomVM *vm) {
    if (vm->sp >= 0) {
        return vm->stack[vm->sp];
    }
    return 0;
}

void print_stack(AtomVM *vm) {
    printf("Stack [%d]: ", vm->sp + 1);
    for (int i = 0; i <= vm->sp && i < 10; i++) {
        printf("%ld ", vm->stack[i]);
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
                if (value < 128) {
                    putchar((char)value);
                }
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

bool parse_atom_system(const char *filename) {
    FILE *file = fopen(filename, "r");
    if (!file) {
        fprintf(stderr, "[ERROR] Cannot open system file/container: %s\n", filename);
        return false;
    }

    char line[MAX_LINE_LEN];
    int line_num = 0;
    
    current_mh.stack_size = 0;
    memset(current_mh.stack_context, 0, sizeof(current_mh.stack_context));
    memset(current_mh.raw_bytes, 0, sizeof(current_mh.raw_bytes));
    current_mh.raw_bytes_count = 0;
    current_mh.sector_mapping = 0;
    current_mh.target_address = 0;
    current_mh.tiny_ram_fallback = false;
    strcpy(current_mh.container_name, "");
    
    while (fgets(line, sizeof(line), file)) {
        line_num++;
        
        char *comment = strchr(line, ';');
        if (comment) *comment = '\0';

        char *ptr = line;
        while (*ptr && isspace((unsigned char)*ptr)) ptr++;
        if (!*ptr) continue;

        if (strncmp(ptr, "F/", 2) == 0) {
            char name[64];
            if (sscanf(ptr, "F/%[^/]/", name) == 1) {
                strncpy(current_mh.container_name, name, 63);
                printf("[MH] Container: %s\n", current_mh.container_name);
            }
            continue;
        }
        
        if (*ptr == 'S') {
            current_mh.sector_mapping = atoi(ptr + 1);
            printf("[MH] Sector mapping: %d\n", current_mh.sector_mapping);
            continue;
        }
        
        if (*ptr == 'M') {
            unsigned int addr = atoi(ptr + 1);
            if (MEMORY_SIZE < 1024 && addr > 512) {
                current_mh.target_address = 3; 
                current_mh.tiny_ram_fallback = true;
                printf("[MH] Tiny RAM fallback: %d -> 3\n", addr);
            } else {
                current_mh.target_address = addr;
                current_mh.tiny_ram_fallback = false;
                printf("[MH] Target address: %d\n", addr);
            }
            continue;
        }
        
        if (strncmp(ptr, "0x", 2) == 0) {
            unsigned int byte_val;
            if (sscanf(ptr, "%x", &byte_val) == 1) {
                if (current_mh.raw_bytes_count < 256) {
                    current_mh.raw_bytes[current_mh.raw_bytes_count++] = (unsigned char)byte_val;
                }
            }
            continue;
        }
        
        if (isdigit((unsigned char)*ptr) || *ptr == '-') {
            while (*ptr) {
                while (*ptr && isspace((unsigned char)*ptr)) ptr++;
                if (!*ptr) break;
                
                if (isdigit((unsigned char)*ptr) || *ptr == '-') {
                    long val = atol(ptr);
                    if (current_mh.stack_size < STACK_SIZE) {
                        current_mh.stack_context[current_mh.stack_size++] = val;
                    }
                    while (isdigit((unsigned char)*ptr) || *ptr == '-') ptr++;
                }
            }
            continue;
        }

        while (*ptr) {
            while (*ptr && (isspace((unsigned char)*ptr) || *ptr == '\r' || *ptr == '\n')) ptr++;
            if (!*ptr) break;

            if (isdigit((unsigned char)*ptr)) {
                while (*ptr && *ptr != ':') ptr++;
                if (*ptr == ':') ptr++;
                continue;
            }

            if (isalpha((unsigned char)*ptr)) {
                char cmd = toupper((unsigned char)*ptr);
                ptr++;

                int arg = 0;
                int sub_arg = 0;
                bool has_arg = false;

                if (isdigit((unsigned char)*ptr) || *ptr == '(' || *ptr == '-') {
                    if (*ptr == '(') {
                        ptr++;
                        arg = atoi(ptr);
                        while (*ptr && *ptr != ')') ptr++;
                        if (*ptr == ')') ptr++;
                    } else {
                        arg = atoi(ptr);
                        while (isdigit((unsigned char)*ptr) || *ptr == '-') ptr++;
                    }
                    has_arg = true;
                }

                if (cmd == 'H' && arg == 9 && *ptr == '(') {
                    ptr++;
                    sub_arg = atoi(ptr);
                    while (*ptr && *ptr != ')') ptr++;
                    if (*ptr == ')') ptr++;
                }

                if (program_length < MAX_CODE_LEN) {
                    program[program_length++] = (AtomInstruction){cmd, arg, sub_arg, has_arg};
                } else {
                    fprintf(stderr, "[ERROR] Program too long! Max %d instructions.\n", MAX_CODE_LEN);
                    fclose(file);
                    return false;
                }
            } else {
                ptr++;
            }
        }
    }

    fclose(file);
    
    if (current_mh.raw_bytes_count > 0) {
        printf("[MH] Loading %d raw bytes to address %d\n", 
               current_mh.raw_bytes_count, current_mh.target_address);
    }
    
    if (current_mh.stack_size > 0) {
        printf("[MH] Loading stack context with %d values\n", current_mh.stack_size);
    }
    
    return true;
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
        push(vm, current_mh.stack_context[i]);
    }
}

void vm_execute(AtomVM *vm) {
    int pc = 0;
    bool skip_execution = false;  // Флаг для пропуска выполнения при ложном условии
    int condition_depth = 0;      // Глубина вложенности условий
    
    load_raw_bytes(vm);
    load_stack_context(vm);
    
    printf("Program instructions: %d\n", program_length);
    for (int i = 0; i < program_length && i < 20; i++) {
        printf("  [%d] %c", i, program[i].cmd);
        if (program[i].has_arg) printf(" %d", program[i].arg);
        if (program[i].sub_arg) printf("(%d)", program[i].sub_arg);
        printf("\n");
    }
    printf("\n");
    
    while (pc < program_length && vm->running) {
        vm->execution_steps++;
        
        // Защита от бесконечных циклов
        if (vm->execution_steps > MAX_EXECUTION_STEPS) {
            fprintf(stderr, "[FATAL ERROR] Execution limit exceeded (%d steps)! Possible infinite loop.\n", 
                    MAX_EXECUTION_STEPS);
            vm->running = false;
            break;
        }
        
        AtomInstruction inst = program[pc];
        vm->line_number = pc + 1;

        // Отладка (можно закомментировать)
        // printf("[%4d] %c", vm->line_number, inst.cmd);
        // if (inst.has_arg) printf(" %d", inst.arg);
        // if (inst.sub_arg) printf("(%d)", inst.sub_arg);
        // printf(" | Stack: ");
        // print_stack(vm);

        switch (inst.cmd) {
            case 'A': {
                long size = pop(vm);
                long allocated_addr = vm->heap_pointer;
                vm->heap_pointer += size;
                if (vm->heap_pointer >= MEMORY_SIZE) {
                    fprintf(stderr, "[FATAL] Out of RAM at line %d!\n", vm->line_number);
                    vm->running = false;
                } else {
                    push(vm, allocated_addr);
                }
                break;
            }

            case 'B': {
                if (inst.arg == 1) { // B1: If
                    long condition = pop(vm);
                    if (vm->cond_sp < 15) {
                        vm->condition_flag[++vm->cond_sp] = (condition != 0);
                    }
                    // Если условие ложно, пропускаем выполнение до B3
                    if (!vm->condition_flag[vm->cond_sp]) {
                        // Ищем соответствующий B3
                        int depth = 1;
                        int temp_pc = pc + 1;
                        while (temp_pc < program_length && depth > 0) {
                            if (program[temp_pc].cmd == 'B' && program[temp_pc].arg == 1) depth++;
                            else if (program[temp_pc].cmd == 'B' && program[temp_pc].arg == 3) depth--;
                            temp_pc++;
                        }
                        if (depth == 0) {
                            pc = temp_pc - 1;
                        }
                    }
                } else if (inst.arg == 2) { // B2: Else
                    if (vm->cond_sp >= 0) {
                        vm->condition_flag[vm->cond_sp] = !vm->condition_flag[vm->cond_sp];
                    }
                } else if (inst.arg == 3) { // B3: End if
                    if (vm->cond_sp > 0) vm->cond_sp--;
                } else if (inst.arg == 4) { // B4: Elseif
                    if (vm->cond_sp >= 0) {
                        long condition = pop(vm);
                        if (!vm->condition_flag[vm->cond_sp]) {
                            vm->condition_flag[vm->cond_sp] = (condition != 0);
                        }
                    }
                }
                break;
            }

            case 'C': {
                long b = pop(vm);
                long a = pop(vm);
                push(vm, (a == b) ? 1 : 0);
                break;
            }

            case 'D': {
                push(vm, inst.arg);
                break;
            }

            case 'E': {
                long target_pc = pop(vm);
                if (target_pc >= 0 && target_pc < program_length) {
                    pc = (int)target_pc - 1;
                }
                break;
            }

            case 'F': {
                if (inst.arg == 1) {
                    long name_addr = pop(vm);
                    char *filename = (char*)&vm->memory[name_addr];
                    int fd = -1;
                    for (int i = 0; i < MAX_FILES; i++) {
                        if (!vm->files[i].active) {
                            vm->files[i].fp = fopen(filename, "r+");
                            if (!vm->files[i].fp) vm->files[i].fp = fopen(filename, "w+");
                            if (vm->files[i].fp) {
                                vm->files[i].active = true;
                                strncpy(vm->files[i].name, filename, 63);
                                vm->files[i].position = 0;
                                fd = i;
                            }
                            break;
                        }
                    }
                    push(vm, fd);
                } else if (inst.arg == 2) {
                    int fd = (int)pop(vm);
                    if (fd >= 0 && fd < MAX_FILES && vm->files[fd].active && vm->files[fd].fp) {
                        int ch = fgetc(vm->files[fd].fp);
                        if (ch != EOF) {
                            vm->files[fd].position++;
                        }
                        push(vm, ch != EOF ? ch : -1);
                    } else {
                        push(vm, -1);
                    }
                } else if (inst.arg == 3) {
                    long val = pop(vm);
                    int fd = (int)pop(vm);
                    if (fd >= 0 && fd < MAX_FILES && vm->files[fd].active && vm->files[fd].fp) {
                        fputc((char)val, vm->files[fd].fp);
                        vm->files[fd].position++;
                    }
                } else if (inst.arg == 4) {
                    int fd = (int)pop(vm);
                    if (fd >= 0 && fd < MAX_FILES && vm->files[fd].active) {
                        if (vm->files[fd].fp) {
                            fclose(vm->files[fd].fp);
                        }
                        vm->files[fd].active = false;
                        vm->files[fd].position = 0;
                    }
                }
                break;
            }

            case 'G': {
                if (inst.has_arg) {
                    uint32_t val = read_port(vm, inst.arg);
                    push(vm, val);
                } else {
                    push(vm, vm->r7);
                }
                break;
            }

            case 'H': {
                if (inst.arg == 1) {
                    printf("[HAL H1] SATA Controller active. Sector %d mapped.\n", 
                           current_mh.sector_mapping);
                } else if (inst.arg == 2) {
                    printf("[HAL H2] COM-port (UART) transmitting stream.\n");
                } else if (inst.arg == 3) {
                    printf("[HAL H3] USB 1.1-3.1 Controller initialized.\n");
                } else if (inst.arg == 4) {
                    printf("[HAL H4] VGA display mode & palette configured.\n");
                } else if (inst.arg == 5) {
                    printf("[HAL H5] PS/2 Keyboard/Mouse controller polled.\n");
                } else if (inst.arg == 6) {
                    printf("[HAL H6] HDMI digital stream active.\n");
                } else if (inst.arg == 7) {
                    printf("[HAL H7] RJ-45 Ethernet controller up.\n");
                } else if (inst.arg == 8) {
                    printf("[HAL H8] Audio Jack chip initialized.\n");
                } else if (inst.arg == 9) {
                    if (inst.sub_arg == 1) printf("[HAL H9(1)] Legacy RJ9 port accessed.\n");
                    else if (inst.sub_arg == 2) printf("[HAL H9(2)] Legacy RJ11 phone line accessed.\n");
                    else if (inst.sub_arg == 3) printf("[HAL H9(3)] Legacy RJ14 port accessed.\n");
                    else if (inst.sub_arg == 4) printf("[HAL H9(4)] Legacy RJ25 port accessed.\n");
                    else printf("[HAL H9(%d)] Unknown legacy RJ port.\n", inst.sub_arg);
                }
                break;
            }

            case 'I': {
                if (inst.arg == 1) {
                    long addr = pop(vm);
                    if (addr >= 0 && addr < MEMORY_SIZE) {
                        char buffer[256];
                        printf("INPUT: ");
                        fgets(buffer, sizeof(buffer), stdin);
                        size_t len = strlen(buffer);
                        if (len > 0 && buffer[len-1] == '\n') buffer[len-1] = '\0';
                        strncpy((char*)&vm->memory[addr], buffer, 255);
                        push(vm, addr);
                    }
                } else if (inst.arg == 2) {
                    long val = pop(vm);
                    if (val >= 0 && val < 256) {
                        vm->memory[2] = (unsigned char)val;
                    }
                } else if (inst.arg == 3) {
                    long val = pop(vm);
                    vm->r7 = (uint8_t)(val & 0xFF);
                }
                break;
            }

            case 'J': {
                if (inst.has_arg) {
                    pc = inst.arg - 1;
                }
                break;
            }

            case 'K': {
                if (inst.has_arg) {
                    printf("[KERNEL] System call %d at line %d.\n", inst.arg, vm->line_number);
                } else {
                    printf("[KERNEL INTERRUPT] Micro-OS core function called at line %d.\n", 
                           vm->line_number);
                }
                break;
            }

            case 'L': {
                if (inst.arg == 1) {
                    if (vm->loop_sp < MAX_LOOP_STACK - 1) {
                        vm->loop_stack[++vm->loop_sp] = pc;
                    }
                } else if (inst.arg == 2) {
                    if (vm->loop_sp >= 0) {
                        int loop_start = vm->loop_stack[vm->loop_sp];
                        long condition = pop(vm);
                        if (condition != 0) {
                            pc = loop_start;
                        } else {
                            vm->loop_sp--;
                        }
                    }
                }
                break;
            }

            case 'M': {
                if (inst.has_arg) {
                    long addr = inst.arg;
                    if (addr >= 0 && addr < MEMORY_SIZE) {
                        long val = pop(vm);
                        vm->memory[addr] = (unsigned char)val;
                    }
                } else {
                    long addr = pop(vm);
                    if (addr >= 0 && addr < MEMORY_SIZE) {
                        if (vm->sp >= 0) {
                            long val = pop(vm);
                            vm->memory[addr] = (unsigned char)val;
                            push(vm, addr);
                        } else {
                            push(vm, vm->memory[addr]);
                        }
                    }
                }
                break;
            }

            case 'N': {
                if (vm->sp >= 0) {
                    vm->stack[vm->sp]++;
                }
                break;
            }

            case 'O': {
                if (vm->sp >= 0) {
                    long val = vm->stack[vm->sp];
                    
                    if (val >= 0 && val < MEMORY_SIZE) {
                        char *ptr = (char*)&vm->memory[val];
                        if (ptr[0] == '/') {
                            ptr++;
                            while (*ptr && *ptr != '/') {
                                putchar(*ptr++);
                            }
                            putchar('\n');
                            pop(vm);
                            break;
                        }
                    }
                    
                    if (val >= 0 && val <= 255 && isprint((int)val)) {
                        printf("%c (ASCII: %ld)\n", (char)val, val);
                    } else {
                        printf("%ld\n", val);
                    }
                    pop(vm);
                }
                break;
            }

            case 'P': {
                if (vm->sp >= 0) {
                    push(vm, vm->stack[vm->sp]);
                }
                break;
            }

            case 'Q': {
                vm->running = false;
                printf("[SYS] Atom execution terminated at line %d.\n", vm->line_number);
                break;
            }

            case 'R': {
                if (inst.has_arg) {
                    long val = pop(vm);
                    if (inst.arg == 1) vm->r1 = (uint8_t)val;
                    else if (inst.arg == 2) vm->r2 = (uint8_t)val;
                    else if (inst.arg == 3) vm->r3 = (uint8_t)val;
                    else if (inst.arg == 4) vm->r4 = (uint8_t)val;
                    else if (inst.arg == 5) vm->r5 = (uint8_t)val;
                    else if (inst.arg == 6) vm->r6 = (uint8_t)val;
                    else if (inst.arg == 7) vm->r7 = (uint8_t)val;
                    else if (inst.arg == 0) vm->r0 = (uint8_t)val;
                } else {
                    push(vm, vm->r7);
                }
                break;
            }

            case 'S': {
                if (inst.arg == 1) {
                    vm->heap_pointer = 0x1000;
                    vm->r0 = vm->r1 = vm->r2 = vm->r3 = 0;
                    vm->r4 = vm->r5 = vm->r6 = vm->r7 = 0;
                    printf("[SETUP] System environment initialized.\n");
                } else if (inst.arg == 2) {
                    long addr = pop(vm);
                    if (addr >= 0 && addr < MEMORY_SIZE - 64) {
                        int count = (vm->sp + 1 < 16) ? vm->sp + 1 : 16;
                        for (int i = 0; i < count; i++) {
                            vm->memory[addr + i] = (unsigned char)(vm->stack[i] & 0xFF);
                        }
                        vm->memory[addr + 16] = vm->r0;
                        vm->memory[addr + 17] = vm->r1;
                        vm->memory[addr + 18] = vm->r2;
                        vm->memory[addr + 19] = vm->r3;
                        vm->memory[addr + 20] = vm->r4;
                        vm->memory[addr + 21] = vm->r5;
                        vm->memory[addr + 22] = vm->r6;
                        vm->memory[addr + 23] = vm->r7;
                        push(vm, addr);
                    }
                } else if (inst.arg == 3) {
                    long addr = pop(vm);
                    if (addr >= 0 && addr < MEMORY_SIZE - 64) {
                        int count = 16;
                        for (int i = 0; i < count; i++) {
                            if (vm->sp < STACK_SIZE - 1) {
                                vm->stack[++vm->sp] = vm->memory[addr + i];
                            }
                        }
                        vm->r0 = vm->memory[addr + 16];
                        vm->r1 = vm->memory[addr + 17];
                        vm->r2 = vm->memory[addr + 18];
                        vm->r3 = vm->memory[addr + 19];
                        vm->r4 = vm->memory[addr + 20];
                        vm->r5 = vm->memory[addr + 21];
                        vm->r6 = vm->memory[addr + 22];
                        vm->r7 = vm->memory[addr + 23];
                    }
                }
                break;
            }

            case 'T': {
                if (inst.arg == 1) {
                    long b = pop(vm); long a = pop(vm);
                    push(vm, a + b);
                } else if (inst.arg == 2) {
                    long b = pop(vm); long a = pop(vm);
                    push(vm, a - b);
                } else if (inst.arg == 3) {
                    long b = pop(vm); long a = pop(vm);
                    push(vm, a * b);
                } else if (inst.arg == 4) {
                    long b = pop(vm); long a = pop(vm);
                    if (b != 0) {
                        push(vm, a / b);
                    } else {
                        fprintf(stderr, "[ERROR] Division by zero at line %d!\n", vm->line_number);
                        push(vm, 0);
                    }
                }
                break;
            }

            case 'U': {
                if (inst.arg == 1) {
                    long b = pop(vm);
                    long a = pop(vm);
                    push(vm, pack_data(a, b));
                } else if (inst.arg == 2) {
                    long val = pop(vm);
                    long a, b;
                    unpack_data(val, &a, &b);
                    push(vm, a);
                    push(vm, b);
                }
                break;
            }

            case 'V': {
                if (inst.has_arg) {
                    long handler_addr = pop(vm);
                    printf("[VECTOR] Interrupt %d handler set to address %ld\n", 
                           inst.arg, handler_addr);
                    if (handler_addr >= 0 && handler_addr < MEMORY_SIZE) {
                        int vector_addr = inst.arg * 4;
                        if (vector_addr < MEMORY_SIZE - 4) {
                            vm->memory[vector_addr] = (handler_addr >> 24) & 0xFF;
                            vm->memory[vector_addr + 1] = (handler_addr >> 16) & 0xFF;
                            vm->memory[vector_addr + 2] = (handler_addr >> 8) & 0xFF;
                            vm->memory[vector_addr + 3] = handler_addr & 0xFF;
                        }
                    }
                } else {
                    printf("[VECTOR] Hardware interrupt vector configured.\n");
                }
                break;
            }

            case 'W': {
                if (inst.has_arg) {
                    for (int i = 0; i < inst.arg; i++) {
                        // Пустой цикл
                    }
                } else {
                    for (volatile int i = 0; i < 1000; i++);
                }
                break;
            }

            case 'X': {
                if (inst.arg == 1) {
                    long b = pop(vm); long a = pop(vm);
                    push(vm, a & b);
                } else if (inst.arg == 2) {
                    long b = pop(vm); long a = pop(vm);
                    push(vm, a | b);
                } else if (inst.arg == 3) {
                    long b = pop(vm); long a = pop(vm);
                    push(vm, a ^ b);
                }
                break;
            }

            case 'Y': {
                if (inst.has_arg) {
                    save_context(vm, vm->current_context);
                    vm->current_context = (vm->current_context + 1) % MAX_CONTEXTS;
                    restore_context(vm, vm->current_context);
                    printf("[YIELD] Switched to context %d\n", vm->current_context);
                } else {
                    printf("[YIELD] Process yielded control at line %d.\n", vm->line_number);
                }
                break;
            }

            case 'Z': {
                vm->sp = -1;
                break;
            }

            default: {
                fprintf(stderr, "[WARNING] Unknown instruction '%c' at line %d\n", 
                        inst.cmd, vm->line_number);
                break;
            }
        }
        pc++;
    }
    
    if (vm->execution_steps >= MAX_EXECUTION_STEPS) {
        printf("\n[WARNING] Execution stopped due to step limit.\n");
    }
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        printf("Usage: %s <system.mh>\n", argv[0]);
        printf("Example: %s program.mh\n", argv[0]);
        return 1;
    }

    AtomVM vm;
    vm_init(&vm);

    printf("═══════════════════════════════════════════════════\n");
    printf("[ATOM SYSTEM] Initializing container & parsing .mh\n");
    printf("File: %s\n", argv[1]);
    printf("═══════════════════════════════════════════════════\n\n");
    
    if (parse_atom_system(argv[1])) {
        printf("\n[ATOM SYSTEM] Loaded instructions: %d\n", program_length);
        printf("Container: %s | Target sector: %d\n", 
               current_mh.container_name, current_mh.sector_mapping);
        
        if (current_mh.raw_bytes_count > 0) {
            printf("Raw bytes loaded: %d\n", current_mh.raw_bytes_count);
        }
        if (current_mh.stack_size > 0) {
            printf("Stack context loaded: %d values\n", current_mh.stack_size);
        }
        
        printf("\n[VM] Starting bare-metal execution loop...\n");
        printf("───────────────────────────────────────────────\n\n");
        
        vm_execute(&vm);
        
        printf("\n───────────────────────────────────────────────\n");
        printf("[VM] Execution finished.\n");
        printf("Steps executed: %d\n", vm.execution_steps);
        printf("Stack size: %d\n", vm.sp + 1);
        if (vm.sp >= 0) {
            printf("Top of stack: %ld\n", vm.stack[vm.sp]);
            printf("Stack dump:\n");
            for (int i = 0; i <= vm.sp && i < 20; i++) {
                printf("  [%d] %ld\n", i, vm.stack[i]);
            }
            if (vm.sp > 19) printf("  ...\n");
        }
        printf("Registers: R0=%d R1=%d R2=%d R3=%d R4=%d R5=%d R6=%d R7=%d\n",
               vm.r0, vm.r1, vm.r2, vm.r3, vm.r4, vm.r5, vm.r6, vm.r7);
    } else {
        printf("[ERROR] Failed to parse system file.\n");
        return 1;
    }

    return 0;
}
