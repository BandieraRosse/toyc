#include "tlibc_everything.h"
#include "rasterfall_world_content.h"
#include "core.h"

#define CONTENT_MAX_FILE (256 * 1024)

const char *rasterfall_world_map_path(enum rasterfall_world_id world)
{
    return world == RASTERFALL_WORLD_OUTPOST ?
        "rasterfall/assets/maps/outpost.map" :
        "rasterfall/assets/maps/rasterfall.map";
}

const char *rasterfall_world_content_path(enum rasterfall_world_id world)
{
    return world == RASTERFALL_WORLD_OUTPOST ?
        "rasterfall/assets/worlds/outpost.content" :
        "rasterfall/assets/worlds/campaign_01.content";
}

static char *trim(char *s)
{
    char *e;
    while (*s == ' ' || *s == '\t' || *s == '\r') s++;
    e = s + strlen(s);
    while (e > s && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r')) *--e = 0;
    return s;
}

static int field(char **p, char *key, int key_size, char *value, int value_size)
{
    char *s = *p, *eq, *e;
    while (*s == ' ' || *s == '\t') s++;
    if (!*s) { *p = s; return 0; }
    e = s;
    while (*e && *e != ' ' && *e != '\t') e++;
    if (*e) *e++ = 0;
    eq = strchr(s, '=');
    if (!eq || eq == s || !eq[1]) return -1;
    *eq++ = 0;
    if ((int)strlen(s) >= key_size || (int)strlen(eq) >= value_size) return -1;
    strcpy(key, s); strcpy(value, eq); *p = e; return 1;
}

static int number(const char *s, int *out)
{
    char *e; long v;
    if (!s || !*s) return -1;
    v = strtol(s, &e, 10);
    if (*e || v < -2147483648L || v > 2147483647L) return -1;
    *out = (int)v; return 0;
}

static const char *get(char keys[][RASTERFALL_CONTENT_KIND_SIZE],
                       char values[][RASTERFALL_CONTENT_ERROR_SIZE], int n,
                       const char *wanted)
{
    int i;
    for (i = 0; i < n; i++) if (!strcmp(keys[i], wanted)) return values[i];
    return NULL;
}

static int duplicate_id(const struct rasterfall_world_content *c,
                        const char *id)
{
    int i;
    for (i = 0; i < c->actor_count; i++) if (!strcmp(c->actors[i].id, id)) return 1;
    for (i = 0; i < c->terminal_count; i++) if (!strcmp(c->terminals[i].id, id)) return 1;
    for (i = 0; i < c->flag_definition_count; i++) if (!strcmp(c->flag_definitions[i].id, id)) return 1;
    for (i = 0; i < c->fixture_count; i++) if (!strcmp(c->fixtures[i].id, id)) return 1;
    return 0;
}

void rasterfall_world_content_clear(struct rasterfall_world_content *content)
{
    if (content) __memset(content, 0, sizeof(*content));
}

void rasterfall_world_content_build(struct rasterfall_world_content *content,
                                    enum rasterfall_world_id world)
{
    int i;
    static const char *maid_members[] = {
        "ANIME_GUARD_1", "ANIME_GUARD_2",
        "ANIME_GUARD_3", "ANIME_GUARD_4"
    };

    rasterfall_world_content_clear(content);
    if (!content) return;
    if (world == RASTERFALL_WORLD_OUTPOST) {
        content->spawn_null = 1;
        content->spawn_terminals = 1;
        return;
    }
    if (world != RASTERFALL_WORLD_CAMPAIGN_01) return;

    content->spawn_campaign_roster = 1;
    content->spawn_maid_squad = 1;
    content->spawn_campaign_support = 1;
    content->campaign_flags_enabled = 1;
    content->model_gallery_enabled = 1;
    content->character_test_strip_enabled = 1;
    content->campaign_fixture_enabled = 1;
    content->formation_count = 1;
    strcpy(content->formations[0].id, "maid_squad");
    content->formations[0].member_count = 4;
    for (i = 0; i < 4; i++)
        strcpy(content->formations[0].member_ids[i], maid_members[i]);
}

int rasterfall_world_content_load(struct rasterfall_world_content *content,
                                  enum rasterfall_world_id world,
                                  const char *path)
{
    int fd, total = 0, n, size, line_no = 1;
    struct stat st;
    char *data, *line;
    if (!content || !path) return -1;
    rasterfall_world_content_build(content, world);
    /* Keep the world policy, but replace its V0 static records with the
     * parsed definition. */
    content->actor_count = 0;
    content->terminal_count = 0;
    content->flag_definition_count = 0;
    content->fixture_count = 0;
    content->formation_count = 0;
    strncpy(content->source, path, sizeof(content->source) - 1);
    fd = __openat(AT_FDCWD, path, O_RDONLY, 0);
    if (fd < 0) { content->error_line = 0; strcpy(content->error, "cannot open content file"); return -1; }
    if (__fstat(fd, &st) < 0 || st.st_size <= 0 || st.st_size >= CONTENT_MAX_FILE) { __close(fd); strcpy(content->error, "content file size is invalid"); return -1; }
    size = (int)st.st_size; data = tlibc_malloc((unsigned long)size + 1);
    if (!data) { __close(fd); strcpy(content->error, "out of memory"); return -1; }
    while (total < size) { n = (int)__read(fd, data + total, size - total); if (n <= 0) { tlibc_free(data); __close(fd); strcpy(content->error, "cannot read content file"); return -1; } total += n; }
    __close(fd); data[size] = 0; line = data;
    while (line && *line) {
        char *next = strchr(line, '\n'), *comment, *p, key[RASTERFALL_CONTENT_KIND_SIZE], value[RASTERFALL_CONTENT_ERROR_SIZE];
        char keys[16][RASTERFALL_CONTENT_KIND_SIZE], values[16][RASTERFALL_CONTENT_ERROR_SIZE];
        int count = 0, result;
        if (next) { *next = 0; next++; }
        comment = strchr(line, '#'); if (comment) *comment = 0;
        p = trim(line);
        if (*p) {
            char *record = p;
            while (*p && *p != ' ' && *p != '\t') p++;
            if (*p) *p++ = 0;
            while ((result = field(&p, key, sizeof(key), value, sizeof(value))) > 0) {
                if (count >= 16) { strcpy(content->error, "too many fields"); content->error_line = line_no; goto fail; }
                strcpy(keys[count], key); strcpy(values[count], value); count++;
            }
            if (result < 0) { strcpy(content->error, "invalid field syntax"); content->error_line = line_no; goto fail; }
            {
                const char *id = get(keys, values, count, "id");
                if (!id || duplicate_id(content, id)) { strcpy(content->error, !id ? "record missing id" : "duplicate content id"); content->error_line = line_no; goto fail; }
                if (!strcmp(record, "actor")) {
                    struct rasterfall_content_actor *a;
                    if (content->actor_count >= RASTERFALL_CONTENT_MAX_ACTORS) goto capacity;
                    a = &content->actors[content->actor_count++]; __memset(a, 0, sizeof(*a));
                    strcpy(a->id, id); { const char *v = get(keys, values, count, "name"); strcpy(a->name, v ? v : id); v = get(keys, values, count, "character"); if (v) strcpy(a->character, v); }
                    if (number(get(keys, values, count, "x"), &a->x) < 0 || number(get(keys, values, count, "y"), &a->y) < 0 || number(get(keys, values, count, "z"), &a->z) < 0) goto malformed;
                    { const char *v = get(keys, values, count, "yaw"); if (v && number(v, &a->yaw) < 0) goto malformed; }
                    a->line = line_no;
                } else if (!strcmp(record, "terminal")) {
                    struct rasterfall_content_terminal *t;
                    if (content->terminal_count >= RASTERFALL_CONTENT_MAX_TERMINALS) goto capacity;
                    t = &content->terminals[content->terminal_count++]; __memset(t, 0, sizeof(*t)); strcpy(t->id, id);
                    { const char *v = get(keys, values, count, "kind"); if (!v) goto malformed; strcpy(t->kind, v); }
                    if (number(get(keys, values, count, "x"), &t->x) < 0 ||
                        number(get(keys, values, count, "y"), &t->y) < 0 ||
                        number(get(keys, values, count, "z"), &t->z) < 0)
                        goto malformed;
                    { const char *v = get(keys, values, count, "yaw"); if (v && number(v, &t->yaw) < 0) goto malformed; }
                    t->line = line_no;
                } else if (!strcmp(record, "flag")) {
                    struct rasterfall_content_flag *f;
                    if (content->flag_definition_count >= RASTERFALL_CONTENT_MAX_FLAGS) goto capacity;
                    f = &content->flag_definitions[content->flag_definition_count++]; __memset(f, 0, sizeof(*f)); strcpy(f->id, id);
                    if (number(get(keys, values, count, "x"), &f->x) < 0 ||
                        number(get(keys, values, count, "y"), &f->y) < 0 ||
                        number(get(keys, values, count, "z"), &f->z) < 0)
                        goto malformed;
                    f->line = line_no;
                } else if (!strcmp(record, "fixture")) {
                    struct rasterfall_content_fixture *f;
                    if (content->fixture_count >= RASTERFALL_CONTENT_MAX_FIXTURES) goto capacity;
                    f = &content->fixtures[content->fixture_count++]; __memset(f, 0, sizeof(*f)); strcpy(f->id, id); { const char *v = get(keys, values, count, "kind"); if (!v) goto malformed; strcpy(f->kind, v); } if (number(get(keys, values, count, "x"), &f->x) < 0 || number(get(keys, values, count, "y"), &f->y) < 0 || number(get(keys, values, count, "z"), &f->z) < 0) goto malformed; { const char *v = get(keys, values, count, "yaw"); if (v && number(v, &f->yaw) < 0) goto malformed; } f->line = line_no;
                } else if (!strcmp(record, "formation")) {
                    const char *members = get(keys, values, count, "members"); int j = 0; char *q, *comma;
                    { int k; for (k = 0; k < content->formation_count; k++)
                        if (!strcmp(content->formations[k].id, id)) {
                            strcpy(content->error, "duplicate content id");
                            content->error_line = line_no; goto fail;
                        } }
                    if (!members || content->formation_count >= RASTERFALL_CONTENT_MAX_FORMATIONS) goto malformed;
                    strcpy(content->formations[content->formation_count].id, id); content->formations[content->formation_count].member_count = 0;
                    q = (char *)members; while (*q && j < RASTERFALL_CONTENT_MAX_MEMBERS) { comma = strchr(q, ','); if (comma) *comma = 0; if ((int)strlen(q) >= RASTERFALL_CONTENT_ID_SIZE) goto malformed; strcpy(content->formations[content->formation_count].member_ids[j++], q); if (!comma) break; q = comma + 1; }
                    content->formations[content->formation_count].member_count = j; content->formation_count++;
                } else { strcpy(content->error, "unknown content record"); content->error_line = line_no; goto fail; }
            }
        }
        line = next; line_no++;
    }
    for (int i = 0; i < content->formation_count; i++) for (int j = 0; j < content->formations[i].member_count; j++) { int k, found = 0; for (k = 0; k < content->actor_count; k++) if (!strcmp(content->formations[i].member_ids[j], content->actors[k].id)) found = 1; if (!found) { strcpy(content->error, "formation member not found"); content->error_line = 0; goto fail; } }
    content->loaded = 1; tlibc_free(data); return 0;
capacity: strcpy(content->error, "content capacity exceeded"); content->error_line = line_no; goto fail;
malformed: strcpy(content->error, "malformed content record"); content->error_line = line_no;
fail: tlibc_free(data); return -1;
}
