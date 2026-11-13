#include "platform.h"

#include <stddef.h>
#include <stdint.h>
#include <limits.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#include <direct.h>
#include <io.h>
#include <process.h>
#include <windows.h>
#include <wchar.h>
#ifndef EOVERFLOW
#define EOVERFLOW ERANGE
#endif
#else
#include <errno.h>
#include <spawn.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
extern char **environ;
#endif

#if defined(_WIN32)
static wchar_t *
vitte_platform_utf8_to_wide(
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

static void
vitte_platform_free_wide_arguments(
    wchar_t **arguments,
    size_t count)
{
    size_t index;

    if (arguments == NULL) {
        return;
    }
    for (index = 0u; index < count; ++index) {
        free(arguments[index]);
    }
    free(arguments);
}

static bool
vitte_platform_run_windows(
    const char *const arguments[],
    bool inherit_environment)
{
    wchar_t **wide_arguments;
    size_t count;
    size_t index;
    intptr_t result;

    count = 0u;
    while (arguments[count] != NULL) {
        if (count == SIZE_MAX - 1u) {
            errno = ERANGE;
            return false;
        }
        ++count;
    }
    if (count > SIZE_MAX / sizeof(*wide_arguments) - 1u) {
        errno = EOVERFLOW;
        return false;
    }
    wide_arguments = (wchar_t **)calloc(
        count + 1u,
        sizeof(*wide_arguments));
    if (wide_arguments == NULL) {
        errno = ENOMEM;
        return false;
    }
    for (index = 0u; index < count; ++index) {
        wide_arguments[index] = vitte_platform_utf8_to_wide(arguments[index]);
        if (wide_arguments[index] == NULL) {
            vitte_platform_free_wide_arguments(wide_arguments, count);
            return false;
        }
    }
    if (inherit_environment) {
        result = _wspawnvp(
            _P_WAIT,
            wide_arguments[0],
            (const wchar_t *const *)wide_arguments);
    } else {
        wchar_t *empty_environment[] = {NULL};
        result = _wspawnve(
            _P_WAIT,
            wide_arguments[0],
            (const wchar_t *const *)wide_arguments,
            (const wchar_t *const *)empty_environment);
    }
    vitte_platform_free_wide_arguments(wide_arguments, count);
    return result == 0;
}
#endif

bool
vitte_platform_getcwd(
    char *buffer,
    size_t capacity)
{
    if (buffer == NULL || capacity == 0u) {
        return false;
    }
#if defined(_WIN32)
    {
        DWORD required;
        DWORD written;
        wchar_t *wide_buffer;
        int utf8_size;

        required = GetCurrentDirectoryW(0u, NULL);
        if (required == 0u || required == (DWORD)-1) {
            return false;
        }
#if SIZE_MAX <= UINT32_MAX
        if ((size_t)required > SIZE_MAX / sizeof(*wide_buffer) - 1u) {
            return false;
        }
#endif
        wide_buffer = (wchar_t *)malloc(
            ((size_t)required + 1u) * sizeof(*wide_buffer));
        if (wide_buffer == NULL) {
            return false;
        }
        written = GetCurrentDirectoryW(required + 1u, wide_buffer);
        if (written == 0u || written >= required + 1u) {
            free(wide_buffer);
            return false;
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
        return utf8_size > 0;
    }
#else
    return getcwd(buffer, capacity) != NULL;
#endif
}

bool
vitte_platform_is_executable(
    const char *path)
{
    if (path == NULL || path[0] == '\0') {
        return false;
    }
#if defined(_WIN32)
    {
        wchar_t *wide_path = vitte_platform_utf8_to_wide(path);
        int result;
        if (wide_path == NULL) {
            return false;
        }
        result = _waccess(wide_path, 0);
        free(wide_path);
        return result == 0;
    }
#else
    return access(path, X_OK) == 0;
#endif
}

char
vitte_platform_path_list_separator(void)
{
#if defined(_WIN32)
    return ';';
#else
    return ':';
#endif
}

bool
vitte_platform_run(
    const char *const arguments[],
    bool inherit_environment)
{
    if (arguments == NULL || arguments[0] == NULL) {
        return false;
    }
#if defined(_WIN32)
    return vitte_platform_run_windows(arguments, inherit_environment);
#else
    {
        pid_t process;
        int status;
        int result;
        pid_t waited;
        char *empty_environment[] = {NULL};
        char *const *environment = inherit_environment
            ? environ
            : empty_environment;

        result = posix_spawnp(
            &process,
            arguments[0],
            NULL,
            NULL,
            (char *const *)arguments,
            environment);
        if (result != 0) {
            return false;
        }
        do {
            waited = waitpid(process, &status, 0);
        } while (waited < 0 && errno == EINTR);
        return waited == process && WIFEXITED(status) &&
               WEXITSTATUS(status) == 0;
    }
#endif
}
