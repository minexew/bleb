#include <bleb/byteio_stdio.hpp>
#include <bleb/repository.hpp>

#include <extras/argument_parsing.hpp>
#include <reflection/basic_types.hpp>

using String = std::string;

bleb::Repository* open(const String& path, bool canCreateNew) {
    FILE* f = bleb::StdioFileByteIO::getFile(path.c_str(), canCreateNew);

    if (!f) {
        fprintf(stderr, "blebtool: failed to open file '%s'\n", path.c_str());
        return nullptr;
    }

    auto io = new bleb::StdioFileByteIO(f, true);
    auto repo = new bleb::Repository(io, canCreateNew, true);

    if (!repo->open()) {
        fprintf(stderr, "blebtool: failed to open repository in file '%s'\n", path.c_str());
        delete repo;
        return nullptr;
    }

    return repo;
}

struct GetCommand {
    GetCommand() : show(false) {}

    String objectName;
    String repository;
    String outputFile;
    bool show;

    REFL_BEGIN("GetCommand", 1)
        ARG_REQUIRED(objectName,    "",     "name of the object to retrieve")
        ARG_REQUIRED(repository,    "-R",   "filename of the repository")
        //ARG(outputFile,             "-o",   "path to the output file (standard output if not specified)")
    REFL_END

    int execute() {
        auto repo = open(repository, false);

        if (repo == nullptr)
            return -1;

        uint8_t* contents;
        size_t length;

        repo->getObjectContents1(objectName.c_str(), contents, length);
        delete repo;

        fwrite(contents, 1, length, stdout);
        return 0;
    }
};

struct PutCommand {
    PutCommand() : size(0) {}

    String objectName;
    String repository;
    String inputFile;
    String text;
    int64_t size;

    REFL_BEGIN("PutCommand", 1)
        ARG_REQUIRED(objectName,    "",     "name of the object to create or replace")
        ARG_REQUIRED(repository,    "-R",   "filename of the repository")
        //ARG(inputFile,              "-i",   "path to the input file (standard input if not specified)")
        ARG(text,                   "-T",   "directly specifies the data to store")
    REFL_END

    int execute() {
        auto repo = open(repository, true);

        if (repo == nullptr)
            return -1;

        if (text.empty()) {
            uint8_t* buffer = nullptr;
            size_t used = 0;

            while (!feof(stdin)) {
                uint8_t in[4096];
                size_t got = fread(in, 1, sizeof(in), stdin);

                if (got) {
                    buffer = (uint8_t*) realloc(buffer, used + got);
                    memcpy(buffer + used, in, got);
                    used += got;
                }
            }

            repo->setObjectContents1(objectName.c_str(), buffer, used);
        }
        else
            repo->setObjectContents1(objectName.c_str(), text.c_str(), text.length());

        delete repo;

        return 0;
    }
};

using argument_parsing::Command_t;
using argument_parsing::execute;
using argument_parsing::help;

static const Command_t commands[] = {
    {"get",         "get the contents of an object in the repository", execute<GetCommand>, help<GetCommand>},
    {"put",         "set the contents of an object in the repository", execute<PutCommand>, help<PutCommand>},
    {}
};

int main(int argc, char* argv[]) {
    return argument_parsing::multiCommandDispatch(argc - 1, argv + 1, "blebtool", commands);
}

#include <reflection/default_error_handler.cpp>
