#ifndef RASTERFALL_UNITS_H
#define RASTERFALL_UNITS_H

/* Rasterfall world length convention.
 *
 * World/map/gameplay coordinates use integer RFU.  Asset-local units (PMX,
 * GLB, animation translations) must be converted at the presentation edge;
 * they are never implicitly treated as RFU. */
#define RASTERFALL_RFU_PER_METER 512
#define RASTERFALL_RFU_FROM_MM(mm) ((mm) * RASTERFALL_RFU_PER_METER / 1000)
#define RASTERFALL_RFU_FROM_CM(cm) ((cm) * RASTERFALL_RFU_PER_METER / 100)

/* The legacy world ground plane is Y=-900.  Gameplay ground/airborne values
 * are offsets from that plane, so the standing camera base is negative. */
#define RASTERFALL_WORLD_GROUND_Y (-900)
#define RASTERFALL_HUMAN_HEIGHT_MM 1750
#define RASTERFALL_HUMAN_HEIGHT_RFU RASTERFALL_RFU_FROM_MM(RASTERFALL_HUMAN_HEIGHT_MM)
#define RASTERFALL_HUMAN_EYE_HEIGHT_MM 1610
#define RASTERFALL_HUMAN_EYE_HEIGHT_RFU RASTERFALL_RFU_FROM_MM(RASTERFALL_HUMAN_EYE_HEIGHT_MM)
#define RASTERFALL_STANDING_CAMERA_Y \
    (RASTERFALL_WORLD_GROUND_Y + RASTERFALL_HUMAN_EYE_HEIGHT_RFU)

/* The procedural actor was authored from -900 through +200 (1100 RFU).
 * Scale its local vertical coordinates about the feet to the 175 cm norm. */
#define RASTERFALL_LEGACY_ACTOR_HEIGHT_RFU 1100

#endif
