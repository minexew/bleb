#include <bleb/byteio_stdio.hpp>
#include <bleb/repository.hpp>

#include <extras/argument_parsing.hpp>
#include <reflection/basic_types.hpp>

#include <memory>

using String = std::string;
using std::unique_ptr;

unique_ptr<bleb::Repository> open(const String& path, bool canCreateNew) {
    FILE* f = bleb::StdioFileByteIO::getFile(path.c_str(), canCreateNew);

    if (!f) {
        fprintf(stderr, "blebtool: failed to open file '%s'\n", path.c_str());
        return nullptr;
    }

    auto io = new bleb::StdioFileByteIO(f, true);
    unique_ptr<bleb::Repository> repo(new bleb::Repository(io, true));

    if (!repo->open(canCreateNew)) {
        fprintf(stderr, "blebtool: failed to open repository in file '%s'\n", path.c_str());
        return nullptr;
    }

    return std::move(repo);
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
        ARG(outputFile,             "-o",   "path to the output file (standard output if not specified)")
    REFL_END

    int execute() {
        auto repo = open(repository, false);

        if (repo == nullptr)
            return -1;

        FILE* output = outputFile.empty() ? stdout : fopen(outputFile.c_str(), "wb");

        if (!output) {
            fprintf(stderr, "blebtool: failed to open '%s' for ouput\n", outputFile.c_str());
            return -1;
        }

        uint8_t* contents;
        size_t length;

        repo->getObjectContents(objectName.c_str(), contents, length);
        repo.reset();

        fwrite(contents, 1, length, output);
        free(contents);

        if (output != stdout)
            fclose(output);
        else
            fflush(output);

        return 0;
    }
};

struct MergeCommand {
    MergeCommand() {}

    String inputRepository;
    String repository;
    String prefix;

    REFL_BEGIN("MergeCommand", 1)
        ARG_REQUIRED(inputRepository,   "",     "filename of the repository to merge from")
        ARG_REQUIRED(repository,        "-R",   "filename of the repository to merge into")
        ARG(prefix,                     "-p",   "prefix for copied objects in the destination repository")
    REFL_END

    int execute() {
        auto inputRepo = open(inputRepository, false);

        if (inputRepo == nullptr)
            return -1;

        auto repo = open(repository, true);

        if (repo == nullptr)
            return -1;

        for (auto entry : *inputRepo)
        {
            uint8_t* contents;
            size_t length;

            inputRepo->getObjectContents(entry, contents, length);
            repo->setObjectContents((prefix + entry).c_str(), contents, length, (length < 256) ? bleb::kPreferInlinePayload : 0);

            free(contents);
        }

        return 0;
    }
};

struct PutCommand {
    PutCommand() : noInline(false) {}

    String objectName;
    String repository;
    String inputFile;
    String text;
    bool noInline;

    REFL_BEGIN("PutCommand", 1)
        ARG_REQUIRED(objectName,    "",     "name of the object to create or replace")
        ARG_REQUIRED(repository,    "-R",   "filename of the repository")
        ARG(inputFile,              "-i",   "path to the input file (standard input if not specified)")
        ARG(text,                   "-T",   "directly specifies the data to store")
        ARG(noInline,               "--no-inline", "do not use an Inline Payload for this object")
    REFL_END

    int execute() {
        auto repo = open(repository, true);

        if (repo == nullptr)
            return -1;

        int flags = 0;

        if (!noInline)
            flags |= bleb::kPreferInlinePayload;

        if (!text.empty()) {
            repo->setObjectContents(objectName.c_str(), text.c_str(), text.length(), flags);
        }
        else {
            std::unique_ptr<bleb::ByteIO> stream(repo->openStream(objectName.c_str(),
                    bleb::kStreamCreate | bleb::kStreamTruncate));
            assert(stream);
            size_t pos = 0;

            FILE* input = inputFile.empty() ? stdin : fopen(inputFile.c_str(), "rb");

            if (!input) {
                fprintf(stderr, "blebtool: failed to open '%s' for input\n", inputFile.c_str());
                return -1;
            }

            while (!feof(input)) {
                uint8_t in[4096];
                size_t got = fread(in, 1, sizeof(in), input);

                if (got) {
                    stream->setBytesAt(pos, in, got);
                    pos += got;
                }
            }

            if (input != stdin)
                fclose(input);
        }

        repo.reset();

        return 0;
    }
};

using argument_parsing::Command_t;
using argument_parsing::execute;
using argument_parsing::help;

static const Command_t commands[] = {
    {"get",         "get the contents of an object in the repository", execute<GetCommand>, help<GetCommand>},
    {"merge",       "merge the contents of one repository into another", execute<MergeCommand>, help<MergeCommand>},
    {"put",         "set the contents of an object in the repository", execute<PutCommand>, help<PutCommand>},
    {}
};

int main(int argc, char* argv[]) {
    return argument_parsing::multiCommandDispatch(argc - 1, argv + 1, "blebtool", commands);
}

#include <reflection/default_error_handler.cpp>
