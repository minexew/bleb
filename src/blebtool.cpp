#include <bleb/byteio_stdio.hpp>
#include <bleb/repository.hpp>

#include "args.hxx"
#include <iostream>

using std::unique_ptr;

unique_ptr<bleb::Repository> open(const std::string& path, bool canCreateNew) {
    FILE* f = bleb::StdioFileByteIO::getFile(path.c_str(), canCreateNew);

    if (!f) {
        fprintf(stderr, "blebtool: failed to open file '%s'\n", path.c_str());
        return nullptr;
    }

    auto io = std::make_unique<bleb::StdioFileByteIO>(f, true);
    auto repo = std::make_unique<bleb::Repository>(io.get());
    repo->setOwnedIO(std::move(io));        // tie ByteIO lifetime to Repository lifetime

    if (!repo->open(canCreateNew)) {
        fprintf(stderr, "blebtool: failed to open repository in file '%s'\n", path.c_str());
        return nullptr;
    }

    return std::move(repo);
}

int executeGetCommand(std::string objectName, std::string repository, std::string outputFile) {
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

int executeMergeCommand(std::string inputRepository, std::string repository, std::string prefix) {
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

int executePutCommand(std::string objectName, std::string repository, std::string inputFile, std::string text,
                      bool noInline) {
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

int main(int argc, const char **argv)
{
    args::ArgumentParser p("blebtool");
    args::Group commands(p, "commands");

    args::Command get(commands, "get", "retrieve an object from the repository", [&](args::Subparser &parser) {
        args::Positional<std::string> objectName(parser, "objectName", "name of the object to retrieve");
        args::ValueFlag<std::string> repository(parser, "repository", "filename of the repository", {'R'});
        args::ValueFlag<std::string> outputFile(parser, "outputFile", "path to the output file (defaults to stdout)", {'o'});
        parser.Parse();

        return executeGetCommand(objectName.Get(), repository.Get(), outputFile.Get());
    });

    args::Command merge(commands, "merge", "merge the contents of one repository into another", [&](args::Subparser &parser) {
        args::Positional<std::string> sourceRepository(parser, "sourceRepository", "filename of the repository to merge from");
        args::ValueFlag<std::string> repository(parser, "destRepository", "filename of the repository to merge into", {'R'});
        args::ValueFlag<std::string> prefix(parser, "prefix", "prefix for copied objects in the destination repository", {'p'});
        parser.Parse();

        return executeMergeCommand(sourceRepository.Get(), repository.Get(), prefix.Get());
    });

    args::Command put(commands, "put", "create or replace an object in the repository", [&](args::Subparser &parser) {
        args::Positional<std::string> objectName(parser, "objectName", "name of the object to create or replace");
        args::ValueFlag<std::string> repository(parser, "repository", "filename of the repository", {'R'});
        args::ValueFlag<std::string> inputFile(parser, "inputFile", "path to the input file (defaults to stdin)", {'i'});
        args::ValueFlag<std::string> text(parser, "text", "directly specifies the data to store", {'T'});
        args::Flag noInline(parser, "noInline", "do not use an Inline Payload for this object", {"no-inline"});
        parser.Parse();

        return executePutCommand(objectName.Get(), repository.Get(), inputFile.Get(), text.Get(), noInline);
    });

    args::Group arguments("arguments");
    args::GlobalOptions globals(p, arguments);

    try
    {
        p.ParseCLI(argc, argv);
    }
    catch (args::Help)
    {
        std::cout << p;
    }
    catch (args::Error& e)
    {
        std::cerr << e.what() << std::endl << p;
        return 1;
    }

    return 0;
}
