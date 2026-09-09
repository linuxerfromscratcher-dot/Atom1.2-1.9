// fish - PLEASE ADD INCLUDES HERE NOT IN FILES!!!! INCLUDE ATOM.H ONLY INSTEAD!!!
#ifndef ATOM_H
#define ATOM_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdbool.h>
#include <stdint.h>
#include <time.h>

#ifdef _WIN32
    #include <direct.h>
    #include <io.h>
    #include <sys/stat.h>
#else
    #include <dlfcn.h>
    #include <sys/stat.h>
    #include <unistd.h>
#endif

#define STACK_SIZE 256
#define MAX_CODE_LEN 4096
#define MAX_LINE_LEN 512
#define MEMORY_SIZE 8192
#define MAX_FILES 16
#define MAX_LOOP_STACK 64
#define MAX_CONTEXTS 16
#define MAX_PORTS 256
#define MAX_EXECUTION_STEPS 1000000
#define MAX_WORD_LEN 64
#define MAX_WORDS 256
#define MAX_CONTAINER_NAME 64
#define AOT_CACHE_DIR ".atom_cache"
#define AOT_MAGIC 0x41544F4D
#define AOT_VERSION 0x00010002

typedef enum {
    TYPE_NUMBER,
    TYPE_CHAR,
    TYPE_STRING,
    TYPE_WORD,
    TYPE_INSTRUCTION
} StackType;

typedef struct {
    StackType type;
    union {
        long number;
        char character;
        char string[256];
        char word[MAX_WORD_LEN];
        struct {
            char cmd;
            int arg;
            int sub_arg;
        } instruction;
    } value;
} StackItem;

typedef struct {
    char cmd;
    int arg;
    int sub_arg;
    bool has_arg;
} AtomInstruction;

typedef struct {
    char container_name[MAX_CONTAINER_NAME];
    int sector_mapping;
    unsigned int target_address;
    bool tiny_ram_fallback;
    unsigned char raw_bytes[256];
    int raw_bytes_count;
    StackItem stack_context[STACK_SIZE];
    int stack_size;
} MhContainer;

typedef struct {
    char name[64];
    FILE *fp;
    bool active;
    long position;
} VirtualFile;

typedef struct {
    char name[MAX_WORD_LEN];
    int start_pc;
    int end_pc;
    bool defined;
} WordDefinition;

typedef struct {
    void *fortran_lib;
    void (*process_file)(const char*);
    char temp_file[256];
    bool jit_enabled;
    long last_result;
    bool has_result;
} JITContext;

typedef struct {
    StackItem stack[STACK_SIZE];
    int sp;
    unsigned char memory[MEMORY_SIZE];
    uint8_t r0, r1, r2, r3, r4, r5, r6, r7;
    VirtualFile files[MAX_FILES];
    long heap_pointer;
    bool running;
    bool condition_flag[16];
    int cond_sp;
    int loop_stack[MAX_LOOP_STACK];
    int loop_sp;
    int line_number;
    int execution_steps;
    uint32_t port_values[MAX_PORTS];
    StackItem saved_context[MAX_CONTEXTS][STACK_SIZE];
    int context_sp[MAX_CONTEXTS];
    int current_context;
    WordDefinition words[MAX_WORDS];
    int word_count;
    JITContext jit;
    int pc;
} AtomVM;

typedef struct {
    uint32_t magic;
    uint32_t version;
    uint32_t timestamp;
    uint32_t program_length;
    uint32_t checksum;
    char source_file[256];
    AtomInstruction program[MAX_CODE_LEN];
    MhContainer container;
} AotCache;
extern AtomInstruction program[MAX_CODE_LEN];
extern int program_length;
extern MhContainer current_mh;
void vm_init(AtomVM *vm);
void vm_cleanup(AtomVM *vm);
void push_item(AtomVM *vm, StackItem item);
StackItem pop_item(AtomVM *vm);
void push_number(AtomVM *vm, long val);
void push_char(AtomVM *vm, char c);
void push_string(AtomVM *vm, const char *str);
void push_word(AtomVM *vm, const char *word);
long pop_number(AtomVM *vm);
char pop_char(AtomVM *vm);
const char* pop_string(AtomVM *vm);
const char* pop_word(AtomVM *vm);
void print_stack_item(StackItem item);
void print_stack(AtomVM *vm);
uint32_t read_port(AtomVM *vm, int port);
void write_port(AtomVM *vm, int port, uint32_t value);
long pack_data(long a, long b);
void unpack_data(long val, long *a, long *b);
void save_context(AtomVM *vm, int context_id);
void restore_context(AtomVM *vm, int context_id);
void load_raw_bytes(AtomVM *vm);
void load_stack_context(AtomVM *vm);
bool define_word(AtomVM *vm, const char *name, int start, int end);
int find_word(AtomVM *vm, const char *name);
bool parse_atom_system(const char *filename);
unsigned int compute_checksum(const char *filename);
bool load_aot_cache(const char *source_file, AotCache *cache);
void save_aot_cache(const char *source_file, const AotCache *cache);
void build_aot_cache(const char *source_file, AotCache *cache);
void ensure_cache_dir(void);
char* get_cache_path(const char *source_file);
void jit_init(JITContext *jit);
void jit_cleanup(JITContext *jit);
bool jit_compile_arithmetic(JITContext *jit, const char *operations);
void jit_execute_arithmetic(JITContext *jit);
void generate_fortran_ops(AtomVM *vm, const char *cmd, long a, long b);
long jit_get_result(JITContext *jit);
void vm_execute(AtomVM *vm);

#endif // ATOM_H
