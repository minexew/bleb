
#include "internal.hpp"

#include <bleb/repository.hpp>

#include <cstdarg>
#include <cstdio>
#include <cstring>

namespace bleb {
void diagnostic(const char* format, ...) {
#ifdef _DEBUG
    va_list args;
    va_start(args, format);

    fprintf(stderr, "bleb diagnostic: ");
    vfprintf(stderr, format, args);
    fprintf(stderr, "\n");

    va_end(args);
#endif
}

void ErrorStruct_::operator()(ErrorKind kind, const char* desc) {
    free(errorDesc);

    errorKind = kind;
    errorDesc = (char*)malloc(strlen(desc) + 1);
    strcpy(errorDesc, desc);
}

void ErrorStruct_::readError() {
    (*this)(errReadFailed, "failed to read data");
}

void ErrorStruct_::repositoryCorruption(const char* hint) {
    diagnostic("warning: repository corruption: %s", hint);
    (*this)(errRepositoryCorruption, "repository file is corrupted");
}

void ErrorStruct_::unexpectedEndOfStream() {
    diagnostic("warning: trying to seek past allocated stream end; repository corruption?");
    (*this)(errRepositoryCorruption, "unexpected end of stream");
}

void ErrorStruct_::writeError() {
    (*this)(errWriteFailed, "failed to write data");
}
}
