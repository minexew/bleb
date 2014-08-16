#pragma once

#include <cstdarg>
#include <cstdio>

void diagnostic(const char* format, ...) {
    va_list args;
    va_start(args, format);

    vfprintf(stderr, format, args);
    fprintf(stderr, "\n");

    va_end(args);
}
