#ifndef RASTERFALL_CAMERA_H
#define RASTERFALL_CAMERA_H

#define RASTERFALL_PITCH_LIMIT_SY 989
#define RASTERFALL_PITCH_LIMIT_CY 265

/* Body position and view input intentionally occupy separate namespaces while
 * preserving the historic flat layout used by the network codec.  The
 * anonymous members keep old call sites source-compatible during migration;
 * new gameplay code should use body, and new view/input code should use view. */
struct rasterfall_camera_body {
    int x, z;
};

/* Rasterfall 使用 1024 定点单位保存水平偏航和垂直俯仰。y is the
 * presentation height derived from body/ground state, not a gameplay body
 * position authority. */
struct rasterfall_camera_view {
    int sy, cy;
    int pitch_sy, pitch_cy;
    int y;
};

struct camera {
    union {
        struct rasterfall_camera_body body;
        struct { int x, z; };       /* legacy layout alias */
    };
    union {
        struct rasterfall_camera_view view;
        struct {                     /* legacy layout alias */
            int sy, cy;
            int pitch_sy, pitch_cy;
            int y;
        };
    };
};

static inline void rasterfall_camera_set_body(struct camera *camera,
                                              int x, int z)
{
    if (!camera) return;
    camera->body.x = x;
    camera->body.z = z;
}

#endif
