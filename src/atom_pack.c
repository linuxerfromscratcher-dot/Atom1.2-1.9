#include "atom.h"
#include "atom_abi.h"


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
} PackHeader;

typedef struct {
    unsigned char *data;
    size_t len;
    size_t cap;
    bool ok;
} PackWriter;

typedef struct {
    const unsigned char *data;
    size_t len;
    size_t pos;
    bool ok;
} PackReader;

static bool aot_cache_enabled = true;

void set_aot_enabled(bool enabled) {
    aot_cache_enabled = enabled;
}


static bool writer_reserve(PackWriter *w, size_t extra) {
    if (!w->ok) return false;
    if (w->len + extra <= w->cap) return true;

    size_t cap = w->cap ? w->cap : 4096;
    while (cap < w->len + extra) cap *= 2;

    unsigned char *grown = (unsigned char *)realloc(w->data, cap);
    if (!grown) {
        w->ok = false;
        return false;
    }
    w->data = grown;
    w->cap = cap;
    return true;
}

static void writer_raw(PackWriter *w, const void *src, size_t size) {
    if (!writer_reserve(w, size)) return;
    memcpy(w->data + w->len, src, size);
    w->len += size;
}

static void writer_u8(PackWriter *w, uint8_t value) {
    writer_raw(w, &value, sizeof(value));
}

static void writer_u32(PackWriter *w, uint32_t value) {
    unsigned char raw[4];
    raw[0] = (unsigned char)(value & 0xFF);
    raw[1] = (unsigned char)((value >> 8) & 0xFF);
    raw[2] = (unsigned char)((value >> 16) & 0xFF);
    raw[3] = (unsigned char)((value >> 24) & 0xFF);
    writer_raw(w, raw, sizeof(raw));
}

static void writer_u64(PackWriter *w, uint64_t value) {
    unsigned char raw[8];
    for (int i = 0; i < 8; i++) raw[i] = (unsigned char)((value >> (8 * i)) & 0xFF);
    writer_raw(w, raw, sizeof(raw));
}

static void writer_str(PackWriter *w, const char *text, size_t cap) {
    size_t len = 0;
    if (text) {
        while (len < cap && text[len]) len++;
    }
    writer_u32(w, (uint32_t)len);
    writer_raw(w, text ? text : "", len);
}


static bool reader_need(PackReader *r, size_t size) {
    if (!r->ok) return false;
    if (r->pos + size > r->len) {
        r->ok = false;
        return false;
    }
    return true;
}

static void reader_raw(PackReader *r, void *dst, size_t size) {
    if (!reader_need(r, size)) return;
    memcpy(dst, r->data + r->pos, size);
    r->pos += size;
}

static uint8_t reader_u8(PackReader *r) {
    uint8_t value = 0;
    reader_raw(r, &value, sizeof(value));
    return value;
}

static uint32_t reader_u32(PackReader *r) {
    unsigned char raw[4] = {0, 0, 0, 0};
    reader_raw(r, raw, sizeof(raw));
    return (uint32_t)raw[0] | ((uint32_t)raw[1] << 8) |
           ((uint32_t)raw[2] << 16) | ((uint32_t)raw[3] << 24);
}

static uint64_t reader_u64(PackReader *r) {
    unsigned char raw[8] = {0, 0, 0, 0, 0, 0, 0, 0};
    reader_raw(r, raw, sizeof(raw));
    uint64_t value = 0;
    for (int i = 0; i < 8; i++) value |= (uint64_t)raw[i] << (8 * i);
    return value;
}

static void reader_str(PackReader *r, char *out, size_t cap) {
    out[0] = '\0';
    uint32_t len = reader_u32(r);
    if (!r->ok) return;
    if ((size_t)len >= cap || !reader_need(r, len)) {
        r->ok = false;
        return;
    }
    memcpy(out, r->data + r->pos, len);
    out[len] = '\0';
    r->pos += len;
}

static int64_t reader_word(PackReader *r) {
    return (int64_t)reader_u64(r);
}


static void write_item(PackWriter *w, const StackItem *item) {
    writer_u32(w, (uint32_t)item->type);
    switch (item->type) {
        case TYPE_NUMBER:
            writer_u64(w, (uint64_t)item->value.number);
            break;
        case TYPE_CHAR:
            writer_u8(w, (uint8_t)item->value.character);
            break;
        case TYPE_STRING:
            writer_str(w, item->value.string, sizeof(item->value.string));
            break;
        case TYPE_WORD:
            writer_str(w, item->value.word, sizeof(item->value.word));
            break;
        case TYPE_INSTRUCTION:
            writer_u8(w, (uint8_t)item->value.instruction.cmd);
            writer_u32(w, (uint32_t)item->value.instruction.arg);
            writer_u32(w, (uint32_t)item->value.instruction.sub_arg);
            break;
        default:
            w->ok = false;
            break;
    }
}

static bool read_item(PackReader *r, StackItem *item) {
    memset(item, 0, sizeof(*item));
    uint32_t type = reader_u32(r);
    if (!r->ok || type > (uint32_t)TYPE_INSTRUCTION) {
        r->ok = false;
        return false;
    }
    item->type = (StackType)type;
    switch (item->type) {
        case TYPE_NUMBER:
            item->value.number = (long)reader_word(r);
            break;
        case TYPE_CHAR:
            item->value.character = (char)reader_u8(r);
            break;
        case TYPE_STRING:
            reader_str(r, item->value.string, sizeof(item->value.string));
            break;
        case TYPE_WORD:
            reader_str(r, item->value.word, sizeof(item->value.word));
            break;
        case TYPE_INSTRUCTION:
            item->value.instruction.cmd = (char)reader_u8(r);
            item->value.instruction.arg = (int32_t)reader_u32(r);
            item->value.instruction.sub_arg = (int32_t)reader_u32(r);
            break;
    }
    return r->ok;
}


unsigned char *pack_serialize_program(const char *source_file, size_t *out_size) {
    PackWriter w;
    memset(&w, 0, sizeof(w));
    w.ok = true;

    PackHeader head;
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
    head.tiny_ram_fallback = current_mh.tiny_ram_fallback ? 1u : 0u;
    snprintf(head.container_name, sizeof(head.container_name), "%s", current_mh.container_name);
    snprintf(head.source_file, sizeof(head.source_file), "%s", source_file ? source_file : "");

    writer_u32(&w, head.magic);
    writer_u32(&w, head.version);
    writer_u32(&w, head.timestamp);
    writer_u32(&w, head.checksum);
    writer_u32(&w, head.program_length);
    writer_u32(&w, head.name_count);
    writer_u32(&w, head.word_count);
    writer_u32(&w, head.label_count);
    writer_u32(&w, head.lib_count);
    writer_u32(&w, head.file_count);
    writer_u32(&w, head.stack_size);
    writer_u32(&w, head.raw_bytes_count);
    writer_u32(&w, head.sector_mapping);
    writer_u32(&w, head.target_address);
    writer_u32(&w, head.tiny_ram_fallback);
    writer_str(&w, head.container_name, sizeof(head.container_name));
    writer_str(&w, head.source_file, sizeof(head.source_file));

    for (int i = 0; i < code_lib_count; i++) {
        writer_str(&w, code_libs[i].path, sizeof(code_libs[i].path));
        writer_str(&w, code_libs[i].name, sizeof(code_libs[i].name));
        writer_u32(&w, code_libs[i].checksum);
        writer_u8(&w, code_libs[i].head_include ? 1 : 0);
    }

    for (int i = 0; i < program_length; i++) {
        writer_u8(&w, (uint8_t)program[i].cmd);
        writer_u32(&w, (uint32_t)program[i].arg);
        writer_u32(&w, (uint32_t)program[i].sub_arg);
        writer_u32(&w, (uint32_t)program[i].name_idx);
        writer_u32(&w, (uint32_t)program[i].line);
        writer_u32(&w, (uint32_t)program[i].file_idx);
        writer_u8(&w, program[i].has_arg ? 1 : 0);
        writer_u8(&w, program[i].is_ret ? 1 : 0);
    }

    for (int i = 0; i < code_name_count; i++) {
        writer_str(&w, code_names[i], MAX_WORD_LEN);
    }

    for (int i = 0; i < code_word_count; i++) {
        writer_str(&w, code_words[i].name, sizeof(code_words[i].name));
        writer_u32(&w, (uint32_t)code_words[i].start_pc);
        writer_u32(&w, (uint32_t)code_words[i].end_pc);
        writer_u32(&w, (uint32_t)code_words[i].library);
        writer_u8(&w, code_words[i].defined ? 1 : 0);
    }

    for (int i = 0; i < code_label_count; i++) {
        writer_u32(&w, (uint32_t)code_labels[i].id);
        writer_u32(&w, (uint32_t)code_labels[i].pc);
    }

    for (int i = 0; i < code_file_count; i++) {
        writer_str(&w, code_files[i], sizeof(code_files[i]));
    }

    for (int i = 0; i < current_mh.stack_size; i++) {
        write_item(&w, &current_mh.stack_context[i]);
    }

    if (current_mh.raw_bytes_count > 0) {
        writer_raw(&w, current_mh.raw_bytes, (size_t)current_mh.raw_bytes_count);
    }

    if (!w.ok) {
        fprintf(stderr, "[AOT] Warning: cannot serialise the program image\n");
        free(w.data);
        if (out_size) *out_size = 0;
        return NULL;
    }

    if (out_size) *out_size = w.len;
    return w.data;
}

static bool read_header(PackReader *r, PackHeader *head) {
    memset(head, 0, sizeof(*head));
    head->magic = reader_u32(r);
    head->version = reader_u32(r);
    head->timestamp = reader_u32(r);
    head->checksum = reader_u32(r);
    head->program_length = reader_u32(r);
    head->name_count = reader_u32(r);
    head->word_count = reader_u32(r);
    head->label_count = reader_u32(r);
    head->lib_count = reader_u32(r);
    head->file_count = reader_u32(r);
    head->stack_size = reader_u32(r);
    head->raw_bytes_count = reader_u32(r);
    head->sector_mapping = reader_u32(r);
    head->target_address = reader_u32(r);
    head->tiny_ram_fallback = reader_u32(r);
    reader_str(r, head->container_name, sizeof(head->container_name));
    reader_str(r, head->source_file, sizeof(head->source_file));
    return r->ok;
}

static bool header_counts_fit(const PackHeader *head) {
    return head->program_length <= MAX_CODE_LEN &&
           head->name_count <= MAX_CODE_NAMES &&
           head->word_count <= MAX_WORDS &&
           head->label_count <= MAX_LABELS &&
           head->lib_count <= MAX_LIBS &&
           head->file_count <= MAX_LIBS &&
           head->stack_size <= STACK_SIZE &&
           head->raw_bytes_count <= (uint32_t)sizeof(current_mh.raw_bytes);
}

static void read_body(PackReader *r, const PackHeader *head) {
    for (uint32_t i = 0; i < head->lib_count && r->ok; i++) {
        reader_str(r, code_libs[i].path, sizeof(code_libs[i].path));
        reader_str(r, code_libs[i].name, sizeof(code_libs[i].name));
        code_libs[i].checksum = reader_u32(r);
        code_libs[i].head_include = reader_u8(r) != 0;
        code_libs[i].expanded = true;
    }

    for (uint32_t i = 0; i < head->program_length && r->ok; i++) {
        program[i].cmd = (char)reader_u8(r);
        program[i].arg = (int32_t)reader_u32(r);
        program[i].sub_arg = (int32_t)reader_u32(r);
        program[i].name_idx = (int32_t)reader_u32(r);
        program[i].line = (int32_t)reader_u32(r);
        program[i].file_idx = (int32_t)reader_u32(r);
        program[i].has_arg = reader_u8(r) != 0;
        program[i].is_ret = reader_u8(r) != 0;
    }

    for (uint32_t i = 0; i < head->name_count && r->ok; i++) {
        reader_str(r, code_names[i], MAX_WORD_LEN);
    }

    for (uint32_t i = 0; i < head->word_count && r->ok; i++) {
        reader_str(r, code_words[i].name, sizeof(code_words[i].name));
        code_words[i].start_pc = (int32_t)reader_u32(r);
        code_words[i].end_pc = (int32_t)reader_u32(r);
        code_words[i].library = (int32_t)reader_u32(r);
        code_words[i].defined = reader_u8(r) != 0;
    }

    for (uint32_t i = 0; i < head->label_count && r->ok; i++) {
        code_labels[i].id = (int32_t)reader_u32(r);
        code_labels[i].pc = (int32_t)reader_u32(r);
    }

    for (uint32_t i = 0; i < head->file_count && r->ok; i++) {
        reader_str(r, code_files[i], sizeof(code_files[i]));
    }

    for (uint32_t i = 0; i < head->stack_size && r->ok; i++) {
        read_item(r, &current_mh.stack_context[i]);
    }

    if (head->raw_bytes_count > 0 && r->ok) {
        reader_raw(r, current_mh.raw_bytes, (size_t)head->raw_bytes_count);
    }

    if (head->program_length == 0) {
        r->ok = false;
    }
}

bool pack_deserialize_program(const unsigned char *data, size_t size, bool verify,
                              const char *source_file, const char *cache_path) {
    if (!data || size < 8) return false;

    PackReader r;
    memset(&r, 0, sizeof(r));
    r.data = data;
    r.len = size;
    r.ok = true;

    PackHeader head;
    if (!read_header(&r, &head)) return false;

    if (head.magic != AOT_MAGIC || head.version != AOT_VERSION) {
        return false;
    }

    if (!header_counts_fit(&head)) {
        if (verify) printf("[AOT] Cache rejected: image counts are out of range\n");
        return false;
    }

    if (verify && source_file) {
        if (head.checksum != compute_checksum(source_file)) {
            printf("[AOT] Cache invalid (checksum mismatch), recompiling...\n");
            return false;
        }

        if (cache_path) {
            struct stat src_stat, cache_stat;
            if (stat(source_file, &src_stat) == 0 && stat(cache_path, &cache_stat) == 0 &&
                src_stat.st_mtime > cache_stat.st_mtime) {
                printf("[AOT] Source newer than cache, recompiling...\n");
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

    read_body(&r, &head);

    if (!r.ok) {
        reset_program();
        return false;
    }

    if (verify) {
        for (int i = 0; i < code_lib_count; i++) {
            if (compute_checksum(code_libs[i].path) != code_libs[i].checksum) {
                printf("[AOT] Cache invalid (library changed): %s\n", code_libs[i].path);
                reset_program();
                return false;
            }
        }
    }

    return true;
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
    if (!filename || !*filename) return 0;
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

static bool read_whole_file(const char *path, unsigned char **out, size_t *out_size) {
    FILE *f = fopen(path, "rb");
    if (!f) return false;

    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return false;
    }
    long size = ftell(f);
    if (size <= 0 || fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return false;
    }

    unsigned char *data = (unsigned char *)malloc((size_t)size);
    if (!data) {
        fclose(f);
        return false;
    }

    size_t got = fread(data, 1, (size_t)size, f);
    fclose(f);

    if (got != (size_t)size) {
        free(data);
        return false;
    }

    *out = data;
    *out_size = got;
    return true;
}

bool load_aot_cache(const char *source_file) {
    if (!aot_cache_enabled) return false;

    char path[512];
    cache_path(source_file, path, sizeof(path));

    unsigned char *data = NULL;
    size_t size = 0;
    if (!read_whole_file(path, &data, &size)) return false;

    if (!pack_deserialize_program(data, size, true, source_file, path)) {
        free(data);
        return false;
    }
    free(data);

    printf("[AOT] Cache loaded: %s\n", path);
    return true;
}

void save_aot_cache(const char *source_file) {
    if (!aot_cache_enabled) return;
    ensure_cache_dir();

    char path[512];
    cache_path(source_file, path, sizeof(path));

    size_t size = 0;
    unsigned char *image = pack_serialize_program(source_file, &size);
    if (!image) return;

    FILE *f = fopen(path, "wb");
    if (!f) {
        fprintf(stderr, "[AOT] Warning: cannot write cache to %s\n", path);
        free(image);
        return;
    }

    bool ok = fwrite(image, 1, size, f) == size;
    fclose(f);
    free(image);

    if (!ok) {
        fprintf(stderr, "[AOT] Warning: short write for %s\n", path);
        return;
    }
    printf("[AOT] Cache saved: %s\n", path);
}
