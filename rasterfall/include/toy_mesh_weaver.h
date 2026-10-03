#ifndef TOY_MESH_WEAVER_H
#define TOY_MESH_WEAVER_H

#define TOY_WEAVER_ENVELOPE_MM 1020
#define TOY_WEAVER_PRODUCT_YAW_DEG 70

/* Manufacturing consumes a fixed, full asset catalog entry, never a render
 * LOD. Volumes are closed-component solid volumes in mm^3; bounds are mm.
 * All task work, power and energy below use work units, kW and kJ. */
struct toy_mesh_blueprint {
    int weapon;
    unsigned vertex_count, triangle_count;
    unsigned long long volume_mm3;
    unsigned bounds_mm[3];
    unsigned long long texture_bytes;
};

enum toy_mesh_weaver_phase {
    TOY_WEAVER_IDLE, TOY_WEAVER_CALIBRATING, TOY_WEAVER_WEAVING,
    TOY_WEAVER_DELIVERING, TOY_WEAVER_READY
};

enum toy_mesh_weaver_reason {
    TOY_WEAVER_OK, TOY_WEAVER_DISABLED, TOY_WEAVER_NO_POWER,
    TOY_WEAVER_NO_COMPUTE, TOY_WEAVER_NO_STORAGE, TOY_WEAVER_BAD_BLUEPRINT,
    TOY_WEAVER_TOO_LARGE, TOY_WEAVER_BUSY, TOY_WEAVER_OUTPUT_OCCUPIED,
    TOY_WEAVER_NO_OUTPUT
};

struct toy_mesh_weaver_coefficients {
    double work_per_vertex, work_per_triangle, work_per_texture_mib;
    double kj_per_m3, kj_per_vertex, kj_per_triangle;
    double work_per_round, kj_per_round;
    double calibration_ms, delivery_ms;
};

struct toy_mesh_weaver_cost {
    double work, form_kj;
    unsigned long long storage_bytes;
    int initial_rounds;
};

/* An explicitly fitted experimental RF1 power source and workstation. GHz
 * maps through task-specific work/GHz; it is not a FLOPS claim. The finite
 * source reserve pays actual base and formation energy only once. */
struct toy_mesh_weaver_supply {
    int power_on, cpu_on, x1_on;
    double source_kw, energy_kj;
    double cpu_ghz, cpu_work_per_ghz, x1_work_per_sec, compute_limit;
    double base_kw, cpu_kw, x1_kw;
    unsigned long long storage_bytes;
};

struct toy_mesh_weaver {
    int enabled, phase, pause_reason;
    int output_x, output_z, output_y;
    unsigned job_serial, produced_count, collected_count;
    double progress, phase_ms, elapsed_ms;
    double used_compute, used_form_kj, used_base_kj;
    struct toy_mesh_blueprint blueprint;
    struct toy_mesh_weaver_cost cost;
    struct toy_mesh_weaver_coefficients coefficients;
    struct toy_mesh_weaver_supply supply;
};

struct toy_game;
struct toy_game_actor;

void toy_mesh_weaver_defaults(struct toy_mesh_weaver *weaver);
int toy_mesh_weaver_cost(const struct toy_mesh_blueprint *blueprint,
    const struct toy_mesh_weaver_coefficients *coefficients,
    struct toy_mesh_weaver_cost *cost);
double toy_mesh_weaver_compute(const struct toy_mesh_weaver *weaver);
double toy_mesh_weaver_form_power(const struct toy_mesh_weaver *weaver);
double toy_mesh_weaver_estimate(const struct toy_mesh_weaver *weaver,
    const struct toy_mesh_weaver_cost *cost);
void toy_game_weaver_set_supply(struct toy_game *game,
    const struct toy_mesh_weaver_supply *supply);
int toy_game_weaver_start(struct toy_game *game,
    const struct toy_mesh_blueprint *blueprint);
void toy_game_weaver_update(struct toy_game *game, int dt_ms);
int toy_game_weaver_collect(struct toy_game *game, struct toy_game_actor *actor);
const char *toy_mesh_weaver_reason_name(int reason);
const char *toy_mesh_weaver_phase_name(int phase);

#endif
