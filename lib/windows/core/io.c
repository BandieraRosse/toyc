#include <windows.h>
#include <stdint.h>

#include "core.h"
#include "toy_windows_errors.h"
#include "toy_windows_io.h"

#define TOYC_WIN_FD_CAPACITY 256
#define TOYC_WIN_FD_EMPTY 0
#define TOYC_WIN_FD_FILE 1
#define TOYC_WIN_FD_CONSOLE 2
#define TOYC_WIN_FD_SOCKET 3 /* reserved; keeps one future table */

struct toy_windows_fd_entry {
    HANDLE handle;
    unsigned char kind;
    CRITICAL_SECTION io_lock;
};

static struct toy_windows_fd_entry fd_table[TOYC_WIN_FD_CAPACITY];
static INIT_ONCE fd_once = INIT_ONCE_STATIC_INIT;
static CRITICAL_SECTION fd_lock;

static BOOL CALLBACK toy_windows_fd_init(PINIT_ONCE once, PVOID param, PVOID *ctx)
{
    int fd;
    (void)once; (void)param; (void)ctx;
    InitializeCriticalSection(&fd_lock);
    for (fd = 0; fd < TOYC_WIN_FD_CAPACITY; fd++)
        InitializeCriticalSection(&fd_table[fd].io_lock);
    for (fd = 0; fd < 3; fd++) {
        DWORD type;
        fd_table[fd].handle = GetStdHandle(fd == 0 ? STD_INPUT_HANDLE :
                                           fd == 1 ? STD_OUTPUT_HANDLE :
                                           STD_ERROR_HANDLE);
        type = fd_table[fd].handle ? GetFileType(fd_table[fd].handle) : FILE_TYPE_UNKNOWN;
        fd_table[fd].kind = type == FILE_TYPE_CHAR ? TOYC_WIN_FD_CONSOLE :
                            (fd_table[fd].handle && fd_table[fd].handle != INVALID_HANDLE_VALUE ?
                             TOYC_WIN_FD_FILE : TOYC_WIN_FD_EMPTY);
    }
    return TRUE;
}

static int toy_windows_fd_ready(void)
{
    return InitOnceExecuteOnce(&fd_once, toy_windows_fd_init, NULL, NULL) ? 0 : -EIO;
}

static wchar_t *toy_windows_utf8_to_wide(const char *path)
{
    int chars;
    wchar_t *wide;
    if (!path) { SetLastError(ERROR_INVALID_PARAMETER); return NULL; }
    chars = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1, NULL, 0);
    if (chars <= 0) return NULL;
    wide = (wchar_t *)HeapAlloc(GetProcessHeap(), 0, (SIZE_T)chars * sizeof(*wide));
    if (!wide) { SetLastError(ERROR_NOT_ENOUGH_MEMORY); return NULL; }
    if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1, wide, chars)) {
        HeapFree(GetProcessHeap(), 0, wide);
        return NULL;
    }
    return wide;
}

static int toy_windows_access(int flags, unsigned long *access)
{
    switch (flags & 3) {
    case O_WRONLY: *access = (flags & O_APPEND) ? FILE_APPEND_DATA : GENERIC_WRITE; return 0;
    case O_RDWR: *access = (flags & O_APPEND) ? GENERIC_READ | FILE_APPEND_DATA : GENERIC_READ | GENERIC_WRITE; return 0;
    case O_RDONLY: *access = GENERIC_READ; return 0;
    default: return -EINVAL;
    }
}

static DWORD toy_windows_creation(int flags)
{
    if (flags & O_CREAT) return (flags & O_TRUNC) ? CREATE_ALWAYS : OPEN_ALWAYS;
    if (flags & O_TRUNC) return TRUNCATE_EXISTING;
    return OPEN_EXISTING;
}

static int toy_windows_alloc_fd(HANDLE handle, unsigned char kind)
{
    int fd;
    for (fd = 3; fd < TOYC_WIN_FD_CAPACITY; fd++) {
        if (fd_table[fd].kind == TOYC_WIN_FD_EMPTY) {
            fd_table[fd].handle = handle;
            fd_table[fd].kind = kind;
            return fd;
        }
    }
    return -EMFILE;
}

int __openat(int dirfd, const char *path, int flags, unsigned int mode)
{
    wchar_t *wide;
    HANDLE handle;
    unsigned long access;
    int fd;
    (void)mode;
    if (dirfd != AT_FDCWD) return -ENOSYS;
    if (toy_windows_access(flags, &access) < 0) return -EINVAL;
    if (toy_windows_fd_ready() < 0) return -EIO;
    wide = toy_windows_utf8_to_wide(path);
    if (!wide) return toy_windows_negative_error(GetLastError());
    handle = CreateFileW(wide, access,
                         FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                         NULL, toy_windows_creation(flags),
                         FILE_ATTRIBUTE_NORMAL, NULL);
    HeapFree(GetProcessHeap(), 0, wide);
    if (handle == INVALID_HANDLE_VALUE) return toy_windows_negative_error(GetLastError());
    EnterCriticalSection(&fd_lock);
    fd = toy_windows_alloc_fd(handle, TOYC_WIN_FD_FILE);
    LeaveCriticalSection(&fd_lock);
    if (fd < 0) { CloseHandle(handle); return fd; }
    return fd;
}

ssize_t __read(int fd, void *buffer, size_t length)
{
    DWORD count;
    struct toy_windows_fd_entry *entry;
    BOOL ok;
    if (toy_windows_fd_ready() < 0) return -EIO;
    EnterCriticalSection(&fd_lock);
    if (fd < 0 || fd >= TOYC_WIN_FD_CAPACITY || fd_table[fd].kind == TOYC_WIN_FD_EMPTY) {
        LeaveCriticalSection(&fd_lock); return -EBADF;
    }
    entry = &fd_table[fd];
    EnterCriticalSection(&entry->io_lock);
    LeaveCriticalSection(&fd_lock);
    ok = ReadFile(entry->handle, buffer, (DWORD)toy_windows_io_chunk(length), &count, NULL);
    /* Anonymous pipe EOF is reported by Win32 as ERROR_BROKEN_PIPE; the
     * common read contract represents it as a zero-byte successful read.
     * WriteFile keeps the normal -EPIPE mapping below. */
    if (!ok) {
        DWORD error_code = GetLastError();
        if (error_code == ERROR_BROKEN_PIPE) {
            LeaveCriticalSection(&entry->io_lock);
            return 0;
        }
        { int error = toy_windows_negative_error(error_code); LeaveCriticalSection(&entry->io_lock); return error; }
    }
    LeaveCriticalSection(&entry->io_lock);
    return (ssize_t)count;
}

ssize_t __write(int fd, const void *buffer, size_t length)
{
    DWORD count;
    struct toy_windows_fd_entry *entry;
    BOOL ok;
    if (toy_windows_fd_ready() < 0) return -EIO;
    EnterCriticalSection(&fd_lock);
    if (fd < 0 || fd >= TOYC_WIN_FD_CAPACITY || fd_table[fd].kind == TOYC_WIN_FD_EMPTY) {
        LeaveCriticalSection(&fd_lock); return -EBADF;
    }
    entry = &fd_table[fd];
    EnterCriticalSection(&entry->io_lock);
    LeaveCriticalSection(&fd_lock);
    ok = WriteFile(entry->handle, buffer, (DWORD)toy_windows_io_chunk(length), &count, NULL);
    if (!ok) { int error = toy_windows_negative_error(GetLastError()); LeaveCriticalSection(&entry->io_lock); return error; }
    LeaveCriticalSection(&entry->io_lock);
    return (ssize_t)count;
}

int __close(int fd)
{
    HANDLE handle;
    if (toy_windows_fd_ready() < 0) return -EIO;
    EnterCriticalSection(&fd_lock);
    if (fd < 0 || fd >= TOYC_WIN_FD_CAPACITY || fd_table[fd].kind == TOYC_WIN_FD_EMPTY) {
        LeaveCriticalSection(&fd_lock); return -EBADF;
    }
    EnterCriticalSection(&fd_table[fd].io_lock);
    handle = fd_table[fd].handle;
    fd_table[fd].handle = NULL; fd_table[fd].kind = TOYC_WIN_FD_EMPTY;
    LeaveCriticalSection(&fd_lock);
    if (!CloseHandle(handle)) { int error = toy_windows_negative_error(GetLastError()); LeaveCriticalSection(&fd_table[fd].io_lock); return error; }
    LeaveCriticalSection(&fd_table[fd].io_lock);
    return 0;
}

/* ── Portable metadata/path/目录 contract ── */

static int toy_windows_info_from_handle(HANDLE handle,
                                        struct toyc_file_info *info)
{
    BY_HANDLE_FILE_INFORMATION native;
    ULARGE_INTEGER size;
    ULARGE_INTEGER mtime;
    const unsigned long long windows_epoch = 11644473600ULL;
    if (!info) return -EINVAL;
    if (!GetFileInformationByHandle(handle, &native))
        return toy_windows_negative_error(GetLastError());
    info->type = (native.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ?
                 TOYC_FILE_DIRECTORY : TOYC_FILE_REGULAR;
    size.HighPart = native.nFileSizeHigh;
    size.LowPart = native.nFileSizeLow;
    info->size = (int64_t)size.QuadPart;
    mtime.HighPart = native.ftLastWriteTime.dwHighDateTime;
    mtime.LowPart = native.ftLastWriteTime.dwLowDateTime;
    info->mtime_sec = (int64_t)(mtime.QuadPart / 10000000ULL);
    if ((unsigned long long)info->mtime_sec >= windows_epoch)
        info->mtime_sec -= (int64_t)windows_epoch;
    else
        info->mtime_sec = 0;
    return 0;
}

int64_t toyc_lseek(int fd, int64_t offset, int whence)
{
    LARGE_INTEGER distance;
    LARGE_INTEGER result;
    struct toy_windows_fd_entry *entry;
    if (toy_windows_fd_ready() < 0) return -EIO;
    EnterCriticalSection(&fd_lock);
    if (fd < 0 || fd >= TOYC_WIN_FD_CAPACITY ||
        fd_table[fd].kind == TOYC_WIN_FD_EMPTY) {
        LeaveCriticalSection(&fd_lock);
        return -EBADF;
    }
    entry = &fd_table[fd];
    EnterCriticalSection(&entry->io_lock);
    LeaveCriticalSection(&fd_lock);
    distance.QuadPart = (LONGLONG)offset;
    if (!SetFilePointerEx(entry->handle, distance, &result, (DWORD)whence)) {
        int error = toy_windows_negative_error(GetLastError());
        LeaveCriticalSection(&entry->io_lock);
        return (int64_t)error;
    }
    LeaveCriticalSection(&entry->io_lock);
    return (int64_t)result.QuadPart;
}

int toyc_fstat(int fd, struct toyc_file_info *info)
{
    struct toy_windows_fd_entry *entry;
    int result;
    if (toy_windows_fd_ready() < 0) return -EIO;
    EnterCriticalSection(&fd_lock);
    if (fd < 0 || fd >= TOYC_WIN_FD_CAPACITY ||
        fd_table[fd].kind == TOYC_WIN_FD_EMPTY) {
        LeaveCriticalSection(&fd_lock);
        return -EBADF;
    }
    entry = &fd_table[fd];
    EnterCriticalSection(&entry->io_lock);
    LeaveCriticalSection(&fd_lock);
    result = toy_windows_info_from_handle(entry->handle, info);
    LeaveCriticalSection(&entry->io_lock);
    return result;
}

int toyc_stat(const char *utf8_path, struct toyc_file_info *info)
{
    wchar_t *wide;
    HANDLE handle;
    int result;
    if (!info) return -EINVAL;
    wide = toy_windows_utf8_to_wide(utf8_path);
    if (!wide) return toy_windows_negative_error(GetLastError());
    handle = CreateFileW(wide, FILE_READ_ATTRIBUTES,
                         FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                         NULL, OPEN_EXISTING,
                         FILE_ATTRIBUTE_NORMAL | FILE_FLAG_BACKUP_SEMANTICS, NULL);
    HeapFree(GetProcessHeap(), 0, wide);
    if (handle == INVALID_HANDLE_VALUE)
        return toy_windows_negative_error(GetLastError());
    result = toy_windows_info_from_handle(handle, info);
    CloseHandle(handle);
    return result;
}

static int toy_windows_wide_to_utf8(const wchar_t *wide, char *utf8,
                                    size_t capacity)
{
    int bytes;
    if (!wide || !utf8 || capacity == 0) return -EINVAL;
    bytes = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide, -1,
                                NULL, 0, NULL, NULL);
    if (bytes <= 0) return toy_windows_negative_error(GetLastError());
    if ((size_t)bytes > capacity) return -ENAMETOOLONG;
    if (!WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide, -1,
                             utf8, (int)capacity, NULL, NULL))
        return toy_windows_negative_error(GetLastError());
    return 0;
}

int toyc_getcwd(char *utf8_buf, size_t capacity)
{
    DWORD count;
    wchar_t *wide;
    int result;
    if (!utf8_buf || capacity == 0) return -EINVAL;
    count = GetCurrentDirectoryW(0, NULL);
    if (count == 0) return toy_windows_negative_error(GetLastError());
    wide = (wchar_t *)HeapAlloc(GetProcessHeap(), 0,
                                ((SIZE_T)count + 1) * sizeof(*wide));
    if (!wide) return -ENOMEM;
    if (!GetCurrentDirectoryW(count + 1, wide)) {
        result = toy_windows_negative_error(GetLastError());
        HeapFree(GetProcessHeap(), 0, wide);
        return result;
    }
    result = toy_windows_wide_to_utf8(wide, utf8_buf, capacity);
    HeapFree(GetProcessHeap(), 0, wide);
    return result;
}

int toyc_chdir(const char *utf8_path)
{
    wchar_t *wide = toy_windows_utf8_to_wide(utf8_path);
    BOOL ok;
    int result;
    if (!wide) return toy_windows_negative_error(GetLastError());
    ok = SetCurrentDirectoryW(wide);
    result = ok ? 0 : toy_windows_negative_error(GetLastError());
    HeapFree(GetProcessHeap(), 0, wide);
    return result;
}

int toyc_mkdir(const char *utf8_path, unsigned int mode)
{
    wchar_t *wide;
    BOOL ok;
    int result;
    (void)mode;
    wide = toy_windows_utf8_to_wide(utf8_path);
    if (!wide) return toy_windows_negative_error(GetLastError());
    ok = CreateDirectoryW(wide, NULL);
    result = ok ? 0 : toy_windows_negative_error(GetLastError());
    HeapFree(GetProcessHeap(), 0, wide);
    return result;
}

int toyc_unlink(const char *utf8_path)
{
    wchar_t *wide = toy_windows_utf8_to_wide(utf8_path);
    BOOL ok;
    int result;
    if (!wide) return toy_windows_negative_error(GetLastError());
    ok = DeleteFileW(wide);
    result = ok ? 0 : toy_windows_negative_error(GetLastError());
    HeapFree(GetProcessHeap(), 0, wide);
    return result;
}

int toyc_rmdir(const char *utf8_path)
{
    wchar_t *wide = toy_windows_utf8_to_wide(utf8_path);
    BOOL ok;
    int result;
    if (!wide) return toy_windows_negative_error(GetLastError());
    ok = RemoveDirectoryW(wide);
    result = ok ? 0 : toy_windows_negative_error(GetLastError());
    HeapFree(GetProcessHeap(), 0, wide);
    return result;
}

int toyc_rename(const char *old_utf8_path, const char *new_utf8_path)
{
    wchar_t *old_wide = toy_windows_utf8_to_wide(old_utf8_path);
    wchar_t *new_wide;
    BOOL ok;
    int result;
    if (!old_wide) return toy_windows_negative_error(GetLastError());
    new_wide = toy_windows_utf8_to_wide(new_utf8_path);
    if (!new_wide) {
        result = toy_windows_negative_error(GetLastError());
        HeapFree(GetProcessHeap(), 0, old_wide);
        return result;
    }
    ok = MoveFileExW(old_wide, new_wide, MOVEFILE_REPLACE_EXISTING);
    result = ok ? 0 : toy_windows_negative_error(GetLastError());
    HeapFree(GetProcessHeap(), 0, old_wide);
    HeapFree(GetProcessHeap(), 0, new_wide);
    return result;
}

static HANDLE toy_windows_dir_handle(const struct toyc_dir_iterator *iterator)
{
    return (HANDLE)(uintptr_t)iterator->native;
}

static int toy_windows_dir_set_pending(struct toyc_dir_iterator *iterator,
                                       const WIN32_FIND_DATAW *data)
{
    int result;
    result = toy_windows_wide_to_utf8(data->cFileName,
                                      iterator->pending_name,
                                      sizeof(iterator->pending_name));
    if (result < 0) return result;
    iterator->pending_len = 0;
    while (iterator->pending_name[iterator->pending_len])
        iterator->pending_len++;
    iterator->pending_type = (data->dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ?
                             TOYC_FILE_DIRECTORY : TOYC_FILE_REGULAR;
    return 0;
}

int toyc_dir_open(struct toyc_dir_iterator *iterator, const char *utf8_path)
{
    wchar_t *wide;
    wchar_t *pattern;
    size_t path_len;
    HANDLE handle;
    WIN32_FIND_DATAW data;
    int result;
    if (!iterator || !utf8_path) return -EINVAL;
    for (size_t i = 0; i < sizeof(*iterator); i++)
        ((unsigned char *)iterator)[i] = 0;
    iterator->fd = -1;
    wide = toy_windows_utf8_to_wide(utf8_path);
    if (!wide) return toy_windows_negative_error(GetLastError());
    path_len = 0;
    while (wide[path_len]) path_len++;
    pattern = (wchar_t *)HeapAlloc(GetProcessHeap(), 0,
                                   (path_len + 3) * sizeof(*pattern));
    if (!pattern) {
        HeapFree(GetProcessHeap(), 0, wide);
        return -ENOMEM;
    }
    for (size_t i = 0; i <= path_len; i++) pattern[i] = wide[i];
    if (path_len && pattern[path_len - 1] != L'\\' && pattern[path_len - 1] != L'/')
        pattern[path_len++] = L'\\';
    pattern[path_len++] = L'*';
    pattern[path_len] = L'\0';
    handle = FindFirstFileW(pattern, &data);
    HeapFree(GetProcessHeap(), 0, pattern);
    HeapFree(GetProcessHeap(), 0, wide);
    if (handle == INVALID_HANDLE_VALUE)
        return toy_windows_negative_error(GetLastError());
    iterator->native = (uint64_t)(uintptr_t)handle;
    iterator->dir_state = 1;
    result = toy_windows_dir_set_pending(iterator, &data);
    if (result < 0) {
        FindClose(handle);
        iterator->native = 0;
        iterator->dir_state = 0;
    }
    return result;
}

int toyc_dir_next(struct toyc_dir_iterator *iterator, char *utf8_name,
                  size_t capacity, struct toyc_dir_entry *entry)
{
    WIN32_FIND_DATAW data;
    int result;
    if (!iterator || !utf8_name || !entry || capacity == 0 || !iterator->dir_state)
        return -EINVAL;
    if (iterator->pending_name[0]) {
        if (iterator->pending_len + 1 > capacity) return -ENAMETOOLONG;
        for (size_t i = 0; i <= iterator->pending_len; i++)
            utf8_name[i] = iterator->pending_name[i];
        entry->type = iterator->pending_type;
        entry->name_len = iterator->pending_len;
        iterator->pending_name[0] = 0;
        return 1;
    }
    if (!FindNextFileW(toy_windows_dir_handle(iterator), &data)) {
        DWORD error = GetLastError();
        if (error == ERROR_NO_MORE_FILES) return 0;
        return toy_windows_negative_error(error);
    }
    result = toy_windows_dir_set_pending(iterator, &data);
    if (result < 0) return result;
    return toyc_dir_next(iterator, utf8_name, capacity, entry);
}

int toyc_dir_close(struct toyc_dir_iterator *iterator)
{
    BOOL ok;
    if (!iterator || !iterator->dir_state) return -EBADF;
    ok = FindClose(toy_windows_dir_handle(iterator));
    iterator->native = 0;
    iterator->dir_state = 0;
    iterator->pending_name[0] = 0;
    return ok ? 0 : toy_windows_negative_error(GetLastError());
}
