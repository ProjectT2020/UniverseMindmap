#include "os_specific.h"

#include <limits.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>

#if defined(__APPLE__)
    #include <mach-o/dyld.h>
    #include <stdlib.h>
#endif

const char *os_get_executable_path(void) {
    static char cached_executable_path[PATH_MAX];
    static int path_initialized = 0;

    if (path_initialized) {
        return cached_executable_path;
    }

#if defined(__linux__)
    ssize_t len = readlink("/proc/self/exe", cached_executable_path, sizeof(cached_executable_path) - 1);
    if (len < 0 || (size_t)len >= sizeof(cached_executable_path)) {
        return NULL;
    }
    cached_executable_path[len] = '\0';
#elif defined(__APPLE__)
    uint32_t size = (uint32_t)sizeof(cached_executable_path);
    if (_NSGetExecutablePath(cached_executable_path, &size) != 0) {
        return NULL;
    }
    cached_executable_path[sizeof(cached_executable_path) - 1] = '\0';

    char resolved[PATH_MAX];
    if (realpath(cached_executable_path, resolved) != NULL) {
        strncpy(cached_executable_path, resolved, sizeof(cached_executable_path));
        cached_executable_path[sizeof(cached_executable_path) - 1] = '\0';
    }
#else
    return NULL;
#endif

    path_initialized = 1;
    return cached_executable_path;
}
