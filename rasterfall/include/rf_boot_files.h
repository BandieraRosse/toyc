#ifndef RASTERFALL_RF_BOOT_FILES_H
#define RASTERFALL_RF_BOOT_FILES_H

#include <stddef.h>

#define RF_BOOT_FILE_ROOT "rasterfall/assets"
#define RF_BOOT_LIST_LIMIT 12

/* Relative paths are normalized by the shell. The provider also checks every
 * on-disk component and refuses links/reparse points before opening a file. */
int rf_boot_files_stat(const char *relative, int *directory);
int rf_boot_files_list(const char *relative,
                       char entries[RF_BOOT_LIST_LIMIT][176],
                       int *count, int *more);
int rf_boot_files_read(const char *relative, unsigned char *bytes, size_t cap,
                       size_t *size, int *more);

#endif
