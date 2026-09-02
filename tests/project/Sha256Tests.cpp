#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>
#include <stdexcept>

#include "core/project/Sha256.hpp"

TEST_CASE("SHA-256 matches standard vectors and exact bounded file bytes") {
    using holobench::project::sha256FileHex;
    using holobench::project::sha256Hex;

    CHECK(sha256Hex("")
        == "e3b0c44298fc1c149afbf4c8996fb924"
           "27ae41e4649b934ca495991b7852b855");
    CHECK(sha256Hex("abc")
        == "ba7816bf8f01cfea414140de5dae2223"
           "b00361a396177a9cb410ff61f20015ad");
    CHECK(sha256Hex(
        "abcdbcdecdefdefgefghfghighijhijk"
        "ijkljklmklmnlmnomnopnopq")
        == "248d6a61d20638b8e5c026930c3e6039"
           "a33ce45964ff2167f6ecedd419db06c1");

    const auto path = std::filesystem::temp_directory_path()
        / "holobench_sha256_exact_file_test.bin";
    {
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        REQUIRE(output.good());
        output.write("abc", 3);
        REQUIRE(output.good());
    }
    CHECK(sha256FileHex(path)
        == "ba7816bf8f01cfea414140de5dae2223"
           "b00361a396177a9cb410ff61f20015ad");
    CHECK_THROWS_WITH_AS(
        static_cast<void>(sha256FileHex(path, 2U)),
        doctest::Contains("exceeds the configured size bound"),
        std::invalid_argument);
    std::error_code removeError;
    CHECK(std::filesystem::remove(path, removeError));
    CHECK_FALSE(removeError);
}
