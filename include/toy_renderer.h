#ifndef TOYC_TOY_RENDERER_H
#define TOYC_TOY_RENDERER_H

#include "toy_window.h"
#include "pthread.h"

struct toy_screen_vertex {
    int x;
    int y;
    int z;
    /* Optional Q16.16 UV and perspective terms. Pure-color callers may leave
     * these zero; textured callers use the *_over_z fields. */
    int u;
    int v;
    long inv_z;
    long u_over_z;
    long v_over_z;
};

struct toy_texture_view {
    const unsigned char *data;
    uint32_t width;
    uint32_t height;
    uint32_t data_size;
};

/* 一条待光栅化三角形命令。投影/裁剪在记录阶段完成，包围盒与 area 一并
 * 缓存，工作线程按自己的扫描行带直接消费，无需重算顶点级数据。 */
struct toy_raster_cmd {
    int textured;
    int repeat;
    uint32_t color;
    uint32_t fallback;
    const struct toy_texture_view *texture;
    long area;
    int bbox_minx;
    int bbox_maxx;
    int bbox_miny;
    int bbox_maxy;
    struct toy_screen_vertex a;
    struct toy_screen_vertex b;
    struct toy_screen_vertex c;
};

/* 渲染工作线程。job 期间只写本线程字段，主线程在 job_done_count 到齐后
 * 汇总，避免逐像素原子操作。 */
struct toy_render_worker {
    pthread_t thread;
    struct toy_renderer *renderer;
    int id;
    long pixels;
    unsigned long textured_pixels;
    unsigned long texture_fallback_pixels;
    /* 逐像素漏斗与路径统计（job 开始清零，主线程 flush 后汇总） */
    unsigned long bbox_px;    /* 包围盒内实际扫描像素（逐条带精确） */
    unsigned long inside_px;  /* 通过边函数覆盖测试的像素 */
    long flat_us;             /* 纯色路径光栅化累计耗时（us，路径段计时） */
    long tex_us;              /* 纹理路径光栅化累计耗时（us） */
    int last_path;            /* 路径段计时：上一条命令类型（-1=无） */
    long path_start;          /* 当前路径段起点（us） */
};

struct toy_renderer {
    struct toy_surface surface;
    int *depth;
    size_t depth_size;
    unsigned long textured_pixels;
    unsigned long textured_triangles;
    unsigned long texture_fallback_pixels;
    /* 记录阶段成功入列的三角形数与提交顶点数（每三角形 3 个），
     * toy_renderer_begin 清零；供调用方按阶段做性能统计。 */
    unsigned long submitted_triangles;
    unsigned long submitted_vertices;
    /* 最近一次 flush 的逐像素漏斗与路径统计；调用方在 flush 返回后读取
     * （覆盖式写入，非累计），toy_renderer_begin 清零。flat 三角形与像素
     * 由调用方用本 flush 的总数减去 last_tex_* 获得。 */
    unsigned long last_bbox_px;
    unsigned long last_inside_px;
    unsigned long last_tex_px;
    unsigned long last_tex_tris;
    long last_flat_us;
    long last_tex_us;
    unsigned long tex_tris_mark;   /* textured_triangles 的 flush 分界点 */
    /* 命令列表（记录阶段） */
    struct toy_raster_cmd *cmds;
    int cmd_count;
    int cmd_cap;
    int cmd_overflow;
    /* 并行光栅化线程池 */
    struct toy_render_worker *workers;
    int worker_count;
    volatile int job_generation;
    volatile int job_done_count;
    volatile int quit;
    int job_is_clear;
    uint32_t job_clear_color;
};

void toy_renderer_init(struct toy_renderer *renderer);
void toy_renderer_destroy(struct toy_renderer *renderer);
int toy_renderer_begin(struct toy_renderer *renderer,
                       const struct toy_surface *surface, uint32_t clear_color);
int toy_renderer_triangle(struct toy_renderer *renderer,
                          const struct toy_screen_vertex *a,
                          const struct toy_screen_vertex *b,
                          const struct toy_screen_vertex *c,
                          uint32_t color);
int toy_renderer_triangle_textured(struct toy_renderer *renderer,
                                   const struct toy_screen_vertex *a,
                                   const struct toy_screen_vertex *b,
                                   const struct toy_screen_vertex *c,
                                   const struct toy_texture_view *texture,
                                   int repeat, uint32_t fallback_color);
/* 把记录阶段的三角形命令并行光栅化到 surface；返回实际写入像素数
 * （接替 toy_renderer_triangle 系列的返回值语义）。 */
int toy_renderer_flush(struct toy_renderer *renderer);

#endif
