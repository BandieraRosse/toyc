/* File I/O belongs to startup. Game parses an atomic, per-instance policy. */
void toy_game_gameplay_defaults(struct toy_game_gameplay_config *out)
{
    if (!out) return;
#define TOY_GAMEPLAY_FIELD(name, fallback, minimum, maximum) out->name = fallback;
#include "toy_gameplay_fields.inc"
#undef TOY_GAMEPLAY_FIELD
}

static int gameplay_integer(const char *text, int *value)
{
    int result = 0;
    if (*text < '0' || *text > '9') return 0;
    while (*text >= '0' && *text <= '9') {
        if (result > 3600000 / 10) return 0;
        result = result * 10 + (*text++ - '0');
    }
    if (*text) return 0;
    *value = result;
    return 1;
}

int toy_game_gameplay_parse(const char *text, int size,
    struct toy_game_gameplay_config *out, int *error_line, const char **error)
{
    static const char *keys[] = {
#define TOY_GAMEPLAY_FIELD(name, fallback, minimum, maximum) #name,
#include "toy_gameplay_fields.inc"
#undef TOY_GAMEPLAY_FIELD
    };
    static const int minimums[] = {
#define TOY_GAMEPLAY_FIELD(name, fallback, minimum, maximum) minimum,
#include "toy_gameplay_fields.inc"
#undef TOY_GAMEPLAY_FIELD
    };
    static const int maximums[] = {
#define TOY_GAMEPLAY_FIELD(name, fallback, minimum, maximum) maximum,
#include "toy_gameplay_fields.inc"
#undef TOY_GAMEPLAY_FIELD
    };
    struct toy_game_gameplay_config parsed;
    int *values[] = {
#define TOY_GAMEPLAY_FIELD(name, fallback, minimum, maximum) &parsed.name,
#include "toy_gameplay_fields.inc"
#undef TOY_GAMEPLAY_FIELD
    };
    unsigned char seen[sizeof(keys) / sizeof(keys[0])] = {0};
    int position = 0, line_number = 0;
    int key_count = sizeof(keys) / sizeof(keys[0]);
    const char *reason = "invalid input";
    if (error_line) *error_line = 0;
    if (error) *error = NULL;
    if (!text || !out || size < 0 || size > 8192) goto invalid;
    toy_game_gameplay_defaults(&parsed);
    if (size >= 3 && (unsigned char)text[0] == 239 &&
        (unsigned char)text[1] == 187 && (unsigned char)text[2] == 191) position = 3;
    while (position < size) {
        char line[256], *key, *equal, *comment, *value_text;
        int count = 0, index, value;
        line_number++;
        while (position < size && text[position] != '\n') {
            if (count >= 255) { reason = "line too long"; goto invalid; }
            if (!text[position]) { reason = "embedded NUL"; goto invalid; }
            line[count++] = text[position++];
        }
        if (position < size) position++;
        line[count] = 0;
        comment = strchr(line, '#'); if (comment) *comment = 0;
        key = game_config_trim(line); if (!*key) continue;
        equal = strchr(key, '=');
        if (!equal) { reason = "expected key = value"; goto invalid; }
        *equal = 0; key = game_config_trim(key); value_text = game_config_trim(equal + 1);
        for (index = 0; index < key_count; ++index)
            if (!strcmp(key, keys[index])) break;
        if (index == key_count) { reason = "unknown parameter"; goto invalid; }
        if (seen[index]) { reason = "duplicate parameter"; goto invalid; }
        if (!gameplay_integer(value_text, &value)) {
            reason = "expected nonnegative integer"; goto invalid;
        }
        if (value < minimums[index] || value > maximums[index]) {
            reason = "parameter out of range"; goto invalid;
        }
        *values[index] = value;
        seen[index] = 1;
    }
    memcpy(out, &parsed, sizeof(parsed));
    return 0;
invalid:
    if (error_line) *error_line = line_number;
    if (error) *error = reason;
    return -1;
}

/* Apply after init, before any gameplay steps or world actors are created. */
void toy_game_apply_gameplay_config(struct toy_game *g,
    const struct toy_game_gameplay_config *config)
{
    if (!g || !config) return;
    memcpy(&g->gameplay_config, config, sizeof(*config));
    g->spawn_timer_ms = config->wave_first_delay_ms;
    g->base_regen_timer_ms = config->base_regen_ms;
}
