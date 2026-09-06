#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdbool.h>
#include <stdint.h>

#define STACK_SIZE 256
#define MAX_CODE_LEN 4096
#define MAX_LINE_LEN 512
#define MEMORY_SIZE 8192
#define MAX_FILES 16
#define MAX_LOOP_STACK 64

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
} MhContainer;

typedef struct {
    char name[64];
    FILE *fp;
    bool active;
} VirtualFile;

typedef struct {
    long stack[STACK_SIZE];
    int sp;                            // Stack pointer
    unsigned char memory[MEMORY_SIZE]; 
    uint8_t r7;                       
    VirtualFile files[MAX_FILES];     
    long heap_pointer;                
    bool running;
    bool condition_flag[16];          
    int cond_sp;
    int loop_stack[MAX_LOOP_STACK];    // Стек для циклов
    int loop_sp;                       // Указатель стека циклов
    int line_number;                   // Номер текущей строки
} AtomVM;

AtomInstruction program[MAX_CODE_LEN];
int program_length = 0;
MhContainer current_mh;

void vm_init(AtomVM *vm) {
    vm->sp = -1;
    vm->r7 = 0;
    vm->running = true;
    vm->heap_pointer = 0x1000; 
    vm->cond_sp = 0;
    vm->loop_sp = -1;
    vm->line_number = 0;
    memset(vm->stack, 0, sizeof(vm->stack));
    memset(vm->memory, 0, sizeof(vm->memory));
    memset(vm->condition_flag, 0, sizeof(vm->condition_flag));
    memset(vm->loop_stack, 0, sizeof(vm->loop_stack));
    
    for(int i = 0; i < MAX_FILES; i++) {
        vm->files[i].active = false;
        vm->files[i].fp = NULL;
    }
}

void push(AtomVM *vm, long val) {
    if (vm->sp < STACK_SIZE - 1) {
        vm->stack[++vm->sp] = val;
    } else {
        fprintf(stderr, "[FATAL ERROR] Stack overflow at line %d!\n", vm->line_number);
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

bool parse_atom_system(const char *filename) {
    FILE *file = fopen(filename, "r");
    if (!file) {
        fprintf(stderr, "[ERROR] Cannot open system file/container: %s\n", filename);
        return false;
    }

    char line[MAX_LINE_LEN];
    int line_num = 0;
    
    while (fgets(line, sizeof(line), file)) {
        line_num++;
        
        // Пропускаємо коментарі
        char *comment = strchr(line, ';');
        if (comment) *comment = '\0';

        char *ptr = line;
        while (*ptr && isspace((unsigned char)*ptr)) ptr++;
        if (!*ptr) continue;

        // 1. Парсинг заголовків лінкування (.mh)
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
            // Адаптивний адресний блок з ануляцією для Tiny RAM
            if (MEMORY_SIZE < 1024 && addr > 512) {
                current_mh.target_address = 3; 
                current_mh.tiny_ram_fallback = true;
                printf("[MH] Tiny RAM fallback activated: address %d -> 3\n", addr);
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

        // Парсинг инструкций
        while (*ptr) {
            while (*ptr && (isspace((unsigned char)*ptr) || *ptr == '\r' || *ptr == '\n')) ptr++;
            if (!*ptr) break;

            // Пропускаем метки (число с двоеточием)
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

                // Парсинг аргумента
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

                // Парсинг sub_arg для H9
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
    
    // Загружаем сырые байты в память, если они есть
    if (current_mh.raw_bytes_count > 0) {
        printf("[MH] Loading %d raw bytes to address %d\n", 
               current_mh.raw_bytes_count, current_mh.target_address);
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

void vm_execute(AtomVM *vm) {
    int pc = 0;
    
    // Загружаем сырые байты в память
    load_raw_bytes(vm);
    
    while (pc < program_length && vm->running) {
        AtomInstruction inst = program[pc];
        vm->line_number = pc + 1;

        switch (inst.cmd) {
            case 'A': { // Allocate: Виділення динамічної пам'яті на купі
                long size = pop(vm);
                long allocated_addr = vm->heap_pointer;
                vm->heap_pointer += size;
                if (vm->heap_pointer >= MEMORY_SIZE) {
                    fprintf(stderr, "[FATAL] Out of RAM memory during allocation at line %d!\n", vm->line_number);
                    vm->running = false;
                } else {
                    push(vm, allocated_addr);
                }
                break;
            }

            case 'B': { // Branch / Conditions
                if (inst.arg == 1) { // B1: If
                    long condition = pop(vm);
                    if (vm->cond_sp < 15) {
                        vm->condition_flag[++vm->cond_sp] = (condition != 0);
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
                        vm->condition_flag[vm->cond_sp] = (condition != 0);
                    }
                }
                break;
            }

            case 'C': { // Compare
                long b = pop(vm);
                long a = pop(vm);
                push(vm, (a == b) ? 1 : 0);
                break;
            }

            case 'D': { // Data
                push(vm, inst.arg);
                break;
            }

            case 'E': { // Execute
                long target_pc = pop(vm);
                if (target_pc >= 0 && target_pc < program_length) {
                    pc = (int)target_pc - 1;
                }
                break;
            }

            case 'F': { // File operations
                if (inst.arg == 1) { // F1: Open file
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
                                fd = i;
                            }
                            break;
                        }
                    }
                    push(vm, fd);
                } else if (inst.arg == 2) { // F2: Read byte
                    int fd = (int)pop(vm);
                    if (fd >= 0 && fd < MAX_FILES && vm->files[fd].active && vm->files[fd].fp) {
                        int ch = fgetc(vm->files[fd].fp);
                        push(vm, ch != EOF ? ch : -1);
                    } else {
                        push(vm, -1);
                    }
                } else if (inst.arg == 3) { // F3: Write data
                    long val = pop(vm);
                    int fd = (int)pop(vm);
                    if (fd >= 0 && fd < MAX_FILES && vm->files[fd].active && vm->files[fd].fp) {
                        fputc((char)val, vm->files[fd].fp);
                    }
                } else if (inst.arg == 4) { // F4: Close file
                    int fd = (int)pop(vm);
                    if (fd >= 0 && fd < MAX_FILES && vm->files[fd].active) {
                        if (vm->files[fd].fp) {
                            fclose(vm->files[fd].fp);
                        }
                        vm->files[fd].active = false;
                    }
                }
                break;
            }

            case 'G': { // Get
                push(vm, 0); 
                break;
            }

            case 'H': { // Hardware
                if (inst.arg == 1) printf("[HAL H1] SATA Controller active. Sector mapped.\n");
                else if (inst.arg == 2) printf("[HAL H2] COM-port (UART) transmitting stream.\n");
                else if (inst.arg == 3) printf("[HAL H3] USB 1.1-3.1 Controller initialized.\n");
                else if (inst.arg == 4) printf("[HAL H4] VGA display mode & palette configured.\n");
                else if (inst.arg == 5) printf("[HAL H5] PS/2 Keyboard/Mouse controller polled.\n");
                else if (inst.arg == 6) printf("[HAL H6] HDMI digital stream active.\n");
                else if (inst.arg == 7) printf("[HAL H7] RJ-45 Ethernet controller up.\n");
                else if (inst.arg == 8) printf("[HAL H8] Audio Jack chip initialized.\n");
                else if (inst.arg == 9) {
                    if (inst.sub_arg == 1) printf("[HAL H9(1)] Legacy RJ9 port accessed.\n");
                    else if (inst.sub_arg == 2) printf("[HAL H9(2)] Legacy RJ11 phone line accessed.\n");
                    else if (inst.sub_arg == 3) printf("[HAL H9(3)] Legacy RJ14 port accessed.\n");
                    else if (inst.sub_arg == 4) printf("[HAL H9(4)] Legacy RJ25 port accessed.\n");
                    else printf("[HAL H9(%d)] Unknown legacy RJ port.\n", inst.sub_arg);
                }
                break;
            }

            case 'I': { // Input
                if (inst.arg == 1) {
                    long val;
                    printf("INPUT: ");
                    scanf("%ld", &val);
                    push(vm, val);
                } else if (inst.arg == 2) {
                    vm->memory[2] = (unsigned char)pop(vm);
                } else if (inst.arg == 3) {
                    push(vm, vm->r7); 
                }
                break;
            }

            case 'J': { // Jump
                if (inst.has_arg) {
                    pc = inst.arg - 1;
                }
                break;
            }

            case 'K': { // Kernel
                printf("[KERNEL INTERRUPT] Micro-OS core function called at line %d.\n", vm->line_number);
                break;
            }

            case 'L': { // Loop
                if (inst.arg == 1) { // L1: Start loop
                    if (vm->loop_sp < MAX_LOOP_STACK - 1) {
                        vm->loop_stack[++vm->loop_sp] = pc;
                    }
                } else if (inst.arg == 2) { // L2: End loop
                    if (vm->loop_sp >= 0) {
                        int loop_start = vm->loop_stack[vm->loop_sp];
                        // Проверяем условие выхода (верх стека)
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

            case 'M': { // Memory
                if (inst.has_arg) {
                    // Запись по адресу с аргументом
                    long addr = inst.arg;
                    if (addr >= 0 && addr < MEMORY_SIZE) {
                        long val = pop(vm);
                        vm->memory[addr] = (unsigned char)val;
                    }
                } else {
                    // Чтение по адресу со стека
                    long addr = pop(vm);
                    if (addr >= 0 && addr < MEMORY_SIZE) {
                        push(vm, vm->memory[addr]);
                    }
                }
                break;
            }

            case 'N': { // Next (Increment)
                if (vm->sp >= 0) {
                    vm->stack[vm->sp]++;
                }
                break;
            }

            case 'O': { // Output
                if (vm->sp >= 0) {
                    printf("%ld\n", vm->stack[vm->sp]);
                }
                break;
            }

            case 'P': { // Push (Duplicate)
                if (vm->sp >= 0) {
                    push(vm, vm->stack[vm->sp]);
                }
                break;
            }

            case 'Q': { // Quit
                vm->running = false;
                printf("[SYS] Atom execution terminated securely at line %d.\n", vm->line_number);
                break;
            }

            case 'R': { // Register
                vm->r7 = (uint8_t)pop(vm);
                break;
            }

            case 'S': { // Setup/Store
                // Инициализация системных регистров
                break;
            }

            case 'T': { // Transform
                if (inst.arg == 1) { // T1: Addition
                    long b = pop(vm); long a = pop(vm);
                    push(vm, a + b);
                } else if (inst.arg == 2) { // T2: Subtraction
                    long b = pop(vm); long a = pop(vm);
                    push(vm, a - b);
                } else if (inst.arg == 3) { // T3: Multiplication
                    long b = pop(vm); long a = pop(vm);
                    push(vm, a * b);
                } else if (inst.arg == 4) { // T4: Division
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

            case 'U': { // Unpack/Pack
                if (inst.arg == 1) { // U1: Pack
                    // Упаковка данных
                } else if (inst.arg == 2) { // U2: Unpack
                    // Распаковка данных
                }
                break;
            }

            case 'V': { // Vector
                // Настройка векторов прерываний
                printf("[VECTOR] Hardware interrupt vector configured.\n");
                break;
            }

            case 'W': { // Wait
                if (inst.has_arg) {
                    // Задержка на inst.arg тактов
                    for (int i = 0; i < inst.arg; i++) {
                        // Пустой цикл для задержки
                    }
                }
                break;
            }

            case 'X': { // XOR/Logic
                if (inst.arg == 1) { // X1: AND
                    long b = pop(vm); long a = pop(vm);
                    push(vm, a & b);
                } else if (inst.arg == 2) { // X2: OR
                    long b = pop(vm); long a = pop(vm);
                    push(vm, a | b);
                } else if (inst.arg == 3) { // X3: XOR
                    long b = pop(vm); long a = pop(vm);
                    push(vm, a ^ b);
                }
                break;
            }

            case 'Y': { // Yield
                // Передача управления другому процессу
                printf("[YIELD] Process yielded control.\n");
                break;
            }

            case 'Z': { // Zero
                vm->sp = -1;
                break;
            }

            default: {
                fprintf(stderr, "[WARNING] Unknown instruction '%c' at line %d\n", inst.cmd, vm->line_number);
                break;
            }
        }
        pc++;
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
        
        printf("\n[VM] Starting bare-metal execution loop...\n");
        printf("───────────────────────────────────────────────\n\n");
        
        vm_execute(&vm);
        
        printf("\n───────────────────────────────────────────────\n");
        printf("[VM] Execution finished. Stack size: %d\n", vm.sp + 1);
        if (vm.sp >= 0) {
            printf("[VM] Top of stack: %ld\n", vm.stack[vm.sp]);
        }
    } else {
        printf("[ERROR] Failed to parse system file.\n");
        return 1;
    }

    return 0;
}

// TIP - Beter use one Repo with Tags like v1.0 v1.1 v1.2
