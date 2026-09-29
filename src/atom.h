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
    #include <windows.h>
#else
    #include <dlfcn.h>
    #include <sys/stat.h>
    #include <unistd.h>
    #include <errno.h>
#endif

#define STACK_SIZE 256
#define MAX_CODE_LEN 4096
#define MAX_LINE_LEN 512
#define MEMORY_SIZE 8192
#define MAX_FILES 16
#define MAX_CONTEXTS 16
#define MAX_CALL_STACK 64
#define MAX_PORTS 1024
#define MAX_EXECUTION_STEPS 10000000
#define MAX_WORD_LEN 64
#define MAX_WORDS 256
#define MAX_LABELS 256
#define MAX_CODE_NAMES 512
#define MAX_CONTAINER_NAME 64
#define MAX_LIBS 64
#define MAX_SEARCH_PATHS 8
#define MAX_HAL_DEVICES 14
#define HAL_WINDOW_BASE 0x0700
#define HAL_WINDOW_SIZE 64
#define HAL_BUF_COUNT 0
#define HAL_BUF_DATA 1
#define HAL_BUF_CAPACITY 16
#define FFI_MAX_ARGS 16
#define FFI_MAX_MODULES 8
#define FFI_MAX_SYMBOLS 64
#define AOT_CACHE_DIR ".atom_cache"
#define AOT_MAGIC 0x41544F4D
#define AOT_VERSION 0x00030000
#define PACK_RECORD_SIZE 8
#define PACK_SHADOW_SIZE 256
#define FORTRAN_LIB_NAME "libatom_fortran.so"

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
    int name_idx;
    int line;
    int file_idx;
    bool has_arg;
    bool is_ret;
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
    int id;
    int pc;
} CodeLabel;

typedef struct {
    char name[MAX_WORD_LEN];
    int start_pc;
    int end_pc;
    int library;
    bool defined;
} WordDefinition;

typedef struct {
    char path[256];
    char name[MAX_WORD_LEN];
    uint32_t checksum;
    bool head_include;
    bool expanded;
} LibraryRef;

typedef struct {
    char path[256];
    void *handle;
    bool loaded;
} FfiModule;

typedef struct FfiBlock {
    struct FfiBlock *next;
    void *ptr;
    size_t size;
} FfiBlock;

typedef struct {
    FfiModule modules[FFI_MAX_MODULES];
    FfiBlock *blocks;
    void *symbols[FFI_MAX_SYMBOLS];
    int symbol_count;
    int calls;
    bool ready;
} FfiState;

typedef struct {
    bool enabled;
    void *sym_add;
    void *sym_sub;
    void *sym_mul;
    void *sym_div;
    void *sym_pow;
    char lib_path[256];
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
    int vector_labels[MAX_PORTS / 4];
    unsigned char pack_shadow[PACK_SHADOW_SIZE];
    int pack_shadow_pointer;
    int pack_shadow_used;
    bool running;
    bool condition_flag;
    int call_stack[MAX_CALL_STACK];
    int call_sp;
    int line_number;
    int pc_origin;
    int execution_steps;
    uint32_t port_values[MAX_PORTS];
    StackItem saved_context[MAX_CONTEXTS][STACK_SIZE];
    int context_sp[MAX_CONTEXTS];
    int current_context;
    FfiState ffi;
    JITContext jit;
    bool trace;
    int pc;
} AtomVM;

typedef struct {
    uint32_t magic;
    uint32_t version;
    uint32_t timestamp;
    uint32_t checksum;
    uint32_t program_length;
    uint32_t name_count;
    uint32_t word_count;
    uint32_t label_count;
    uint32_t lib_count;
    uint32_t file_count;
    uint32_t stack_size;
    uint32_t raw_bytes_count;
    uint32_t sector_mapping;
    uint32_t target_address;
    uint32_t tiny_ram_fallback;
    char container_name[MAX_CONTAINER_NAME];
    char source_file[256];
} AotHeader;

extern AtomInstruction program[MAX_CODE_LEN];
extern int program_length;
extern MhContainer current_mh;
extern WordDefinition code_words[MAX_WORDS];
extern int code_word_count;
extern CodeLabel code_labels[MAX_LABELS];
extern int code_label_count;
int find_label(int id);
extern LibraryRef code_libs[MAX_LIBS];
extern int code_lib_count;
extern char code_names[MAX_CODE_NAMES][MAX_WORD_LEN];
extern int code_name_count;
extern char code_files[MAX_LIBS][256];
extern int code_file_count;

void set_program_path(const char *self_path);
void set_self_dir(const char *path);
const char *get_self_dir(void);
void add_search_path(const char *path);
bool resolve_runtime_file(const char *name, char *out, size_t size);
void set_aot_enabled(bool enabled);
void set_trace_enabled(bool enabled);
void reset_program(void);
bool parse_atom_system(const char *filename);
bool load_aot_cache(const char *source_file);
void save_aot_cache(const char *source_file);
void ensure_cache_dir(void);
unsigned int compute_checksum(const char *filename);
int intern_name(const char *name);
const char *get_name(const AtomInstruction *inst);
const char *code_file_name(int idx);
bool code_file_is_library(int idx);
int find_word(const char *name);
bool define_word(const char *name, int start, int end, int library);
void dump_program(void);

void vm_init(AtomVM *vm, const char *self_path);
void vm_cleanup(AtomVM *vm);
void push_item(AtomVM *vm, StackItem item);
StackItem pop_item(AtomVM *vm);
void push_number(AtomVM *vm, long val);
void push_char(AtomVM *vm, char c);
void push_string(AtomVM *vm, const char *str);
void push_word(AtomVM *vm, const char *word);
long pop_number(AtomVM *vm);
char pop_char(AtomVM *vm);
const char *pop_string(AtomVM *vm);
const char *pop_word(AtomVM *vm);
void print_stack_item(StackItem item);
void print_stack(AtomVM *vm);
void save_context(AtomVM *vm, int context_id);
void restore_context(AtomVM *vm, int context_id);
void load_raw_bytes(AtomVM *vm);
void load_stack_context(AtomVM *vm);
void vm_execute(AtomVM *vm);

void hal_init(AtomVM *vm);
void hal_reset(AtomVM *vm);
long hal_read(AtomVM *vm, int device);
bool hal_write(AtomVM *vm, int device, long value);
bool hal_port_device(int port, int *device);
const char *hal_device_name(int device);
int hal_window(int device);
int hal_device_count(void);
uint32_t read_port(AtomVM *vm, int port);
void write_port(AtomVM *vm, int port, uint32_t value);

void ffi_init(FfiState *ffi);
void ffi_cleanup(FfiState *ffi);
bool ffi_open(FfiState *ffi, const char *path, int *handle, bool quiet);
void ffi_close(FfiState *ffi, int handle);
bool ffi_resolve(FfiState *ffi, int handle, const char *name, void **out);
bool ffi_symbol_known(FfiState *ffi, void *sym);
bool ffi_call_long(FfiState *ffi, void *fn, const long *args, int nargs, long *result);
void *ffi_alloc(FfiState *ffi, long size);
void ffi_free(FfiState *ffi, void *ptr);
char *ffi_cstr(FfiState *ffi, const char *text);
bool ffi_store8(FfiState *ffi, void *ptr, long value);
long ffi_load8(FfiState *ffi, void *ptr);
bool ffi_store32(FfiState *ffi, void *ptr, long value);
long ffi_load32(FfiState *ffi, void *ptr);
const char *ffi_backend(void);
int ffi_module_count(FfiState *ffi);
void *ffi_native_base(void);

void jit_init(AtomVM *vm);
void jit_cleanup(AtomVM *vm);
void generate_fortran_ops(AtomVM *vm, const char *cmd, long a, long b);
long jit_get_result(JITContext *jit);

int item_compare(const StackItem *left, const StackItem *right, int *order);
void pack_write_record(AtomVM *vm, long addr, long cluster, long size);
bool pack_read_record(AtomVM *vm, long addr, long *cluster, long *size);

#endif
