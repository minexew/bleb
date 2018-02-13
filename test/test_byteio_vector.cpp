#include "catch.hpp"

#include <bleb/byteio_vector.hpp>

TEST_CASE("VectorByteIO can be written to and read from", "[VectorByteIO]") {
    bleb::VectorByteIO vbio(100, true);

    const uint8_t testData[] = u8"Hello, World";
    uint8_t readBuffer[sizeof(testData)];

    REQUIRE(vbio.getSize() == 0);
    REQUIRE(vbio.setBytesAt(0, testData, sizeof(testData)) == true);
    REQUIRE(vbio.getBytesAt(0, readBuffer, sizeof(readBuffer)) == true);
    REQUIRE(memcmp(testData, readBuffer, sizeof(testData)) == 0);
    REQUIRE(vbio.getSize() == sizeof(testData));
}

TEST_CASE("VectorByteIO doesn't expand if not allowed to", "[VectorByteIO]") {
    bleb::VectorByteIO vbio(5, false);

    REQUIRE(vbio.clearBytesAt(0, 4) == true);
    REQUIRE(vbio.clearBytesAt(0, 6) == false);
    REQUIRE(vbio.getSize() == 4);
}

TEST_CASE("VectorByteIO can be filled up to the brim", "[VectorByteIO]") {
    bleb::VectorByteIO vbio(5, false);

    REQUIRE(vbio.clearBytesAt(0, 5) == true);
}

/*TEST_CASE("Repository can be initialized") {
    bleb::VectorByteIO vbio(300, false);
}*/
