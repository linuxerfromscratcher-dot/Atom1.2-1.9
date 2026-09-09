#include "atom.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

// ============================================

typedef enum {
    FILE_MODE_READ = 0,
    FILE_MODE_WRITE = 1,
    FILE_MODE_APPEND = 2,
    FILE_MODE_READ_WRITE = 3,
    FILE_MODE_READ_WRITE_CREATE = 4,
    FILE_MODE_APPEND_READ = 5
} FileMode;

static bool file_exists(const char *filename) {
    if (!filename || strlen(filename) == 0) return false;
    FILE *f = fopen(filename, "r");
    if (f) {
        fclose(f);
        return true;
    }
    return false;
}

static long file_size(const char *filename) {
    if (!filename) return -1;
    FILE *f = fopen(filename, "rb");
    if (!f) return -1;
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fclose(f);
    return size;
}

static void print_file_error(const char *msg, const char *file) {
    fprintf(stderr, "[FILE ERROR] %s: '%s'\n", msg, file ? file : "(null)");
}


static void vm_delay(long ms) {
    if (ms <= 0) ms = 10;
    #ifdef _WIN32
    Sleep((DWORD)ms);
    #else
    usleep((useconds_t)(ms * 1000L));
    #endif
}


static void execute_loop(AtomVM *vm, int pc, int arg) {
    if (arg == 1) { 
        if (vm->loop_sp < 0 || vm->loop_stack[vm->loop_sp] != pc) {
            if (vm->loop_sp < MAX_LOOP_STACK - 1) {
                vm->loop_stack[++vm->loop_sp] = pc;
            } else {
                fprintf(stderr, "[ERROR] Loop stack overflow!\n");
                vm->running = false;
            }
        }
    } else if (arg == 2) { 
        if (vm->loop_sp >= 0) {
            long condition = pop_number(vm);
            int loop_start = vm->loop_stack[vm->loop_sp];

            if (condition != 0) {
                vm->pc = loop_start;
            } else {
                vm->loop_sp--;
            }
        } else {
            fprintf(stderr, "[ERROR] Loop stack underflow!\n");
            vm->running = false;
        }
    }
}


static void execute_file(AtomVM *vm, int arg) {
    switch(arg) {
        case 1: { 
            StackItem name_item = pop_item(vm);
            char filename[256];
            filename[0] = '\0';

            if (name_item.type == TYPE_STRING) {
                strncpy(filename, name_item.value.string, sizeof(filename) - 1);
                filename[sizeof(filename) - 1] = '\0';
            } else if (name_item.type == TYPE_NUMBER) {
                long name_addr = name_item.value.number;
                if (name_addr < 0 || name_addr >= MEMORY_SIZE) {
                    print_file_error("Invalid filename address", NULL);
                    push_number(vm, -1);
                    break;
                }
                const char *src = (const char*)&vm->memory[name_addr];
                strncpy(filename, src, sizeof(filename) - 1);
                filename[sizeof(filename) - 1] = '\0';
            } else {
                print_file_error("Expected string or address for filename", NULL);
                push_number(vm, -1);
                break;
            }

            if (strlen(filename) == 0) {
                print_file_error("Empty filename", filename);
                push_number(vm, -1);
                break;
            }

            bool exists = file_exists(filename);
            long size = exists ? file_size(filename) : 0;

            int fd = -1;
            for (int i = 0; i < MAX_FILES; i++) {
                if (!vm->files[i].active) {
                    fd = i;
                    break;
                }
            }

            if (fd == -1) {
                print_file_error("No free file descriptors", filename);
                push_number(vm, -1);
                break;
            }

            const char *mode = exists ? "r+" : "w+";
            vm->files[fd].fp = fopen(filename, mode);

            if (vm->files[fd].fp) {
                vm->files[fd].active = true;
                strncpy(vm->files[fd].name, filename, 63);
                vm->files[fd].name[63] = '\0';
                vm->files[fd].position = 0;

                printf("[FILE] Opened: '%s' (fd=%d, mode=%s, size=%ld bytes)\n", 
                       filename, fd, mode, size);
                push_number(vm, fd);
            } else {
                print_file_error("Cannot open", filename);
                push_number(vm, -1);
            }
            break;
        }
        
        case 2: { 
            int fd = (int)pop_number(vm);
            
            if (fd < 0 || fd >= MAX_FILES || !vm->files[fd].active || !vm->files[fd].fp) {
                printf("[FILE] ERROR: Invalid fd=%d\n", fd);
                push_number(vm, -1);
                break;
            }
            
            int ch = fgetc(vm->files[fd].fp);
            if (ch != EOF) {
                vm->files[fd].position++;
                push_number(vm, ch);
                printf("[FILE] Read fd=%d: '%c' (0x%02X) at pos %ld\n", 
                       fd, ch, ch, vm->files[fd].position);
            } else {
                push_number(vm, -1);
                printf("[FILE] EOF fd=%d at pos %ld\n", fd, vm->files[fd].position);
            }
            break;
        }
        
        case 3: { 
            long val = pop_number(vm);
            int fd = (int)pop_number(vm);
            
            if (fd < 0 || fd >= MAX_FILES || !vm->files[fd].active || !vm->files[fd].fp) {
                printf("[FILE] ERROR: Invalid fd=%d\n", fd);
                break;
            }
            
            int result = fputc((char)val, vm->files[fd].fp);
            if (result != EOF) {
                vm->files[fd].position++;
                printf("[FILE] Write fd=%d: '%c' (0x%02X) at pos %ld\n", 
                       fd, (char)val, (int)val, vm->files[fd].position);
                fflush(vm->files[fd].fp);
            } else {
                printf("[FILE] ERROR: Write failed fd=%d\n", fd);
            }
            break;
        }
        
        case 4: { 
            int fd = (int)pop_number(vm);
            
            if (fd < 0 || fd >= MAX_FILES || !vm->files[fd].active) {
                printf("[FILE] ERROR: Invalid fd=%d\n", fd);
                break;
            }
            
            if (vm->files[fd].fp) {
                fclose(vm->files[fd].fp);
                vm->files[fd].fp = NULL;
                printf("[FILE] Closed fd=%d: '%s'\n", fd, vm->files[fd].name);
            }
            
            vm->files[fd].active = false;
            vm->files[fd].position = 0;
            memset(vm->files[fd].name, 0, 64);
            break;
        }
        
        default: {
            printf("[FILE] ERROR: Unknown subcommand F%d\n", arg);
            break;
        }
    }
}


static int find_endif(AtomVM *vm, int pc) {
    int depth = 1;
    int temp_pc = pc + 1;
    while (temp_pc < program_length && depth > 0) {
        if (program[temp_pc].cmd == 'B' && program[temp_pc].arg == 1) depth++;
        else if (program[temp_pc].cmd == 'B' && program[temp_pc].arg == 3) {
            depth--;
            if (depth == 0) return temp_pc;
        }
        temp_pc++;
    }
    return -1;
}

static int find_alternative(AtomVM *vm, int pc) {
    int depth = 1;
    int temp_pc = pc + 1;
    while (temp_pc < program_length && depth > 0) {
        if (program[temp_pc].cmd == 'B' && program[temp_pc].arg == 1) depth++;
        else if (program[temp_pc].cmd == 'B' && program[temp_pc].arg == 3) {
            depth--;
            if (depth == 0) return temp_pc;
        } else if (depth == 1 &&
                   ((program[temp_pc].cmd == 'B' && program[temp_pc].arg == 2) ||
                    (program[temp_pc].cmd == 'B' && program[temp_pc].arg == 4))) {
            return temp_pc;
        }
        temp_pc++;
    }
    return -1;
}

static void execute_branch(AtomVM *vm, int pc, int arg) {
    switch(arg) {
        case 1: { 
            long condition = pop_number(vm);
            if (vm->cond_sp < 15) {
                vm->condition_flag[++vm->cond_sp] = (condition != 0);
            }

            if (!vm->condition_flag[vm->cond_sp]) {
                int jump = find_alternative(vm, pc);
                if (jump >= 0) vm->pc = jump;
            }
            break;
        }

        case 2: { 
            if (vm->cond_sp >= 0) {
                if (vm->condition_flag[vm->cond_sp]) {
                    int jump = find_endif(vm, pc);
                    if (jump >= 0) vm->pc = jump;
                } else {
                    vm->condition_flag[vm->cond_sp] = true;
                }
            }
            break;
        }

        case 3: { 
            if (vm->cond_sp > 0) {
                vm->cond_sp--;
            }
            break;
        }

        case 4: { 
            if (vm->cond_sp >= 0) {
                long condition = pop_number(vm);
                if (vm->condition_flag[vm->cond_sp]) {
                    int jump = find_alternative(vm, pc);
                    if (jump >= 0) vm->pc = jump;
                } else {
                    vm->condition_flag[vm->cond_sp] = (condition != 0);
                    if (!vm->condition_flag[vm->cond_sp]) {
                        int jump = find_alternative(vm, pc);
                        if (jump >= 0) vm->pc = jump;
                    }
                }
            }
            break;
        }
    }
}


void vm_execute(AtomVM *vm) {
    vm->pc = 0;
    vm->execution_steps = 0;
    
    load_raw_bytes(vm);
    load_stack_context(vm);
    
    printf("Program instructions: %d\n", program_length);
    for (int i = 0; i < program_length && i < 30; i++) {
        printf("  [%d] %c", i, program[i].cmd);
        if (program[i].has_arg) printf(" %d", program[i].arg);
        if (program[i].sub_arg) printf("(%d)", program[i].sub_arg);
        printf("\n");
    }
    if (program_length > 30) printf("  ...\n");
    printf("\n");
    
    while (vm->pc < program_length && vm->running) {
        vm->execution_steps++;
        
        if (vm->execution_steps > MAX_EXECUTION_STEPS) {
            fprintf(stderr, "[FATAL] Execution limit exceeded (%d steps)!\n", 
                    MAX_EXECUTION_STEPS);
            vm->running = false;
            break;
        }
        
        int current_pc = vm->pc;
        AtomInstruction inst = program[vm->pc];
        vm->line_number = vm->pc + 1;
        
        switch (inst.cmd) {
            case 'A': { 
                long size = pop_number(vm);
                long addr = vm->heap_pointer;
                vm->heap_pointer += size;
                if (vm->heap_pointer >= MEMORY_SIZE) {
                    fprintf(stderr, "[FATAL] Out of RAM at line %d!\n", vm->line_number);
                    vm->running = false;
                } else {
                    push_number(vm, addr);
                }
                break;
            }
            
            case 'B': { 
                execute_branch(vm, vm->pc, inst.arg);
                break;
            }
            
            case 'C': { 
                long b = pop_number(vm);
                long a = pop_number(vm);
                push_number(vm, (a == b) ? 1 : 0);
                break;
            }
            
            case 'D': { 
                push_number(vm, inst.arg);
                break;
            }
            
            case 'E': { 
                long target = pop_number(vm);
                if (target >= 0 && target < program_length) {
                    vm->pc = (int)target - 1;
                }
                break;
            }
            
            case 'F': { 
                execute_file(vm, inst.arg);
                break;
            }
            
            case 'G': { 
                if (inst.has_arg) {
                    push_number(vm, read_port(vm, inst.arg));
                } else {
                    push_number(vm, vm->r7);
                }
                break;
            }
            
            case 'H': { 
                if (inst.arg == 1) {
                    printf("[HAL H1] SATA active. Sector %d.\n", current_mh.sector_mapping);
                } else if (inst.arg == 2) {
                    printf("[HAL H2] COM-port (UART) active.\n");
                } else if (inst.arg == 3) {
                    printf("[HAL H3] USB controller active.\n");
                } else if (inst.arg == 4) {
                    printf("[HAL H4] VGA configured.\n");
                } else if (inst.arg == 5) {
                    printf("[HAL H5] PS/2 controller active.\n");
                } else if (inst.arg == 6) {
                    printf("[HAL H6] HDMI active.\n");
                } else if (inst.arg == 7) {
                    printf("[HAL H7] Ethernet active.\n");
                } else if (inst.arg == 8) {
                    printf("[HAL H8] Audio Jack active.\n");
                } else if (inst.arg == 9) {
                    if (inst.sub_arg == 1) printf("[HAL H9(1)] RJ9.\n");
                    else if (inst.sub_arg == 2) printf("[HAL H9(2)] RJ11.\n");
                    else if (inst.sub_arg == 3) printf("[HAL H9(3)] RJ14.\n");
                    else if (inst.sub_arg == 4) printf("[HAL H9(4)] RJ25.\n");
                    else printf("[HAL H9(%d)] Unknown.\n", inst.sub_arg);
                }
                break;
            }
            
            case 'I': { 
                if (inst.arg == 1) {
                    long addr = pop_number(vm);
                    if (addr >= 0 && addr < MEMORY_SIZE) {
                        char buffer[256];
                        printf("INPUT: ");
                        fgets(buffer, sizeof(buffer), stdin);
                        size_t len = strlen(buffer);
                        if (len > 0 && buffer[len-1] == '\n') buffer[len-1] = '\0';
                        strncpy((char*)&vm->memory[addr], buffer, 255);
                        push_number(vm, addr);
                    }
                } else if (inst.arg == 2) {
                    long val = pop_number(vm);
                    if (val >= 0 && val < 256) {
                        vm->memory[2] = (unsigned char)val;
                    }
                } else if (inst.arg == 3) {
                    vm->r7 = (uint8_t)(pop_number(vm) & 0xFF);
                }
                break;
            }
            
            case 'J': { 
                if (inst.has_arg) {
                    vm->pc = inst.arg - 1;
                }
                break;
            }
            
            case 'K': { 
                if (inst.has_arg) {
                    printf("[KERNEL] Syscall %d at line %d.\n", inst.arg, vm->line_number);
                } else {
                    printf("[KERNEL] Interrupt at line %d.\n", vm->line_number);
                }
                break;
            }
            
            case 'L': { 
                execute_loop(vm, vm->pc, inst.arg);
                break;
            }
            
            case 'M': { 
                long addr = inst.has_arg ? (long)inst.arg : pop_number(vm);
                if (addr >= 0 && addr < MEMORY_SIZE) {
                    if (inst.has_arg) {
                        long val = pop_number(vm);
                        vm->memory[addr] = (unsigned char)val;
                        printf("[MEM] Write %ld to 0x%04lX\n", val, addr);
                    } else {
                        push_number(vm, vm->memory[addr]);
                        printf("[MEM] Read 0x%04lX = %d\n", addr, vm->memory[addr]);
                    }
                }
                break;
            }
            
            case 'N': { 
                if (vm->sp >= 0 && vm->stack[vm->sp].type == TYPE_NUMBER) {
                    vm->stack[vm->sp].value.number++;
                }
                break;
            }
            
            case 'O': { 
                if (vm->sp >= 0) {
                    StackItem item = vm->stack[vm->sp];
                    
                    if (item.type == TYPE_NUMBER && item.value.number >= 0 && 
                        item.value.number < MEMORY_SIZE) {
                        char *ptr = (char*)&vm->memory[item.value.number];
                        if (ptr[0] == '/') {
                            ptr++;
                            while (*ptr && *ptr != '/') {
                                putchar(*ptr++);
                            }
                            putchar('\n');
                            pop_item(vm);
                            break;
                        }
                    }
                    
                    switch(item.type) {
                        case TYPE_NUMBER:
                            printf("%ld\n", item.value.number);
                            break;
                        case TYPE_CHAR:
                            printf("%c\n", item.value.character);
                            break;
                        case TYPE_STRING:
                            printf("%s\n", item.value.string);
                            break;
                        case TYPE_WORD:
                            printf(":%s\n", item.value.word);
                            break;
                        default:
                            printf("Unknown type\n");
                    }
                    pop_item(vm);
                }
                break;
            }
            
            case 'P': { 
                if (vm->sp >= 0) {
                    push_item(vm, vm->stack[vm->sp]);
                }
                break;
            }
            
            case 'Q': { 
                vm->running = false;
                printf("[SYS] Terminated at line %d.\n", vm->line_number);
                break;
            }
            
            case 'R': { 
                if (inst.has_arg) {
                    long val = pop_number(vm);
                    if (inst.arg == 0) vm->r0 = (uint8_t)val;
                    else if (inst.arg == 1) vm->r1 = (uint8_t)val;
                    else if (inst.arg == 2) vm->r2 = (uint8_t)val;
                    else if (inst.arg == 3) vm->r3 = (uint8_t)val;
                    else if (inst.arg == 4) vm->r4 = (uint8_t)val;
                    else if (inst.arg == 5) vm->r5 = (uint8_t)val;
                    else if (inst.arg == 6) vm->r6 = (uint8_t)val;
                    else if (inst.arg == 7) vm->r7 = (uint8_t)val;
                } else {
                    push_number(vm, vm->r7);
                }
                break;
            }
            
            case 'S': { 
                if (inst.arg == 1) {
                    vm->heap_pointer = 0x1000;
                    vm->r0 = vm->r1 = vm->r2 = vm->r3 = 0;
                    vm->r4 = vm->r5 = vm->r6 = vm->r7 = 0;
                    printf("[SETUP] System initialized.\n");
                } else if (inst.arg == 2) {
                    long addr = pop_number(vm);
                    if (addr >= 0 && addr < MEMORY_SIZE - 64) {
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
                        push_number(vm, addr);
                        printf("[STORE] Saved state to 0x%04lX\n", addr);
                    }
                } else if (inst.arg == 3) {
                    long addr = pop_number(vm);
                    if (addr >= 0 && addr < MEMORY_SIZE - 64) {
                        for (int i = 0; i < 16; i++) {
                            if (vm->sp < STACK_SIZE - 1) {
                                push_number(vm, vm->memory[addr + i]);
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
                        printf("[RESTORE] Restored state from 0x%04lX\n", addr);
                    }
                }
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
                    long b = pop_number(vm);
                    long a = pop_number(vm);
                    push_number(vm, pack_data(a, b));
                } else if (inst.arg == 2) {
                    long val = pop_number(vm);
                    long a, b;
                    unpack_data(val, &a, &b);
                    push_number(vm, a);
                    push_number(vm, b);
                }
                break;
            }
            
            case 'V': { 
                if (inst.has_arg) {
                    long handler = pop_number(vm);
                    printf("[VECTOR] Int %d -> %ld\n", inst.arg, handler);
                    if (handler >= 0 && handler < MEMORY_SIZE) {
                        int vec = inst.arg * 4;
                        if (vec < MEMORY_SIZE - 4) {
                            vm->memory[vec] = (handler >> 24) & 0xFF;
                            vm->memory[vec + 1] = (handler >> 16) & 0xFF;
                            vm->memory[vec + 2] = (handler >> 8) & 0xFF;
                            vm->memory[vec + 3] = handler & 0xFF;
                        }
                    }
                } else {
                    printf("[VECTOR] Configured.\n");
                }
                break;
            }
            
            case 'W': { 
                long ms = inst.has_arg ? inst.arg : 10;
                printf("[WAIT] %ld ms\n", ms);
                vm_delay(ms);
                break;
            }
            
            case 'X': { 
                if (inst.arg == 1) {
                    long b = pop_number(vm), a = pop_number(vm);
                    push_number(vm, a & b);
                } else if (inst.arg == 2) {
                    long b = pop_number(vm), a = pop_number(vm);
                    push_number(vm, a | b);
                } else if (inst.arg == 3) {
                    long b = pop_number(vm), a = pop_number(vm);
                    push_number(vm, a ^ b);
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
                    printf("[YIELD] Yield at line %d.\n", vm->line_number);
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
        
        if (vm->pc == current_pc) {
            vm->pc++;
        } else {
            if (vm->pc < 0 || vm->pc >= program_length) {
                fprintf(stderr, "[ERROR] Invalid PC: %d\n", vm->pc);
                vm->running = false;
            }
        }
    }
    
    if (vm->execution_steps >= MAX_EXECUTION_STEPS) {
        printf("\n[WARNING] Execution stopped due to step limit.\n");
    }
}
