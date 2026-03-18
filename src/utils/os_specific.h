#ifndef OS_SPECIFIC_H
#define OS_SPECIFIC_H

#pragma once

// Returns executable path as an internal cached string, or NULL on failure.
const char *os_get_executable_path(void);

#endif // OS_SPECIFIC_H
