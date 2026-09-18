#include "rf_gpu_raster_pack.h"
#include "toy_renderer.h"

#include <limits.h>
#include "string.h"

static int host_is_little_endian(void)
{
    const uint32_t value = 1;
    return *(const unsigned char *)&value == 1;
}

size_t rf_gpu_raster_stream_size_v1(uint32_t command_count)
{
    if (sizeof(size_t) < 8 && command_count > 44739242U)
        return 0;
    return sizeof(struct rf_gpu_raster_stream_header_v1) +
           (size_t)command_count * sizeof(struct rf_gpu_raster_cmd_v1);
}

static int pack_clear(struct rf_gpu_raster_cmd_v1 *out, uint32_t kind,
                      uint32_t value)
{
    memset(out, 0, sizeof(*out));
    out->kind = kind;
    out->byte_size = sizeof(*out);
    out->payload.clear.value = value;
    return RF_GPU_RASTER_PACK_OK;
}

static int texture_command_supported(const struct toy_raster_cmd *in)
{
    const struct toy_texture_view *t = in->texture;
    if (!in->textured || in->overlay || in->edge ||
        !in->base_texture_valid || !t || in->base_texture_bilinear ||
        in->material_features || in->has_toon || in->texture2 || in->texture3 ||
        in->material_add || in->material_tint != 0x00ffffffU ||
        in->light < 0 || in->fog < 0 ||
        (t->channels != 3 && t->channels != 4) ||
        in->a.u_over_z < INT_MIN || in->a.u_over_z > INT_MAX ||
        in->a.v_over_z < INT_MIN || in->a.v_over_z > INT_MAX ||
        in->b.u_over_z < INT_MIN || in->b.u_over_z > INT_MAX ||
        in->b.v_over_z < INT_MIN || in->b.v_over_z > INT_MAX ||
        in->c.u_over_z < INT_MIN || in->c.u_over_z > INT_MAX ||
        in->c.v_over_z < INT_MIN || in->c.v_over_z > INT_MAX)
        return 0;
    return 1;
}

int rf_gpu_raster_measure_textures_toy_v1(const struct toy_renderer *renderer,
                                          uint32_t *unique_count,
                                          size_t *texel_size)
{
    uint32_t unique = 0;
    size_t bytes = 0;
    int i, j;
    if (unique_count) *unique_count = 0;
    if (texel_size) *texel_size = 0;
    if (!renderer || renderer->cmd_count < 0) return RF_GPU_RASTER_PACK_INVALID;
    for (i = 0; i < renderer->cmd_count; ++i) {
        const struct toy_raster_cmd *cmd = &renderer->cmds[i];
        if (!cmd->textured) continue;
        if (!texture_command_supported(cmd)) return RF_GPU_RASTER_PACK_UNSUPPORTED;
        for (j = 0; j < i; ++j)
            if (renderer->cmds[j].textured &&
                renderer->cmds[j].texture == cmd->texture) break;
        if (j == i) {
            if (unique == UINT_MAX || bytes > (size_t)-1 - cmd->texture->data_size)
                return RF_GPU_RASTER_PACK_CAPACITY;
            unique++;
            bytes += cmd->texture->data_size;
        }
    }
    if (unique_count) *unique_count = unique;
    if (texel_size) *texel_size = bytes;
    return RF_GPU_RASTER_PACK_OK;
}

static int texture_handle(const struct toy_renderer *renderer, int command_index,
                          struct rf_gpu_texture_resources_v1 *resources,
                          uint32_t *handle)
{
    const struct toy_texture_view *texture = renderer->cmds[command_index].texture;
    uint32_t index = 0;
    int i, j;
    for (i = 0; i < command_index; ++i) {
        if (!renderer->cmds[i].textured) continue;
        if (renderer->cmds[i].texture == texture) { *handle = index + 1; return 0; }
        for (j = 0; j < i; ++j)
            if (renderer->cmds[j].textured &&
                renderer->cmds[j].texture == renderer->cmds[i].texture) break;
        if (j == i) index++;
    }
    if (!resources || index >= resources->desc_capacity ||
        resources->texel_size > resources->texel_capacity - texture->data_size ||
        resources->texel_size > UINT_MAX ||
        texture->width > UINT_MAX / texture->channels)
        return -1;
    resources->descs[index].texel_offset = (uint32_t)resources->texel_size;
    resources->descs[index].width = texture->width;
    resources->descs[index].height = texture->height;
    resources->descs[index].stride = texture->width * texture->channels;
    resources->descs[index].format = texture->channels == 4 ?
        RF_GPU_TEXTURE_FORMAT_RGBA8_V1 : RF_GPU_TEXTURE_FORMAT_RGB8_V1;
    resources->descs[index].sampling = RF_GPU_TEXTURE_SAMPLING_NEAREST_V1;
    memcpy(resources->texels + resources->texel_size, texture->data,
           texture->data_size);
    resources->texel_size += texture->data_size;
    resources->desc_count = index + 1;
    *handle = index + 1;
    return 0;
}

static int pack_triangle(struct rf_gpu_raster_cmd_v1 *out,
                         const struct toy_renderer *renderer, int command_index,
                         struct rf_gpu_texture_resources_v1 *resources)
{
    const struct toy_raster_cmd *in = &renderer->cmds[command_index];
    struct rf_gpu_raster_flat_triangle_v1 *triangle;
    if ((!resources && in->textured) || in->overlay)
        return RF_GPU_RASTER_PACK_UNSUPPORTED;
    if (in->area >= 0 || in->a.inv_z < INT_MIN || in->a.inv_z > INT_MAX ||
        in->b.inv_z < INT_MIN || in->b.inv_z > INT_MAX ||
        in->c.inv_z < INT_MIN || in->c.inv_z > INT_MAX)
        return RF_GPU_RASTER_PACK_INVALID;
    memset(out, 0, sizeof(*out));
    out->kind = in->textured ? RF_GPU_RASTER_CMD_TEXTURED_TRIANGLE_V1 :
        in->planar_vertex_lit ?
        RF_GPU_RASTER_CMD_VERTEX_LIT_TRIANGLE_V1 :
        RF_GPU_RASTER_CMD_FLAT_TRIANGLE_V1;
    out->byte_size = sizeof(*out);
    if (in->transparent || in->material_alpha != 255 ||
        (in->textured && in->texture && in->texture->has_transparency))
        out->flags = RF_GPU_RASTER_FLAG_DEPTH_TEST_V1 |
                     RF_GPU_RASTER_FLAG_SOURCE_OVER_V1;
    else
        out->flags = RF_GPU_RASTER_FLAG_DEPTH_TEST_V1 |
                     RF_GPU_RASTER_FLAG_DEPTH_WRITE_V1 |
                     RF_GPU_RASTER_FLAG_OPAQUE_V1;
    if (in->fog != 0) out->flags |= RF_GPU_RASTER_FLAG_FOG_V1;
    triangle = &out->payload.flat_triangle;
    triangle->area = in->area;
    triangle->a.x = in->a.x; triangle->a.y = in->a.y;
    triangle->a.inv_z = (int32_t)in->a.inv_z;
    triangle->b.x = in->b.x; triangle->b.y = in->b.y;
    triangle->b.inv_z = (int32_t)in->b.inv_z;
    triangle->c.x = in->c.x; triangle->c.y = in->c.y;
    triangle->c.inv_z = (int32_t)in->c.inv_z;
    triangle->bbox_minx = in->bbox_minx;
    triangle->bbox_maxx = in->bbox_maxx;
    triangle->bbox_miny = in->bbox_miny;
    triangle->bbox_maxy = in->bbox_maxy;
    triangle->color = in->color;
    triangle->light_q8 = in->light;
    triangle->fog_q8 = in->fog;
    if (out->flags & RF_GPU_RASTER_FLAG_SOURCE_OVER_V1) {
        if (in->planar_vertex_lit)
            return RF_GPU_RASTER_PACK_UNSUPPORTED;
        triangle->reserved[0] = (uint32_t)(in->material_alpha < 0 ? 0 :
            in->material_alpha > 255 ? 255 : in->material_alpha);
    }
    if (in->planar_vertex_lit) {
        struct rf_gpu_raster_vertex_lit_triangle_v1 *vertex_lit =
            &out->payload.vertex_lit_triangle;
        vertex_lit->light_a_q8 = in->a.light;
        vertex_lit->light_b_q8 = in->b.light;
        vertex_lit->light_c_q8 = in->c.light;
    } else if (in->textured) {
        struct rf_gpu_raster_textured_triangle_v1 *textured =
            &out->payload.textured_triangle;
        uint32_t handle;
        if (!texture_command_supported(in) ||
            texture_handle(renderer, command_index, resources, &handle) < 0)
            return RF_GPU_RASTER_PACK_UNSUPPORTED;
        out->resource_handle = handle;
        if (in->repeat) out->flags |= RF_GPU_RASTER_FLAG_TEXTURE_REPEAT_V1;
        textured->a_u_over_z = (int32_t)in->a.u_over_z;
        textured->a_v_over_z = (int32_t)in->a.v_over_z;
        textured->b_u_over_z = (int32_t)in->b.u_over_z;
        textured->b_v_over_z = (int32_t)in->b.v_over_z;
        textured->c_u_over_z = (int32_t)in->c.u_over_z;
        textured->c_v_over_z = (int32_t)in->c.v_over_z;
        textured->light_q8 = in->light;
        textured->fog_q8 = in->fog;
        textured->reserved = (out->flags & RF_GPU_RASTER_FLAG_SOURCE_OVER_V1) ?
            (uint32_t)(in->material_alpha < 0 ? 0 :
                       in->material_alpha > 255 ? 255 : in->material_alpha) : 0;
    }
    return RF_GPU_RASTER_PACK_OK;
}

static int pack_toy(const struct toy_renderer *renderer,
                              uint32_t clear_color, int32_t clear_depth,
                              void *destination, size_t destination_size,
                              size_t *written_size,
                              struct rf_gpu_texture_resources_v1 *resources,
                              uint32_t transparent_offset,
                              uint32_t viewmodel_offset)
{
    struct rf_gpu_raster_stream_header_v1 *header;
    struct rf_gpu_raster_cmd_v1 *commands;
    uint32_t count, marker_count = 0;
    int has_transparent_marker = 0, has_viewmodel_marker = 0;
    size_t required;
    int i, result;
    if (written_size) *written_size = 0;
    if (!renderer || !destination || !renderer->surface.width ||
        !renderer->surface.height || renderer->surface.width > INT_MAX ||
        renderer->surface.height > INT_MAX || renderer->cmd_count < 0 ||
        !host_is_little_endian())
        return RF_GPU_RASTER_PACK_INVALID;
    if (transparent_offset != UINT_MAX &&
        transparent_offset > (uint32_t)renderer->cmd_count)
        return RF_GPU_RASTER_PACK_INVALID;
    if (viewmodel_offset != UINT_MAX &&
        viewmodel_offset > (uint32_t)renderer->cmd_count)
        return RF_GPU_RASTER_PACK_INVALID;
    if (transparent_offset != UINT_MAX && viewmodel_offset != UINT_MAX &&
        transparent_offset >= viewmodel_offset)
        return RF_GPU_RASTER_PACK_INVALID;
    if (transparent_offset != UINT_MAX &&
        transparent_offset < (uint32_t)renderer->cmd_count) {
        has_transparent_marker = 1;
        marker_count = 1;
    }
    if (viewmodel_offset != UINT_MAX &&
        viewmodel_offset < (uint32_t)renderer->cmd_count) {
        has_viewmodel_marker = 1;
        marker_count++;
    }
    count = (uint32_t)renderer->cmd_count + 2U + marker_count;
    required = rf_gpu_raster_stream_size_v1(count);
    if (!required || destination_size < required)
        return RF_GPU_RASTER_PACK_CAPACITY;
    memset(destination, 0, required);
    header = destination;
    header->magic = RF_GPU_RASTER_ABI_MAGIC;
    header->version = RF_GPU_RASTER_ABI_VERSION;
    header->header_size = sizeof(*header);
    header->command_size = sizeof(struct rf_gpu_raster_cmd_v1);
    header->command_count = count;
    header->framebuffer_width = (uint32_t)renderer->surface.width;
    header->framebuffer_height = (uint32_t)renderer->surface.height;
    header->endian_tag = RF_GPU_RASTER_ENDIAN_LITTLE;
    commands = (struct rf_gpu_raster_cmd_v1 *)(header + 1);
    pack_clear(&commands[0], RF_GPU_RASTER_CMD_CLEAR_COLOR_V1, clear_color);
    pack_clear(&commands[1], RF_GPU_RASTER_CMD_CLEAR_DEPTH_V1,
               (uint32_t)clear_depth);
    if (has_transparent_marker)
        pack_clear(&commands[2U + transparent_offset],
                   RF_GPU_RASTER_CMD_BEGIN_TRANSPARENT_V1, 0);
    if (has_viewmodel_marker)
        pack_clear(&commands[2U + viewmodel_offset +
                              (has_transparent_marker ? 1U : 0U)],
                   RF_GPU_RASTER_CMD_BEGIN_VIEWMODEL_V1, 0);
    for (i = 0; i < renderer->cmd_count; ++i) {
        uint32_t destination_index = (uint32_t)i + 2U +
            (has_transparent_marker && (uint32_t)i >= transparent_offset ? 1U : 0U) +
            (has_viewmodel_marker && (uint32_t)i >= viewmodel_offset ? 1U : 0U);
        result = pack_triangle(&commands[destination_index], renderer, i,
                               resources);
        if (result != RF_GPU_RASTER_PACK_OK) {
            memset(destination, 0, required);
            return result;
        }
    }
    if (rf_gpu_raster_validate_v1(destination, required) !=
        RF_GPU_RASTER_PACK_OK) {
        memset(destination, 0, required);
        return RF_GPU_RASTER_PACK_INVALID;
    }
    if (written_size) *written_size = required;
    return RF_GPU_RASTER_PACK_OK;
}

int rf_gpu_raster_pack_toy_v1(const struct toy_renderer *renderer,
                              uint32_t clear_color, int32_t clear_depth,
                              void *destination, size_t destination_size,
                              size_t *written_size)
{
    return pack_toy(renderer, clear_color, clear_depth, destination,
                    destination_size, written_size, NULL, UINT_MAX, UINT_MAX);
}

int rf_gpu_raster_pack_toy_textured_v1(
                              const struct toy_renderer *renderer,
                              uint32_t clear_color, int32_t clear_depth,
                              void *destination, size_t destination_size,
                              size_t *written_size,
                              struct rf_gpu_texture_resources_v1 *resources)
{
    if (!resources || (resources->desc_capacity && !resources->descs) ||
        (resources->texel_capacity && !resources->texels))
        return RF_GPU_RASTER_PACK_INVALID;
    resources->desc_count = 0; resources->texel_size = 0;
    return pack_toy(renderer, clear_color, clear_depth, destination,
                    destination_size, written_size, resources, UINT_MAX, UINT_MAX);
}

int rf_gpu_raster_pack_toy_textured_spans_v1(
                              const struct toy_renderer *renderer,
                              uint32_t clear_color, int32_t clear_depth,
                              void *destination, size_t destination_size,
                              size_t *written_size,
                              struct rf_gpu_texture_resources_v1 *resources,
                              uint32_t viewmodel_offset)
{
    if (!resources || (resources->desc_capacity && !resources->descs) ||
        (resources->texel_capacity && !resources->texels))
        return RF_GPU_RASTER_PACK_INVALID;
    resources->desc_count = 0; resources->texel_size = 0;
    return pack_toy(renderer, clear_color, clear_depth, destination,
                    destination_size, written_size, resources,
                    UINT_MAX, viewmodel_offset);
}

int rf_gpu_raster_pack_toy_textured_spans_v2(
                              const struct toy_renderer *renderer,
                              uint32_t clear_color, int32_t clear_depth,
                              void *destination, size_t destination_size,
                              size_t *written_size,
                              struct rf_gpu_texture_resources_v1 *resources,
                              uint32_t transparent_offset,
                              uint32_t viewmodel_offset)
{
    if (!resources || (resources->desc_capacity && !resources->descs) ||
        (resources->texel_capacity && !resources->texels))
        return RF_GPU_RASTER_PACK_INVALID;
    resources->desc_count = 0; resources->texel_size = 0;
    return pack_toy(renderer, clear_color, clear_depth, destination,
                    destination_size, written_size, resources,
                    transparent_offset, viewmodel_offset);
}

static int bytes_are_zero(const void *memory, size_t count)
{
    const unsigned char *bytes = memory;
    while (count--) if (*bytes++) return 0;
    return 1;
}

int rf_gpu_raster_validate_v1(const void *stream, size_t stream_size)
{
    const struct rf_gpu_raster_stream_header_v1 *header = stream;
    const struct rf_gpu_raster_cmd_v1 *commands;
    size_t required;
    uint32_t i, viewmodel_markers = 0, transparent_markers = 0;
    if (!stream || !host_is_little_endian() ||
        stream_size < sizeof(*header)) return RF_GPU_RASTER_PACK_INVALID;
    if (header->magic != RF_GPU_RASTER_ABI_MAGIC ||
        header->version != RF_GPU_RASTER_ABI_VERSION ||
        header->header_size != sizeof(*header) ||
        header->command_size != sizeof(struct rf_gpu_raster_cmd_v1) ||
        header->endian_tag != RF_GPU_RASTER_ENDIAN_LITTLE ||
        !header->framebuffer_width || !header->framebuffer_height ||
        header->framebuffer_width > INT_MAX ||
        header->framebuffer_height > INT_MAX)
        return RF_GPU_RASTER_PACK_INVALID;
    required = rf_gpu_raster_stream_size_v1(header->command_count);
    if (!required || required != stream_size) return RF_GPU_RASTER_PACK_INVALID;
    if (header->command_count < 2) return RF_GPU_RASTER_PACK_INVALID;
    commands = (const struct rf_gpu_raster_cmd_v1 *)(header + 1);
    if ((commands[0].kind != RF_GPU_RASTER_CMD_CLEAR_COLOR_V1 &&
         commands[0].kind != RF_GPU_RASTER_CMD_SKY_V1) ||
        commands[1].kind != RF_GPU_RASTER_CMD_CLEAR_DEPTH_V1)
        return RF_GPU_RASTER_PACK_INVALID;
    for (i = 0; i < header->command_count; ++i) {
        const struct rf_gpu_raster_cmd_v1 *cmd = &commands[i];
        if (cmd->byte_size != sizeof(*cmd))
            return RF_GPU_RASTER_PACK_INVALID;
        if (cmd->kind == RF_GPU_RASTER_CMD_SKY_V1) {
            if (i != 0 || cmd->flags || cmd->resource_handle ||
                !cmd->payload.sky.direction_cy ||
                !cmd->payload.sky.pitch_cy || cmd->payload.sky.flags ||
                !bytes_are_zero(cmd->payload.sky.reserved,
                                sizeof(cmd->payload.sky.reserved)))
                return RF_GPU_RASTER_PACK_INVALID;
        } else if (cmd->kind == RF_GPU_RASTER_CMD_BEGIN_TRANSPARENT_V1) {
            if (i < 2 || transparent_markers || viewmodel_markers ||
                cmd->flags || cmd->resource_handle ||
                cmd->payload.clear.value != 0 ||
                !bytes_are_zero(cmd->payload.clear.reserved,
                                sizeof(cmd->payload.clear.reserved)))
                return RF_GPU_RASTER_PACK_INVALID;
            transparent_markers++;
        } else if (cmd->kind == RF_GPU_RASTER_CMD_BEGIN_VIEWMODEL_V1) {
            if (i < 2 || viewmodel_markers || cmd->flags ||
                cmd->resource_handle || cmd->payload.clear.value != 0 ||
                !bytes_are_zero(cmd->payload.clear.reserved,
                                sizeof(cmd->payload.clear.reserved)))
                return RF_GPU_RASTER_PACK_INVALID;
            viewmodel_markers++;
        } else if (cmd->kind == RF_GPU_RASTER_CMD_CLEAR_COLOR_V1 ||
            cmd->kind == RF_GPU_RASTER_CMD_CLEAR_DEPTH_V1) {
            if (i > 1 || cmd->flags || cmd->resource_handle ||
                !bytes_are_zero(cmd->payload.clear.reserved,
                                sizeof(cmd->payload.clear.reserved)))
                return RF_GPU_RASTER_PACK_INVALID;
            if (cmd->kind == RF_GPU_RASTER_CMD_CLEAR_DEPTH_V1 &&
                (int32_t)cmd->payload.clear.value != 0)
                return RF_GPU_RASTER_PACK_INVALID;
        } else if (cmd->kind == RF_GPU_RASTER_CMD_FLAT_TRIANGLE_V1 ||
                   cmd->kind == RF_GPU_RASTER_CMD_VERTEX_LIT_TRIANGLE_V1 ||
                   cmd->kind == RF_GPU_RASTER_CMD_TEXTURED_TRIANGLE_V1) {
            const struct rf_gpu_raster_flat_triangle_v1 *t =
                &cmd->payload.flat_triangle;
            __int128 area_wide = ((__int128)t->c.x - t->a.x) *
                                     ((__int128)t->b.y - t->a.y) -
                                 ((__int128)t->c.y - t->a.y) *
                                     ((__int128)t->b.x - t->a.x);
            uint32_t expected;
            int source_over = (cmd->flags & RF_GPU_RASTER_FLAG_SOURCE_OVER_V1) != 0;
            if (source_over)
                expected = RF_GPU_RASTER_FLAG_DEPTH_TEST_V1 |
                           RF_GPU_RASTER_FLAG_SOURCE_OVER_V1;
            else
                expected = RF_GPU_RASTER_FLAG_DEPTH_TEST_V1 |
                           RF_GPU_RASTER_FLAG_DEPTH_WRITE_V1 |
                           RF_GPU_RASTER_FLAG_OPAQUE_V1;
            if ((cmd->kind == RF_GPU_RASTER_CMD_TEXTURED_TRIANGLE_V1 ?
                 cmd->payload.textured_triangle.fog_q8 : t->fog_q8) != 0)
                expected |= RF_GPU_RASTER_FLAG_FOG_V1;
            if (cmd->kind == RF_GPU_RASTER_CMD_TEXTURED_TRIANGLE_V1 &&
                (cmd->flags & RF_GPU_RASTER_FLAG_TEXTURE_REPEAT_V1))
                expected |= RF_GPU_RASTER_FLAG_TEXTURE_REPEAT_V1;
            if (i < 2 || cmd->flags != expected || t->area >= 0 ||
                area_wide < -9223372036854775807LL - 1 ||
                area_wide > 9223372036854775807LL ||
                t->area != (int64_t)area_wide ||
                (cmd->kind != RF_GPU_RASTER_CMD_TEXTURED_TRIANGLE_V1 &&
                 (t->bbox_minx < 0 || t->bbox_miny < 0 ||
                  t->bbox_maxx < t->bbox_minx ||
                  t->bbox_maxy < t->bbox_miny ||
                  (uint32_t)t->bbox_maxx >= header->framebuffer_width ||
                  (uint32_t)t->bbox_maxy >= header->framebuffer_height)) ||
                (cmd->kind == RF_GPU_RASTER_CMD_TEXTURED_TRIANGLE_V1 &&
                 (!cmd->resource_handle ||
                  (!source_over && cmd->payload.textured_triangle.reserved != 0) ||
                  (source_over && cmd->payload.textured_triangle.reserved > 255))) ||
                (cmd->kind != RF_GPU_RASTER_CMD_TEXTURED_TRIANGLE_V1 &&
                 cmd->resource_handle != 0) ||
                (cmd->kind == RF_GPU_RASTER_CMD_FLAT_TRIANGLE_V1 &&
                 ((source_over && (t->reserved[0] > 255 ||
                                   t->reserved[1] != 0)) ||
                  (!source_over && !bytes_are_zero(t->reserved, sizeof(t->reserved))))) ||
                (cmd->kind == RF_GPU_RASTER_CMD_VERTEX_LIT_TRIANGLE_V1 && source_over))
                return RF_GPU_RASTER_PACK_INVALID;
        } else return RF_GPU_RASTER_PACK_INVALID;
    }
    return RF_GPU_RASTER_PACK_OK;
}
