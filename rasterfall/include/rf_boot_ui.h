#ifndef RASTERFALL_RF_BOOT_UI_H
#define RASTERFALL_RF_BOOT_UI_H

#include "rf_core_host.h"

/* Boot presentation is transient.  The caller retains Core and Game ownership. */
struct rf_boot_result {
    int renderer; /* RF_CORE_RENDERER_* */
    int graphical;
    int automatic; /* Start Rasterfall: fall back to CPU on GPU init failure */
};

#define RF_BOOT_MAX_EVENTS 16
struct rf_boot_event {
    const char *service;
    int result; /* 0 ready, 1 optional unavailable, negative failure */
    int64_t elapsed_us;
};
struct rf_boot_journal {
    struct rf_boot_event events[RF_BOOT_MAX_EVENTS];
    int count;
    int64_t started_us, completed_us;
};

void rf_boot_record_event(void *context, const char *service, int result,
                          int64_t elapsed_us);
int rf_boot_init_display(void *context, struct rf_core *core,
                         const char *current_task);

/* Returns 1 to start the game, 0 on user exit, negative on display failure. */
int rf_boot_run(struct rf_core *core, struct rf_boot_result *result,
                const struct rf_boot_journal *journal, const char *error);
int rf_boot_progress(struct rf_core *core, int graphical,
                     const struct rf_boot_journal *journal,
                     const char *current_task, int completed, int total);

/* A completed, measured startup task.  The log remains useful without a UI. */
void rf_boot_log_task(const char *owner, const char *task, int result,
                      int64_t elapsed_us);

#endif
