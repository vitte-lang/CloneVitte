/* Small OS boundary for the Vitte-owned host-toolchain driver. */
#if !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "platform/platform.h"

#if defined(_WIN32)
#include <windows.h>
#include <wchar.h>
#elif defined(__APPLE__) || defined(__unix__)
#include <stdlib.h>
#include <unistd.h>
#endif

#ifdef VITTE_NATIVE_FRONTEND_LINKED

/* Return the platform's per-user Vitte module directory as UTF-8. */
size_t
vitte_native_host_user_modules_root(uint8_t *buffer, size_t capacity)
{
    const char *base;
    int written;
    if (buffer == NULL || capacity == 0u) {
        return 0u;
    }
#if defined(_WIN32)
    (void)base;
    (void)written;
    {
        wchar_t wide_base[MAX_PATH];
        wchar_t wide_path[MAX_PATH * 2u];
        static const wchar_t suffix[] = L"\\Vitte\\modules";
        DWORD wide_length = GetEnvironmentVariableW(
            L"LOCALAPPDATA", wide_base, MAX_PATH);
        size_t suffix_length = sizeof(suffix) / sizeof(suffix[0]) - 1u;
        int byte_length;
        if (wide_length == 0u || wide_length >= MAX_PATH ||
            (size_t)wide_length + suffix_length + 1u >
                sizeof(wide_path) / sizeof(wide_path[0])) {
            return 0u;
        }
        memcpy(wide_path, wide_base,
               (size_t)wide_length * sizeof(wide_path[0]));
        memcpy(wide_path + wide_length, suffix,
               (suffix_length + 1u) * sizeof(wide_path[0]));
        byte_length = WideCharToMultiByte(
            CP_UTF8,
            WC_ERR_INVALID_CHARS,
            wide_path,
            -1,
            (char *)buffer,
            capacity > (size_t)INT_MAX ? INT_MAX : (int)capacity,
            NULL,
            NULL);
        if (byte_length <= 0) {
            return 0u;
        }
        return (size_t)byte_length - 1u;
    }
#elif defined(__APPLE__) || defined(__unix__)
    base = getenv("XDG_DATA_HOME");
    if (base != NULL && base[0] != '\0') {
        written = snprintf((char *)buffer, capacity, "%s/vitte/modules", base);
    } else {
        base = getenv("HOME");
        if (base == NULL || base[0] == '\0') {
            return 0u;
        }
        written = snprintf((char *)buffer, capacity,
                           "%s/.local/share/vitte/modules", base);
    }
    if (written < 0 || (size_t)written >= capacity) {
        return 0u;
    }
    return (size_t)written;
#else
    (void)base;
    (void)written;
    return 0u;
#endif
}

/* Create and reserve a unique native temporary path. `kind` is 0 for source,
 * 1 for object, and 2 for an executable. The returned length excludes NUL. */
size_t
vitte_native_host_temp_path(
    uint32_t kind,
    uint8_t *buffer,
    size_t capacity)
{
    if (buffer == NULL || capacity == 0u || kind > 2u) {
        return 0u;
    }
#if defined(_WIN32)
    {
        wchar_t directory[MAX_PATH];
        wchar_t path[MAX_PATH];
        DWORD directory_length;
        int byte_length;

        directory_length = GetTempPathW(MAX_PATH, directory);
        if (directory_length == 0u || directory_length >= MAX_PATH ||
            GetTempFileNameW(directory, L"vit", 0u, path) == 0u) {
            return 0u;
        }
        if (kind == 2u) {
            const size_t path_length = wcslen(path);
            wchar_t executable_path[MAX_PATH];
            if (path_length < 4u ||
                wcscmp(path + path_length - 4u, L".tmp") != 0) {
                (void)DeleteFileW(path);
                return 0u;
            }
            memcpy(executable_path, path, (path_length + 1u) * sizeof(wchar_t));
            executable_path[path_length - 3u] = L'e';
            executable_path[path_length - 2u] = L'x';
            executable_path[path_length - 1u] = L'e';
            if (!MoveFileW(path, executable_path)) {
                (void)DeleteFileW(path);
                return 0u;
            }
            memcpy(path, executable_path, (path_length + 1u) * sizeof(wchar_t));
        }
        byte_length = WideCharToMultiByte(
            CP_UTF8,
            WC_ERR_INVALID_CHARS,
            path,
            -1,
            (char *)buffer,
            capacity > (size_t)INT_MAX ? INT_MAX : (int)capacity,
            NULL,
            NULL);
        if (byte_length <= 0) {
            (void)DeleteFileW(path);
            return 0u;
        }
        return (size_t)byte_length - 1u;
    }
#elif defined(__APPLE__) || defined(__unix__)
    {
        const char *temporary_directory = getenv("TMPDIR");
        int written;
        int descriptor;
        if (temporary_directory == NULL || temporary_directory[0] == '\0') {
            temporary_directory = "/tmp";
        }
        written = snprintf(
            (char *)buffer,
            capacity,
            "%s/%sXXXXXX",
            temporary_directory,
            kind == 0u ? "vitte-source-" :
                (kind == 1u ? "vitte-object-" : "vitte-exec-"));
        if (written < 0 || (size_t)written >= capacity) {
            return 0u;
        }
        descriptor = mkstemp((char *)buffer);
        if (descriptor < 0) {
            return 0u;
        }
        if (close(descriptor) != 0) {
            (void)unlink((char *)buffer);
            return 0u;
        }
        return (size_t)written;
    }
#else
    (void)kind;
    return 0u;
#endif
}

/* Vitte provides counted arguments; this adapter adds the terminator required
 * by the native process API. */
int32_t
vitte_native_host_process_run(
    const uint8_t *program,
    const uint8_t *const *arguments,
    size_t argument_count)
{
    const char **terminated_arguments;
    size_t index;
    bool succeeded;

    if (program == NULL || (arguments == NULL && argument_count != 0u) ||
        argument_count > SIZE_MAX / sizeof(*terminated_arguments) - 2u) {
        return -1;
    }
    terminated_arguments = (const char **)malloc(
        (argument_count + 2u) * sizeof(*terminated_arguments));
    if (terminated_arguments == NULL) {
        return -1;
    }
    terminated_arguments[0] = (const char *)program;
    for (index = 0u; index < argument_count; ++index) {
        terminated_arguments[index + 1u] = (const char *)arguments[index];
    }
    terminated_arguments[argument_count + 1u] = NULL;
    succeeded = vitte_platform_run(
        (const char *const *)terminated_arguments,
        true);
    free(terminated_arguments);
    return succeeded ? 1 : 0;
}

#endif
