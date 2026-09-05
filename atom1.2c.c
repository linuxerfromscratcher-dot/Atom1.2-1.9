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

typedef struct {
    char cmd;          // A - Z
    int arg;           // Числовий модифікатор (напр., 3 у H3 або B1)
    int sub_arg;       // Длякласних легасі портів на кшталт H9(2)
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
    unsigned char memory[MEMORY_SIZE]; // Системна пам'ять / купа
    uint8_t r7;                        // Апаратний регістр для I3
    VirtualFile files[MAX_FILES];      // Дескриптори файлів
    long heap_pointer;                 // Вказівник для виділення пам'яті через A
    bool running;
    bool condition_flag[16];           // Стековий індикатор умов (B1-B4)
    int cond_sp;
} AtomVM;

AtomInstruction program[MAX_CODE_LEN];
int program_length = 0;
MhContainer current_mh;

void vm_init(AtomVM *vm) {
    vm->sp = -1;
    vm->r7 = 0;
    vm->running = true;
    vm->heap_pointer = 0x1000; // Початок купи в пам'яті
    vm->cond_sp = 0;
    memset(vm->stack, 0, sizeof(vm->stack));
    memset(vm->memory, 0, sizeof(vm->memory));
    memset(vm->condition_flag, 0, sizeof(vm->condition_flag));
    
    for(int i = 0; i < MAX_FILES; i++) {
        vm->files[i].active = false;
        vm->files[i].fp = NULL;
    }
}

void push(AtomVM *vm, long val) {
    if (vm->sp < STACK_SIZE - 1) {
        vm->stack[++vm->sp] = val;
    } else {
        fprintf(stderr, "[FATAL ERROR] Stack overflow!\n");
        vm->running = false;
    }
}

long pop(AtomVM *vm) {
    if (vm->sp >= 0) {
        return vm->stack[vm->sp--];
    } else {
        fprintf(stderr, "[FATAL ERROR] Stack underflow!\n");
        vm->running = false;
        return 0;
    }
}

bool parse_atom_system(const char *filename) {
    FILE *file = fopen(filename, "r");
    if (!file) {
        fprintf(stderr, "[ERROR] Cannot open system file/container: %s\n", filename);
        return false;
    }

    char line[MAX_LINE_LEN];
    while (fgets(line, sizeof(line), file)) {
        // Пропускаємо коментарі
        char *comment = strchr(line, ';');
        if (comment) *comment = '\0';

        char *ptr = line;
        while (*ptr && isspace((unsigned char)*ptr)) ptr++;
        if (!*ptr) continue;

        // 1. Парсинг заголовків лінкування (.mh)
        if (strncmp(ptr, "F/", 2) == 0) {
            sscanf(ptr, "F/%[^/]/", current_mh.container_name);
            continue;
        }
        if (*ptr == 'S') {
            current_mh.sector_mapping = atoi(ptr + 1);
            continue;
        }
        if (*ptr == 'M') {
            unsigned int addr = atoi(ptr + 1);
            // Адаптивний адресний блок з ануляцією для Tiny RAM
            if (MEMORY_SIZE < 1024 && addr > 512) {
                current_mh.target_address = 3; 
                current_mh.tiny_ram_fallback = true;
            } else {
                current_mh.target_address = addr;
                current_mh.tiny_ram_fallback = false;
            }
            continue;
        }
        if (strncmp(ptr, "0x", 2) == 0) {
            unsigned int byte_val;
            sscanf(ptr, "%x", &byte_val);
            if (current_mh.raw_bytes_count < 256) {
                current_mh.raw_bytes[current_mh.raw_bytes_count++] = (unsigned char)byte_val;
            }
            continue;
        }

        while (*ptr) {
            while (*ptr && (isspace((unsigned char)*ptr) || *ptr == '\r' || *ptr == '\n')) ptr++;
            if (!*ptr) break;

            // Пропуск міток типу "10:"
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

                if (isdigit((unsigned char)*ptr) || *ptr == '(') {
                    if (*ptr == '(') {
                        ptr++;
                        arg = atoi(ptr);
                        while (*ptr && *ptr != ')') ptr++;
                        if (*ptr == ')') ptr++;
                    } else {
                        arg = atoi(ptr);
                        while (isdigit((unsigned char)*ptr)) ptr++;
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
                }
            } else {
                ptr++;
            }
        }
    }

    fclose(file);
    return true;
}

void vm_execute(AtomVM *vm) {
    int pc = 0;
    while (pc < program_length && vm->running) {
        AtomInstruction inst = program[pc];

        switch (inst.cmd) {
            case 'A': // Allocate: Виділення динамічної пам'яті на купі
                {
                    long size = pop(vm);
                    long allocated_addr = vm->heap_pointer;
                    vm->heap_pointer += size;
                    if (vm->heap_pointer >= MEMORY_SIZE) {
                        fprintf(stderr, "[FATAL] Out of RAM memory during allocation!\n");
                        vm->running = false;
                    } else {
                        push(vm, allocated_addr);
                    }
                }
                break;

            case 'B': // Branch / Умови (B1-B4)
                if (inst.arg == 1) { // B1: Початок умови (if)
                    long condition = pop(vm);
                    vm->condition_flag[++vm->cond_sp] = (condition != 0);
                } else if (inst.arg == 2) { // B2: Else
                    if (vm->cond_sp > 0) {
                        vm->condition_flag[vm->cond_sp] = !vm->condition_flag[vm->cond_sp];
                    }
                } else if (inst.arg == 3) { // B3: End if
                    if (vm->cond_sp > 0) vm->cond_sp--;
                } else if (inst.arg == 4) { // B4: Elseif
                    // Перемикання альтернативної перевірки потоку
                }
                break;

            case 'C':
                {
                    long b = pop(vm);
                    long a = pop(vm);
                    push(vm, (a == b) ? 1 : 0);
                }
                break;

            case 'D':
                push(vm, inst.arg);
                break;

            case 'E':
                {
                    long target_pc = pop(vm);
                    if (target_pc >= 0 && target_pc < program_length) {
                        pc = (int)target_pc - 1; // Зсув з урахуванням інкременту в кінці циклу
                    }
                }
                break;

            case 'F': 
                if (inst.arg == 1) { 
                    long name_addr = pop(vm);
                    char *filename = (char*)&vm->memory[name_addr];
                    int fd = -1;
                    for (int i = 0; i < MAX_FILES; i++) {
                        if (!vm->files[i].active) {
                            vm->files[i].fp = fopen(filename, "r+");
                            if (!vm->files[i].fp) vm->files[i].fp = fopen(filename, "w+");
                            vm->files[i].active = true;
                            strncpy(vm->files[i].name, filename, 63);
                            fd = i;
                            break;
                        }
                    }
                    push(vm, fd);
                } else if (inst.arg == 2) { // F2: Читати байт з файлу
                    int fd = (int)pop(vm);
                    if (fd >= 0 && fd < MAX_FILES && vm->files[fd].active) {
                        int ch = fgetc(vm->files[fd].fp);
                        push(vm, ch != EOF ? ch : -1);
                    } else {
                        push(vm, -1);
                    }
                } else if (inst.arg == 3) { // F3: Записати дані у файл
                    long val = pop(vm);
                    int fd = (int)pop(vm);
                    if (fd >= 0 && fd < MAX_FILES && vm->files[fd].active) {
                        fputc((char)val, vm->files[fd].fp);
                    }
                } else if (inst.arg == 4) { // F4: Закрити файл
                    int fd = (int)pop(vm);
                    if (fd >= 0 && fd < MAX_FILES && vm->files[fd].active) {
                        fclose(vm->files[fd].fp);
                        vm->files[fd].active = false;
                    }
                }
                break;

            case 'G': // Get: Зчитати з апаратного порту
                push(vm, 0); // Повертає базовий стан апаратного порту
                break;

            case 'H': 
                if (inst.arg == 1) printf("[HAL H1] SATA Controller active. Sector mapped.\n");
                else if (inst.arg == 2) printf("[HAL H2] COM-port (UART) transmitting stream.\n");
                else if (inst.arg == 3) printf("[HAL H3] USB 1.1-3.1 Controller initialized on bare-metal.\n");
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
                }
                break;

            case 'I': 
                if (inst.arg == 1) {
                    long val;
                    scanf("%ld", &val);
                    push(vm, val);
                } else if (inst.arg == 2) {
                    // Запис у фіксовану адресу ОЗП 2
                    vm->memory[2] = (unsigned char)pop(vm);
                } else if (inst.arg == 3) {
                    push(vm, vm->r7); // Зчитати в апаратний регістр R7
                }
                break;

            case 'J': 
                if (inst.has_arg) {
                    pc = inst.arg - 1;
                }
                break;

            case 'K': 
                printf("[KERNEL INTERRUPT] Micro-OS core function called.\n");
                break;

            case 'L':
                break;

            case 'M':
                {
                    long addr = pop(vm);
                    if (addr >= 0 && addr < MEMORY_SIZE) {
                        // Якщо маємо справу з читанням/записом
                        push(vm, vm->memory[addr]);
                    }
                }
                break;

            case 'N': 
                if (vm->sp >= 0) {
                    vm->stack[vm->sp]++;
                }
                break;

            case 'O': 
                if (vm->sp >= 0) {
                    printf("[ATOM OUTPUT] %ld\n", vm->stack[vm->sp]);
                }
                break;

            case 'P': 
                if (vm->sp >= 0) {
                    push(vm, vm->stack[vm->sp]);
                }
                break;

            case 'Q': 
                vm->running = false;
                printf("[SYS] Atom execution terminated securely.\n");
                break;

            case 'R': 
                vm->r7 = (uint8_t)pop(vm);
                break;

            case 'S': 
                break;

            case 'T': 
                if (inst.arg == 1) { // Додавання
                    long b = pop(vm); long a = pop(vm);
                    push(vm, a + b);
                } else if (inst.arg == 2) { // Віднімання
                    long b = pop(vm); long a = pop(vm);
                    push(vm, a - b);
                } else if (inst.arg == 3) { // Множення
                    long b = pop(vm); long a = pop(vm);
                    push(vm, a * b);
                } else if (inst.arg == 4) { // Ділення
                    long b = pop(vm); long a = pop(vm);
                    push(vm, b != 0 ? a / b : 0);
                }
                break;

            case 'U': 
                if (inst.arg == 1) {
            
                } else if (inst.arg == 2) {
                
                }
                break;

            case 'V':
                break;

            case 'W': 
                break;

            case 'X':
                if (inst.arg == 1) { // AND
                    long b = pop(vm); long a = pop(vm);
                    push(vm, a & b);
                } else if (inst.arg == 2) { // OR
                    long b = pop(vm); long a = pop(vm);
                    push(vm, a | b);
                } else if (inst.arg == 3) { // XOR
                    long b = pop(vm); long a = pop(vm);
                    push(vm, a ^ b);
                }
                break;

            case 'Y': 
                break;

            case 'Z': 
                vm->sp = -1;
                break;

            default:
                break;
        }
        pc++;
    }
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        printf("Usage: %s <system.mh>\n", argv[0]);
        return 1;
    }

    AtomVM vm;
    vm_init(&vm);

    printf("[ATOM SYSTEM] Initializing container & parsing .mh configuration: %s...\n", argv[1]);
    if (parse_atom_system(argv[1])) {
        printf("[ATOM SYSTEM] Loaded instructions: %d | Target sector: %d\n", program_length, current_mh.sector_mapping);
        printf("[VM] Starting bare-metal execution loop...\n");
        vm_execute(&vm);
    }

    return 0;
}
