#include "atom.h"

#define MAX_IMPORT_DEPTH 32
#define LIB_EXTENSIONS {"mh", "atom", "atml", NULL}

AtomInstruction program[MAX_CODE_LEN];
int program_length = 0;
MhContainer current_mh;
WordDefinition code_words[MAX_WORDS];
int code_word_count = 0;
CodeLabel code_labels[MAX_LABELS];
int code_label_count = 0;
LibraryRef code_libs[MAX_LIBS];
int code_lib_count = 0;
char code_names[MAX_CODE_NAMES][MAX_WORD_LEN];
int code_name_count = 0;
char code_files[MAX_LIBS][256];
int code_file_count = 0;

static AtomInstruction s_pool[MAX_CODE_LEN];
static int s_pool_len = 0;
static int s_open_word = -1;
static bool s_in_library = false;
static int s_file_idx = 0;
static char s_paths[MAX_SEARCH_PATHS][512];
static int s_path_count = 0;
static bool s_aot_enabled = true;
static bool s_trace = false;
static bool s_overflow = false;

static bool parse_lines(FILE *file, const char *dir, bool *in_block_comment, int depth);

void set_trace_enabled(bool enabled) {
    s_trace = enabled;
}

void set_aot_enabled(bool enabled) {
    s_aot_enabled = enabled;
}

void add_search_path(const char *path) {
    if (!path || !*path) return;
    for (int i = 0; i < s_path_count; i++) {
        if (strcmp(s_paths[i], path) == 0) return;
    }
    if (s_path_count >= MAX_SEARCH_PATHS) return;
    snprintf(s_paths[s_path_count], sizeof(s_paths[0]), "%s", path);
    s_path_count++;
}

static void add_env_paths(const char *var) {
    const char *value = getenv(var);
    if (!value || !*value) return;
    char buffer[2048];
    snprintf(buffer, sizeof(buffer), "%s", value);
    for (char *token = strtok(buffer, ":;"); token; token = strtok(NULL, ":;")) {
        add_search_path(token);
    }
}

static void path_dir(const char *path, char *out, size_t size) {
    const char *slash = strrchr(path, '/');
    const char *bslash = strrchr(path, '\\');
    const char *cut = NULL;
    if (slash && bslash) cut = (slash > bslash) ? slash : bslash;
    else if (slash) cut = slash;
    else if (bslash) cut = bslash;
    if (!cut) {
        snprintf(out, size, ".");
        return;
    }
    size_t len = (size_t)(cut - path);
    if (len == 0) len = 1;
    if (len >= size) len = size - 1;
    memcpy(out, path, len);
    out[len] = '\0';
}

void set_program_path(const char *self_path) {
    add_env_paths("ATOM_LIB");
    if (self_path && *self_path) set_self_dir(self_path);
    const char *dir = get_self_dir();
    char parent[512];
    char sibling[520];
    path_dir(dir, parent, sizeof(parent));
    add_search_path(dir);
    add_search_path(parent);
    snprintf(sibling, sizeof(sibling), "%.*s/lib", (int)(sizeof(sibling) - 8), dir);
    add_search_path(sibling);
    snprintf(sibling, sizeof(sibling), "%.*s/lib", (int)(sizeof(sibling) - 8), parent);
    add_search_path(sibling);
    add_search_path("lib");
    add_search_path(".");
    add_env_paths("LD_LIBRARY_PATH");
}

const char *code_file_name(int idx) {
    if (idx < 0 || idx >= code_file_count) return "?";
    return code_files[idx];
}

bool code_file_is_library(int idx) {
    if (idx < 0 || idx >= code_file_count) return false;
    for (int i = 0; i < code_lib_count; i++) {
        if (strcmp(code_libs[i].path, code_files[idx]) == 0) return true;
    }
    return false;
}

int intern_name(const char *name) {
    if (!name) return -1;
    for (int i = 0; i < code_name_count; i++) {
        if (strcmp(code_names[i], name) == 0) return i;
    }
    if (code_name_count >= MAX_CODE_NAMES) return -1;
    snprintf(code_names[code_name_count], MAX_WORD_LEN, "%s", name);
    return code_name_count++;
}

const char *get_name(const AtomInstruction *inst) {
    if (!inst || inst->name_idx < 0 || inst->name_idx >= code_name_count) return NULL;
    return code_names[inst->name_idx];
}

int find_word(const char *name) {
    if (!name) return -1;
    for (int i = 0; i < code_word_count; i++) {
        if (code_words[i].defined && strcmp(code_words[i].name, name) == 0) return i;
    }
    return -1;
}

bool define_word(const char *name, int start, int end, int library) {
    int existing = find_word(name);
    if (existing >= 0) {
        code_words[existing].start_pc = start;
        code_words[existing].end_pc = end;
        code_words[existing].library = library;
        return true;
    }
    if (code_word_count >= MAX_WORDS) {
        fprintf(stderr, "[ERROR] Too many commands (max %d)\n", MAX_WORDS);
        return false;
    }
    WordDefinition *word = &code_words[code_word_count];
    memset(word, 0, sizeof(*word));
    snprintf(word->name, sizeof(word->name), "%s", name);
    word->start_pc = start;
    word->end_pc = end;
    word->library = library;
    word->defined = true;
    code_word_count++;
    return true;
}

void reset_program(void) {
    program_length = 0;
    s_pool_len = 0;
    code_word_count = 0;
    code_label_count = 0;
    code_lib_count = 0;
    code_name_count = 0;
    code_file_count = 0;
    s_open_word = -1;
    s_in_library = false;
    s_file_idx = 0;
    s_overflow = false;
    memset(program, 0, sizeof(program));
    memset(s_pool, 0, sizeof(s_pool));
    memset(code_words, 0, sizeof(code_words));
    memset(code_libs, 0, sizeof(code_libs));
    memset(code_names, 0, sizeof(code_names));
    memset(code_files, 0, sizeof(code_files));
    memset(&current_mh, 0, sizeof(current_mh));
}

static int register_file(const char *path) {
    for (int i = 0; i < code_file_count; i++) {
        if (strcmp(code_files[i], path) == 0) return i;
    }
    if (code_file_count >= MAX_LIBS) return 0;
    snprintf(code_files[code_file_count], sizeof(code_files[0]), "%s", path);
    return code_file_count++;
}

static int register_library(const char *path, const char *name, bool head_include) {
    for (int i = 0; i < code_lib_count; i++) {
        if (strcmp(code_libs[i].path, path) == 0) return i;
    }
    if (code_lib_count >= MAX_LIBS) {
        fprintf(stderr, "[ERROR] Too many libraries (max %d)\n", MAX_LIBS);
        return -1;
    }
    LibraryRef *lib = &code_libs[code_lib_count];
    memset(lib, 0, sizeof(*lib));
    snprintf(lib->path, sizeof(lib->path), "%s", path);
    snprintf(lib->name, sizeof(lib->name), "%s", name);
    lib->checksum = compute_checksum(path);
    lib->head_include = head_include;
    lib->expanded = false;
    return code_lib_count++;
}

static bool emit(const AtomInstruction *inst) {
    AtomInstruction *target = s_in_library || s_open_word >= 0 ? s_pool : program;
    int *length = s_in_library || s_open_word >= 0 ? &s_pool_len : &program_length;
    if (*length >= MAX_CODE_LEN) {
        if (!s_overflow) {
            fprintf(stderr, "[ERROR] Program too long (max %d instructions)\n", MAX_CODE_LEN);
            s_overflow = true;
        }
        return false;
    }
    target[(*length)++] = *inst;
    return true;
}

static int current_pc(void) {
    return (s_in_library || s_open_word >= 0) ? s_pool_len : program_length;
}

static void register_label(int id, int pc, int line) {
    for (int i = 0; i < code_label_count; i++) {
        if (code_labels[i].id == id) {
            if (code_labels[i].pc != pc) {
                fprintf(stderr, "[WARNING] line %d: label %d redefined (%d -> %d)\n",
                        line, id, code_labels[i].pc, pc);
            }
            code_labels[i].pc = pc;
            return;
        }
    }
    if (code_label_count >= MAX_LABELS) {
        fprintf(stderr, "[ERROR] Too many labels (max %d), label %d at line %d dropped\n",
                MAX_LABELS, id, line);
        return;
    }
    code_labels[code_label_count].id = id;
    code_labels[code_label_count].pc = pc;
    code_label_count++;
}

int find_label(int id) {
    for (int i = 0; i < code_label_count; i++) {
        if (code_labels[i].id == id) return code_labels[i].pc;
    }
    return -1;
}

static AtomInstruction make_inst(char cmd, int line) {
    AtomInstruction inst;
    memset(&inst, 0, sizeof(inst));
    inst.cmd = cmd;
    inst.arg = 0;
    inst.sub_arg = 0;
    inst.name_idx = -1;
    inst.line = line;
    inst.file_idx = s_file_idx;
    inst.has_arg = false;
    inst.is_ret = false;
    return inst;
}

static long parse_number(const char *text, int *end) {
    char *stop = NULL;
    long value;
    if (text[0] == '$') {
        value = strtol(text + 1, &stop, 16);
        *end = (int)(stop - text);
        return value;
    }
    if (text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) {
        value = strtol(text + 2, &stop, 16);
        *end = (int)(stop - text);
        return value;
    }
    value = strtol(text, &stop, 10);
    *end = (int)(stop - text);
    return value;
}

static bool file_exists(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) return false;
    fclose(f);
    return true;
}

static bool resolve_library(const char *dir, const char *name, char *out, size_t size) {
    static const char *exts[] = LIB_EXTENSIONS;
    char candidate[512];

    if (name[0] == '/' || name[0] == '\\' ||
        (name[0] && name[1] == ':')) {
        snprintf(candidate, sizeof(candidate), "%s", name);
        if (file_exists(candidate)) {
            snprintf(out, size, "%s", candidate);
            return true;
        }
    }

    if (strchr(name, '.') && strrchr(name, '/')) {
        snprintf(candidate, sizeof(candidate), "%s/%s", dir, name);
        if (file_exists(candidate)) {
            snprintf(out, size, "%s", candidate);
            return true;
        }
    }

    snprintf(candidate, sizeof(candidate), "%s/%s", dir, name);
    if (file_exists(candidate)) {
        snprintf(out, size, "%s", candidate);
        return true;
    }

    for (int e = 0; exts[e]; e++) {
        snprintf(candidate, sizeof(candidate), "%s/%s.%s", dir, name, exts[e]);
        if (file_exists(candidate)) {
            snprintf(out, size, "%s", candidate);
            return true;
        }
    }

    for (int i = 0; i < s_path_count; i++) {
        snprintf(candidate, sizeof(candidate), "%s/%s", s_paths[i], name);
        if (file_exists(candidate)) {
            snprintf(out, size, "%s", candidate);
            return true;
        }
        for (int e = 0; exts[e]; e++) {
            snprintf(candidate, sizeof(candidate), "%s/%s.%s", s_paths[i], name, exts[e]);
            if (file_exists(candidate)) {
                snprintf(out, size, "%s", candidate);
                return true;
            }
        }
    }

    return false;
}

bool resolve_runtime_file(const char *name, char *out, size_t size) {
    if (!name || !*name) return false;
    if (file_exists(name)) {
        snprintf(out, size, "%s", name);
        return true;
    }
    for (int i = 0; i < s_path_count; i++) {
        snprintf(out, size, "%s/%s", s_paths[i], name);
        if (file_exists(out)) return true;
    }
    return false;
}

static void strip_comments(char *line, bool *in_block_comment) {
    char cleaned[MAX_LINE_LEN];
    int j = 0;
    for (int i = 0; line[i] != '\0'; i++) {
        if (*in_block_comment) {
            if (line[i] == '*' && line[i + 1] == '/') {
                *in_block_comment = false;
                i++;
            }
            continue;
        }
        if (line[i] == ';') break;
        if (line[i] == '/' && line[i + 1] == '/') break;
        if (line[i] == '/' && line[i + 1] == '*') {
            *in_block_comment = true;
            i++;
            continue;
        }
        cleaned[j++] = line[i];
    }
    cleaned[j] = '\0';
    memcpy(line, cleaned, (size_t)j + 1);
}

static bool has_ref_colon(const char *p) {
    p++;
    while (*p && isdigit((unsigned char)*p)) p++;
    return *p == ':';
}

static bool is_label_line(const char *p) {
    if (!isdigit((unsigned char)*p) && *p != '-') return false;
    if (*p == '-') p++;
    while (isdigit((unsigned char)*p)) p++;
    return *p == ':';
}

static void open_word(const char *name, int line) {
    if (s_open_word >= 0) {
        fprintf(stderr, "[WARNING] line %d: nested command ':%s', closing '%s'\n",
                line, name, code_words[s_open_word].name);
        AtomInstruction ret = make_inst('*', line);
        ret.is_ret = true;
        emit(&ret);
        code_words[s_open_word].end_pc = s_pool_len;
        s_open_word = -1;
    }
    if (code_word_count >= MAX_WORDS) {
        fprintf(stderr, "[ERROR] Too many commands (max %d)\n", MAX_WORDS);
        return;
    }
    WordDefinition *word = &code_words[code_word_count];
    memset(word, 0, sizeof(*word));
    snprintf(word->name, sizeof(word->name), "%s", name);
    word->start_pc = s_pool_len;
    word->end_pc = s_pool_len;
    word->library = s_file_idx;
    word->defined = false;
    s_open_word = code_word_count;
    code_word_count++;
    if (s_trace) {
        printf("[PARSE] command ':%s' opened at %s:%d\n", name, code_file_name(s_file_idx), line);
    }
}

static void close_word(int line) {
    if (s_open_word < 0) {
        AtomInstruction nop = make_inst('W', line);
        emit(&nop);
        return;
    }
    AtomInstruction ret = make_inst('*', line);
    ret.is_ret = true;
    emit(&ret);
    code_words[s_open_word].end_pc = s_pool_len;
    code_words[s_open_word].defined = true;
    if (s_trace) {
        printf("[PARSE] command ':%s' closed at %s:%d (pool %d..%d)\n",
               code_words[s_open_word].name, code_file_name(s_file_idx), line,
               code_words[s_open_word].start_pc, code_words[s_open_word].end_pc);
    }
    s_open_word = -1;
}

static bool parse_hex_bytes(const char *ptr) {
    const char *p = ptr;
    bool any = false;
    while (*p) {
        while (*p && (isspace((unsigned char)*p) || *p == ',')) p++;
        if (!*p) break;
        int end = 0;
        long value = parse_number(p, &end);
        if (end == 0) {
            p++;
            continue;
        }
        p += end;
        if (current_mh.raw_bytes_count < (int)sizeof(current_mh.raw_bytes)) {
            current_mh.raw_bytes[current_mh.raw_bytes_count++] = (unsigned char)(value & 0xFF);
        }
        any = true;
    }
    return any;
}

static void handle_container_line(const char *ptr) {
    if (strncmp(ptr, "F/", 2) == 0) {
        char name[MAX_CONTAINER_NAME];
        if (sscanf(ptr, "F/%[^/]/", name) == 1) {
            snprintf(current_mh.container_name, sizeof(current_mh.container_name), "%s", name);
        }
        return;
    }

    if (*ptr == 'S' && has_ref_colon(ptr)) {
        int end = 0;
        current_mh.sector_mapping = (int)parse_number(ptr + 1, &end);
        return;
    }

    if (*ptr == 'M' && has_ref_colon(ptr)) {
        int end = 0;
        unsigned int addr = (unsigned int)parse_number(ptr + 1, &end);
        if (addr >= MEMORY_SIZE) {
            current_mh.target_address = 3;
            current_mh.tiny_ram_fallback = true;
        } else {
            current_mh.target_address = addr;
            current_mh.tiny_ram_fallback = false;
        }
        return;
    }

    if (strncmp(ptr, "0x", 2) == 0 || strncmp(ptr, "0X", 2) == 0) {
        parse_hex_bytes(ptr);
        return;
    }

    if (*ptr == '"') {
        ptr++;
        char str_val[256];
        int i = 0;
        while (*ptr && *ptr != '"' && i < 255) str_val[i++] = *ptr++;
        str_val[i] = '\0';
        if (current_mh.stack_size < STACK_SIZE) {
            StackItem item;
            memset(&item, 0, sizeof(item));
            item.type = TYPE_STRING;
            snprintf(item.value.string, sizeof(item.value.string), "%s", str_val);
            current_mh.stack_context[current_mh.stack_size++] = item;
        }
        return;
    }

    if (*ptr == '\'') {
        ptr++;
        if (current_mh.stack_size < STACK_SIZE) {
            StackItem item;
            memset(&item, 0, sizeof(item));
            item.type = TYPE_CHAR;
            item.value.character = *ptr;
            current_mh.stack_context[current_mh.stack_size++] = item;
        }
        return;
    }

    const char *p = ptr;
    while (*p) {
        while (*p && (isspace((unsigned char)*p) || *p == ',')) p++;
        if (!*p) break;
        if (isdigit((unsigned char)*p) || *p == '-' || *p == '+' || *p == '$') {
            int end = 0;
            long value = parse_number(p, &end);
            if (end == 0) {
                p++;
                continue;
            }
            p += end;
            if (current_mh.stack_size < STACK_SIZE) {
                StackItem item;
                memset(&item, 0, sizeof(item));
                item.type = TYPE_NUMBER;
                item.value.number = value;
                current_mh.stack_context[current_mh.stack_size++] = item;
            }
        } else {
            p++;
        }
    }
}

static bool read_token(char **ptr, char close, char *out, size_t size) {
    char *p = *ptr;
    size_t i = 0;
    while (*p && *p != close && !isspace((unsigned char)*p) && i < size - 1) {
        out[i++] = *p++;
    }
    out[i] = '\0';
    if (*p == close) p++;
    *ptr = p;
    return i > 0;
}

static void handle_head_include(const char *name, int line) {
    char dir[512];
    path_dir(code_files[s_file_idx], dir, sizeof(dir));
    char path[512];
    if (!resolve_library(dir, name, path, sizeof(path))) {
        fprintf(stderr, "[ERROR] line %d: HEADINCLUDE cannot find library '%s'\n", line, name);
        return;
    }
    int lib = register_library(path, name, true);
    if (lib < 0) return;
    printf("[HEADINCLUDE] %s -> %s\n", name, path);
    AtomInstruction inst = make_inst('H', line);
    inst.name_idx = intern_name(name);
    inst.has_arg = true;
    emit(&inst);
}

static bool parse_import(const char *dir, bool *in_block_comment, char *ptr, int depth) {
    if (depth >= MAX_IMPORT_DEPTH) {
        fprintf(stderr, "[ERROR] Import nesting too deep: %s\n", ptr);
        return false;
    }

    ptr += 6;
    while (*ptr && isspace((unsigned char)*ptr)) ptr++;

    char name[256];
    if (*ptr == '"') {
        ptr++;
        if (!read_token(&ptr, '"', name, sizeof(name))) {
            fprintf(stderr, "[ERROR] Empty import path\n");
            return false;
        }
    } else {
        size_t i = 0;
        while (*ptr && !isspace((unsigned char)*ptr) && i < sizeof(name) - 1) {
            name[i++] = *ptr++;
        }
        name[i] = '\0';
    }

    if (!*name) {
        fprintf(stderr, "[ERROR] Empty import path\n");
        return false;
    }

    char path[512];
    if (!resolve_library(dir, name, path, sizeof(path))) {
        fprintf(stderr, "[ERROR] Cannot import: %s\n", name);
        return false;
    }

    int lib = register_library(path, name, false);
    if (lib < 0) return false;

    FILE *file = fopen(path, "r");
    if (!file) {
        fprintf(stderr, "[ERROR] Cannot open file: %s\n", path);
        return false;
    }

    int prev_file = s_file_idx;
    s_file_idx = register_file(path);
    char sub_dir[512];
    path_dir(path, sub_dir, sizeof(sub_dir));

    bool ok = parse_lines(file, sub_dir, in_block_comment, depth + 1);
    fclose(file);
    s_file_idx = prev_file;
    return ok;
}

static bool read_quoted_token(char **ptr, char close, char *out, size_t size) {
    char *p = *ptr;
    size_t i = 0;
    bool closed = false;
    while (*p) {
        if (*p == '\\' && p[1] && p[1] != close) {
            if (i < size - 1) out[i++] = p[1];
            p += 2;
            continue;
        }
        if (*p == close) {
            closed = true;
            p++;
            break;
        }
        if (i < size - 1) out[i++] = *p;
        p++;
    }
    out[i] = '\0';
    *ptr = p;
    return closed || i > 0;
}

static bool parse_code_line(char *ptr, int line) {
    while (*ptr) {
        while (*ptr && isspace((unsigned char)*ptr)) ptr++;
        if (!*ptr) break;

        if (isdigit((unsigned char)*ptr) && is_label_line(ptr)) {
            int end = 0;
            int id = (int)parse_number(ptr, &end);
            while (*ptr && *ptr != ':') ptr++;
            if (*ptr == ':') ptr++;
            register_label(id, current_pc(), line);
            continue;
        }

        if (*ptr == ':') {
            ptr++;
            while (*ptr && isspace((unsigned char)*ptr)) ptr++;
            if (*ptr) {
                char close = ' ';
                char *open = NULL;
                if (*ptr == '{') { open = ptr; close = '}'; ptr++; }
                else if (*ptr == '"') { open = ptr; close = '"'; ptr++; }
                char token[MAX_WORD_LEN];
                if (read_token(&ptr, close, token, sizeof(token))) {
                    open_word(token, line);
                } else if (open) {
                    open_word("", line);
                }
            } else {
                close_word(line);
            }
            continue;
        }

        if (*ptr == '"' || *ptr == '\'') {
            char close = *ptr;
            ptr++;
            char token[MAX_LINE_LEN];
            if (read_quoted_token(&ptr, close, token, sizeof(token))) {
                AtomInstruction inst = make_inst(close, line);
                inst.name_idx = intern_name(token);
                inst.has_arg = true;
                emit(&inst);
            }
            continue;
        }

        if (isalpha((unsigned char)*ptr)) {
            char cmd = (char)toupper((unsigned char)*ptr);
            ptr++;

            if (!strchr("ACDEFGHIJKMNOPQRSTUVWXYZ", cmd)) {
                fprintf(stderr, "[ERROR] line %d: '%c' is not an Atom command (see ref/spec.txt)\n",
                        line, cmd);
                return true;
            }

            AtomInstruction inst = make_inst(cmd, line);

            if (*ptr == '{' || *ptr == '"' || *ptr == '(') {
                char close = (*ptr == '{') ? '}' : (*ptr == '"') ? '"' : ')';
                ptr++;
                char token[MAX_WORD_LEN];
                if (read_token(&ptr, close, token, sizeof(token))) {
                    inst.name_idx = intern_name(token);
                    inst.has_arg = true;
                }
            } else if (isdigit((unsigned char)*ptr) || *ptr == '-' || *ptr == '+' || *ptr == '$') {
                int end = 0;
                inst.arg = (int)parse_number(ptr, &end);
                ptr += end;
                inst.has_arg = true;
                if (*ptr == '(') {
                    const char *inner = ptr + 1;
                    int sub_end = 0;
                    inst.sub_arg = (int)parse_number(inner, &sub_end);
                    if (sub_end == 0) inst.sub_arg = 0;
                    while (*ptr && *ptr != ')') ptr++;
                    if (*ptr == ')') ptr++;
                }
            }

            if (*ptr == '"' || *ptr == '\'' || *ptr == '{') {
                char close = (*ptr == '{') ? '}' : *ptr;
                ptr++;
                char token[MAX_WORD_LEN];
                if (read_quoted_token(&ptr, close, token, sizeof(token))) {
                    inst.name_idx = intern_name(token);
                    inst.has_arg = true;
                }
            }

            if (cmd == 'Q') {
                if (s_open_word >= 0 || s_in_library) {
                    AtomInstruction ret = make_inst('*', line);
                    ret.is_ret = true;
                    emit(&ret);
                } else {
                    emit(&inst);
                    return true;
                }
                continue;
            }

            emit(&inst);
            continue;
        }

        ptr++;
    }

    return false;
}

static bool parse_lines(FILE *file, const char *dir, bool *in_block_comment, int depth) {
    char line[MAX_LINE_LEN];
    int line_number = 0;
    bool quit = false;

    while (fgets(line, sizeof(line), file)) {
        line_number++;
        strip_comments(line, in_block_comment);
        if (quit) continue;

        char *ptr = line;
        while (*ptr && isspace((unsigned char)*ptr)) ptr++;
        if (!*ptr) continue;

        if (strncmp(ptr, "import", 6) == 0 &&
            (ptr[6] == '\0' || isspace((unsigned char)ptr[6]) || ptr[6] == '"')) {
            if (!parse_import(dir, in_block_comment, ptr, depth)) return false;
            continue;
        }

        if (isalpha((unsigned char)*ptr) &&
            (ptr[1] == '{' || ptr[1] == '"' || (toupper((unsigned char)ptr[0]) == 'H' && ptr[1] == '('))) {
            char cmd = (char)toupper((unsigned char)*ptr);
            char *rest = (char *)ptr + 1;
            char token[MAX_WORD_LEN];
            char close = (rest[0] == '{') ? '}' : (rest[0] == '"') ? '"' : ')';
            rest++;
            if (cmd == 'H' && read_token(&rest, close, token, sizeof(token))) {
                handle_head_include(token, line_number);
                continue;
            }
        }

        if (*ptr == 'F' && ptr[1] == '/') {
            handle_container_line(ptr);
            continue;
        }

        if ((*ptr == 'S' || *ptr == 'M') && has_ref_colon(ptr)) {
            handle_container_line(ptr);
            continue;
        }

        if (strncmp(ptr, "0x", 2) == 0 || strncmp(ptr, "0X", 2) == 0) {
            handle_container_line(ptr);
            continue;
        }

        if ((s_in_library || s_open_word >= 0) && (*ptr == '"' || *ptr == '\'')) {
            parse_code_line(ptr, line_number);
            continue;
        }

        if (*ptr == '"' || *ptr == '\'') {
            handle_container_line(ptr);
            continue;
        }

        if (isdigit((unsigned char)*ptr) && !is_label_line(ptr)) {
            handle_container_line(ptr);
            continue;
        }

        if (*ptr == ':') {
            if (parse_code_line(ptr, line_number) && !s_in_library) quit = true;
            if (quit && s_open_word >= 0) close_word(line_number);
            continue;
        }

        if (parse_code_line(ptr, line_number)) quit = true;
        if (quit && s_open_word >= 0) close_word(line_number);
    }

    if (s_open_word >= 0) {
        fprintf(stderr, "[WARNING] line %d: unterminated command ':%s' in %s\n",
                line_number, code_words[s_open_word].name, code_file_name(s_file_idx));
        close_word(line_number);
    }

    return true;
}

static bool expand_libraries(void) {
    bool progress = true;
    while (progress) {
        progress = false;
        for (int i = 0; i < code_lib_count; i++) {
            if (!code_libs[i].head_include || code_libs[i].expanded) continue;
            code_libs[i].expanded = true;
            progress = true;

            FILE *file = fopen(code_libs[i].path, "r");
            if (!file) {
                fprintf(stderr, "[ERROR] Cannot open library: %s\n", code_libs[i].path);
                return false;
            }

            int prev_file = s_file_idx;
            s_file_idx = register_file(code_libs[i].path);
            char dir[512];
            path_dir(code_libs[i].path, dir, sizeof(dir));
            s_in_library = true;
            bool in_block_comment = false;
            bool ok = parse_lines(file, dir, &in_block_comment, MAX_IMPORT_DEPTH - 1);
            s_in_library = false;
            fclose(file);
            s_file_idx = prev_file;
            if (!ok) return false;
        }
    }
    return true;
}

static int assemble_program(void) {
    int total = program_length + s_pool_len;
    if (total > MAX_CODE_LEN) {
        fprintf(stderr, "[ERROR] Program too long: %d + %d > %d\n",
                program_length, s_pool_len, MAX_CODE_LEN);
        return -1;
    }
    if (s_pool_len > 0) {
        memcpy(program + program_length, s_pool, sizeof(AtomInstruction) * (size_t)s_pool_len);
        for (int i = 0; i < code_word_count; i++) {
            code_words[i].start_pc += program_length;
            code_words[i].end_pc += program_length;
        }
        for (int i = 0; i < code_label_count; i++) {
            code_labels[i].pc += program_length;
        }
        program_length = total;
    }
    return program_length;
}

void dump_program(void) {
    printf("Program: %d instructions, %d commands, %d names, %d libraries\n",
           program_length, code_word_count, code_name_count, code_lib_count);
    for (int i = 0; i < program_length; i++) {
        const AtomInstruction *inst = &program[i];
        const char *name = get_name(inst);
        printf("  [%4d] %s:%d %s", i, code_file_name(inst->file_idx), inst->line,
               inst->is_ret ? "RET" : "");
        if (!inst->is_ret) {
            printf("%c", inst->cmd);
            if (inst->has_arg) printf(" %d", inst->arg);
            if (inst->sub_arg) printf("(%d)", inst->sub_arg);
            if (name) printf(" \"%s\"", name);
        }
        printf("\n");
    }
    for (int i = 0; i < code_label_count; i++) {
        printf("  label %d -> [%d]\n", code_labels[i].id, code_labels[i].pc);
    }
    for (int i = 0; i < code_word_count; i++) {
        printf("  command :%s -> [%d..%d] from %s\n",
               code_words[i].name, code_words[i].start_pc, code_words[i].end_pc,
               code_file_name(code_words[i].library));
    }
    for (int i = 0; i < code_lib_count; i++) {
        printf("  library %s: %s (head=%d)\n",
               code_libs[i].name, code_libs[i].path, code_libs[i].head_include ? 1 : 0);
    }
}

void ensure_cache_dir(void) {
#ifdef _WIN32
    _mkdir(AOT_CACHE_DIR);
#else
    struct stat st;
    if (stat(AOT_CACHE_DIR, &st) == -1) {
        mkdir(AOT_CACHE_DIR, 0755);
    }
#endif
}

static void cache_path(const char *source_file, char *out, size_t size) {
    const char *slash = strrchr(source_file, '/');
    const char *bslash = strrchr(source_file, '\\');
    const char *base = NULL;
    if (slash && bslash) base = (slash > bslash) ? slash : bslash;
    else if (slash) base = slash;
    else if (bslash) base = bslash;
    if (base) base++;
    else base = source_file;
#ifdef _WIN32
    snprintf(out, size, "%s\\%s.aot", AOT_CACHE_DIR, base);
#else
    snprintf(out, size, "%s/%s.aot", AOT_CACHE_DIR, base);
#endif
}

unsigned int compute_checksum(const char *filename) {
    FILE *f = fopen(filename, "rb");
    if (!f) return 0;
    unsigned int checksum = 2166136261u;
    int ch;
    while ((ch = fgetc(f)) != EOF) {
        checksum ^= (unsigned char)ch;
        checksum *= 16777619u;
    }
    fclose(f);
    return checksum;
}

bool load_aot_cache(const char *source_file) {    if (!s_aot_enabled) return false;

    char path[512];
    cache_path(source_file, path, sizeof(path));

    FILE *f = fopen(path, "rb");
    if (!f) return false;

    AotHeader head;
    if (fread(&head, sizeof(head), 1, f) != 1) {
        fclose(f);
        return false;
    }

    if (head.magic != AOT_MAGIC || head.version != AOT_VERSION) {
        fclose(f);
        return false;
    }

    if (head.checksum != compute_checksum(source_file)) {
        printf("[AOT] Cache invalid (checksum mismatch), recompiling...\n");
        fclose(f);
        return false;
    }

    if (head.program_length > MAX_CODE_LEN || head.name_count > MAX_CODE_NAMES ||
        head.word_count > MAX_WORDS || head.label_count > MAX_LABELS || head.lib_count > MAX_LIBS ||
        head.file_count > MAX_LIBS || head.stack_size > STACK_SIZE ||
        head.raw_bytes_count > (uint32_t)sizeof(current_mh.raw_bytes)) {
        fclose(f);
        return false;
    }

    struct stat src_stat, cache_stat;
    if (stat(source_file, &src_stat) == 0 && stat(path, &cache_stat) == 0) {
        if (src_stat.st_mtime > cache_stat.st_mtime) {
            printf("[AOT] Source newer than cache, recompiling...\n");
            fclose(f);
            return false;
        }
    }

    LibraryRef libs[MAX_LIBS];
    if (head.lib_count > 0 && fread(libs, sizeof(LibraryRef), head.lib_count, f) != head.lib_count) {
        fclose(f);
        return false;
    }
    for (uint32_t i = 0; i < head.lib_count; i++) {
        if (compute_checksum(libs[i].path) != libs[i].checksum) {
            printf("[AOT] Cache invalid (library changed): %s\n", libs[i].path);
            fclose(f);
            return false;
        }
        struct stat lib_stat;
        if (stat(libs[i].path, &lib_stat) == 0 && src_stat.st_mtime > cache_stat.st_mtime) {
            if (lib_stat.st_mtime > cache_stat.st_mtime) {
                printf("[AOT] Cache invalid (library newer): %s\n", libs[i].path);
                fclose(f);
                return false;
            }
        }
    }

    reset_program();

    program_length = (int)head.program_length;
    code_name_count = (int)head.name_count;
    code_word_count = (int)head.word_count;
    code_label_count = (int)head.label_count;
    code_lib_count = (int)head.lib_count;
    code_file_count = (int)head.file_count;
    current_mh.stack_size = (int)head.stack_size;
    current_mh.raw_bytes_count = (int)head.raw_bytes_count;
    current_mh.sector_mapping = (int)head.sector_mapping;
    current_mh.target_address = (unsigned int)head.target_address;
    current_mh.tiny_ram_fallback = head.tiny_ram_fallback != 0;
    snprintf(current_mh.container_name, sizeof(current_mh.container_name), "%s", head.container_name);

    if (program_length > 0 && fread(program, sizeof(AtomInstruction), program_length, f) != (size_t)program_length) {
        fclose(f);
        return false;
    }
    if (code_name_count > 0 && fread(code_names, MAX_WORD_LEN, code_name_count, f) != (size_t)code_name_count) {
        fclose(f);
        return false;
    }
    if (code_word_count > 0 && fread(code_words, sizeof(WordDefinition), code_word_count, f) != (size_t)code_word_count) {
        fclose(f);
        return false;
    }
    if (code_label_count > 0 && fread(code_labels, sizeof(CodeLabel), code_label_count, f) != (size_t)code_label_count) {
        fclose(f);
        return false;
    }
    if (code_file_count > 0 && fread(code_files, 256, code_file_count, f) != (size_t)code_file_count) {
        fclose(f);
        return false;
    }
    if (code_lib_count > 0) memcpy(code_libs, libs, sizeof(LibraryRef) * (size_t)code_lib_count);
    for (int i = 0; i < code_lib_count; i++) code_libs[i].expanded = true;
    if (current_mh.stack_size > 0 &&
        fread(current_mh.stack_context, sizeof(StackItem), current_mh.stack_size, f) != (size_t)current_mh.stack_size) {
        fclose(f);
        return false;
    }
    if (current_mh.raw_bytes_count > 0 &&
        fread(current_mh.raw_bytes, 1, current_mh.raw_bytes_count, f) != (size_t)current_mh.raw_bytes_count) {
        fclose(f);
        return false;
    }

    fclose(f);
    printf("[AOT] Cache loaded: %s (checksum: 0x%08X)\n", path, head.checksum);
    return true;
}

void save_aot_cache(const char *source_file) {
    if (!s_aot_enabled) return;
    ensure_cache_dir();

    char path[512];
    cache_path(source_file, path, sizeof(path));

    FILE *f = fopen(path, "wb");
    if (!f) {
        fprintf(stderr, "[AOT] Warning: cannot write cache to %s\n", path);
        return;
    }

    AotHeader head;
    memset(&head, 0, sizeof(head));
    head.magic = AOT_MAGIC;
    head.version = AOT_VERSION;
    head.timestamp = (uint32_t)time(NULL);
    head.checksum = compute_checksum(source_file);
    head.program_length = (uint32_t)program_length;
    head.name_count = (uint32_t)code_name_count;
    head.word_count = (uint32_t)code_word_count;
    head.label_count = (uint32_t)code_label_count;
    head.lib_count = (uint32_t)code_lib_count;
    head.file_count = (uint32_t)code_file_count;
    head.stack_size = (uint32_t)current_mh.stack_size;
    head.raw_bytes_count = (uint32_t)current_mh.raw_bytes_count;
    head.sector_mapping = (uint32_t)current_mh.sector_mapping;
    head.target_address = current_mh.target_address;
    head.tiny_ram_fallback = current_mh.tiny_ram_fallback ? 1 : 0;
    snprintf(head.container_name, sizeof(head.container_name), "%s", current_mh.container_name);
    snprintf(head.source_file, sizeof(head.source_file), "%s", source_file);

    fwrite(&head, sizeof(head), 1, f);
    if (code_lib_count > 0) fwrite(code_libs, sizeof(LibraryRef), code_lib_count, f);
    if (program_length > 0) fwrite(program, sizeof(AtomInstruction), program_length, f);
    if (code_name_count > 0) fwrite(code_names, MAX_WORD_LEN, code_name_count, f);
    if (code_word_count > 0) fwrite(code_words, sizeof(WordDefinition), code_word_count, f);
    if (code_label_count > 0) fwrite(code_labels, sizeof(CodeLabel), code_label_count, f);
    if (code_file_count > 0) fwrite(code_files, 256, code_file_count, f);
    if (current_mh.stack_size > 0) {
        fwrite(current_mh.stack_context, sizeof(StackItem), current_mh.stack_size, f);
    }
    if (current_mh.raw_bytes_count > 0) {
        fwrite(current_mh.raw_bytes, 1, current_mh.raw_bytes_count, f);
    }
    fclose(f);
    printf("[AOT] Cache saved: %s\n", path);
}

bool parse_atom_system(const char *filename) {
    if (load_aot_cache(filename)) return true;

    printf("[AOT] Compiling %s...\n", filename);

    FILE *file = fopen(filename, "r");
    if (!file) {
        fprintf(stderr, "[ERROR] Cannot open file: %s\n", filename);
        return false;
    }

    reset_program();
    s_file_idx = register_file(filename);

    char dir[512];
    path_dir(filename, dir, sizeof(dir));

    bool in_block_comment = false;
    bool ok = parse_lines(file, dir, &in_block_comment, 0);
    fclose(file);

    if (!ok) return false;

    if (in_block_comment) {
        fprintf(stderr, "[WARNING] Unterminated /* comment in %s\n", filename);
    }

    if (!expand_libraries()) return false;

    if (assemble_program() < 0) return false;

    if (s_trace) dump_program();

    save_aot_cache(filename);
    return true;
}
