#include "atom.h"

int main(int argc, char *argv[]) {
    if (argc < 2) {
        printf("Usage: %s <system.mh>\n", argv[0]);
        return 1;
    }

    AtomVM vm;
    vm_init(&vm);

    printf("═══════════════════════════════════════════════════\n");
    printf("[ATOM SYSTEM] File: %s\n", argv[1]);
    printf("═══════════════════════════════════════════════════\n\n");
    
    if (parse_atom_system(argv[1])) {
        printf("\n[ATOM] Loaded: %d instructions\n", program_length);
        printf("Container: %s | Sector: %d\n", 
               current_mh.container_name, current_mh.sector_mapping);
        
        if (current_mh.raw_bytes_count > 0) {
            printf("Raw bytes: %d\n", current_mh.raw_bytes_count);
        }
        if (current_mh.stack_size > 0) {
            printf("Stack context: %d values\n", current_mh.stack_size);
        }
        
        printf("\n[VM] Execution start...\n");
        printf("───────────────────────────────────────────────\n\n");
        
        vm_execute(&vm);
        
        printf("\n───────────────────────────────────────────────\n");
        printf("[VM] Finished.\n");
        printf("Steps: %d\n", vm.execution_steps);
        printf("Stack size: %d\n", vm.sp + 1);
        if (vm.sp >= 0) {
            printf("Top: ");
            print_stack_item(vm.stack[vm.sp]);
            printf("\n");
            printf("Stack dump:\n");
            for (int i = 0; i <= vm.sp && i < 20; i++) {
                printf("  [%d] ", i);
                print_stack_item(vm.stack[i]);
                printf("\n");
            }
            if (vm.sp > 19) printf("  ...\n");
        }
        printf("Registers: R0=%d R1=%d R2=%d R3=%d R4=%d R5=%d R6=%d R7=%d\n",
               vm.r0, vm.r1, vm.r2, vm.r3, vm.r4, vm.r5, vm.r6, vm.r7);
        
        vm_cleanup(&vm);
    } else {
        printf("[ERROR] Failed to parse.\n");
        return 1;
    }

    return 0;
}
