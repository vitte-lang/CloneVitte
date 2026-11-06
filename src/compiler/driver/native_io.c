/* Small stdio boundary used by the Vitte-owned process and compiler API. */
#if !defined(_WIN32)
#define _POSIX_C_SOURCE 200809L
#endif
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#if defined(_WIN32)
#include <io.h>
#include <windows.h>
#include <wchar.h>
#include <process.h>
#ifndef S_ISREG
#define S_ISREG(mode) (((mode) & _S_IFMT) == _S_IFREG)
#endif
#ifndef S_ISDIR
#define S_ISDIR(mode) (((mode) & _S_IFMT) == _S_IFDIR)
#endif
#define VITTE_NATIVE_STAT struct _stat64
#else
#define VITTE_NATIVE_STAT struct stat
#include <unistd.h>
#endif

#if defined(_WIN32)
static wchar_t *
vitte_native_utf8_to_wide(
    const uint8_t *text)
{
    int required;
    wchar_t *wide;

    if (text == NULL) {
        return NULL;
    }
    required = MultiByteToWideChar(
        CP_UTF8, MB_ERR_INVALID_CHARS, (const char *)text, -1, NULL, 0);
    if (required <= 0 || (size_t)required > SIZE_MAX / sizeof(*wide)) {
        return NULL;
    }
    wide = (wchar_t *)malloc((size_t)required * sizeof(*wide));
    if (wide == NULL) {
        return NULL;
    }
    if (MultiByteToWideChar(
            CP_UTF8, MB_ERR_INVALID_CHARS, (const char *)text, -1,
            wide, required) != required) {
        free(wide);
        return NULL;
    }
    return wide;
}
#endif

/* Core allocation intrinsics use the platform allocator at the host boundary.
 * The bootstrap executable keeps its own implementation in runtime/intrinsics.c.
 */
void *
native_allocate(
    size_t size)
{
    return malloc(size == 0u ? 1u : size);
}

void
native_deallocate(
    void *pointer)
{
    free(pointer);
}

const char *
vitte_native_environment_value(
    const char *name)
{
    if (name == NULL) {
        return NULL;
    }
    return getenv(name);
}

uint8_t *
vitte_native_api_file_open(
    uint8_t *path,
    uint8_t *mode)
{
    return (uint8_t *)(void *)fopen(
        (const char *)path,
        (const char *)mode);
}

size_t
vitte_native_api_file_read(
    uint8_t *data,
    size_t element_size,
    size_t element_count,
    uint8_t *file)
{
    return fread(data, element_size, element_count, (FILE *)(void *)file);
}

int32_t
vitte_native_api_file_error(
    uint8_t *file)
{
    return (int32_t)ferror((FILE *)(void *)file);
}

int32_t
vitte_native_api_file_close(
    uint8_t *file)
{
    return (int32_t)fclose((FILE *)(void *)file);
}

/* Primitive host calls used by the Vitte-owned filesystem policy layer. */
int32_t
vitte_native_fs_path_kind(
    const uint8_t *path)
{
    VITTE_NATIVE_STAT metadata;
    int status;
#if defined(_WIN32)
    wchar_t *wide_path = vitte_native_utf8_to_wide(path);
    if (wide_path == NULL) {
        return 0;
    }
    status = _wstat64(wide_path, &metadata);
    free(wide_path);
#else
    if (path == NULL) {
        return 0;
    }
    status = stat((const char *)path, &metadata);
#endif
    if (status != 0) {
        return 0;
    }
    if (S_ISREG(metadata.st_mode)) {
        return 1;
    }
    if (S_ISDIR(metadata.st_mode)) {
        return 2;
    }
    return 3;
}

uint8_t *
vitte_native_fs_file_open(
    const uint8_t *path,
    const uint8_t *mode)
{
    if (path == NULL || mode == NULL) {
        return NULL;
    }
#if defined(_WIN32)
    wchar_t *wide_path = vitte_native_utf8_to_wide(path);
    wchar_t wide_mode[8];
    size_t index = 0u;
    FILE *file;
    if (wide_path == NULL) {
        return NULL;
    }
    while (mode[index] != 0u && index + 1u < sizeof(wide_mode) / sizeof(wide_mode[0])) {
        wide_mode[index] = (wchar_t)mode[index];
        ++index;
    }
    if (mode[index] != 0u) {
        free(wide_path);
        return NULL;
    }
    wide_mode[index] = L'\0';
    file = _wfopen(wide_path, wide_mode);
    free(wide_path);
    return (uint8_t *)(void *)file;
#else
    return (uint8_t *)(void *)fopen((const char *)path, (const char *)mode);
#endif
}

size_t
vitte_native_fs_file_read(
    uint8_t *data,
    size_t length,
    uint8_t *file)
{
    if ((data == NULL && length != 0u) || file == NULL) {
        return 0u;
    }
    return fread(data, 1u, length, (FILE *)(void *)file);
}

int32_t
vitte_native_fs_file_error(
    uint8_t *file)
{
    return file == NULL ? 1 : (int32_t)ferror((FILE *)(void *)file);
}

int32_t
vitte_native_fs_file_close(
    uint8_t *file)
{
    return file == NULL ? -1 : (int32_t)fclose((FILE *)(void *)file);
}

size_t
vitte_native_fs_file_write(
    const uint8_t *data,
    size_t length,
    uint8_t *file)
{
    if ((data == NULL && length != 0u) || file == NULL) {
        return 0u;
    }
    return fwrite(data, 1u, length, (FILE *)(void *)file);
}

int32_t
vitte_native_fs_file_flush(
    uint8_t *file)
{
    return file == NULL ? -1 : (int32_t)fflush((FILE *)(void *)file);
}

int32_t
vitte_native_fs_file_sync(
    uint8_t *file)
{
    if (file == NULL) {
        return -1;
    }
#if defined(_WIN32)
    return (int32_t)_commit(_fileno((FILE *)(void *)file));
#else
    return (int32_t)fsync(fileno((FILE *)(void *)file));
#endif
}

int32_t
vitte_native_fs_remove(
    const uint8_t *path)
{
    if (path == NULL) {
        return -1;
    }
#if defined(_WIN32)
    wchar_t *wide_path = vitte_native_utf8_to_wide(path);
    BOOL removed;
    if (wide_path == NULL) {
        return -1;
    }
    removed = DeleteFileW(wide_path);
    free(wide_path);
    return removed != 0 ? 0 : -1;
#else
    return remove((const char *)path);
#endif
}

int32_t
vitte_native_fs_rename_replace(
    const uint8_t *source,
    const uint8_t *destination)
{
    if (source == NULL || destination == NULL) {
        return -1;
    }
#if defined(_WIN32)
    wchar_t *wide_source = vitte_native_utf8_to_wide(source);
    wchar_t *wide_destination = vitte_native_utf8_to_wide(destination);
    BOOL renamed;
    if (wide_source == NULL || wide_destination == NULL) {
        free(wide_source);
        free(wide_destination);
        return -1;
    }
    renamed = MoveFileExW(
        wide_source, wide_destination,
        MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
    free(wide_source);
    free(wide_destination);
    return renamed != 0 ? 0 : -1;
#else
    return rename((const char *)source, (const char *)destination);
#endif
}

uint64_t
vitte_native_fs_process_id(void)
{
#if defined(_WIN32)
    return (uint64_t)_getpid();
#else
    return (uint64_t)getpid();
#endif
}

bool
vitte_native_fs_file_size(
    uint8_t *file,
    size_t *size)
{
    VITTE_NATIVE_STAT metadata;
    int status;

    if (file == NULL || size == NULL) {
        return false;
    }
#if defined(_WIN32)
    status = _fstat64(_fileno((FILE *)(void *)file), &metadata);
#else
    status = fstat(fileno((FILE *)(void *)file), &metadata);
#endif
    if (status != 0 || !S_ISREG(metadata.st_mode) || metadata.st_size < 0 ||
        (uintmax_t)metadata.st_size > (uintmax_t)SIZE_MAX) {
        return false;
    }
    *size = (size_t)metadata.st_size;
    return true;
}

uint8_t *
vitte_native_api_allocate(
    size_t size)
{
    return (uint8_t *)malloc(size);
}

void
vitte_native_api_deallocate(
    uint8_t *pointer)
{
    free(pointer);
}

char *
vitte_native_api_bytes_to_string(
    uint8_t *data,
    size_t length)
{
    char *text;

    if (data == NULL && length != 0u) {
        return NULL;
    }
    if (length == SIZE_MAX) {
        return NULL;
    }

    text = (char *)malloc(length + 1u);
    if (text == NULL) {
        return NULL;
    }
    if (length != 0u) {
        memcpy(text, data, length);
    }
    text[length] = '\0';
    return text;
}

void
vitte_native_write_stdout(
    const char *data,
    size_t length)
{
    if (data == NULL || length == 0u) {
        return;
    }
    (void)fwrite(data, 1u, length, stdout);
    (void)fflush(stdout);
}

void
vitte_native_write_stderr(
    const char *data,
    size_t length)
{
    if (data == NULL || length == 0u) {
        return;
    }
    (void)fwrite(data, 1u, length, stderr);
    (void)fflush(stderr);
}

void
vitte_native_write_stdout_bytes(
    const uint8_t *data,
    size_t length)
{
    if (data == NULL || length == 0u) {
        return;
    }
    (void)fwrite(data, 1u, length, stdout);
    (void)fflush(stdout);
}

void
vitte_native_write_stderr_bytes(
    const uint8_t *data,
    size_t length)
{
    if (data == NULL || length == 0u) {
        return;
    }
    (void)fwrite(data, 1u, length, stderr);
    (void)fflush(stderr);
}

int32_t
vitte_native_write_file(
    const char *path,
    const uint8_t *data,
    size_t length)
{
    FILE *output;
    size_t written;
    int close_status;

    if (path == NULL || data == NULL) {
        return 0;
    }
    output = fopen(path, "wb");
    if (output == NULL) {
        return 0;
    }
    written = fwrite(data, 1u, length, output);
    close_status = fclose(output);
    return written == length && close_status == 0 ? 1 : 0;
}
