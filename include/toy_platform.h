#ifndef TOYC_PLATFORM_H
#define TOYC_PLATFORM_H

#define TOY_PLATFORM_PATH_MAX 160

int toy_platform_list_models(char paths[][TOY_PLATFORM_PATH_MAX], int max);

/* Read-only local hardware facts. Zero means unavailable, not zero capacity. */
struct toy_platform_hardware {
    int physical_cores;
    unsigned long long memory_mib;
};
void toy_platform_hardware_query(struct toy_platform_hardware *out);

#endif
