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

#define TOY_PLATFORM_HOST_CORES 24
/* System-wide presentation sample. Core indices follow the hardware query's
 * physical-core enumeration order; a valid core load averages its SMT threads. */
struct toy_platform_host_sample {
    unsigned long long memory_total_mib;
    unsigned long long memory_used_mib;
    unsigned char core_percent[TOY_PLATFORM_HOST_CORES];
    unsigned char core_valid[TOY_PLATFORM_HOST_CORES];
    int physical_cores;
    int memory_valid;
};
void toy_platform_host_sample(struct toy_platform_host_sample *out);

#ifdef TOYC_WINDOWS
/* Opt-in native frame diagnosis. CPU accounting may have coarse resolution;
 * wall minus CPU includes blocking and scheduling, not scheduling alone. */
unsigned long long toy_platform_thread_cpu_us(void);
#endif

#endif
