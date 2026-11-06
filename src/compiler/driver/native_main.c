/* Minimal process entry trampoline. Command dispatch lives in Vitte. */
#include <stdint.h>

extern int32_t
vitte_api_process_entry(
    int32_t argc,
    const char *const argv[]);

int
main(
    int argc,
    char **argv)
{
    return (int)vitte_api_process_entry(
        (int32_t)argc,
        (const char *const *)argv);
}
