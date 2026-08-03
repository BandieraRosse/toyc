/* SPDX-License-Identifier: MIT */
#include "checkpoint.h"

static unsigned int read_u32(const unsigned char *p)
{
    return (unsigned int)p[0] | ((unsigned int)p[1] << 8) |
           ((unsigned int)p[2] << 16) | ((unsigned int)p[3] << 24);
}

static unsigned long long read_u64(const unsigned char *p)
{
    unsigned long long lo = read_u32(p);
    unsigned long long hi = read_u32(p + 4);
    return lo | (hi << 32);
}

static int bytes_equal(const char *a, const char *b, int count)
{
    for (int i = 0; i < count; i++) if (a[i] != b[i]) return 0;
    return 1;
}

static int bounded_name_equal(const char *entry, const char *name)
{
    for (int i = 0; i < LLM_TENSOR_NAME_SIZE; i++) {
        if (entry[i] != name[i]) return 0;
        if (name[i] == '\0') return 1;
    }
    return 0;
}

static const unsigned char *find_bytes(const unsigned char *p,
                                       const unsigned char *end,
                                       const char *needle)
{
    size_t n = (size_t)strlen(needle);
    for (; (size_t)(end - p) >= n; p++)
        if (bytes_equal((const char *)p, needle, (int)n)) return p;
    return NULL;
}

static int parse_uint(const unsigned char **cursor, const unsigned char *end,
                      unsigned long long *value)
{
    const unsigned char *p = *cursor;
    while (p < end && (*p == ' ' || *p == '\n' || *p == '\r' || *p == '\t')) p++;
    if (p == end || *p < '0' || *p > '9') return -1;
    unsigned long long result = 0;
    while (p < end && *p >= '0' && *p <= '9') {
        unsigned int digit = (unsigned int)(*p++ - '0');
        if (result > (~0ULL - digit) / 10) return -1;
        result = result * 10 + digit;
    }
    *cursor = p; *value = result;
    return 0;
}

static int safetensors_open(LLMCheckpoint *checkpoint, void *mapping,
                            size_t file_size)
{
    const unsigned char *base = mapping;
    unsigned long long header_size = read_u64(base);
    if (header_size == 0 || header_size > file_size - 8) return -1;
    const unsigned char *p = base + 8, *end = p + header_size;
    int count = 0, depth = 0;
    for (const unsigned char *q = p; q < end; q++) {
        if (*q == '{') { depth++; continue; }
        if (*q == '}') { depth--; continue; }
        if (*q != '"' || depth != 1) continue;
        const unsigned char *quote = q + 1;
        while (quote < end && *quote != '"') quote++;
        if (quote == end) return -1;
        if (quote - q - 1 < LLM_TENSOR_NAME_SIZE &&
            !(quote - q - 1 == 12 && bytes_equal((const char *)q + 1, "__metadata__", 12)))
            count++;
        q = quote;
    }
    LLMTensor *tensors = tlibc_malloc((size_t)count * sizeof(*tensors));
    void **owned = tlibc_malloc((size_t)count * sizeof(*owned));
    if (!tensors || !owned) { if (tensors) tlibc_free(tensors); if (owned) tlibc_free(owned); return -1; }
    for (int i = 0; i < count; i++) owned[i] = NULL;
    int index = 0;
    depth = 0;
    while (p < end && index < count) {
        while (p < end) {
            if (*p == '{') depth++;
            else if (*p == '}') depth--;
            else if (*p == '"' && depth == 1) break;
            p++;
        }
        if (p == end) break;
        const unsigned char *name = ++p;
        while (p < end && *p != '"') p++;
        size_t name_len = (size_t)(p - name);
        p++;
        if (name_len == 12 && bytes_equal((const char *)name, "__metadata__", 12)) continue;
        if (name_len == 0 || name_len >= LLM_TENSOR_NAME_SIZE) goto fail;
        const unsigned char *object_end = find_bytes(p, end, "}");
        if (!object_end) object_end = end;
        const unsigned char *dtype = find_bytes(p, object_end, "\"dtype\":\"");
        const unsigned char *shape = find_bytes(p, object_end, "\"shape\":[");
        const unsigned char *offsets = find_bytes(p, object_end, "\"data_offsets\":[");
        if (!dtype || !shape || !offsets) goto fail;
        dtype += 9; shape += 9; offsets += 16;
        int type = 0, bytes = 0;
        if (dtype + 4 <= end && bytes_equal((const char *)dtype, "BF16", 4)) { type = 2; bytes = 2; }
        else if (dtype + 3 <= end && bytes_equal((const char *)dtype, "F32", 3)) { type = 1; bytes = 4; }
        else goto fail;
        LLMTensor *t = &tensors[index];
        llm_memcpy(t, &(LLMTensor){0}, sizeof(*t));
        char *saved_name = tlibc_malloc(name_len + 1);
        if (!saved_name) goto fail;
        llm_memcpy(saved_name, name, name_len); saved_name[name_len] = 0;
        t->name = saved_name;
        const unsigned char *s = shape;
        while (s < end && *s != ']' && t->ndim < LLM_TENSOR_MAX_DIMS) {
            unsigned long long dim;
            if (parse_uint(&s, end, &dim) != 0 || dim == 0) goto fail;
            t->shape[t->ndim++] = dim;
            while (s < end && (*s == ' ' || *s == ',')) s++;
        }
        unsigned long long begin, finish;
        if (!t->ndim || parse_uint(&offsets, end, &begin) != 0) goto fail;
        while (offsets < end && *offsets != ',') offsets++;
        if (offsets == end) goto fail;
        offsets++;
        if (parse_uint(&offsets, end, &finish) != 0 || finish < begin) goto fail;
        unsigned long long elements = 1;
        for (int d = 0; d < t->ndim; d++) elements *= t->shape[d];
        if (finish - begin != elements * (unsigned long long)bytes ||
            finish > file_size - 8 - header_size) goto fail;
        const unsigned char *raw = base + 8 + header_size + begin;
        if (type == 1) t->data = (const float *)raw;
        else {
            float *converted = tlibc_malloc((size_t)elements * sizeof(float));
            if (!converted) goto fail;
            for (unsigned long long j = 0; j < elements; j++) {
                unsigned int bits = (unsigned int)raw[j * 2] | ((unsigned int)raw[j * 2 + 1] << 8);
                bits <<= 16; llm_memcpy(&converted[j], &bits, 4);
            }
            owned[index] = converted; t->data = converted;
        }
        t->dtype = LLM_DTYPE_F32; t->nbytes = elements * 4; index++;
    }
    checkpoint->mapping = mapping; checkpoint->mapping_size = file_size;
    checkpoint->tensors = tensors; checkpoint->owned_data = owned;
    checkpoint->tensor_count = index; return index == count ? 0 : -1;
fail:
    for (int i = 0; i < count; i++) { if (owned[i]) tlibc_free(owned[i]); if (i < index && tensors[i].name) tlibc_free((void *)tensors[i].name); }
    tlibc_free(owned); tlibc_free(tensors); return -1;
}

int llm_checkpoint_open(LLMCheckpoint *checkpoint, const char *path)
{
    checkpoint->mapping = NULL;
    checkpoint->mapping_size = 0;
    checkpoint->directory = NULL;
    checkpoint->tensor_count = 0;
    checkpoint->tensors = NULL;
    checkpoint->owned_data = NULL;

    int fd = __openat(AT_FDCWD, path, O_RDONLY, 0);
    if (fd < 0) return -1;
    off_t file_size = __lseek(fd, 0, SEEK_END);
    if (file_size < LLM_CHECKPOINT_HEADER_SIZE) { __close(fd); return -1; }
    void *mapping = __mmap(NULL, (size_t)file_size, PROT_READ, MAP_PRIVATE, fd, 0);
    __close(fd);
    if (mapping == MAP_FAILED) return -1;

    const unsigned char *header = (const unsigned char *)mapping;
    if (!bytes_equal((const char *)header, LLM_CHECKPOINT_MAGIC, 8)) {
        if (safetensors_open(checkpoint, mapping, (size_t)file_size) == 0) return 0;
        __munmap(mapping, (size_t)file_size); return -1;
    }
    if (
        read_u32(header + 8) != LLM_CHECKPOINT_VERSION) {
        __munmap(mapping, (size_t)file_size);
        return -1;
    }
    unsigned int count = read_u32(header + 12);
    unsigned long long directory_offset = read_u64(header + 16);
    unsigned long long directory_end = directory_offset +
        (unsigned long long)count * LLM_CHECKPOINT_ENTRY_SIZE;
    if (directory_offset < LLM_CHECKPOINT_HEADER_SIZE ||
        directory_end < directory_offset || directory_end > (size_t)file_size) {
        __munmap(mapping, (size_t)file_size);
        return -1;
    }
    checkpoint->mapping = mapping;
    checkpoint->mapping_size = (size_t)file_size;
    checkpoint->directory = header + directory_offset;
    checkpoint->tensor_count = (int)count;
    return 0;
}

void llm_checkpoint_close(LLMCheckpoint *checkpoint)
{
    if (checkpoint->tensors) {
        for (int i = 0; i < checkpoint->tensor_count; i++) {
            if (checkpoint->owned_data[i]) tlibc_free(checkpoint->owned_data[i]);
            if (checkpoint->tensors[i].name) tlibc_free((void *)checkpoint->tensors[i].name);
        }
        tlibc_free(checkpoint->owned_data); tlibc_free(checkpoint->tensors);
    }
    if (checkpoint->mapping)
        __munmap(checkpoint->mapping, checkpoint->mapping_size);
    checkpoint->mapping = NULL;
    checkpoint->mapping_size = 0;
    checkpoint->directory = NULL;
    checkpoint->tensor_count = 0;
    checkpoint->tensors = NULL; checkpoint->owned_data = NULL;
}

int llm_checkpoint_tensor(const LLMCheckpoint *checkpoint, int index,
                          LLMTensor *tensor)
{
    if (!checkpoint->mapping || index < 0 || index >= checkpoint->tensor_count)
        return -1;
    if (checkpoint->tensors) { *tensor = checkpoint->tensors[index]; return 0; }
    const unsigned char *entry = checkpoint->directory +
        (size_t)index * LLM_CHECKPOINT_ENTRY_SIZE;
    int dtype = (int)read_u32(entry + 64);
    int ndim = (int)read_u32(entry + 68);
    unsigned long long offset = read_u64(entry + 104);
    unsigned long long nbytes = read_u64(entry + 112);
    if (dtype != LLM_DTYPE_F32 || ndim < 1 || ndim > LLM_TENSOR_MAX_DIMS ||
        offset > checkpoint->mapping_size ||
        nbytes > checkpoint->mapping_size - offset)
        return -1;
    tensor->name = (const char *)entry;
    tensor->data = (const float *)((const unsigned char *)checkpoint->mapping + offset);
    tensor->dtype = dtype;
    tensor->ndim = ndim;
    tensor->nbytes = nbytes;
    for (int i = 0; i < LLM_TENSOR_MAX_DIMS; i++)
        tensor->shape[i] = read_u64(entry + 72 + i * 8);
    unsigned long long elements = 1;
    for (int i = 0; i < ndim; i++) {
        if (tensor->shape[i] == 0 || elements > nbytes / 4 / tensor->shape[i])
            return -1;
        elements *= tensor->shape[i];
    }
    if (elements * 4 != nbytes) return -1;
    return 0;
}

int llm_checkpoint_find(const LLMCheckpoint *checkpoint, const char *name,
                        LLMTensor *tensor)
{
    if (checkpoint->tensors) {
        for (int i = 0; i < checkpoint->tensor_count; i++)
            if (strcmp(checkpoint->tensors[i].name, name) == 0) {
                *tensor = checkpoint->tensors[i]; return 0;
            }
        return -1;
    }
    for (int i = 0; i < checkpoint->tensor_count; i++) {
        const char *entry_name = (const char *)(checkpoint->directory +
            (size_t)i * LLM_CHECKPOINT_ENTRY_SIZE);
        if (bounded_name_equal(entry_name, name))
            return llm_checkpoint_tensor(checkpoint, i, tensor);
    }
    return -1;
}
