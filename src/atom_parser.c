#include "atom.h"

AtomInstruction program[MAX_CODE_LEN];
int program_length = 0;
MhContainer current_mh;

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
    snprintf(cache_path, sizeof(cache_path), "%s\\%s.aot", AOT_CACHE_DIR, basename);
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

    char line[MAX_LINE_LEN];
    current_mh.stack_size = 0;
    memset(current_mh.stack_context, 0, sizeof(current_mh.stack_context));
    memset(current_mh.raw_bytes, 0, sizeof(current_mh.raw_bytes));
    current_mh.raw_bytes_count = 0;
    current_mh.sector_mapping = 0;
    current_mh.target_address = 0;
    current_mh.tiny_ram_fallback = false;
    strcpy(current_mh.container_name, "");
    program_length = 0;
    
    while (fgets(line, sizeof(line), file)) {
        char *comment = strchr(line, ';');
        if (comment) *comment = '\0';

        char *ptr = line;
        while (*ptr && isspace((unsigned char)*ptr)) ptr++;
        if (!*ptr) continue;

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
                    fclose(file);
                    return false;
                }
            } else {
                ptr++;
            }
        }
    }

    fclose(file);
    
    AotCache new_cache;
    build_aot_cache(filename, &new_cache);
    save_aot_cache(filename, &new_cache);
    
    return true;
}
