#include "core.h"
#include "tlibc_everything.h"
#include "rasterfall_map_parser.h"

#define MAP_MAX_FILE (1024 * 1024)
#define MAP_MAX_FIELDS 32

struct map_field { char *key; char *value; };
struct map_line { char *record; struct map_field fields[MAP_MAX_FIELDS]; int count; };

static int fail(struct rasterfall_map_ir *ir, int line, const char *message)
{
    ir->error_line = line;
    snprintf(ir->error, sizeof(ir->error), "%s", message);
    return -1;
}

static int fail_field(struct rasterfall_map_ir *ir, int line, const char *message, const char *field)
{
    ir->error_line = line;
    snprintf(ir->error, sizeof(ir->error), message, field);
    return -1;
}

static int fail_missing(struct rasterfall_map_ir *ir, int line,
                        const char *record, const char *field)
{
    ir->error_line = line;
    snprintf(ir->error, sizeof(ir->error), "%s missing %s", record, field);
    return -1;
}

static char *trim(char *s)
{
    char *end;
    while (*s == ' ' || *s == '\t' || *s == '\r') s++;
    end = s + strlen(s);
    while (end > s && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\r')) *--end = 0;
    return s;
}

static int split_line(char *line, struct map_line *out)
{
    char *p, *eq;
    out->count = 0;
    line = trim(line);
    if (!*line) { out->record = 0; return 0; }
    p = line;
    while (*p && *p != ' ' && *p != '\t') p++;
    if (*p) *p++ = 0;
    out->record = line;
    while (*p) {
        char *token;
        while (*p == ' ' || *p == '\t') p++;
        if (!*p) break;
        token = p;
        while (*p && *p != ' ' && *p != '\t') p++;
        if (*p) *p++ = 0;
        if (out->count >= MAP_MAX_FIELDS) return -1;
        eq = strchr(token, '=');
        if (!eq || eq == token || !eq[1]) return -1;
        *eq = 0;
        out->fields[out->count].key = token;
        out->fields[out->count].value = eq + 1;
        out->count++;
    }
    return 0;
}

static int valid_name(const char *s, int allow_dot)
{
    int i;
    if (!s || !s[0] || !((s[0] >= 'a' && s[0] <= 'z') ||
                         (s[0] >= 'A' && s[0] <= 'Z') || s[0] == '_')) return 0;
    for (i = 1; s[i]; i++) {
        if ((s[i] >= 'a' && s[i] <= 'z') || (s[i] >= 'A' && s[i] <= 'Z') ||
            (s[i] >= '0' && s[i] <= '9') || s[i] == '_' || s[i] == '-' ||
            (allow_dot && s[i] == '.')) continue;
        return 0;
    }
    return 1;
}

static int copy_text(char *out, int capacity, const char *value)
{
    int length = (int)strlen(value);
    if (length <= 0 || length >= capacity) return -1;
    strcpy(out, value);
    return 0;
}

static int int_value(const char *s, int *out)
{
    long value = 0;
    int sign = 1, i = 0;
    if (!s || !s[0]) return -1;
    if (s[0] == '-') { sign = -1; i++; }
    else if (s[0] == '+') i++;
    if (!s[i]) return -1;
    for (; s[i]; i++) {
        if (s[i] < '0' || s[i] > '9') return -1;
        value = value * 10 + s[i] - '0';
        if (value > 2147483648L) return -1;
    }
    value *= sign;
    if (value < -2147483648L || value > 2147483647L) return -1;
    *out = (int)value;
    return 0;
}

static int bool_value(const char *s, int *out)
{
    if (!strcmp(s, "true")) *out = 1;
    else if (!strcmp(s, "false")) *out = 0;
    else return -1;
    return 0;
}

static int value_of(struct map_line *line, const char *key, const char **value)
{
    int i;
    for (i = 0; i < line->count; i++) {
        if (!strcmp(line->fields[i].key, key)) {
            *value = line->fields[i].value;
            return 1;
        }
    }
    return 0;
}

static int check_duplicate_fields(struct rasterfall_map_ir *ir, int line, struct map_line *fields)
{
    int i, j;
    for (i = 0; i < fields->count; i++) for (j = i + 1; j < fields->count; j++)
        if (!strcmp(fields->fields[i].key, fields->fields[j].key))
            return fail_field(ir, line, "duplicate field %s", fields->fields[i].key);
    return 0;
}

static int extension_key(const char *key)
{
    return !strncmp(key, "attr.", 5) && valid_name(key + 5, 1);
}

static int copy_extensions(struct rasterfall_map_ir_attribute *out, int *count,
                           struct map_line *line)
{
    int i;
    *count = 0;
    for (i = 0; i < line->count; i++) {
        if (!extension_key(line->fields[i].key)) continue;
        if (*count >= RASTERFALL_MAP_IR_MAX_ATTRIBUTES ||
            copy_text(out[*count].key, sizeof(out[*count].key), line->fields[i].key + 5) < 0 ||
            copy_text(out[*count].value, sizeof(out[*count].value), line->fields[i].value) < 0) return -1;
        (*count)++;
    }
    return 0;
}

static int parse_id_bounds(struct rasterfall_map_ir *ir, int line, struct map_line *fields,
                           const char *record, char *id,
                           struct rasterfall_map_ir_bounds *bounds)
{
    const char *value;
    const char *keys[] = {"id", "min_x", "max_x", "min_z", "max_z"};
    int i, n, required[5] = {0, 0, 0, 0, 0};
    int *dest[] = {0, &bounds->min_x, &bounds->max_x, &bounds->min_z, &bounds->max_z};
    for (i = 0; i < fields->count; i++) {
        if (extension_key(fields->fields[i].key)) continue;
        for (n = 0; n < 5 && strcmp(fields->fields[i].key, keys[n]); n++);
        if (n == 5) continue;
        required[n] = 1;
        value = fields->fields[i].value;
        if (n == 0) {
            if (!valid_name(value, 1) || copy_text(id, RASTERFALL_MAP_IR_ID_SIZE, value) < 0) return fail(ir, line, "invalid id");
        } else if (int_value(value, dest[n]) < 0) return fail_field(ir, line, "invalid integer for %s", keys[n]);
    }
    for (i = 0; i < 5; i++) if (!required[i]) return fail_missing(ir, line, record, keys[i]);
    if (bounds->min_x > bounds->max_x || bounds->min_z > bounds->max_z) return fail(ir, line, "invalid bounds: minimum exceeds maximum");
    return 0;
}

static int id_exists(struct rasterfall_map_ir *ir, const char *id)
{
    int i;
    for (i = 0; i < ir->region_count; i++) if (!strcmp(ir->regions[i].id, id)) return 1;
    for (i = 0; i < ir->collision_count; i++) if (!strcmp(ir->collisions[i].id, id)) return 1;
    for (i = 0; i < ir->surface_count; i++) if (!strcmp(ir->surfaces[i].id, id)) return 1;
    for (i = 0; i < ir->render_count; i++) if (!strcmp(ir->renders[i].id, id)) return 1;
    for (i = 0; i < ir->interaction_count; i++) if (!strcmp(ir->interactions[i].id, id)) return 1;
    for (i = 0; i < ir->actor_spawn_count; i++) if (!strcmp(ir->actor_spawns[i].id, id)) return 1;
    for (i = 0; i < ir->pickup_count; i++) if (!strcmp(ir->pickups[i].id, id)) return 1;
    for (i = 0; i < ir->object_count; i++) if (!strcmp(ir->objects[i].id, id)) return 1;
    return 0;
}

static int allowed(struct rasterfall_map_ir *ir, int line, struct map_line *fields,
                   const char **names, int count)
{
    int i, n;
    for (i = 0; i < fields->count; i++) {
        if (extension_key(fields->fields[i].key)) continue;
        for (n = 0; n < count && strcmp(fields->fields[i].key, names[n]); n++);
        if (n == count) return fail_field(ir, line, "unknown field %s", fields->fields[i].key);
    }
    return 0;
}

static int parse_record(struct rasterfall_map_ir *ir, int line, struct map_line *record)
{
    const char *value;
    if (check_duplicate_fields(ir, line, record) < 0) return -1;
    if (!strcmp(record->record, "map")) {
        int version;
        if (ir->has_map) return fail(ir, line, "duplicate map record");
        { const char *keys[] = {"version", "units"}; if (allowed(ir, line, record, keys, 2) < 0) return -1; }
        if (!value_of(record, "version", &value)) return fail(ir, line, "map missing version");
        if (int_value(value, &version) < 0 || version != 1) return fail(ir, line, "map version must be 1");
        if (!value_of(record, "units", &value) || strcmp(value, "rfu")) return fail(ir, line, "map units must be rfu");
        ir->version = version; strcpy(ir->units, value); ir->has_map = 1; return 0;
    }
    if (!strcmp(record->record, "world")) {
        struct rasterfall_map_ir_bounds b = {0};
        int have_room = 0;
        const char *keys[] = {"min_x", "max_x", "min_z", "max_z", "room_limit"};
        if (ir->has_world) return fail(ir, line, "duplicate world record");
        if (allowed(ir, line, record, keys, 5) < 0) return -1;
        if (!value_of(record, "min_x", &value) || int_value(value, &b.min_x) < 0) return fail(ir, line, "world missing or invalid min_x");
        if (!value_of(record, "max_x", &value) || int_value(value, &b.max_x) < 0) return fail(ir, line, "world missing or invalid max_x");
        if (!value_of(record, "min_z", &value) || int_value(value, &b.min_z) < 0) return fail(ir, line, "world missing or invalid min_z");
        if (!value_of(record, "max_z", &value) || int_value(value, &b.max_z) < 0) return fail(ir, line, "world missing or invalid max_z");
        if (b.min_x > b.max_x || b.min_z > b.max_z) return fail(ir, line, "invalid world bounds");
        if (value_of(record, "room_limit", &value)) { if (int_value(value, &ir->world.room_limit) < 0 || ir->world.room_limit < 0) return fail(ir, line, "invalid room_limit"); have_room = 1; }
        ir->world.bounds = b; ir->world.has_room_limit = have_room; ir->has_world = 1; return 0;
    }
    if (!strcmp(record->record, "region") || !strcmp(record->record, "collision") ||
        !strcmp(record->record, "surface") || !strcmp(record->record, "render")) {
        char id[RASTERFALL_MAP_IR_ID_SIZE]; struct rasterfall_map_ir_bounds b = {0};
        if (parse_id_bounds(ir, line, record, record->record, id, &b) < 0) return -1;
        if (id_exists(ir, id)) return fail_field(ir, line, "duplicate id %s", id);
        if (!strcmp(record->record, "region")) {
            const char *keys[] = {"id", "kind", "min_x", "max_x", "min_z", "max_z"}; struct rasterfall_map_ir_region *o;
            if (allowed(ir, line, record, keys, 6) < 0) return -1;
            if (!value_of(record, "kind", &value) || !valid_name(value, 1)) return fail(ir, line, "region missing or invalid kind");
            if (ir->region_count >= RASTERFALL_MAP_IR_MAX_REGIONS) return fail(ir, line, "region capacity exceeded");
            o = &ir->regions[ir->region_count++]; strcpy(o->id, id); strcpy(o->kind, value); o->bounds = b; o->line = line; if (copy_extensions(o->attributes, &o->attribute_count, record) < 0) return fail(ir, line, "too many attributes or attribute value too long"); return 0;
        }
        if (!strcmp(record->record, "collision")) {
            const char *keys[] = {"id", "shape", "min_x", "max_x", "min_z", "max_z", "height", "height2", "collision", "visible", "walkable", "blocks_airborne", "color", "role"}; struct rasterfall_map_ir_collision *o; int height, height2 = 0, collision = 1, visible = 1, walkable = 0, airborne = 0;
            if (allowed(ir, line, record, keys, 14) < 0) return -1;
            if (!value_of(record, "shape", &value) || !valid_name(value, 1)) return fail(ir, line, "collision missing or invalid shape");
            if (!value_of(record, "height", &value) || int_value(value, &height) < 0 || height < 0) return fail(ir, line, "collision missing or invalid height");
            if (value_of(record, "height2", &value) && (int_value(value, &height2) < 0 || height2 < 0)) return fail(ir, line, "invalid collision height2");
            if (value_of(record, "collision", &value) && bool_value(value, &collision) < 0) return fail(ir, line, "collision must be true or false");
            if (value_of(record, "visible", &value) && bool_value(value, &visible) < 0) return fail(ir, line, "visible must be true or false");
            if (value_of(record, "walkable", &value) && bool_value(value, &walkable) < 0) return fail(ir, line, "walkable must be true or false");
            if (value_of(record, "blocks_airborne", &value) && bool_value(value, &airborne) < 0) return fail(ir, line, "blocks_airborne must be true or false");
            if (ir->collision_count >= RASTERFALL_MAP_IR_MAX_COLLISIONS) return fail(ir, line, "collision capacity exceeded");
            o = &ir->collisions[ir->collision_count++]; strcpy(o->id, id); strcpy(o->shape, value_of(record, "shape", &value) ? value : ""); o->bounds = b; o->height = height; o->has_height2 = value_of(record, "height2", &value); o->height2 = height2; o->collision = collision; o->visible = visible; o->walkable = walkable; o->blocks_airborne = airborne; o->has_color = value_of(record, "color", &value); o->has_role = value_of(record, "role", &value); if (o->has_color && copy_text(o->color, sizeof(o->color), value) < 0) return fail(ir, line, "invalid collision color"); if (o->has_role && (!valid_name(value, 1) || copy_text(o->role, sizeof(o->role), value) < 0)) return fail(ir, line, "invalid collision role"); o->line = line; if (copy_extensions(o->attributes, &o->attribute_count, record) < 0) return fail(ir, line, "too many attributes or attribute value too long"); return 0;
        }
        if (!strcmp(record->record, "surface")) {
            const char *keys[] = {"id", "kind", "min_x", "max_x", "min_z", "max_z", "height", "height2", "axis", "material"}; struct rasterfall_map_ir_surface *o; int height, height2 = 0;
            if (allowed(ir, line, record, keys, 10) < 0) return -1;
            if (!value_of(record, "kind", &value) || !valid_name(value, 1)) return fail(ir, line, "surface missing or invalid kind");
            if (!value_of(record, "height", &value) || int_value(value, &height) < 0) return fail(ir, line, "surface missing or invalid height");
            if (value_of(record, "height2", &value) && int_value(value, &height2) < 0) return fail(ir, line, "invalid surface height2");
            if (value_of(record, "axis", &value) && (!valid_name(value, 0) || strlen(value) >= RASTERFALL_MAP_IR_KIND_SIZE)) return fail(ir, line, "invalid surface axis");
            if (ir->surface_count >= RASTERFALL_MAP_IR_MAX_SURFACES) return fail(ir, line, "surface capacity exceeded");
            o = &ir->surfaces[ir->surface_count++]; strcpy(o->id, id); strcpy(o->kind, value_of(record, "kind", &value) ? value : ""); o->bounds = b; o->height = height; o->has_height2 = value_of(record, "height2", &value); o->height2 = height2; o->has_axis = value_of(record, "axis", &value); if (o->has_axis) strcpy(o->axis, value); o->has_material = value_of(record, "material", &value); if (o->has_material && copy_text(o->material, sizeof(o->material), value) < 0) return fail(ir, line, "invalid material"); o->line = line; if (copy_extensions(o->attributes, &o->attribute_count, record) < 0) return fail(ir, line, "too many attributes or attribute value too long"); return 0;
        }
        {
            const char *keys[] = {"id", "kind", "min_x", "max_x", "min_z", "max_z", "x", "y", "z", "asset", "color", "height"}; struct rasterfall_map_ir_render *o; int height = 0, position[3], position_count = 0;
            if (allowed(ir, line, record, keys, 12) < 0) return -1;
            if (!value_of(record, "kind", &value) || !valid_name(value, 1)) return fail(ir, line, "render missing or invalid kind");
            if (value_of(record, "height", &value) && int_value(value, &height) < 0) return fail(ir, line, "invalid render height");
            if (value_of(record, "x", &value) && (int_value(value, &position[0]) < 0)) return fail(ir, line, "invalid render x"); else if (value_of(record, "x", &value)) position_count++;
            if (value_of(record, "y", &value) && (int_value(value, &position[1]) < 0)) return fail(ir, line, "invalid render y"); else if (value_of(record, "y", &value)) position_count++;
            if (value_of(record, "z", &value) && (int_value(value, &position[2]) < 0)) return fail(ir, line, "invalid render z"); else if (value_of(record, "z", &value)) position_count++;
            if (position_count != 0 && position_count != 3) return fail(ir, line, "render position requires x, y and z");
            if (ir->render_count >= RASTERFALL_MAP_IR_MAX_RENDERS) return fail(ir, line, "render capacity exceeded");
            o = &ir->renders[ir->render_count++]; strcpy(o->id, id); strcpy(o->kind, value_of(record, "kind", &value) ? value : ""); o->bounds = b; o->has_position = position_count == 3; if (o->has_position) { o->x = position[0]; o->y = position[1]; o->z = position[2]; } o->has_asset = value_of(record, "asset", &value); if (o->has_asset && (!valid_name(value, 1) || copy_text(o->asset, sizeof(o->asset), value) < 0)) return fail(ir, line, "invalid render asset"); o->has_color = value_of(record, "color", &value); if (o->has_color && copy_text(o->color, sizeof(o->color), value) < 0) return fail(ir, line, "invalid color"); o->has_height = value_of(record, "height", &value); o->height = height; o->line = line; if (copy_extensions(o->attributes, &o->attribute_count, record) < 0) return fail(ir, line, "too many attributes or attribute value too long"); return 0;
        }
    }
    if (!strcmp(record->record, "interaction")) {
        const char *keys[] = {"id", "action", "x", "y", "z"}; struct rasterfall_map_ir_interaction *o; char id[RASTERFALL_MAP_IR_ID_SIZE]; const char *action; int x, y, z;
        if (allowed(ir, line, record, keys, 5) < 0) return -1;
        if (!value_of(record, "id", &value) || !valid_name(value, 1) || copy_text(id, sizeof(id), value) < 0) return fail(ir, line, "interaction missing or invalid id");
        if (id_exists(ir, id)) return fail_field(ir, line, "duplicate id %s", id);
        if (!value_of(record, "action", &action) || !valid_name(action, 1)) return fail(ir, line, "interaction missing or invalid action");
        if (!value_of(record, "x", &value) || int_value(value, &x) < 0) return fail(ir, line, "interaction missing or invalid x");
        if (!value_of(record, "y", &value) || int_value(value, &y) < 0) return fail(ir, line, "interaction missing or invalid y");
        if (!value_of(record, "z", &value) || int_value(value, &z) < 0) return fail(ir, line, "interaction missing or invalid z");
        if (ir->interaction_count >= RASTERFALL_MAP_IR_MAX_INTERACTIONS) return fail(ir, line, "interaction capacity exceeded");
        o = &ir->interactions[ir->interaction_count++]; strcpy(o->id, id); strcpy(o->action, action); o->x = x; o->y = y; o->z = z; o->line = line; if (copy_extensions(o->attributes, &o->attribute_count, record) < 0) return fail(ir, line, "too many attributes or attribute value too long"); return 0;
    }
    if (!strcmp(record->record, "actor_spawn")) {
        const char *keys[] = {"id", "class", "type", "base_id", "x", "y", "z", "downed", "weapon"};
        struct rasterfall_map_ir_actor_spawn *o;
        char id[RASTERFALL_MAP_IR_ID_SIZE];
        const char *class_value, *type_value;
        int x, y, z, base_id, downed;
        if (allowed(ir, line, record, keys, 9) < 0) return -1;
        if (!value_of(record, "id", &value) || !valid_name(value, 1) ||
            copy_text(id, sizeof(id), value) < 0) return fail(ir, line, "actor_spawn missing or invalid id");
        if (id_exists(ir, id)) return fail_field(ir, line, "duplicate id %s", id);
        class_value = value_of(record, "class", &value) ? value : NULL;
        type_value = value_of(record, "type", &value) ? value : NULL;
        if ((class_value && type_value) || (!class_value && !type_value) ||
            !valid_name(class_value ? class_value : type_value, 1))
            return fail(ir, line, "actor_spawn requires exactly one valid class or type");
        if (!value_of(record, "base_id", &value) || int_value(value, &base_id) < 0)
            return fail(ir, line, "actor_spawn missing or invalid base_id");
        if (!value_of(record, "x", &value) || int_value(value, &x) < 0)
            return fail(ir, line, "actor_spawn missing or invalid x");
        if (!value_of(record, "y", &value) || int_value(value, &y) < 0)
            return fail(ir, line, "actor_spawn missing or invalid y");
        if (!value_of(record, "z", &value) || int_value(value, &z) < 0)
            return fail(ir, line, "actor_spawn missing or invalid z");
        if (!value_of(record, "downed", &value) || int_value(value, &downed) < 0 ||
            (downed != 0 && downed != 1))
            return fail(ir, line, "actor_spawn missing or invalid downed");
        if (ir->actor_spawn_count >= RASTERFALL_MAP_IR_MAX_ACTOR_SPAWNS)
            return fail(ir, line, "actor_spawn capacity exceeded");
        o = &ir->actor_spawns[ir->actor_spawn_count++];
        strcpy(o->id, id);
        strcpy(o->class_name, class_value ? class_value : type_value);
        o->base_id = base_id; o->x = x; o->y = y; o->z = z; o->downed = downed;
        o->has_weapon = value_of(record, "weapon", &value);
        if (o->has_weapon && (!valid_name(value, 1) || copy_text(o->weapon, sizeof(o->weapon), value) < 0))
            return fail(ir, line, "invalid actor_spawn weapon");
        o->line = line;
        if (copy_extensions(o->attributes, &o->attribute_count, record) < 0)
            return fail(ir, line, "too many attributes or attribute value too long");
        return 0;
    }
    if (!strcmp(record->record, "pickup")) {
        const char *keys[] = {"id", "kind", "x", "y", "z"};
        struct rasterfall_map_ir_pickup *o;
        char id[RASTERFALL_MAP_IR_ID_SIZE]; int x, y, z;
        if (allowed(ir, line, record, keys, 5) < 0) return -1;
        if (!value_of(record, "id", &value) || !valid_name(value, 1) ||
            copy_text(id, sizeof(id), value) < 0) return fail(ir, line, "pickup missing or invalid id");
        if (id_exists(ir, id)) return fail_field(ir, line, "duplicate id %s", id);
        if (!value_of(record, "kind", &value) || !valid_name(value, 1)) return fail(ir, line, "pickup missing or invalid kind");
        if (!value_of(record, "x", &value) || int_value(value, &x) < 0) return fail(ir, line, "pickup missing or invalid x");
        if (!value_of(record, "y", &value) || int_value(value, &y) < 0) return fail(ir, line, "pickup missing or invalid y");
        if (!value_of(record, "z", &value) || int_value(value, &z) < 0) return fail(ir, line, "pickup missing or invalid z");
        if (ir->pickup_count >= RASTERFALL_MAP_IR_MAX_PICKUPS) return fail(ir, line, "pickup capacity exceeded");
        o = &ir->pickups[ir->pickup_count++]; strcpy(o->id, id); strcpy(o->kind, value_of(record, "kind", &value) ? value : ""); o->x = x; o->y = y; o->z = z; o->line = line;
        if (copy_extensions(o->attributes, &o->attribute_count, record) < 0) return fail(ir, line, "too many attributes or attribute value too long");
        return 0;
    }
    if (!strcmp(record->record, "object")) {
        const char *keys[] = {"id", "kind", "x", "y", "z", "yaw", "scale"};
        struct rasterfall_map_ir_object *o;
        char id[RASTERFALL_MAP_IR_ID_SIZE]; int x, y = 0, z, yaw, scale;
        if (allowed(ir, line, record, keys, 7) < 0) return -1;
        if (!value_of(record, "id", &value) || !valid_name(value, 1) ||
            copy_text(id, sizeof(id), value) < 0) return fail(ir, line, "object missing or invalid id");
        if (id_exists(ir, id)) return fail_field(ir, line, "duplicate id %s", id);
        if (!value_of(record, "kind", &value) || !valid_name(value, 1)) return fail(ir, line, "object missing or invalid kind");
        if (!value_of(record, "x", &value) || int_value(value, &x) < 0) return fail(ir, line, "object missing or invalid x");
        if (value_of(record, "y", &value) && int_value(value, &y) < 0) return fail(ir, line, "invalid object y");
        if (!value_of(record, "z", &value) || int_value(value, &z) < 0) return fail(ir, line, "object missing or invalid z");
        if (!value_of(record, "yaw", &value) || int_value(value, &yaw) < 0) return fail(ir, line, "object missing or invalid yaw");
        if (!value_of(record, "scale", &value) || int_value(value, &scale) < 0 || scale <= 0) return fail(ir, line, "object missing or invalid scale");
        if (ir->object_count >= RASTERFALL_MAP_IR_MAX_OBJECTS) return fail(ir, line, "object capacity exceeded");
        o = &ir->objects[ir->object_count++]; strcpy(o->id, id); strcpy(o->kind, value_of(record, "kind", &value) ? value : ""); o->x = x; o->y = y; o->z = z; o->yaw = yaw; o->scale = scale; o->line = line;
        if (copy_extensions(o->attributes, &o->attribute_count, record) < 0) return fail(ir, line, "too many attributes or attribute value too long");
        return 0;
    }
    return fail_field(ir, line, "unknown record type %s", record->record);
}

static int inside(struct rasterfall_map_ir_bounds *inner, struct rasterfall_map_ir_bounds *outer)
{
    return inner->min_x >= outer->min_x && inner->max_x <= outer->max_x && inner->min_z >= outer->min_z && inner->max_z <= outer->max_z;
}

static int validate(struct rasterfall_map_ir *ir)
{
    int i;
    if (!ir->has_map) return fail(ir, 0, "missing map record");
    if (!ir->has_world) return fail(ir, 0, "missing world record");
    for (i = 0; i < ir->region_count; i++) if (!inside(&ir->regions[i].bounds, &ir->world.bounds)) return fail(ir, ir->regions[i].line, "region bounds outside world");
    for (i = 0; i < ir->collision_count; i++) if (!inside(&ir->collisions[i].bounds, &ir->world.bounds)) return fail(ir, ir->collisions[i].line, "collision bounds outside world");
    for (i = 0; i < ir->surface_count; i++) if (!inside(&ir->surfaces[i].bounds, &ir->world.bounds)) return fail(ir, ir->surfaces[i].line, "surface bounds outside world");
    /* Decorative render records may deliberately form a visual envelope
     * outside the gameplay world (the legacy outer wall does this).  Their
     * geometry is still range-checked by the integer parser; collision and
     * surface records remain constrained to the authored world. */
    for (i = 0; i < ir->interaction_count; i++) if (ir->interactions[i].x < ir->world.bounds.min_x || ir->interactions[i].x > ir->world.bounds.max_x || ir->interactions[i].z < ir->world.bounds.min_z || ir->interactions[i].z > ir->world.bounds.max_z) return fail(ir, ir->interactions[i].line, "interaction position outside world");
    for (i = 0; i < ir->actor_spawn_count; i++) if (ir->actor_spawns[i].x < ir->world.bounds.min_x || ir->actor_spawns[i].x > ir->world.bounds.max_x || ir->actor_spawns[i].z < ir->world.bounds.min_z || ir->actor_spawns[i].z > ir->world.bounds.max_z) return fail(ir, ir->actor_spawns[i].line, "actor_spawn position outside world");
    for (i = 0; i < ir->pickup_count; i++) if (ir->pickups[i].x < ir->world.bounds.min_x || ir->pickups[i].x > ir->world.bounds.max_x || ir->pickups[i].z < ir->world.bounds.min_z || ir->pickups[i].z > ir->world.bounds.max_z) return fail(ir, ir->pickups[i].line, "pickup position outside world");
    for (i = 0; i < ir->object_count; i++) if (ir->objects[i].x < ir->world.bounds.min_x || ir->objects[i].x > ir->world.bounds.max_x || ir->objects[i].z < ir->world.bounds.min_z || ir->objects[i].z > ir->world.bounds.max_z) return fail(ir, ir->objects[i].line, "object position outside world");
    return 0;
}

int rasterfall_map_ir_parse_file(const char *path, struct rasterfall_map_ir *ir)
{
    int fd, total = 0, n, size; struct stat st; char *data, *line; int line_no = 1;
    if (!ir || !path) return -1;
    __memset(ir, 0, sizeof(*ir));
    fd = __openat(AT_FDCWD, path, O_RDONLY, 0);
    if (fd < 0) return fail(ir, 0, "cannot open map file");
    if (__fstat(fd, &st) < 0 || st.st_size <= 0 || st.st_size >= MAP_MAX_FILE) { __close(fd); return fail(ir, 0, "map file size is invalid"); }
    size = (int)st.st_size; data = tlibc_malloc((unsigned long)size + 1);
    if (!data) { __close(fd); return fail(ir, 0, "out of memory"); }
    while (total < size) { n = (int)__read(fd, data + total, size - total); if (n <= 0) { tlibc_free(data); __close(fd); return fail(ir, 0, "cannot read map file"); } total += n; }
    __close(fd); data[size] = 0;
    line = data;
    while (line && *line) {
        struct map_line parsed; char *next = strchr(line, '\n'); char *comment;
        if (next) { *next = 0; next++; }
        comment = strchr(line, '#'); if (comment) *comment = 0;
        if (split_line(line, &parsed) < 0) { tlibc_free(data); return fail(ir, line_no, "invalid field syntax; expected key=value"); }
        if (parsed.record && parse_record(ir, line_no, &parsed) < 0) { tlibc_free(data); return -1; }
        line = next;
        line_no++;
    }
    n = validate(ir); tlibc_free(data); return n;
}
