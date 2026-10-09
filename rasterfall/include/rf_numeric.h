#ifndef RF_NUMERIC_H
#define RF_NUMERIC_H

/* Integer authority: inputs are signed deltas, not necessarily directions.
 * Accepted component magnitude <= 2147483647. Outputs are rounded Q10;
 * component error < 1, length in [1022,1026]. Failure leaves outputs intact.
 * Return 1: success, 0: zero delta, -1: invalid/range. Aliasing is supported. */
int rf_direction_q10(long long dx, long long dz, int *sy, int *cy);
int rf_direction_valid(int sy, int cy);

struct rf_numeric_context {
    unsigned long long tick, frame, world;
    int kind, slot, id, generation, x, z;
};
struct rf_numeric_event {
    const char *source;
    int category, result, old_sy, old_cy, sy, cy;
    long long dx, dz;
    double raw_sy, raw_cy;
    unsigned long long count;
    struct rf_numeric_context context;
};
enum { RF_NUMERIC_ZERO=1, RF_NUMERIC_DIRECTION, RF_NUMERIC_RANGE,
       RF_NUMERIC_NONFINITE, RF_NUMERIC_TRANSFORM };
/* Main simulation/render thread owns diagnostics. No RNG or gameplay effects.
 * 32 recent keys; duplicates count without IO. Detailed dump is explicit. */
void rf_numeric_record(const struct rf_numeric_event *event);
void rf_numeric_dump(void);
void rf_numeric_dump_fatal(void);
void rf_numeric_reset(int detailed);
void rf_numeric_set_strict(int strict);
int rf_numeric_strict(void);
unsigned rf_numeric_event_count(void);
int rf_numeric_event_get(unsigned index, struct rf_numeric_event *event);
int rf_direction_set(const char *source, long long dx, long long dz,
    int *sy, int *cy, const struct rf_numeric_context *context);
void rf_direction_check(const char *source, int sy, int cy,
    const struct rf_numeric_context *context);

#endif
