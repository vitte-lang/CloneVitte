/*
 * Vitte Compiler
 * src/filesystem/filesystem.c
 *
 * Canonical filesystem abstraction used by compiler infrastructure.
 *
 * Responsibilities:
 *
 *   - file existence and metadata queries;
 *   - regular-file / directory classification;
 *   - bounded whole-file reads;
 *   - deterministic whole-file writes;
 *   - atomic replacement writes;
 *   - directory creation;
 *   - recursive directory creation;
 *   - file removal and rename;
 *   - path joining and normalization;
 *   - absolute-path detection;
 *   - canonical path resolution;
 *   - current working directory queries;
 *   - deterministic directory enumeration;
 *   - checked size arithmetic;
 *   - stable error translation;
 *   - filesystem statistics.
 *
 * Non-responsibilities:
 *
 *   - source decoding;
 *   - module resolution;
 *   - package resolution;
 *   - manifest parsing;
 *   - compiler diagnostics rendering;
 *   - virtual filesystems;
 *   - network filesystems;
 *   - archive formats;
 *   - process execution.
 *
 * Design goals:
 *
 *   1. No unchecked arithmetic.
 *   2. No silent partial writes.
 *   3. No dependence on directory enumeration order.
 *   4. Binary-safe file contents.
 *   5. Explicit ownership.
 *   6. Bounded allocations.
 *   7. Stable compiler-facing errors.
 *   8. Atomic replacement when requested.
 *   9. POSIX-first implementation with platform isolation.
 *  10. ISO C17 source with POSIX filesystem extensions.
 */

#if defined(__APPLE__) && !defined(_DARWIN_C_SOURCE)
#define _DARWIN_C_SOURCE 1
#endif
#if !defined(_XOPEN_SOURCE)
#define _XOPEN_SOURCE 700
#endif
#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif

#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#if defined(_WIN32)
#include <direct.h>
#include <fcntl.h>
#include <io.h>
#include <process.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <windows.h>

#ifndef R_OK
#define R_OK 4
#endif
#ifndef W_OK
#define W_OK 2
#endif
#ifndef X_OK
#define X_OK 0
#endif
#ifndef F_OK
#define F_OK 0
#endif
#ifndef ENAMETOOLONG
#define ENAMETOOLONG EINVAL
#endif
#ifndef EOVERFLOW
#define EOVERFLOW ERANGE
#endif
#ifndef S_ISREG
#define S_ISREG(value) (((value) & _S_IFMT) == _S_IFREG)
#endif
#ifndef S_ISDIR
#define S_ISDIR(value) (((value) & _S_IFMT) == _S_IFDIR)
#endif
#ifndef S_ISLNK
#define S_ISLNK(value) (0)
#endif

#define fstat _fstat64
#define close _close
#define read _read
#define write _write
#define fsync _commit

#ifdef O_RDONLY
#undef O_RDONLY
#endif
#define O_RDONLY (_O_RDONLY | _O_BINARY)
#ifdef O_WRONLY
#undef O_WRONLY
#endif
#define O_WRONLY (_O_WRONLY | _O_BINARY)
#ifdef O_RDWR
#undef O_RDWR
#endif
#define O_RDWR (_O_RDWR | _O_BINARY)

#define VITTE_WINDOWS_DIRENT_NAME_CAPACITY (MAX_PATH * 4u)
#define VITTE_FILESYSTEM_STAT_TYPE struct _stat64

/* The public API uses UTF-8 paths.  The A variants of the Windows APIs use
 * the active system code page, so they cannot represent every valid Vitte
 * source path.  Keep conversion at this boundary and use the wide APIs for
 * all path-bearing operations. */
static wchar_t *
vitte_windows_utf8_to_wide(
    const char *text)
{
    int required;
    wchar_t *result;

    if (text == NULL) {
        errno = EINVAL;
        return NULL;
    }
    required = MultiByteToWideChar(
        CP_UTF8,
        MB_ERR_INVALID_CHARS,
        text,
        -1,
        NULL,
        0);
    if (required <= 0) {
        errno = EINVAL;
        return NULL;
    }
    if ((size_t)required > SIZE_MAX / sizeof(*result)) {
        errno = EOVERFLOW;
        return NULL;
    }
    result = (wchar_t *)malloc((size_t)required * sizeof(*result));
    if (result == NULL) {
        errno = ENOMEM;
        return NULL;
    }
    if (MultiByteToWideChar(
            CP_UTF8,
            MB_ERR_INVALID_CHARS,
            text,
            -1,
            result,
            required) != required) {
        free(result);
        errno = EINVAL;
        return NULL;
    }
    return result;
}

static char *
vitte_windows_wide_to_utf8(
    const wchar_t *text)
{
    int required;
    char *result;

    if (text == NULL) {
        errno = EINVAL;
        return NULL;
    }
    required = WideCharToMultiByte(
        CP_UTF8,
        WC_ERR_INVALID_CHARS,
        text,
        -1,
        NULL,
        0,
        NULL,
        NULL);
    if (required <= 0) {
        errno = EINVAL;
        return NULL;
    }
    result = (char *)malloc((size_t)required);
    if (result == NULL) {
        errno = ENOMEM;
        return NULL;
    }
    if (WideCharToMultiByte(
            CP_UTF8,
            WC_ERR_INVALID_CHARS,
            text,
            -1,
            result,
            required,
            NULL,
            NULL) != required) {
        free(result);
        errno = EINVAL;
        return NULL;
    }
    return result;
}

static int
vitte_windows_stat_utf8(
    const char *path,
    struct _stat64 *value)
{
    wchar_t *wide_path;
    int result;

    wide_path = vitte_windows_utf8_to_wide(path);
    if (wide_path == NULL) {
        return -1;
    }
    result = _wstat64(wide_path, value);
    free(wide_path);
    return result;
}

static int
vitte_windows_access_utf8(
    const char *path,
    int mode)
{
    wchar_t *wide_path;
    int result;

    wide_path = vitte_windows_utf8_to_wide(path);
    if (wide_path == NULL) {
        return -1;
    }
    result = _waccess(wide_path, mode);
    free(wide_path);
    return result;
}

static int
vitte_windows_open_utf8(
    const char *path,
    int flags,
    ...)
{
    wchar_t *wide_path;
    int mode;
    int result;
    va_list arguments;

    mode = 0;
    va_start(arguments, flags);
    if ((flags & _O_CREAT) != 0) {
        mode = va_arg(arguments, int);
    }
    va_end(arguments);
    wide_path = vitte_windows_utf8_to_wide(path);
    if (wide_path == NULL) {
        return -1;
    }
    if ((flags & _O_CREAT) != 0) {
        result = _wopen(wide_path, flags, mode);
    } else {
        result = _wopen(wide_path, flags);
    }
    free(wide_path);
    return result;
}

static int
vitte_windows_mkdir_utf8(
    const char *path,
    unsigned int mode)
{
    wchar_t *wide_path;
    int result;

    (void)mode;
    wide_path = vitte_windows_utf8_to_wide(path);
    if (wide_path == NULL) {
        return -1;
    }
    result = _wmkdir(wide_path);
    free(wide_path);
    return result;
}

static int
vitte_windows_unlink_utf8(
    const char *path)
{
    wchar_t *wide_path;
    int result;

    wide_path = vitte_windows_utf8_to_wide(path);
    if (wide_path == NULL) {
        return -1;
    }
    result = _wunlink(wide_path);
    free(wide_path);
    return result;
}

static int
vitte_windows_rmdir_utf8(
    const char *path)
{
    wchar_t *wide_path;
    int result;

    wide_path = vitte_windows_utf8_to_wide(path);
    if (wide_path == NULL) {
        return -1;
    }
    result = _wrmdir(wide_path);
    free(wide_path);
    return result;
}

static int
vitte_windows_rename_utf8(
    const char *source,
    const char *destination)
{
    wchar_t *wide_source;
    wchar_t *wide_destination;
    int result;

    wide_source = vitte_windows_utf8_to_wide(source);
    if (wide_source == NULL) {
        return -1;
    }
    wide_destination = vitte_windows_utf8_to_wide(destination);
    if (wide_destination == NULL) {
        free(wide_source);
        return -1;
    }
    result = _wrename(wide_source, wide_destination);
    free(wide_source);
    free(wide_destination);
    return result;
}

static char *
vitte_windows_getcwd_utf8(
    char *buffer,
    size_t capacity)
{
    DWORD required;
    DWORD written;
    wchar_t *wide_buffer;
    int utf8_size;

    if (buffer == NULL || capacity == 0u) {
        errno = EINVAL;
        return NULL;
    }
    required = GetCurrentDirectoryW(0u, NULL);
    if (required == 0u || required == (DWORD)-1) {
        errno = EIO;
        return NULL;
    }
#if SIZE_MAX <= UINT32_MAX
    if ((size_t)required > SIZE_MAX / sizeof(*wide_buffer) - 1u) {
        errno = EOVERFLOW;
        return NULL;
    }
#endif
    wide_buffer = (wchar_t *)malloc(
        ((size_t)required + 1u) * sizeof(*wide_buffer));
    if (wide_buffer == NULL) {
        errno = ENOMEM;
        return NULL;
    }
    written = GetCurrentDirectoryW(required + 1u, wide_buffer);
    if (written == 0u || written >= required + 1u) {
        free(wide_buffer);
        errno = EIO;
        return NULL;
    }
    utf8_size = WideCharToMultiByte(
        CP_UTF8,
        WC_ERR_INVALID_CHARS,
        wide_buffer,
        -1,
        buffer,
        capacity > (size_t)INT_MAX ? INT_MAX : (int)capacity,
        NULL,
        NULL);
    free(wide_buffer);
    if (utf8_size <= 0) {
        errno = utf8_size == 0 ? EINVAL : ERANGE;
        return NULL;
    }
    return buffer;
}

#define open vitte_windows_open_utf8
#define mkdir(path, mode) vitte_windows_mkdir_utf8((path), (mode))
#define unlink vitte_windows_unlink_utf8
#define rmdir vitte_windows_rmdir_utf8
#define rename vitte_windows_rename_utf8
#define access vitte_windows_access_utf8
#define getcwd vitte_windows_getcwd_utf8

struct dirent {
    char d_name[VITTE_WINDOWS_DIRENT_NAME_CAPACITY];
};

typedef struct vitte_windows_directory {
    HANDLE handle;
    WIN32_FIND_DATAW data;
    bool first;
    wchar_t *pattern;
    struct dirent entry;
} DIR;

static DIR *
vitte_windows_opendir(const char *path)
{
    DIR *directory;
    wchar_t *wide_path;
    size_t length;

    if (path == NULL) {
        errno = EINVAL;
        return NULL;
    }
    wide_path = vitte_windows_utf8_to_wide(path);
    if (wide_path == NULL) {
        return NULL;
    }
    length = wcslen(wide_path);
    if (length > (SIZE_MAX / sizeof(*wide_path)) - 3u) {
        free(wide_path);
        errno = ENAMETOOLONG;
        return NULL;
    }
    directory = (DIR *)calloc(1u, sizeof(*directory));
    if (directory == NULL) {
        free(wide_path);
        errno = ENOMEM;
        return NULL;
    }
    directory->pattern = (wchar_t *)malloc(
        (length + 3u) * sizeof(*directory->pattern));
    if (directory->pattern == NULL) {
        free(wide_path);
        free(directory);
        errno = ENOMEM;
        return NULL;
    }
    (void)memcpy(
        directory->pattern,
        wide_path,
        (length + 1u) * sizeof(*wide_path));
    free(wide_path);
    if (length != 0u &&
        directory->pattern[length - 1u] != '\\' &&
        directory->pattern[length - 1u] != '/') {
        directory->pattern[length++] = '\\';
    }
    directory->pattern[length++] = L'*';
    directory->pattern[length] = L'\0';
    directory->handle = FindFirstFileW(
        directory->pattern,
        &directory->data);
    if (directory->handle == INVALID_HANDLE_VALUE) {
        free(directory->pattern);
        free(directory);
        errno = ENOENT;
        return NULL;
    }
    directory->first = true;
    return directory;
}

static struct dirent *
vitte_windows_readdir(DIR *directory)
{
    if (directory == NULL) {
        errno = EINVAL;
        return NULL;
    }
    if (!directory->first &&
        !FindNextFileW(directory->handle, &directory->data)) {
        return NULL;
    }
    directory->first = false;
    {
        char *name = vitte_windows_wide_to_utf8(
            directory->data.cFileName);
        size_t name_length;
        if (name == NULL) {
            return NULL;
        }
        name_length = strlen(name);
        if (name_length >= sizeof(directory->entry.d_name)) {
            free(name);
            errno = ENAMETOOLONG;
            return NULL;
        }
        (void)memcpy(
            directory->entry.d_name,
            name,
            name_length + 1u);
        free(name);
    }
    return &directory->entry;
}

static int
vitte_windows_closedir(DIR *directory)
{
    BOOL closed;
    if (directory == NULL) {
        errno = EINVAL;
        return -1;
    }
    closed = FindClose(directory->handle);
    free(directory->pattern);
    free(directory);
    if (!closed) {
        errno = EIO;
        return -1;
    }
    return 0;
}

#define opendir vitte_windows_opendir
#define readdir vitte_windows_readdir
#define closedir vitte_windows_closedir

static int
vitte_windows_mkstemp(char *path)
{
    wchar_t *wide_path;
    char *utf8_path;
    size_t path_length;
    size_t capacity;
    int result;

    if (path == NULL) {
        errno = EINVAL;
        return -1;
    }
    path_length = strlen(path);
    if (path_length == SIZE_MAX) {
        errno = ENAMETOOLONG;
        return -1;
    }
    capacity = path_length + 1u;
    wide_path = vitte_windows_utf8_to_wide(path);
    if (wide_path == NULL) {
        return -1;
    }
    if (_wmktemp_s(wide_path, wcslen(wide_path) + 1u) != 0) {
        free(wide_path);
        errno = EEXIST;
        return -1;
    }
    utf8_path = vitte_windows_wide_to_utf8(wide_path);
    if (utf8_path == NULL || strlen(utf8_path) >= capacity) {
        free(utf8_path);
        free(wide_path);
        errno = ENAMETOOLONG;
        return -1;
    }
    (void)memcpy(path, utf8_path, strlen(utf8_path) + 1u);
    free(utf8_path);
    result = _wopen(wide_path,
                    _O_CREAT | _O_EXCL | _O_RDWR | _O_BINARY,
                    _S_IREAD | _S_IWRITE);
    free(wide_path);
    if (result < 0) {
        errno = EIO;
    }
    return result;
}

#define mkstemp vitte_windows_mkstemp

#else
#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#define VITTE_FILESYSTEM_STAT_TYPE struct stat
#define VITTE_FILESYSTEM_STAT(path, value) stat((path), (value))
#define VITTE_FILESYSTEM_LSTAT(path, value) lstat((path), (value))
#endif

#if defined(_WIN32)
#define VITTE_FILESYSTEM_STAT(path, value) \
    vitte_windows_stat_utf8((path), (value))
#define VITTE_FILESYSTEM_LSTAT(path, value) \
    vitte_windows_stat_utf8((path), (value))
#endif

/* ========================================================================= */
/* Constants                                                                 */
/* ========================================================================= */

#define VITTE_FILESYSTEM_FNV_OFFSET \
    UINT64_C(14695981039346656037)

#define VITTE_FILESYSTEM_FNV_PRIME \
    UINT64_C(1099511628211)

#define VITTE_FILESYSTEM_DEFAULT_MAX_FILE_BYTES \
    ((size_t)(1024u * 1024u * 1024u))

#define VITTE_FILESYSTEM_DEFAULT_MAX_PATH_BYTES \
    ((size_t)(1024u * 1024u))

#define VITTE_FILESYSTEM_DEFAULT_MAX_DIRECTORY_ENTRIES \
    ((size_t)1000000u)

#define VITTE_FILESYSTEM_READ_CHUNK \
    ((size_t)(64u * 1024u))

#if defined(_WIN32)
_Static_assert(
    VITTE_FILESYSTEM_READ_CHUNK <= UINT_MAX,
    "Windows read chunk must fit in the CRT read size");
#endif

#define VITTE_FILESYSTEM_WRITE_MODE \
    ((unsigned int)0666)

#define VITTE_FILESYSTEM_DIRECTORY_MODE \
    ((unsigned int)0777)

/* ========================================================================= */
/* Errors                                                                    */
/* ========================================================================= */

typedef enum vitte_filesystem_error {
    VITTE_FILESYSTEM_ERROR_NONE = 0,

    VITTE_FILESYSTEM_ERROR_INVALID_ARGUMENT,
    VITTE_FILESYSTEM_ERROR_INVALID_PATH,

    VITTE_FILESYSTEM_ERROR_NOT_FOUND,
    VITTE_FILESYSTEM_ERROR_ALREADY_EXISTS,
    VITTE_FILESYSTEM_ERROR_NOT_FILE,
    VITTE_FILESYSTEM_ERROR_NOT_DIRECTORY,
    VITTE_FILESYSTEM_ERROR_DIRECTORY_NOT_EMPTY,

    VITTE_FILESYSTEM_ERROR_PERMISSION_DENIED,
    VITTE_FILESYSTEM_ERROR_READ_ONLY,

    VITTE_FILESYSTEM_ERROR_OPEN,
    VITTE_FILESYSTEM_ERROR_READ,
    VITTE_FILESYSTEM_ERROR_WRITE,
    VITTE_FILESYSTEM_ERROR_FLUSH,
    VITTE_FILESYSTEM_ERROR_CLOSE,

    VITTE_FILESYSTEM_ERROR_STAT,
    VITTE_FILESYSTEM_ERROR_MKDIR,
    VITTE_FILESYSTEM_ERROR_REMOVE,
    VITTE_FILESYSTEM_ERROR_RENAME,
    VITTE_FILESYSTEM_ERROR_DIRECTORY_READ,
    VITTE_FILESYSTEM_ERROR_CURRENT_DIRECTORY,
    VITTE_FILESYSTEM_ERROR_CANONICALIZE,

    VITTE_FILESYSTEM_ERROR_TOO_LARGE,
    VITTE_FILESYSTEM_ERROR_TOO_MANY_ENTRIES,
    VITTE_FILESYSTEM_ERROR_PATH_TOO_LONG,

    VITTE_FILESYSTEM_ERROR_OVERFLOW,
    VITTE_FILESYSTEM_ERROR_OUT_OF_MEMORY,

    VITTE_FILESYSTEM_ERROR_INTERRUPTED,
    VITTE_FILESYSTEM_ERROR_IO,
    VITTE_FILESYSTEM_ERROR_UNSUPPORTED,

    VITTE_FILESYSTEM_ERROR_COUNT
} vitte_filesystem_error_t;

/* ========================================================================= */
/* File kinds                                                                */
/* ========================================================================= */

typedef enum vitte_filesystem_kind {
    VITTE_FILESYSTEM_KIND_UNKNOWN = 0,

    VITTE_FILESYSTEM_KIND_REGULAR,
    VITTE_FILESYSTEM_KIND_DIRECTORY,
    VITTE_FILESYSTEM_KIND_SYMLINK,
    VITTE_FILESYSTEM_KIND_OTHER,

    VITTE_FILESYSTEM_KIND_COUNT
} vitte_filesystem_kind_t;

/* ========================================================================= */
/* Metadata                                                                  */
/* ========================================================================= */

typedef struct vitte_filesystem_metadata {
    vitte_filesystem_kind_t kind;

    uint64_t size;

    uint64_t modified_seconds;
    uint64_t modified_nanoseconds;

    bool readable;
    bool writable;
    bool executable;
} vitte_filesystem_metadata_t;

/* ========================================================================= */
/* Buffer                                                                    */
/* ========================================================================= */

typedef struct vitte_filesystem_buffer {
    unsigned char *data;
    size_t length;
    size_t capacity;
} vitte_filesystem_buffer_t;

/* ========================================================================= */
/* Directory entry                                                          */
/* ========================================================================= */

typedef struct vitte_filesystem_directory_entry {
    char *name;
    vitte_filesystem_kind_t kind;
} vitte_filesystem_directory_entry_t;

/* ========================================================================= */
/* Directory list                                                            */
/* ========================================================================= */

typedef struct vitte_filesystem_directory_list {
    vitte_filesystem_directory_entry_t *entries;
    size_t count;
    size_t capacity;
} vitte_filesystem_directory_list_t;

/* ========================================================================= */
/* Result                                                                    */
/* ========================================================================= */

typedef struct vitte_filesystem_result {
    vitte_filesystem_error_t error;
    int system_error;
} vitte_filesystem_result_t;

/* ========================================================================= */
/* Statistics                                                                */
/* ========================================================================= */

typedef struct vitte_filesystem_stats {
    uint64_t stat_calls;
    uint64_t read_calls;
    uint64_t write_calls;

    uint64_t mkdir_calls;
    uint64_t remove_calls;
    uint64_t rename_calls;
    uint64_t directory_list_calls;

    uint64_t bytes_read;
    uint64_t bytes_written;

    uint64_t allocation_failures;
    uint64_t io_failures;
} vitte_filesystem_stats_t;

/* ========================================================================= */
/* Global instrumentation                                                    */
/* ========================================================================= */

/* Filesystem counters are per calling thread.  This keeps the convenience
 * statistics API deterministic without making concurrent compiler contexts
 * contend on shared mutable state. */
static _Thread_local vitte_filesystem_stats_t vitte_filesystem_thread_stats;

/* ========================================================================= */
/* Public prototypes                                                         */
/* ========================================================================= */

const char *
vitte_filesystem_error_name(vitte_filesystem_error_t error);

const char *
vitte_filesystem_kind_name(vitte_filesystem_kind_t kind);

vitte_filesystem_result_t
vitte_filesystem_stat(
    const char *path,
    bool follow_symlink,
    vitte_filesystem_metadata_t *metadata);

bool
vitte_filesystem_exists(const char *path);

bool
vitte_filesystem_is_file(const char *path);

bool
vitte_filesystem_is_directory(const char *path);

bool
vitte_filesystem_is_symlink(const char *path);

vitte_filesystem_result_t
vitte_filesystem_read_file(
    const char *path,
    size_t max_bytes,
    vitte_filesystem_buffer_t *buffer);

vitte_filesystem_result_t
vitte_filesystem_write_file(
    const char *path,
    const void *data,
    size_t length);

vitte_filesystem_result_t
vitte_filesystem_write_file_atomic(
    const char *path,
    const void *data,
    size_t length);

vitte_filesystem_result_t
vitte_filesystem_create_directory(const char *path);

vitte_filesystem_result_t
vitte_filesystem_create_directories(const char *path);

vitte_filesystem_result_t
vitte_filesystem_remove_file(const char *path);

vitte_filesystem_result_t
vitte_filesystem_remove_directory(const char *path);

vitte_filesystem_result_t
vitte_filesystem_rename(
    const char *source,
    const char *destination);

vitte_filesystem_result_t
vitte_filesystem_list_directory(
    const char *path,
    size_t max_entries,
    vitte_filesystem_directory_list_t *list);

vitte_filesystem_result_t
vitte_filesystem_join(
    const char *left,
    const char *right,
    char **result);

vitte_filesystem_result_t
vitte_filesystem_normalize(
    const char *path,
    char **result);

vitte_filesystem_result_t
vitte_filesystem_canonicalize(
    const char *path,
    char **result);

vitte_filesystem_result_t
vitte_filesystem_current_directory(char **result);

bool
vitte_filesystem_path_is_absolute(const char *path);

uint64_t
vitte_filesystem_path_hash(const char *path);

void
vitte_filesystem_buffer_destroy(vitte_filesystem_buffer_t *buffer);

void
vitte_filesystem_directory_list_destroy(
    vitte_filesystem_directory_list_t *list);

vitte_filesystem_stats_t
vitte_filesystem_stats(void);

void
vitte_filesystem_stats_reset(void);

/* ========================================================================= */
/* Checked arithmetic                                                        */
/* ========================================================================= */

static bool
vitte_filesystem_size_add(
    size_t left,
    size_t right,
    size_t *result)
{
    if (result == NULL) {
        return false;
    }

    if (left > SIZE_MAX - right) {
        return false;
    }

    *result = left + right;
    return true;
}

static bool
vitte_filesystem_size_mul(
    size_t left,
    size_t right,
    size_t *result)
{
    if (result == NULL) {
        return false;
    }

    if (left != 0u && right > SIZE_MAX / left) {
        return false;
    }

    *result = left * right;
    return true;
}

/* ========================================================================= */
/* Saturating statistics                                                     */
/* ========================================================================= */

static void
vitte_filesystem_u64_add(
    uint64_t *destination,
    uint64_t value)
{
    if (destination == NULL) {
        return;
    }

    if (*destination > UINT64_MAX - value) {
        *destination = UINT64_MAX;
        return;
    }

    *destination += value;
}

static void
vitte_filesystem_u64_increment(uint64_t *value)
{
    if (value == NULL) {
        return;
    }

    if (*value != UINT64_MAX) {
        ++(*value);
    }
}

/* ========================================================================= */
/* Results                                                                   */
/* ========================================================================= */

static vitte_filesystem_result_t
vitte_filesystem_result_ok(void)
{
    vitte_filesystem_result_t result;

    result.error = VITTE_FILESYSTEM_ERROR_NONE;
    result.system_error = 0;

    return result;
}

static vitte_filesystem_result_t
vitte_filesystem_result_error(
    vitte_filesystem_error_t error,
    int system_error)
{
    vitte_filesystem_result_t result;

    result.error = error;
    result.system_error = system_error;

    if (error != VITTE_FILESYSTEM_ERROR_NONE) {
        vitte_filesystem_u64_increment(
            &vitte_filesystem_thread_stats.io_failures);
    }

    return result;
}

/* ========================================================================= */
/* errno translation                                                         */
/* ========================================================================= */

static vitte_filesystem_error_t
vitte_filesystem_error_from_errno(
    int value,
    vitte_filesystem_error_t fallback)
{
    switch (value) {
        case 0:
            return VITTE_FILESYSTEM_ERROR_NONE;

        case ENOENT:
            return VITTE_FILESYSTEM_ERROR_NOT_FOUND;

        case EEXIST:
            return VITTE_FILESYSTEM_ERROR_ALREADY_EXISTS;

        case ENOTDIR:
            return VITTE_FILESYSTEM_ERROR_NOT_DIRECTORY;

        case EISDIR:
            return VITTE_FILESYSTEM_ERROR_NOT_FILE;

        case ENOTEMPTY:
            return VITTE_FILESYSTEM_ERROR_DIRECTORY_NOT_EMPTY;

        case EACCES:
        case EPERM:
            return VITTE_FILESYSTEM_ERROR_PERMISSION_DENIED;

#ifdef EROFS
        case EROFS:
            return VITTE_FILESYSTEM_ERROR_READ_ONLY;
#endif

        case EINTR:
            return VITTE_FILESYSTEM_ERROR_INTERRUPTED;

#ifdef EFBIG
        case EFBIG:
            return VITTE_FILESYSTEM_ERROR_TOO_LARGE;
#endif

#ifdef ENAMETOOLONG
        case ENAMETOOLONG:
            return VITTE_FILESYSTEM_ERROR_PATH_TOO_LONG;
#endif

        case ENOMEM:
            return VITTE_FILESYSTEM_ERROR_OUT_OF_MEMORY;

        case EIO:
            return VITTE_FILESYSTEM_ERROR_IO;

        default:
            return fallback;
    }
}

/* ========================================================================= */
/* Names                                                                     */
/* ========================================================================= */

const char *
vitte_filesystem_error_name(vitte_filesystem_error_t error)
{
    switch (error) {
        case VITTE_FILESYSTEM_ERROR_NONE:
            return "none";

        case VITTE_FILESYSTEM_ERROR_INVALID_ARGUMENT:
            return "invalid-argument";

        case VITTE_FILESYSTEM_ERROR_INVALID_PATH:
            return "invalid-path";

        case VITTE_FILESYSTEM_ERROR_NOT_FOUND:
            return "not-found";

        case VITTE_FILESYSTEM_ERROR_ALREADY_EXISTS:
            return "already-exists";

        case VITTE_FILESYSTEM_ERROR_NOT_FILE:
            return "not-file";

        case VITTE_FILESYSTEM_ERROR_NOT_DIRECTORY:
            return "not-directory";

        case VITTE_FILESYSTEM_ERROR_DIRECTORY_NOT_EMPTY:
            return "directory-not-empty";

        case VITTE_FILESYSTEM_ERROR_PERMISSION_DENIED:
            return "permission-denied";

        case VITTE_FILESYSTEM_ERROR_READ_ONLY:
            return "read-only";

        case VITTE_FILESYSTEM_ERROR_OPEN:
            return "open";

        case VITTE_FILESYSTEM_ERROR_READ:
            return "read";

        case VITTE_FILESYSTEM_ERROR_WRITE:
            return "write";

        case VITTE_FILESYSTEM_ERROR_FLUSH:
            return "flush";

        case VITTE_FILESYSTEM_ERROR_CLOSE:
            return "close";

        case VITTE_FILESYSTEM_ERROR_STAT:
            return "stat";

        case VITTE_FILESYSTEM_ERROR_MKDIR:
            return "mkdir";

        case VITTE_FILESYSTEM_ERROR_REMOVE:
            return "remove";

        case VITTE_FILESYSTEM_ERROR_RENAME:
            return "rename";

        case VITTE_FILESYSTEM_ERROR_DIRECTORY_READ:
            return "directory-read";

        case VITTE_FILESYSTEM_ERROR_CURRENT_DIRECTORY:
            return "current-directory";

        case VITTE_FILESYSTEM_ERROR_CANONICALIZE:
            return "canonicalize";

        case VITTE_FILESYSTEM_ERROR_TOO_LARGE:
            return "too-large";

        case VITTE_FILESYSTEM_ERROR_TOO_MANY_ENTRIES:
            return "too-many-entries";

        case VITTE_FILESYSTEM_ERROR_PATH_TOO_LONG:
            return "path-too-long";

        case VITTE_FILESYSTEM_ERROR_OVERFLOW:
            return "overflow";

        case VITTE_FILESYSTEM_ERROR_OUT_OF_MEMORY:
            return "out-of-memory";

        case VITTE_FILESYSTEM_ERROR_INTERRUPTED:
            return "interrupted";

        case VITTE_FILESYSTEM_ERROR_IO:
            return "io";

        case VITTE_FILESYSTEM_ERROR_UNSUPPORTED:
            return "unsupported";

        case VITTE_FILESYSTEM_ERROR_COUNT:
            return "count";
    }

    return "unknown";
}

const char *
vitte_filesystem_kind_name(vitte_filesystem_kind_t kind)
{
    switch (kind) {
        case VITTE_FILESYSTEM_KIND_UNKNOWN:
            return "unknown";

        case VITTE_FILESYSTEM_KIND_REGULAR:
            return "regular";

        case VITTE_FILESYSTEM_KIND_DIRECTORY:
            return "directory";

        case VITTE_FILESYSTEM_KIND_SYMLINK:
            return "symlink";

        case VITTE_FILESYSTEM_KIND_OTHER:
            return "other";

        case VITTE_FILESYSTEM_KIND_COUNT:
            return "count";
    }

    return "unknown";
}

/* ========================================================================= */
/* Path validation                                                           */
/* ========================================================================= */

static bool
vitte_filesystem_path_valid(const char *path)
{
    size_t length;

    if (path == NULL || path[0] == '\0') {
        return false;
    }

    length = strlen(path);

    if (length > VITTE_FILESYSTEM_DEFAULT_MAX_PATH_BYTES) {
        return false;
    }

    return true;
}

static bool
vitte_filesystem_path_separator(char character)
{
    return character == '/' || character == '\\';
}

/* ========================================================================= */
/* File kind                                                                 */
/* ========================================================================= */

static vitte_filesystem_kind_t
vitte_filesystem_kind_from_mode(unsigned int mode)
{
    if (S_ISREG(mode)) {
        return VITTE_FILESYSTEM_KIND_REGULAR;
    }

    if (S_ISDIR(mode)) {
        return VITTE_FILESYSTEM_KIND_DIRECTORY;
    }

    if (S_ISLNK(mode)) {
        return VITTE_FILESYSTEM_KIND_SYMLINK;
    }

    return VITTE_FILESYSTEM_KIND_OTHER;
}

/* ========================================================================= */
/* Metadata                                                                  */
/* ========================================================================= */

vitte_filesystem_result_t
vitte_filesystem_stat(
    const char *path,
    bool follow_symlink,
    vitte_filesystem_metadata_t *metadata)
{
    VITTE_FILESYSTEM_STAT_TYPE value;
    int status;
    int saved_errno;

    if (!vitte_filesystem_path_valid(path) ||
        metadata == NULL) {
        return
            vitte_filesystem_result_error(
                VITTE_FILESYSTEM_ERROR_INVALID_ARGUMENT,
                0);
    }

    vitte_filesystem_u64_increment(
        &vitte_filesystem_thread_stats.stat_calls);

    (void)memset(metadata, 0, sizeof(*metadata));

    errno = 0;

    if (follow_symlink) {
        status = VITTE_FILESYSTEM_STAT(path, &value);
    } else {
        status = VITTE_FILESYSTEM_LSTAT(path, &value);
    }

    if (status != 0) {
        saved_errno = errno;

        return
            vitte_filesystem_result_error(
                vitte_filesystem_error_from_errno(
                    saved_errno,
                    VITTE_FILESYSTEM_ERROR_STAT),
                saved_errno);
    }

    metadata->kind =
        vitte_filesystem_kind_from_mode(value.st_mode);
#if defined(_WIN32)
    if (!follow_symlink && vitte_filesystem_is_symlink(path)) {
        metadata->kind = VITTE_FILESYSTEM_KIND_SYMLINK;
    }
#endif

    if (value.st_size < 0) {
        metadata->size = UINT64_C(0);
    } else {
        metadata->size = (uint64_t)value.st_size;
    }

#if defined(__APPLE__)
    if (value.st_mtimespec.tv_sec >= 0) {
        metadata->modified_seconds =
            (uint64_t)value.st_mtimespec.tv_sec;
    }

    if (value.st_mtimespec.tv_nsec >= 0) {
        metadata->modified_nanoseconds =
            (uint64_t)value.st_mtimespec.tv_nsec;
    }
#elif defined(_POSIX_VERSION)
    if (value.st_mtim.tv_sec >= 0) {
        metadata->modified_seconds =
            (uint64_t)value.st_mtim.tv_sec;
    }

    if (value.st_mtim.tv_nsec >= 0) {
        metadata->modified_nanoseconds =
            (uint64_t)value.st_mtim.tv_nsec;
    }
#endif

    metadata->readable =
        access(path, R_OK) == 0;

    metadata->writable =
        access(path, W_OK) == 0;

    metadata->executable =
        access(path, X_OK) == 0;

    return vitte_filesystem_result_ok();
}

/* ========================================================================= */
/* Existence queries                                                         */
/* ========================================================================= */

bool
vitte_filesystem_exists(const char *path)
{
    VITTE_FILESYSTEM_STAT_TYPE value;

    if (!vitte_filesystem_path_valid(path)) {
        return false;
    }

    return VITTE_FILESYSTEM_LSTAT(path, &value) == 0;
}

bool
vitte_filesystem_is_file(const char *path)
{
    VITTE_FILESYSTEM_STAT_TYPE value;

    if (!vitte_filesystem_path_valid(path)) {
        return false;
    }

    if (VITTE_FILESYSTEM_STAT(path, &value) != 0) {
        return false;
    }

    return S_ISREG(value.st_mode);
}

bool
vitte_filesystem_is_directory(const char *path)
{
    VITTE_FILESYSTEM_STAT_TYPE value;

    if (!vitte_filesystem_path_valid(path)) {
        return false;
    }

    if (VITTE_FILESYSTEM_STAT(path, &value) != 0) {
        return false;
    }

    return S_ISDIR(value.st_mode);
}

bool
vitte_filesystem_is_symlink(const char *path)
{
#if defined(_WIN32)
    DWORD attributes;
    wchar_t *wide_path;

    if (!vitte_filesystem_path_valid(path)) {
        return false;
    }
    wide_path = vitte_windows_utf8_to_wide(path);
    if (wide_path == NULL) {
        return false;
    }
    attributes = GetFileAttributesW(wide_path);
    free(wide_path);
    return attributes != INVALID_FILE_ATTRIBUTES &&
           (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0u;
#else
    VITTE_FILESYSTEM_STAT_TYPE value;

    if (!vitte_filesystem_path_valid(path)) {
        return false;
    }

    if (VITTE_FILESYSTEM_LSTAT(path, &value) != 0) {
        return false;
    }

    return S_ISLNK(value.st_mode);
#endif
}

#if defined(_WIN32)
static bool
vitte_windows_reparse_is_directory(
    const char *path)
{
    wchar_t *wide_path;
    DWORD attributes;

    wide_path = vitte_windows_utf8_to_wide(path);
    if (wide_path == NULL) {
        return false;
    }
    attributes = GetFileAttributesW(wide_path);
    free(wide_path);
    return attributes != INVALID_FILE_ATTRIBUTES &&
           (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0u &&
           (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0u;
}
#endif

/* ========================================================================= */
/* Buffer                                                                    */
/* ========================================================================= */

void
vitte_filesystem_buffer_destroy(vitte_filesystem_buffer_t *buffer)
{
    if (buffer == NULL) {
        return;
    }

    free(buffer->data);

    buffer->data = NULL;
    buffer->length = 0u;
    buffer->capacity = 0u;
}

static bool
vitte_filesystem_buffer_reserve(
    vitte_filesystem_buffer_t *buffer,
    size_t required)
{
    size_t capacity;
    unsigned char *new_data;

    if (buffer == NULL) {
        return false;
    }

    if (required <= buffer->capacity) {
        return true;
    }

    capacity = buffer->capacity;

    if (capacity == 0u) {
        capacity = VITTE_FILESYSTEM_READ_CHUNK;
    }

    while (capacity < required) {
        size_t next;

        if (!vitte_filesystem_size_add(
                capacity,
                capacity,
                &next)) {
            capacity = required;
            break;
        }

        capacity = next;
    }

    new_data =
        (unsigned char *)realloc(
            buffer->data,
            capacity);

    if (new_data == NULL) {
        vitte_filesystem_u64_increment(
            &vitte_filesystem_thread_stats.allocation_failures);

        return false;
    }

    buffer->data = new_data;
    buffer->capacity = capacity;

    return true;
}

/* ========================================================================= */
/* Whole-file read                                                           */
/* ========================================================================= */

vitte_filesystem_result_t
vitte_filesystem_read_file(
    const char *path,
    size_t max_bytes,
    vitte_filesystem_buffer_t *buffer)
{
    int descriptor;
    VITTE_FILESYSTEM_STAT_TYPE metadata;
    size_t limit;
    size_t initial;
    int saved_errno;

    if (!vitte_filesystem_path_valid(path) ||
        buffer == NULL) {
        return
            vitte_filesystem_result_error(
                VITTE_FILESYSTEM_ERROR_INVALID_ARGUMENT,
                0);
    }

    vitte_filesystem_u64_increment(
        &vitte_filesystem_thread_stats.read_calls);

    vitte_filesystem_buffer_destroy(buffer);

    limit =
        max_bytes == 0u
            ? VITTE_FILESYSTEM_DEFAULT_MAX_FILE_BYTES
            : max_bytes;

    errno = 0;

    descriptor = open(path, O_RDONLY);

    if (descriptor < 0) {
        saved_errno = errno;

        return
            vitte_filesystem_result_error(
                vitte_filesystem_error_from_errno(
                    saved_errno,
                    VITTE_FILESYSTEM_ERROR_OPEN),
                saved_errno);
    }

    if (fstat(descriptor, &metadata) != 0) {
        saved_errno = errno;

        (void)close(descriptor);

        return
            vitte_filesystem_result_error(
                vitte_filesystem_error_from_errno(
                    saved_errno,
                    VITTE_FILESYSTEM_ERROR_STAT),
                saved_errno);
    }

    if (!S_ISREG(metadata.st_mode)) {
        (void)close(descriptor);

        return
            vitte_filesystem_result_error(
                VITTE_FILESYSTEM_ERROR_NOT_FILE,
                0);
    }

    initial = 0u;

    if (metadata.st_size > 0) {
        uint64_t file_size;

        file_size = (uint64_t)metadata.st_size;

        if (file_size > (uint64_t)SIZE_MAX ||
            file_size > (uint64_t)limit) {
            (void)close(descriptor);

            return
                vitte_filesystem_result_error(
                    VITTE_FILESYSTEM_ERROR_TOO_LARGE,
                    0);
        }

        initial = (size_t)file_size;
    }

    /*
     * Reserve one extra byte so text-oriented callers may safely observe a
     * trailing NUL. length never includes this terminator.
     */
    {
        size_t required;

        if (!vitte_filesystem_size_add(
                initial,
                1u,
                &required)) {
            (void)close(descriptor);

            return
                vitte_filesystem_result_error(
                    VITTE_FILESYSTEM_ERROR_OVERFLOW,
                    0);
        }

        if (!vitte_filesystem_buffer_reserve(
                buffer,
                required)) {
            (void)close(descriptor);

            return
                vitte_filesystem_result_error(
                    VITTE_FILESYSTEM_ERROR_OUT_OF_MEMORY,
                    0);
        }
    }

    for (;;) {
        size_t available;
        ssize_t count;

        if (buffer->length >= limit) {
            unsigned char probe;
            ssize_t probe_count;

            do {
                probe_count =
                    read(
                        descriptor,
                        &probe,
                        (size_t)1u);
            } while (probe_count < 0 && errno == EINTR);

            if (probe_count > 0) {
                (void)close(descriptor);
                vitte_filesystem_buffer_destroy(buffer);

                return
                    vitte_filesystem_result_error(
                        VITTE_FILESYSTEM_ERROR_TOO_LARGE,
                        0);
            }

            if (probe_count < 0) {
                saved_errno = errno;

                (void)close(descriptor);
                vitte_filesystem_buffer_destroy(buffer);

                return
                    vitte_filesystem_result_error(
                        vitte_filesystem_error_from_errno(
                            saved_errno,
                            VITTE_FILESYSTEM_ERROR_READ),
                        saved_errno);
            }

            break;
        }

        available = limit - buffer->length;

        if (available > VITTE_FILESYSTEM_READ_CHUNK) {
            available = VITTE_FILESYSTEM_READ_CHUNK;
        }

        {
            size_t required;

            if (!vitte_filesystem_size_add(
                    buffer->length,
                    available,
                    &required) ||
                !vitte_filesystem_size_add(
                    required,
                    1u,
                    &required)) {
                (void)close(descriptor);
                vitte_filesystem_buffer_destroy(buffer);

                return
                    vitte_filesystem_result_error(
                        VITTE_FILESYSTEM_ERROR_OVERFLOW,
                        0);
            }

            if (!vitte_filesystem_buffer_reserve(
                    buffer,
                    required)) {
                (void)close(descriptor);
                vitte_filesystem_buffer_destroy(buffer);

                return
                    vitte_filesystem_result_error(
                        VITTE_FILESYSTEM_ERROR_OUT_OF_MEMORY,
                        0);
            }
        }

        do {
#if defined(_WIN32)
            count =
                read(
                    descriptor,
                    buffer->data + buffer->length,
                    (unsigned int)available);
#else
            count =
                read(
                    descriptor,
                    buffer->data + buffer->length,
                    available);
#endif
        } while (count < 0 && errno == EINTR);

        if (count < 0) {
            saved_errno = errno;

            (void)close(descriptor);
            vitte_filesystem_buffer_destroy(buffer);

            return
                vitte_filesystem_result_error(
                    vitte_filesystem_error_from_errno(
                        saved_errno,
                        VITTE_FILESYSTEM_ERROR_READ),
                    saved_errno);
        }

        if (count == 0) {
            break;
        }

        buffer->length += (size_t)count;

        vitte_filesystem_u64_add(
            &vitte_filesystem_thread_stats.bytes_read,
            (uint64_t)count);
    }

    buffer->data[buffer->length] = '\0';

    if (close(descriptor) != 0) {
        saved_errno = errno;

        vitte_filesystem_buffer_destroy(buffer);

        return
            vitte_filesystem_result_error(
                vitte_filesystem_error_from_errno(
                    saved_errno,
                    VITTE_FILESYSTEM_ERROR_CLOSE),
                saved_errno);
    }

    return vitte_filesystem_result_ok();
}

/* ========================================================================= */
/* Complete write helper                                                     */
/* ========================================================================= */

static vitte_filesystem_result_t
vitte_filesystem_write_descriptor(
    int descriptor,
    const void *data,
    size_t length)
{
    const unsigned char *bytes;
    size_t offset;

    if (descriptor < 0 ||
        (data == NULL && length != 0u)) {
        return
            vitte_filesystem_result_error(
                VITTE_FILESYSTEM_ERROR_INVALID_ARGUMENT,
                0);
    }

    bytes = (const unsigned char *)data;
    offset = 0u;

    while (offset < length) {
        size_t remaining;
        ssize_t written;
        int saved_errno;

        remaining = length - offset;

        do {
#if defined(_WIN32)
            if (remaining > UINT_MAX) {
                remaining = UINT_MAX;
            }
            written =
                write(
                    descriptor,
                    bytes + offset,
                    (unsigned int)remaining);
#else
            written =
                write(
                    descriptor,
                    bytes + offset,
                    remaining);
#endif
        } while (written < 0 && errno == EINTR);

        if (written < 0) {
            saved_errno = errno;

            return
                vitte_filesystem_result_error(
                    vitte_filesystem_error_from_errno(
                        saved_errno,
                        VITTE_FILESYSTEM_ERROR_WRITE),
                    saved_errno);
        }

        if (written == 0) {
            return
                vitte_filesystem_result_error(
                    VITTE_FILESYSTEM_ERROR_WRITE,
                    0);
        }

        offset += (size_t)written;

        vitte_filesystem_u64_add(
            &vitte_filesystem_thread_stats.bytes_written,
            (uint64_t)written);
    }

    return vitte_filesystem_result_ok();
}

/* ========================================================================= */
/* Whole-file write                                                          */
/* ========================================================================= */

vitte_filesystem_result_t
vitte_filesystem_write_file(
    const char *path,
    const void *data,
    size_t length)
{
    int descriptor;
    vitte_filesystem_result_t result;
    int saved_errno;

    if (!vitte_filesystem_path_valid(path) ||
        (data == NULL && length != 0u)) {
        return
            vitte_filesystem_result_error(
                VITTE_FILESYSTEM_ERROR_INVALID_ARGUMENT,
                0);
    }

    vitte_filesystem_u64_increment(
        &vitte_filesystem_thread_stats.write_calls);

    errno = 0;

    descriptor =
        open(
            path,
            O_WRONLY | O_CREAT | O_TRUNC,
            VITTE_FILESYSTEM_WRITE_MODE);

    if (descriptor < 0) {
        saved_errno = errno;

        return
            vitte_filesystem_result_error(
                vitte_filesystem_error_from_errno(
                    saved_errno,
                    VITTE_FILESYSTEM_ERROR_OPEN),
                saved_errno);
    }

    result =
        vitte_filesystem_write_descriptor(
            descriptor,
            data,
            length);

    if (result.error != VITTE_FILESYSTEM_ERROR_NONE) {
        (void)close(descriptor);
        return result;
    }

    if (fsync(descriptor) != 0) {
        saved_errno = errno;

        (void)close(descriptor);

        return
            vitte_filesystem_result_error(
                vitte_filesystem_error_from_errno(
                    saved_errno,
                    VITTE_FILESYSTEM_ERROR_FLUSH),
                saved_errno);
    }

    if (close(descriptor) != 0) {
        saved_errno = errno;

        return
            vitte_filesystem_result_error(
                vitte_filesystem_error_from_errno(
                    saved_errno,
                    VITTE_FILESYSTEM_ERROR_CLOSE),
                saved_errno);
    }

    return vitte_filesystem_result_ok();
}

/* ========================================================================= */
/* Atomic write                                                              */
/* ========================================================================= */

static vitte_filesystem_result_t
vitte_filesystem_make_atomic_template(
    const char *path,
    char **result)
{
    static const char suffix[] = ".vitte-tmp-XXXXXX";
    size_t path_length;
    size_t suffix_length;
    size_t total;
    char *buffer;

    if (!vitte_filesystem_path_valid(path) ||
        result == NULL) {
        return
            vitte_filesystem_result_error(
                VITTE_FILESYSTEM_ERROR_INVALID_ARGUMENT,
                0);
    }

    *result = NULL;

    path_length = strlen(path);
    suffix_length = sizeof(suffix);

    if (!vitte_filesystem_size_add(
            path_length,
            suffix_length,
            &total)) {
        return
            vitte_filesystem_result_error(
                VITTE_FILESYSTEM_ERROR_OVERFLOW,
                0);
    }

    if (total > VITTE_FILESYSTEM_DEFAULT_MAX_PATH_BYTES) {
        return
            vitte_filesystem_result_error(
                VITTE_FILESYSTEM_ERROR_PATH_TOO_LONG,
                0);
    }

    buffer = (char *)malloc(total);

    if (buffer == NULL) {
        vitte_filesystem_u64_increment(
            &vitte_filesystem_thread_stats.allocation_failures);

        return
            vitte_filesystem_result_error(
                VITTE_FILESYSTEM_ERROR_OUT_OF_MEMORY,
                0);
    }

    (void)memcpy(buffer, path, path_length);
    (void)memcpy(
        buffer + path_length,
        suffix,
        suffix_length);

    *result = buffer;

    return vitte_filesystem_result_ok();
}

vitte_filesystem_result_t
vitte_filesystem_write_file_atomic(
    const char *path,
    const void *data,
    size_t length)
{
    char *temporary;
    int descriptor;
    vitte_filesystem_result_t result;
    int saved_errno;

    if (!vitte_filesystem_path_valid(path) ||
        (data == NULL && length != 0u)) {
        return
            vitte_filesystem_result_error(
                VITTE_FILESYSTEM_ERROR_INVALID_ARGUMENT,
                0);
    }

    vitte_filesystem_u64_increment(
        &vitte_filesystem_thread_stats.write_calls);

    temporary = NULL;

    result =
        vitte_filesystem_make_atomic_template(
            path,
            &temporary);

    if (result.error != VITTE_FILESYSTEM_ERROR_NONE) {
        return result;
    }

    errno = 0;

    descriptor = mkstemp(temporary);

    if (descriptor < 0) {
        saved_errno = errno;

        free(temporary);

        return
            vitte_filesystem_result_error(
                vitte_filesystem_error_from_errno(
                    saved_errno,
                    VITTE_FILESYSTEM_ERROR_OPEN),
                saved_errno);
    }

    result =
        vitte_filesystem_write_descriptor(
            descriptor,
            data,
            length);

    if (result.error != VITTE_FILESYSTEM_ERROR_NONE) {
        (void)close(descriptor);
        (void)unlink(temporary);
        free(temporary);

        return result;
    }

    if (fsync(descriptor) != 0) {
        saved_errno = errno;

        (void)close(descriptor);
        (void)unlink(temporary);
        free(temporary);

        return
            vitte_filesystem_result_error(
                vitte_filesystem_error_from_errno(
                    saved_errno,
                    VITTE_FILESYSTEM_ERROR_FLUSH),
                saved_errno);
    }

    if (close(descriptor) != 0) {
        saved_errno = errno;

        (void)unlink(temporary);
        free(temporary);

        return
            vitte_filesystem_result_error(
                vitte_filesystem_error_from_errno(
                    saved_errno,
                    VITTE_FILESYSTEM_ERROR_CLOSE),
                saved_errno);
    }

    if (rename(temporary, path) != 0) {
        saved_errno = errno;

        (void)unlink(temporary);
        free(temporary);

        return
            vitte_filesystem_result_error(
                vitte_filesystem_error_from_errno(
                    saved_errno,
                    VITTE_FILESYSTEM_ERROR_RENAME),
                saved_errno);
    }

    free(temporary);

    return vitte_filesystem_result_ok();
}

/* ========================================================================= */
/* Directory creation                                                        */
/* ========================================================================= */

vitte_filesystem_result_t
vitte_filesystem_create_directory(const char *path)
{
    int saved_errno;

    if (!vitte_filesystem_path_valid(path)) {
        return
            vitte_filesystem_result_error(
                VITTE_FILESYSTEM_ERROR_INVALID_PATH,
                0);
    }

    vitte_filesystem_u64_increment(
        &vitte_filesystem_thread_stats.mkdir_calls);

    if (mkdir(path, VITTE_FILESYSTEM_DIRECTORY_MODE) == 0) {
        return vitte_filesystem_result_ok();
    }

    saved_errno = errno;

    if (saved_errno == EEXIST &&
        vitte_filesystem_is_directory(path)) {
        return vitte_filesystem_result_ok();
    }

    return
        vitte_filesystem_result_error(
            vitte_filesystem_error_from_errno(
                saved_errno,
                VITTE_FILESYSTEM_ERROR_MKDIR),
            saved_errno);
}

vitte_filesystem_result_t
vitte_filesystem_create_directories(const char *path)
{
    char *copy;
    size_t length;
    size_t index;
    vitte_filesystem_result_t result;

    if (!vitte_filesystem_path_valid(path)) {
        return
            vitte_filesystem_result_error(
                VITTE_FILESYSTEM_ERROR_INVALID_PATH,
                0);
    }

    length = strlen(path);

    if (length == 0u) {
        return
            vitte_filesystem_result_error(
                VITTE_FILESYSTEM_ERROR_INVALID_PATH,
                0);
    }

    copy = (char *)malloc(length + 1u);

    if (copy == NULL) {
        vitte_filesystem_u64_increment(
            &vitte_filesystem_thread_stats.allocation_failures);

        return
            vitte_filesystem_result_error(
                VITTE_FILESYSTEM_ERROR_OUT_OF_MEMORY,
                0);
    }

    (void)memcpy(copy, path, length + 1u);

    for (index = 0u; index < length; ++index) {
        if (copy[index] == '\\') {
            copy[index] = '/';
        }
    }

    /*
     * Skip the leading '/' for absolute paths.
     */
    index = copy[0] == '/' ? 1u : 0u;
    if (length >= 3u &&
        ((copy[0] >= 'A' && copy[0] <= 'Z') ||
         (copy[0] >= 'a' && copy[0] <= 'z')) &&
        copy[1] == ':' && copy[2] == '/') {
        index = 3u;
    }
#if defined(_WIN32)
    if (length >= 2u && copy[0] == '/' && copy[1] == '/') {
        size_t components = 0u;
        index = 2u;
        while (index < length && components < 2u) {
            while (index < length && copy[index] != '/') {
                ++index;
            }
            while (index < length && copy[index] == '/') {
                ++index;
            }
            ++components;
        }
    }
#endif

    for (; index < length; ++index) {
        if (copy[index] != '/') {
            continue;
        }

        /*
         * Collapse repeated separators while walking.
         */
        if (index == 0u ||
            copy[index - 1u] == '/') {
            continue;
        }

        copy[index] = '\0';

        result =
            vitte_filesystem_create_directory(copy);

        copy[index] = '/';

        if (result.error != VITTE_FILESYSTEM_ERROR_NONE) {
            free(copy);
            return result;
        }
    }

    result =
        vitte_filesystem_create_directory(copy);

    free(copy);

    return result;
}

/* ========================================================================= */
/* Remove                                                                    */
/* ========================================================================= */

vitte_filesystem_result_t
vitte_filesystem_remove_file(const char *path)
{
    VITTE_FILESYSTEM_STAT_TYPE metadata;
    int saved_errno;

    if (!vitte_filesystem_path_valid(path)) {
        return
            vitte_filesystem_result_error(
                VITTE_FILESYSTEM_ERROR_INVALID_PATH,
                0);
    }

    vitte_filesystem_u64_increment(
        &vitte_filesystem_thread_stats.remove_calls);

    if (VITTE_FILESYSTEM_LSTAT(path, &metadata) != 0) {
        saved_errno = errno;

        return
            vitte_filesystem_result_error(
                vitte_filesystem_error_from_errno(
                    saved_errno,
                    VITTE_FILESYSTEM_ERROR_STAT),
                saved_errno);
    }

 #if defined(_WIN32)
    /* Windows reports directory reparse points through _wstat64().  A
     * reparse point must still be removable as a link, even when its target
     * is a directory. */
    if (vitte_filesystem_is_symlink(path)) {
        int remove_result =
            vitte_windows_reparse_is_directory(path)
                ? rmdir(path)
                : unlink(path);
        if (remove_result != 0) {
            saved_errno = errno;
            return vitte_filesystem_result_error(
                vitte_filesystem_error_from_errno(
                    saved_errno,
                    VITTE_FILESYSTEM_ERROR_REMOVE),
                saved_errno);
        }
        return vitte_filesystem_result_ok();
    }
 #endif

    if (S_ISDIR(metadata.st_mode)) {
        return
            vitte_filesystem_result_error(
                VITTE_FILESYSTEM_ERROR_NOT_FILE,
                0);
    }

    if (unlink(path) != 0) {
        saved_errno = errno;

        return
            vitte_filesystem_result_error(
                vitte_filesystem_error_from_errno(
                    saved_errno,
                    VITTE_FILESYSTEM_ERROR_REMOVE),
                saved_errno);
    }

    return vitte_filesystem_result_ok();
}

vitte_filesystem_result_t
vitte_filesystem_remove_directory(const char *path)
{
    VITTE_FILESYSTEM_STAT_TYPE metadata;
    int saved_errno;

    if (!vitte_filesystem_path_valid(path)) {
        return
            vitte_filesystem_result_error(
                VITTE_FILESYSTEM_ERROR_INVALID_PATH,
                0);
    }

    vitte_filesystem_u64_increment(
        &vitte_filesystem_thread_stats.remove_calls);

    if (VITTE_FILESYSTEM_LSTAT(path, &metadata) != 0) {
        saved_errno = errno;

        return
            vitte_filesystem_result_error(
                vitte_filesystem_error_from_errno(
                    saved_errno,
                    VITTE_FILESYSTEM_ERROR_STAT),
                saved_errno);
    }

 #if defined(_WIN32)
    if (vitte_filesystem_is_symlink(path)) {
        return vitte_filesystem_result_error(
            VITTE_FILESYSTEM_ERROR_NOT_DIRECTORY,
            0);
    }
 #endif

    if (!S_ISDIR(metadata.st_mode)) {
        return
            vitte_filesystem_result_error(
                VITTE_FILESYSTEM_ERROR_NOT_DIRECTORY,
                0);
    }

    if (rmdir(path) != 0) {
        saved_errno = errno;

        return
            vitte_filesystem_result_error(
                vitte_filesystem_error_from_errno(
                    saved_errno,
                    VITTE_FILESYSTEM_ERROR_REMOVE),
                saved_errno);
    }

    return vitte_filesystem_result_ok();
}

/* ========================================================================= */
/* Rename                                                                    */
/* ========================================================================= */

vitte_filesystem_result_t
vitte_filesystem_rename(
    const char *source,
    const char *destination)
{
    int saved_errno;

    if (!vitte_filesystem_path_valid(source) ||
        !vitte_filesystem_path_valid(destination)) {
        return
            vitte_filesystem_result_error(
                VITTE_FILESYSTEM_ERROR_INVALID_PATH,
                0);
    }

    vitte_filesystem_u64_increment(
        &vitte_filesystem_thread_stats.rename_calls);

    if (rename(source, destination) != 0) {
        saved_errno = errno;

        return
            vitte_filesystem_result_error(
                vitte_filesystem_error_from_errno(
                    saved_errno,
                    VITTE_FILESYSTEM_ERROR_RENAME),
                saved_errno);
    }

    return vitte_filesystem_result_ok();
}

/* ========================================================================= */
/* Directory list                                                            */
/* ========================================================================= */

void
vitte_filesystem_directory_list_destroy(
    vitte_filesystem_directory_list_t *list)
{
    size_t index;

    if (list == NULL) {
        return;
    }

    for (index = 0u; index < list->count; ++index) {
        free(list->entries[index].name);
        list->entries[index].name = NULL;
    }

    free(list->entries);

    list->entries = NULL;
    list->count = 0u;
    list->capacity = 0u;
}

static bool
vitte_filesystem_directory_list_reserve(
    vitte_filesystem_directory_list_t *list,
    size_t required)
{
    size_t capacity;
    size_t bytes;
    vitte_filesystem_directory_entry_t *entries;

    if (list == NULL) {
        return false;
    }

    if (required <= list->capacity) {
        return true;
    }

    capacity = list->capacity == 0u ? 16u : list->capacity;

    while (capacity < required) {
        size_t next;

        if (!vitte_filesystem_size_add(
                capacity,
                capacity,
                &next)) {
            capacity = required;
            break;
        }

        capacity = next;
    }

    if (!vitte_filesystem_size_mul(
            capacity,
            sizeof(*list->entries),
            &bytes)) {
        return false;
    }

    entries =
        (vitte_filesystem_directory_entry_t *)realloc(
            list->entries,
            bytes);

    if (entries == NULL) {
        vitte_filesystem_u64_increment(
            &vitte_filesystem_thread_stats.allocation_failures);

        return false;
    }

    list->entries = entries;
    list->capacity = capacity;

    return true;
}

static int
vitte_filesystem_directory_entry_compare(
    const void *left,
    const void *right)
{
    const vitte_filesystem_directory_entry_t *a;
    const vitte_filesystem_directory_entry_t *b;

    a = (const vitte_filesystem_directory_entry_t *)left;
    b = (const vitte_filesystem_directory_entry_t *)right;

    if (a->name == NULL && b->name == NULL) {
        return 0;
    }

    if (a->name == NULL) {
        return -1;
    }

    if (b->name == NULL) {
        return 1;
    }

    return strcmp(a->name, b->name);
}

static vitte_filesystem_kind_t
vitte_filesystem_directory_entry_kind(
    const char *directory,
    const char *name)
{
    char *path;
    VITTE_FILESYSTEM_STAT_TYPE metadata;
    vitte_filesystem_kind_t kind;

    path = NULL;

    if (vitte_filesystem_join(
            directory,
            name,
            &path).error != VITTE_FILESYSTEM_ERROR_NONE) {
        return VITTE_FILESYSTEM_KIND_UNKNOWN;
    }

    if (VITTE_FILESYSTEM_LSTAT(path, &metadata) != 0) {
        free(path);
        return VITTE_FILESYSTEM_KIND_UNKNOWN;
    }

    kind =
        vitte_filesystem_kind_from_mode(
            metadata.st_mode);
#if defined(_WIN32)
    if (vitte_filesystem_is_symlink(path)) {
        kind = VITTE_FILESYSTEM_KIND_SYMLINK;
    }
#endif

    free(path);

    return kind;
}

vitte_filesystem_result_t
vitte_filesystem_list_directory(
    const char *path,
    size_t max_entries,
    vitte_filesystem_directory_list_t *list)
{
    DIR *directory;
    struct dirent *entry;
    size_t limit;
    int saved_errno;

    if (!vitte_filesystem_path_valid(path) ||
        list == NULL) {
        return
            vitte_filesystem_result_error(
                VITTE_FILESYSTEM_ERROR_INVALID_ARGUMENT,
                0);
    }

    vitte_filesystem_u64_increment(
        &vitte_filesystem_thread_stats.directory_list_calls);

    vitte_filesystem_directory_list_destroy(list);

    limit =
        max_entries == 0u
            ? VITTE_FILESYSTEM_DEFAULT_MAX_DIRECTORY_ENTRIES
            : max_entries;

    errno = 0;

    directory = opendir(path);

    if (directory == NULL) {
        saved_errno = errno;

        return
            vitte_filesystem_result_error(
                vitte_filesystem_error_from_errno(
                    saved_errno,
                    VITTE_FILESYSTEM_ERROR_DIRECTORY_READ),
                saved_errno);
    }

    for (;;) {
        size_t name_length;
        char *name;

        errno = 0;

        entry = readdir(directory);

        if (entry == NULL) {
            if (errno != 0) {
                saved_errno = errno;

                (void)closedir(directory);
                vitte_filesystem_directory_list_destroy(list);

                return
                    vitte_filesystem_result_error(
                        vitte_filesystem_error_from_errno(
                            saved_errno,
                            VITTE_FILESYSTEM_ERROR_DIRECTORY_READ),
                        saved_errno);
            }

            break;
        }

        if (strcmp(entry->d_name, ".") == 0 ||
            strcmp(entry->d_name, "..") == 0) {
            continue;
        }

        if (list->count >= limit) {
            (void)closedir(directory);
            vitte_filesystem_directory_list_destroy(list);

            return
                vitte_filesystem_result_error(
                    VITTE_FILESYSTEM_ERROR_TOO_MANY_ENTRIES,
                    0);
        }

        if (!vitte_filesystem_directory_list_reserve(
                list,
                list->count + 1u)) {
            (void)closedir(directory);
            vitte_filesystem_directory_list_destroy(list);

            return
                vitte_filesystem_result_error(
                    VITTE_FILESYSTEM_ERROR_OUT_OF_MEMORY,
                    0);
        }

        name_length = strlen(entry->d_name);

        name = (char *)malloc(name_length + 1u);

        if (name == NULL) {
            vitte_filesystem_u64_increment(
                &vitte_filesystem_thread_stats.allocation_failures);

            (void)closedir(directory);
            vitte_filesystem_directory_list_destroy(list);

            return
                vitte_filesystem_result_error(
                    VITTE_FILESYSTEM_ERROR_OUT_OF_MEMORY,
                    0);
        }

        (void)memcpy(
            name,
            entry->d_name,
            name_length + 1u);

        list->entries[list->count].name = name;

        list->entries[list->count].kind =
            vitte_filesystem_directory_entry_kind(
                path,
                name);

        ++list->count;
    }

    if (closedir(directory) != 0) {
        saved_errno = errno;

        vitte_filesystem_directory_list_destroy(list);

        return
            vitte_filesystem_result_error(
                vitte_filesystem_error_from_errno(
                    saved_errno,
                    VITTE_FILESYSTEM_ERROR_CLOSE),
                saved_errno);
    }

    if (list->count > 1u) {
        qsort(
            list->entries,
            list->count,
            sizeof(*list->entries),
            vitte_filesystem_directory_entry_compare);
    }

    return vitte_filesystem_result_ok();
}

/* ========================================================================= */
/* Absolute path                                                             */
/* ========================================================================= */

bool
vitte_filesystem_path_is_absolute(const char *path)
{
    unsigned char drive;

    if (path == NULL || path[0] == '\0') {
        return false;
    }

    if (vitte_filesystem_path_separator(path[0])) {
        return true;
    }

    drive = (unsigned char)path[0];
    return ((drive >= (unsigned char)'A' &&
             drive <= (unsigned char)'Z') ||
            (drive >= (unsigned char)'a' &&
             drive <= (unsigned char)'z')) &&
           path[1] == ':' &&
           vitte_filesystem_path_separator(path[2]);
}

/* ========================================================================= */
/* Join                                                                      */
/* ========================================================================= */

vitte_filesystem_result_t
vitte_filesystem_join(
    const char *left,
    const char *right,
    char **result)
{
    size_t left_length;
    size_t right_length;
    size_t total;
    bool separator;
    char *output;
    size_t offset;

    if (left == NULL ||
        right == NULL ||
        result == NULL) {
        return
            vitte_filesystem_result_error(
                VITTE_FILESYSTEM_ERROR_INVALID_ARGUMENT,
                0);
    }

    *result = NULL;

    if (vitte_filesystem_path_is_absolute(right)) {
        right_length = strlen(right);

        if (right_length >
            VITTE_FILESYSTEM_DEFAULT_MAX_PATH_BYTES) {
            return
                vitte_filesystem_result_error(
                    VITTE_FILESYSTEM_ERROR_PATH_TOO_LONG,
                    0);
        }

        output = (char *)malloc(right_length + 1u);

        if (output == NULL) {
            vitte_filesystem_u64_increment(
                &vitte_filesystem_thread_stats.allocation_failures);

            return
                vitte_filesystem_result_error(
                    VITTE_FILESYSTEM_ERROR_OUT_OF_MEMORY,
                    0);
        }

        (void)memcpy(output, right, right_length + 1u);

        for (offset = 0u; offset < right_length; ++offset) {
            if (output[offset] == '\\') {
                output[offset] = '/';
            }
        }

        *result = output;

        return vitte_filesystem_result_ok();
    }

    left_length = strlen(left);
    right_length = strlen(right);

    separator =
        left_length != 0u &&
        right_length != 0u &&
        !vitte_filesystem_path_separator(left[left_length - 1u]);

    if (!vitte_filesystem_size_add(
            left_length,
            right_length,
            &total)) {
        return
            vitte_filesystem_result_error(
                VITTE_FILESYSTEM_ERROR_OVERFLOW,
                0);
    }

    if (separator) {
        if (!vitte_filesystem_size_add(
                total,
                1u,
                &total)) {
            return
                vitte_filesystem_result_error(
                    VITTE_FILESYSTEM_ERROR_OVERFLOW,
                    0);
        }
    }

    if (!vitte_filesystem_size_add(
            total,
            1u,
            &total)) {
        return
            vitte_filesystem_result_error(
                VITTE_FILESYSTEM_ERROR_OVERFLOW,
                0);
    }

    if (total >
        VITTE_FILESYSTEM_DEFAULT_MAX_PATH_BYTES) {
        return
            vitte_filesystem_result_error(
                VITTE_FILESYSTEM_ERROR_PATH_TOO_LONG,
                0);
    }

    output = (char *)malloc(total);

    if (output == NULL) {
        vitte_filesystem_u64_increment(
            &vitte_filesystem_thread_stats.allocation_failures);

        return
            vitte_filesystem_result_error(
                VITTE_FILESYSTEM_ERROR_OUT_OF_MEMORY,
                0);
    }

    offset = 0u;

    if (left_length != 0u) {
        (void)memcpy(
            output + offset,
            left,
            left_length);

        offset += left_length;
    }

    if (separator) {
        output[offset] = '/';
        ++offset;
    }

    if (right_length != 0u) {
        (void)memcpy(
            output + offset,
            right,
            right_length);

        offset += right_length;
    }

    output[offset] = '\0';

    for (offset = 0u; output[offset] != '\0'; ++offset) {
        if (output[offset] == '\\') {
            output[offset] = '/';
        }
    }

    *result = output;

    return vitte_filesystem_result_ok();
}

/* ========================================================================= */
/* Path normalization                                                        */
/* ========================================================================= */

typedef struct vitte_filesystem_component_stack {
    char **items;
    size_t count;
    size_t capacity;
} vitte_filesystem_component_stack_t;

static void
vitte_filesystem_component_stack_destroy(
    vitte_filesystem_component_stack_t *stack)
{
    size_t index;

    if (stack == NULL) {
        return;
    }

    for (index = 0u; index < stack->count; ++index) {
        free(stack->items[index]);
    }

    free(stack->items);

    stack->items = NULL;
    stack->count = 0u;
    stack->capacity = 0u;
}

static bool
vitte_filesystem_component_stack_push(
    vitte_filesystem_component_stack_t *stack,
    const char *begin,
    size_t length)
{
    char *component;

    if (stack == NULL ||
        (begin == NULL && length != 0u)) {
        return false;
    }

    if (stack->count == stack->capacity) {
        size_t capacity;
        size_t bytes;
        char **items;

        capacity =
            stack->capacity == 0u
                ? 16u
                : stack->capacity * 2u;

        if (capacity < stack->capacity) {
            return false;
        }

        if (!vitte_filesystem_size_mul(
                capacity,
                sizeof(*stack->items),
                &bytes)) {
            return false;
        }

        items =
            (char **)realloc(
                stack->items,
                bytes);

        if (items == NULL) {
            vitte_filesystem_u64_increment(
                &vitte_filesystem_thread_stats.allocation_failures);

            return false;
        }

        stack->items = items;
        stack->capacity = capacity;
    }

    component = (char *)malloc(length + 1u);

    if (component == NULL) {
        vitte_filesystem_u64_increment(
            &vitte_filesystem_thread_stats.allocation_failures);

        return false;
    }

    if (length != 0u) {
        (void)memcpy(component, begin, length);
    }

    component[length] = '\0';

    stack->items[stack->count] = component;
    ++stack->count;

    return true;
}

static void
vitte_filesystem_component_stack_pop(
    vitte_filesystem_component_stack_t *stack)
{
    if (stack == NULL || stack->count == 0u) {
        return;
    }

    --stack->count;

    free(stack->items[stack->count]);
    stack->items[stack->count] = NULL;
}

vitte_filesystem_result_t
vitte_filesystem_normalize_impl(
    const char *path,
    char **result)
{
    vitte_filesystem_component_stack_t stack;
    bool absolute;
    size_t prefix_length;
    size_t length;
    size_t index;
    size_t output_length;
    char *output;
    size_t output_index;

    if (path == NULL || result == NULL) {
        return
            vitte_filesystem_result_error(
                VITTE_FILESYSTEM_ERROR_INVALID_ARGUMENT,
                0);
    }

    *result = NULL;

    length = strlen(path);

    if (length >
        VITTE_FILESYSTEM_DEFAULT_MAX_PATH_BYTES) {
        return
            vitte_filesystem_result_error(
                VITTE_FILESYSTEM_ERROR_PATH_TOO_LONG,
                0);
    }

    (void)memset(&stack, 0, sizeof(stack));

    prefix_length = 0u;
    absolute = length != 0u &&
               vitte_filesystem_path_separator(path[0]);
    if (length >= 3u &&
        ((path[0] >= 'A' && path[0] <= 'Z') ||
         (path[0] >= 'a' && path[0] <= 'z')) &&
        path[1] == ':' &&
        vitte_filesystem_path_separator(path[2])) {
        prefix_length = 3u;
        absolute = true;
    } else if (length >= 2u &&
               vitte_filesystem_path_separator(path[0]) &&
               vitte_filesystem_path_separator(path[1])) {
        prefix_length = 2u;
        absolute = true;
    } else if (absolute) {
        prefix_length = 1u;
    }

    index = prefix_length;

    while (index < length) {
        size_t begin;
        size_t component_length;

        while (index < length &&
               vitte_filesystem_path_separator(path[index])) {
            ++index;
        }

        begin = index;

        while (index < length &&
               !vitte_filesystem_path_separator(path[index])) {
            ++index;
        }

        component_length = index - begin;

        if (component_length == 0u) {
            continue;
        }

        if (component_length == 1u &&
            path[begin] == '.') {
            continue;
        }

        if (component_length == 2u &&
            path[begin] == '.' &&
            path[begin + 1u] == '.') {
            if (stack.count != 0u &&
                strcmp(
                    stack.items[stack.count - 1u],
                    "..") != 0) {
                vitte_filesystem_component_stack_pop(
                    &stack);
            } else if (!absolute) {
                if (!vitte_filesystem_component_stack_push(
                        &stack,
                        path + begin,
                        component_length)) {
                    vitte_filesystem_component_stack_destroy(
                        &stack);

                    return
                        vitte_filesystem_result_error(
                            VITTE_FILESYSTEM_ERROR_OUT_OF_MEMORY,
                            0);
                }
            }

            continue;
        }

        if (!vitte_filesystem_component_stack_push(
                &stack,
                path + begin,
                component_length)) {
            vitte_filesystem_component_stack_destroy(
                &stack);

            return
                vitte_filesystem_result_error(
                    VITTE_FILESYSTEM_ERROR_OUT_OF_MEMORY,
                    0);
        }
    }

    output_length = prefix_length;

    if (!absolute && stack.count == 0u) {
        output_length = 1u;
    }

    for (index = 0u; index < stack.count; ++index) {
        size_t component_length;

        component_length = strlen(stack.items[index]);

        if (!vitte_filesystem_size_add(
                output_length,
                component_length,
                &output_length)) {
            vitte_filesystem_component_stack_destroy(
                &stack);

            return
                vitte_filesystem_result_error(
                    VITTE_FILESYSTEM_ERROR_OVERFLOW,
                    0);
        }

        if (index + 1u < stack.count) {
            if (!vitte_filesystem_size_add(
                    output_length,
                    1u,
                    &output_length)) {
                vitte_filesystem_component_stack_destroy(
                    &stack);

                return
                    vitte_filesystem_result_error(
                        VITTE_FILESYSTEM_ERROR_OVERFLOW,
                        0);
            }
        }
    }

    if (output_length >
        VITTE_FILESYSTEM_DEFAULT_MAX_PATH_BYTES) {
        vitte_filesystem_component_stack_destroy(&stack);

        return
            vitte_filesystem_result_error(
                VITTE_FILESYSTEM_ERROR_PATH_TOO_LONG,
                0);
    }

    output = (char *)malloc(output_length + 1u);

    if (output == NULL) {
        vitte_filesystem_u64_increment(
            &vitte_filesystem_thread_stats.allocation_failures);

        vitte_filesystem_component_stack_destroy(&stack);

        return
            vitte_filesystem_result_error(
                VITTE_FILESYSTEM_ERROR_OUT_OF_MEMORY,
                0);
    }

    output_index = 0u;

    if (prefix_length != 0u) {
        (void)memcpy(output, path, prefix_length);
        for (index = 0u; index < prefix_length; ++index) {
            if (output[index] == '\\') {
                output[index] = '/';
            }
        }
        output_index = prefix_length;
    } else if (stack.count == 0u) {
        output[output_index] = '.';
        ++output_index;
    }

    for (index = 0u; index < stack.count; ++index) {
        size_t component_length;

        if (output_index != 0u &&
            output[output_index - 1u] != '/') {
            output[output_index] = '/';
            ++output_index;
        }

        component_length = strlen(stack.items[index]);

        (void)memcpy(
            output + output_index,
            stack.items[index],
            component_length);

        output_index += component_length;
    }

    output[output_index] = '\0';

    vitte_filesystem_component_stack_destroy(&stack);

    *result = output;

    return vitte_filesystem_result_ok();
}

vitte_filesystem_result_t
vitte_filesystem_normalize(
    const char *path,
    char **result)
{
    char *portable;
    size_t length;
    size_t index;
    vitte_filesystem_result_t normalized;

    if (path == NULL || result == NULL) {
        return vitte_filesystem_result_error(
            VITTE_FILESYSTEM_ERROR_INVALID_ARGUMENT,
            0);
    }

    length = strlen(path);
    if (length > VITTE_FILESYSTEM_DEFAULT_MAX_PATH_BYTES) {
        return vitte_filesystem_result_error(
            VITTE_FILESYSTEM_ERROR_PATH_TOO_LONG,
            0);
    }

    portable = (char *)malloc(length + 1u);
    if (portable == NULL) {
        vitte_filesystem_u64_increment(
            &vitte_filesystem_thread_stats.allocation_failures);
        return vitte_filesystem_result_error(
            VITTE_FILESYSTEM_ERROR_OUT_OF_MEMORY,
            0);
    }
    memcpy(portable, path, length + 1u);
    for (index = 0u; index < length; ++index) {
        if (portable[index] == '\\') {
            portable[index] = '/';
        }
    }

    normalized = vitte_filesystem_normalize_impl(portable, result);
    free(portable);
    return normalized;
}

/* ========================================================================= */
/* Canonical path                                                            */
/* ========================================================================= */

vitte_filesystem_result_t
vitte_filesystem_canonicalize(
    const char *path,
    char **result)
{
    char *resolved;
    int saved_errno;

    if (!vitte_filesystem_path_valid(path) ||
        result == NULL) {
        return
            vitte_filesystem_result_error(
                VITTE_FILESYSTEM_ERROR_INVALID_ARGUMENT,
                0);
    }

    *result = NULL;

    errno = 0;

#if defined(_WIN32)
    {
        wchar_t *wide_path;
        wchar_t *wide_resolved;

        wide_path = vitte_windows_utf8_to_wide(path);
        if (wide_path == NULL) {
            return vitte_filesystem_result_error(
                VITTE_FILESYSTEM_ERROR_CANONICALIZE,
                errno);
        }
        wide_resolved = _wfullpath(NULL, wide_path, 0u);
        free(wide_path);
        if (wide_resolved == NULL) {
            saved_errno = errno;
            return vitte_filesystem_result_error(
                vitte_filesystem_error_from_errno(
                    saved_errno,
                    VITTE_FILESYSTEM_ERROR_CANONICALIZE),
                saved_errno);
        }
        resolved = vitte_windows_wide_to_utf8(wide_resolved);
        free(wide_resolved);
    }
#else
    resolved = realpath(path, NULL);
#endif

    if (resolved == NULL) {
        saved_errno = errno;

        return
            vitte_filesystem_result_error(
                vitte_filesystem_error_from_errno(
                    saved_errno,
                    VITTE_FILESYSTEM_ERROR_CANONICALIZE),
                saved_errno);
    }

    if (strlen(resolved) >
        VITTE_FILESYSTEM_DEFAULT_MAX_PATH_BYTES) {
        free(resolved);

        return
            vitte_filesystem_result_error(
                VITTE_FILESYSTEM_ERROR_PATH_TOO_LONG,
                0);
    }

    *result = resolved;

    return vitte_filesystem_result_ok();
}

/* ========================================================================= */
/* Current directory                                                         */
/* ========================================================================= */

vitte_filesystem_result_t
vitte_filesystem_current_directory(char **result)
{
    size_t capacity;
    char *buffer;

    if (result == NULL) {
        return
            vitte_filesystem_result_error(
                VITTE_FILESYSTEM_ERROR_INVALID_ARGUMENT,
                0);
    }

    *result = NULL;

    capacity = 256u;

    for (;;) {
        char *new_buffer;
        int saved_errno;

        if (capacity >
            VITTE_FILESYSTEM_DEFAULT_MAX_PATH_BYTES) {
            return
                vitte_filesystem_result_error(
                    VITTE_FILESYSTEM_ERROR_PATH_TOO_LONG,
                    0);
        }

        buffer = (char *)malloc(capacity);

        if (buffer == NULL) {
            vitte_filesystem_u64_increment(
                &vitte_filesystem_thread_stats.allocation_failures);

            return
                vitte_filesystem_result_error(
                    VITTE_FILESYSTEM_ERROR_OUT_OF_MEMORY,
                    0);
        }

        errno = 0;

        if (getcwd(buffer, capacity) != NULL) {
            *result = buffer;
            return vitte_filesystem_result_ok();
        }

        saved_errno = errno;

        free(buffer);

        if (saved_errno != ERANGE) {
            return
                vitte_filesystem_result_error(
                    vitte_filesystem_error_from_errno(
                        saved_errno,
                        VITTE_FILESYSTEM_ERROR_CURRENT_DIRECTORY),
                    saved_errno);
        }

        if (capacity >
            VITTE_FILESYSTEM_DEFAULT_MAX_PATH_BYTES / 2u) {
            capacity =
                VITTE_FILESYSTEM_DEFAULT_MAX_PATH_BYTES;
        } else {
            capacity *= 2u;
        }

        /*
         * Keep the declaration used under strict warning sets that diagnose
         * future accidental dead assignments if this loop is extended.
         */
        new_buffer = NULL;
        (void)new_buffer;
    }
}

/* ========================================================================= */
/* Path hash                                                                 */
/* ========================================================================= */

uint64_t
vitte_filesystem_path_hash(const char *path)
{
    uint64_t hash;
    size_t index;

    if (path == NULL) {
        return UINT64_C(0);
    }

    hash = VITTE_FILESYSTEM_FNV_OFFSET;

    for (index = 0u; path[index] != '\0'; ++index) {
        hash ^= (uint64_t)(unsigned char)path[index];
        hash *= VITTE_FILESYSTEM_FNV_PRIME;
    }

    return hash;
}

/* ========================================================================= */
/* Statistics                                                                */
/* ========================================================================= */

vitte_filesystem_stats_t
vitte_filesystem_stats(void)
{
    return vitte_filesystem_thread_stats;
}

void
vitte_filesystem_stats_reset(void)
{
    (void)memset(
        &vitte_filesystem_thread_stats,
        0,
        sizeof(vitte_filesystem_thread_stats));
}

/* ========================================================================= */
/* Compile-time invariants                                                   */
/* ========================================================================= */

_Static_assert(
    VITTE_FILESYSTEM_DEFAULT_MAX_FILE_BYTES > 0u,
    "filesystem maximum file size must be non-zero");

_Static_assert(
    VITTE_FILESYSTEM_DEFAULT_MAX_PATH_BYTES > 0u,
    "filesystem maximum path size must be non-zero");

_Static_assert(
    VITTE_FILESYSTEM_DEFAULT_MAX_DIRECTORY_ENTRIES > 0u,
    "filesystem maximum directory entries must be non-zero");

_Static_assert(
    VITTE_FILESYSTEM_READ_CHUNK > 0u,
    "filesystem read chunk must be non-zero");
