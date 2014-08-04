#pragma once

#include <cstdarg>
#include <cstdio>

void diagnostic(const char* format, ...) {
    va_list args;
    va_start(args, format);

    vprintf(format, args);
    printf("\n");

    va_end(args);
}
