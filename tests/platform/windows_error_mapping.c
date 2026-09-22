#include "toy_windows_errors.h"
#include "toy_windows_io.h"

int main(void)
{
    if (toy_windows_errno_from_error(TOYC_WIN_ERROR_FILE_NOT_FOUND) != ENOENT) return 1;
    if (toy_windows_errno_from_error(TOYC_WIN_ERROR_ACCESS_DENIED) != EACCES) return 2;
    if (toy_windows_errno_from_error(TOYC_WIN_ERROR_FILE_EXISTS) != EEXIST) return 3;
    if (toy_windows_negative_error(TOYC_WIN_ERROR_DISK_FULL) != -ENOSPC) return 4;
    if (toy_windows_negative_error(999999UL) != -EIO) return 5;
    if (toy_windows_errno_from_error(TOYC_WIN_ERROR_NO_UNICODE_TRANSLATION) != EINVAL) return 6;
    if (toy_windows_errno_from_error(TOYC_WIN_ERROR_DIRECTORY) != ENOTDIR) return 7;
    if (toy_windows_io_chunk((size_t)TOYC_WIN_MAX_IO_CHUNK + 1) != TOYC_WIN_MAX_IO_CHUNK) return 8;
    if (toy_windows_io_chunk(4096) != 4096) return 9;
    if (toy_windows_errno_from_error(TOYC_WIN_ERROR_BROKEN_PIPE) != EPIPE) return 10;
    return 0;
}
