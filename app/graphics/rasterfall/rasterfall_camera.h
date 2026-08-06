#ifndef RASTERFALL_CAMERA_H
#define RASTERFALL_CAMERA_H

#define RASTERFALL_PITCH_LIMIT_SY 989
#define RASTERFALL_PITCH_LIMIT_CY 265

/* Rasterfall 使用 1024 定点单位保存水平偏航和垂直俯仰。相机是游戏会话、
 * 世界渲染、天空和网络玩家视图之间共享的数据类型，不归属于任一渲染模块。 */
struct camera {
    int x, z;
    int sy, cy;
    int pitch_sy, pitch_cy;
};

#endif
