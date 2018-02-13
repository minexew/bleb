#include "catch.hpp"

#include <bleb/byteio_vector.hpp>
#include <bleb/repository.hpp>

TEST_CASE("Repository can be initialized") {
    bleb::VectorByteIO vbio(1000, false);
    bleb::Repository repo(&vbio);

    REQUIRE(repo.open(true));
    repo.close();

    REQUIRE(vbio.getSize() > 0);
}

TEST_CASE("Repository Content Directory Stream initialization fails gracefully") {
    // Size large enough to fit repository header, but too small for the initial stream allocation
    // => 16 (RepositoryPrologue_t::SIZE) + 16 (StreamDescriptor_t::SIZE) = 32
    bleb::VectorByteIO vbio(32, false);
    bleb::Repository repo(&vbio);

    REQUIRE(!repo.open(true));
    REQUIRE(repo.getErrorKind());
}

TEST_CASE("Object can be stored and retrieved") {
    bleb::VectorByteIO vbio(1000, false);
    bleb::Repository repo(&vbio);

    REQUIRE(repo.open(true));

    const uint8_t testData[] = u8"Hello, World";

    repo.setObjectContents("message", testData, sizeof(testData), bleb::kPreferInlinePayload);

    uint8_t* contents = nullptr;
    size_t size;
    repo.getObjectContents("message", contents, size);
    REQUIRE(contents != nullptr);

    REQUIRE(size == sizeof(testData));
    REQUIRE(memcmp(contents, testData, size) == 0);
    free(contents);
}
