#include "toyc_platform_contract.h"

typedef char toyc_contract_utf8_enabled[
    (TOYC_PATH_ENCODING_UTF8 == 1) ? 1 : -1];
typedef char toyc_contract_negative_errno[
    (TOYC_NEGATIVE_ERRNO == 1) ? 1 : -1];
typedef char toyc_contract_at_fdcwd[
    (TOYC_OPENAT_V1_DIRFD == AT_FDCWD) ? 1 : -1];
typedef char toyc_contract_stdin[(STDIN == 0 && TOYC_FD_STDIN == 0) ? 1 : -1];
typedef char toyc_contract_stdout[(STDOUT == 1 && TOYC_FD_STDOUT == 1) ? 1 : -1];
typedef char toyc_contract_stderr[(STDERR == 2 && TOYC_FD_STDERR == 2) ? 1 : -1];

static ssize_t (*contract_read)(int, void *, size_t) = __read;
static ssize_t (*contract_write)(int, const void *, size_t) = __write;
static int (*contract_openat)(int, const char *, int, unsigned int) = __openat;
static int (*contract_close)(int) = __close;
static int64_t (*contract_lseek)(int, int64_t, int) = toyc_lseek;
static int (*contract_fstat)(int, struct toyc_file_info *) = toyc_fstat;
static int (*contract_stat)(const char *, struct toyc_file_info *) = toyc_stat;
static int (*contract_dir_open)(struct toyc_dir_iterator *, const char *) = toyc_dir_open;
static int (*contract_dir_next)(struct toyc_dir_iterator *, char *, size_t,
                                struct toyc_dir_entry *) = toyc_dir_next;
static int (*contract_dir_close)(struct toyc_dir_iterator *) = toyc_dir_close;

typedef char toyc_contract_file_info_size[(sizeof(struct toyc_file_info) >=
                                           sizeof(int64_t) * 2) ? 1 : -1];
typedef char toyc_contract_dir_name_capacity[
    (TOYC_DIR_NAME_MAX >= 256) ? 1 : -1];

int toyc_platform_contract_header_test(void)
{
    return contract_read == 0 || contract_write == 0 ||
           contract_openat == 0 || contract_close == 0 ||
           contract_lseek == 0 || contract_fstat == 0 || contract_stat == 0 ||
           contract_dir_open == 0 || contract_dir_next == 0 || contract_dir_close == 0 ||
           TOYC_NEGERR(ENOENT) != -2;
}
