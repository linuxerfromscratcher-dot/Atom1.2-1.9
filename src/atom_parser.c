#include "atom.h"

AtomInstruction program[MAX_CODE_LEN];
int program_length = 0;
MhContainer current_mh;

static char s_import_files[MAX_IMPORTS][256];
static uint32_t s_import_checksums[MAX_IMPORTS];
static int s_import_count = 0;

#define MAX_IMPORT_DEPTH 32

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

char* get_cache_path(const char *source_file) {
    static char cache_path[512];
    const char *slash = strrchr(source_file, '/');
    const char *bslash = strrchr(source_file, '\\');
    const char *basename = NULL;
    if (slash && bslash) {
        basename = (slash > bslash) ? slash : bslash;
    } else if (slash) {
        basename = slash;
    } else if (bslash) {
        basename = bslash;
    }
    if (basename) basename++;
    else basename = source_file;
#ifdef _WIN32
    snprintf(cache_path, sizeof(cache_path), "%s\\%s.aot", AOT_CACHE_DIR, basename);
#else
    snprintf(cache_path, sizeof(cache_path), "%s/%s.aot", AOT_CACHE_DIR, basename);
#endif
    return cache_path;
}

unsigned int compute_checksum(const char *filename) {
    FILE *f = fopen(filename, "rb");
    if (!f) return 0;
    unsigned int checksum = 0;
    int ch;
    while ((ch = fgetc(f)) != EOF) {
        checksum = (checksum << 1) | (checksum >> 31);
        checksum ^= ch;
    }
    fclose(f);
    return checksum;
}

bool load_aot_cache(const char *source_file, AotCache *cache) {
    const char *cache_path = get_cache_path(source_file);
    FILE *f = fopen(cache_path, "rb");
    if (!f) return false;
    
    if (fread(cache, sizeof(AotCache), 1, f) != 1) {
        fclose(f);
        return false;
    }
    fclose(f);
    
    if (cache->magic != AOT_MAGIC || cache->version != AOT_VERSION) {
        return false;
    }
    
    unsigned int current_checksum = compute_checksum(source_file);
    if (cache->checksum != current_checksum) {
        printf("[AOT] Cache invalid (checksum mismatch), recompiling...\n");
        return false;
    }
    
    struct stat src_stat, cache_stat;
    if (stat(source_file, &src_stat) == 0 && stat(cache_path, &cache_stat) == 0) {
        if (src_stat.st_mtime > cache_stat.st_mtime) {
            printf("[AOT] Source file newer than cache, recompiling...\n");
            return false;
        }
    }
    
    for (int i = 0; i < cache->import_count && i < MAX_IMPORTS; i++) {
        const char *import_path = cache->import_files[i];
        if (compute_checksum(import_path) != cache->import_checksums[i]) {
            printf("[AOT] Cache invalid (import changed): %s\n", import_path);
            return false;
        }
        struct stat imp_stat;
        if (stat(import_path, &imp_stat) == 0 && stat(cache_path, &cache_stat) == 0) {
            if (imp_stat.st_mtime > cache_stat.st_mtime) {
                printf("[AOT] Cache invalid (import newer): %s\n", import_path);
                return false;
            }
        }
    }
    
    printf("[AOT] Cache loaded: %s (checksum: 0x%08X)\n", cache_path, cache->checksum);
    return true;
}

void save_aot_cache(const char *source_file, const AotCache *cache) {
    ensure_cache_dir();
    const char *cache_path = get_cache_path(source_file);
    FILE *f = fopen(cache_path, "wb");
    if (!f) {
        fprintf(stderr, "[AOT] Warning: Cannot write cache to %s\n", cache_path);
        return;
    }
    fwrite(cache, sizeof(AotCache), 1, f);
    fclose(f);
    printf("[AOT] Cache saved: %s\n", cache_path);
}

void build_aot_cache(const char *source_file, AotCache *cache) {
    cache->magic = AOT_MAGIC;
    cache->version = AOT_VERSION;
    cache->timestamp = (uint32_t)time(NULL);
    cache->program_length = program_length;
    cache->checksum = compute_checksum(source_file);
    strncpy(cache->source_file, source_file, sizeof(cache->source_file) - 1);
    memcpy(cache->program, program, sizeof(AtomInstruction) * program_length);
    memcpy(&cache->container, &current_mh, sizeof(MhContainer));
    cache->import_count = s_import_count;
    for (int i = 0; i < s_import_count && i < MAX_IMPORTS; i++) {
        strncpy(cache->import_files[i], s_import_files[i], 255);
        cache->import_files[i][255] = '\0';
        cache->import_checksums[i] = s_import_checksums[i];
    }
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
    strcpy(line, cleaned);
}

static bool parse_lines(FILE *file, const char *dir, bool *in_block_comment, int depth);

static bool parse_import(const char *dir, bool *in_block_comment, char *ptr, int depth) {
    if (depth >= MAX_IMPORT_DEPTH) {
        fprintf(stderr, "[ERROR] Import nesting too deep: %s\n", ptr);
        return false;
    }

    ptr += 6;
    while (*ptr && isspace((unsigned char)*ptr)) ptr++;

    char name[256];
    name[0] = '\0';
    if (*ptr == '"') {
        ptr++;
        int i = 0;
        while (*ptr && *ptr != '"' && i < 255) name[i++] = *ptr++;
        name[i] = '\0';
    } else {
        int i = 0;
        while (*ptr && !isspace((unsigned char)*ptr) && i < 255) name[i++] = *ptr++;
        name[i] = '\0';
    }
    if (!*name) {
        fprintf(stderr, "[ERROR] Empty import path\n");
        return false;
    }

    char path[512];
    if (name[0] == '/') {
        snprintf(path, sizeof(path), "%s", name);
    } else {
        snprintf(path, sizeof(path), "%s/%s", dir, name);
    }

    FILE *imp = fopen(path, "r");
    if (!imp) {
        fprintf(stderr, "[ERROR] Cannot import: %s\n", path);
        return false;
    }

    if (s_import_count < MAX_IMPORTS) {
        strncpy(s_import_files[s_import_count], path, 255);
        s_import_files[s_import_count][255] = '\0';
        s_import_checksums[s_import_count] = compute_checksum(path);
        s_import_count++;
    }

    char sub_dir[512];
    const char *slash = strrchr(path, '/');
    if (slash) snprintf(sub_dir, sizeof(sub_dir), "%.*s", (int)(slash - path), path);
    else strcpy(sub_dir, ".");

    bool ok = parse_lines(imp, sub_dir, in_block_comment, depth + 1);
    fclose(imp);
    return ok;
}

static bool parse_lines(FILE *file, const char *dir, bool *in_block_comment, int depth) {
    char line[MAX_LINE_LEN];
    while (fgets(line, sizeof(line), file)) {
        strip_comments(line, in_block_comment);

        char *ptr = line;
        while (*ptr && isspace((unsigned char)*ptr)) ptr++;
        if (!*ptr) continue;

        if (strncmp(ptr, "import", 6) == 0 &&
            (ptr[6] == '\0' || isspace((unsigned char)ptr[6]) || ptr[6] == '"')) {
            if (!parse_import(dir, in_block_comment, ptr, depth)) return false;
            continue;
        }

        if (strncmp(ptr, "F/", 2) == 0) {
            char name[64];
            if (sscanf(ptr, "F/%[^/]/", name) == 1) {
                strncpy(current_mh.container_name, name, 63);
            }
            continue;
        }
        
        if (*ptr == 'S' && has_ref_colon(ptr)) {
            current_mh.sector_mapping = atoi(ptr + 1);
            continue;
        }
        
        if (*ptr == 'M' && has_ref_colon(ptr)) {
            unsigned int addr = atoi(ptr + 1);
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
            if (sscanf(ptr, "%x", &byte_val) == 1) {
                if (current_mh.raw_bytes_count < 256) {
                    current_mh.raw_bytes[current_mh.raw_bytes_count++] = (unsigned char)byte_val;
                }
            }
            continue;
        }
        
        if (*ptr == '"') {
            ptr++;
            char str_val[256];
            int i = 0;
            while (*ptr && *ptr != '"' && i < 255) {
                str_val[i++] = *ptr++;
            }
            str_val[i] = '\0';
            if (current_mh.stack_size < STACK_SIZE) {
                StackItem item;
                item.type = TYPE_STRING;
                strncpy(item.value.string, str_val, 255);
                current_mh.stack_context[current_mh.stack_size++] = item;
            }
            continue;
        }
        
        if (*ptr == '\'') {
            ptr++;
            if (current_mh.stack_size < STACK_SIZE) {
                StackItem item;
                item.type = TYPE_CHAR;
                item.value.character = *ptr;
                current_mh.stack_context[current_mh.stack_size++] = item;
            }
            continue;
        }
        
        if (is_label_line(ptr)) {
            continue;
        }
        
        if (isdigit((unsigned char)*ptr) || *ptr == '-') {
            while (*ptr) {
                while (*ptr && isspace((unsigned char)*ptr)) ptr++;
                if (!*ptr) break;
                if (isdigit((unsigned char)*ptr) || *ptr == '-') {
                    long val = atol(ptr);
                    if (current_mh.stack_size < STACK_SIZE) {
                        StackItem item;
                        item.type = TYPE_NUMBER;
                        item.value.number = val;
                        current_mh.stack_context[current_mh.stack_size++] = item;
                    }
                    while (isdigit((unsigned char)*ptr) || *ptr == '-') ptr++;
                } else {
                    ptr++;
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

            if (*ptr == ':') {
                ptr++;
                while (*ptr && !isspace((unsigned char)*ptr)) ptr++;
                if (program_length < MAX_CODE_LEN) {
                    program[program_length++] = (AtomInstruction){'W', 0, 0, false};
                }
                continue;
            }

            if (isalpha((unsigned char)*ptr)) {
                char cmd = toupper((unsigned char)*ptr);
                ptr++;

                int arg = 0, sub_arg = 0;
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
                    fprintf(stderr, "[ERROR] Program too long!\n");
                    return false;
                }
            } else {
                ptr++;
            }
        }
    }
    return true;
}

bool parse_atom_system(const char *filename) {
    AotCache cache;
    if (load_aot_cache(filename, &cache)) {
        program_length = cache.program_length;
        memcpy(program, cache.program, sizeof(AtomInstruction) * program_length);
        memcpy(&current_mh, &cache.container, sizeof(MhContainer));
        return true;
    }
    
    printf("[AOT] Compiling %s...\n", filename);
    
    FILE *file = fopen(filename, "r");
    if (!file) {
        fprintf(stderr, "[ERROR] Cannot open file: %s\n", filename);
        return false;
    }

    bool in_block_comment = false;
    current_mh.stack_size = 0;
    memset(current_mh.stack_context, 0, sizeof(current_mh.stack_context));
    memset(current_mh.raw_bytes, 0, sizeof(current_mh.raw_bytes));
    current_mh.raw_bytes_count = 0;
    current_mh.sector_mapping = 0;
    current_mh.target_address = 0;
    current_mh.tiny_ram_fallback = false;
    strcpy(current_mh.container_name, "");
    program_length = 0;
    s_import_count = 0;

    const char *slash = strrchr(filename, '/');
    char dir[512];
    if (slash) {
        snprintf(dir, sizeof(dir), "%.*s", (int)(slash - filename), filename);
    } else {
        strcpy(dir, ".");
    }

    bool ok = parse_lines(file, dir, &in_block_comment, 0);
    fclose(file);

    if (!ok) return false;

    if (in_block_comment) {
        fprintf(stderr, "[WARNING] Unterminated /* comment in %s\n", filename);
    }
    
    AotCache new_cache;
    build_aot_cache(filename, &new_cache);
    save_aot_cache(filename, &new_cache);
    
    return true;
}
