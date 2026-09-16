#include "rf_gpu_raster_pack.h"
#include "toy_renderer.h"

#include <stdio.h>
#include <string.h>

#define CHECK(condition) do {                                             \
    if (!(condition)) {                                                   \
        fprintf(stderr, "rf-gpu-raster-pack-test: check failed at line %d\n", \
                __LINE__);                                                \
        return 1;                                                         \
    }                                                                     \
} while (0)

static void make_renderer(struct toy_renderer *renderer,
                          struct toy_raster_cmd *command)
{
    memset(renderer, 0, sizeof(*renderer));
    memset(command, 0, sizeof(*command));
    renderer->surface.width = 64;
    renderer->surface.height = 48;
    renderer->cmds = command;
    renderer->cmd_count = 1;
    command->material_alpha = 255;
    command->area = -600;
    command->bbox_minx = 10;
    command->bbox_maxx = 41;
    command->bbox_miny = 10;
    command->bbox_maxy = 31;
    command->a.x = 10; command->a.y = 10; command->a.inv_z = 1048576;
    command->b.x = 40; command->b.y = 10; command->b.inv_z = 524288;
    command->c.x = 10; command->c.y = 30; command->c.inv_z = 262144;
    command->color = 0xff336699U;
    command->light = 192;
    command->fog = 48;
}

int main(void)
{
    struct toy_renderer renderer;
    struct toy_raster_cmd source;
    unsigned char first[RF_GPU_RASTER_STREAM_HEADER_V1_SIZE +
                        3 * RF_GPU_RASTER_CMD_V1_SIZE];
    unsigned char second[sizeof(first)];
    struct rf_gpu_raster_stream_header_v1 *header;
    struct rf_gpu_raster_cmd_v1 *commands;
    size_t written = 0, written_again = 0;
    make_renderer(&renderer, &source);
    CHECK(rf_gpu_raster_stream_size_v1(3) == sizeof(first));
    CHECK(rf_gpu_raster_pack_toy_v1(&renderer, 0xff102030U, 0,
                                    first, sizeof(first), &written) == 0);
    CHECK(written == sizeof(first));
    CHECK(rf_gpu_raster_pack_toy_v1(&renderer, 0xff102030U, 0,
                                    second, sizeof(second), &written_again) == 0);
    CHECK(written_again == written && memcmp(first, second, written) == 0);
    CHECK(rf_gpu_raster_validate_v1(first, written) == 0);
    header = (struct rf_gpu_raster_stream_header_v1 *)first;
    commands = (struct rf_gpu_raster_cmd_v1 *)(header + 1);
    CHECK(header->command_count == 3 && header->framebuffer_width == 64 &&
          header->framebuffer_height == 48);
    CHECK(commands[0].kind == RF_GPU_RASTER_CMD_CLEAR_COLOR_V1 &&
          commands[0].payload.clear.value == 0xff102030U);
    CHECK(commands[1].kind == RF_GPU_RASTER_CMD_CLEAR_DEPTH_V1 &&
          commands[1].payload.clear.value == 0);
    CHECK(commands[2].kind == RF_GPU_RASTER_CMD_FLAT_TRIANGLE_V1 &&
          commands[2].payload.flat_triangle.area == -600 &&
          commands[2].payload.flat_triangle.c.inv_z == 262144 &&
          commands[2].payload.flat_triangle.light_q8 == 192 &&
          commands[2].payload.flat_triangle.fog_q8 == 48);
    CHECK(commands[2].flags == (RF_GPU_RASTER_FLAG_DEPTH_TEST_V1 |
                                RF_GPU_RASTER_FLAG_DEPTH_WRITE_V1 |
                                RF_GPU_RASTER_FLAG_OPAQUE_V1 |
                                RF_GPU_RASTER_FLAG_FOG_V1));

    CHECK(rf_gpu_raster_pack_toy_v1(&renderer, 0, 0, first,
                                    sizeof(first) - 1, &written) ==
          RF_GPU_RASTER_PACK_CAPACITY && written == 0);
    source.textured = 1;
    CHECK(rf_gpu_raster_pack_toy_v1(&renderer, 0, 0, first, sizeof(first),
                                    &written) == RF_GPU_RASTER_PACK_UNSUPPORTED);
    source.textured = 0;
    CHECK(rf_gpu_raster_pack_toy_v1(&renderer, 0, 1, first, sizeof(first),
                                    &written) == RF_GPU_RASTER_PACK_INVALID);
    CHECK(rf_gpu_raster_pack_toy_v1(&renderer, 0, 0, first, sizeof(first),
                                    &written) == 0);
    header = (struct rf_gpu_raster_stream_header_v1 *)first;
    commands = (struct rf_gpu_raster_cmd_v1 *)(header + 1);
    commands[2].payload.flat_triangle.area--;
    CHECK(rf_gpu_raster_validate_v1(first, written) == RF_GPU_RASTER_PACK_INVALID);
    commands[2].payload.flat_triangle.area++;
    commands[2].resource_handle = 7;
    CHECK(rf_gpu_raster_validate_v1(first, written) == RF_GPU_RASTER_PACK_INVALID);

    puts("rf-gpu-raster-pack-test: PASS");
    return 0;
}
