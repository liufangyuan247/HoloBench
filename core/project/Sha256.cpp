#include "core/project/Sha256.hpp"

#include <array>
#include <bit>
#include <fstream>
#include <limits>
#include <stdexcept>

namespace holobench::project {
namespace {

constexpr std::array<std::uint32_t, 64> kRoundConstants {{
    0x428a2f98U, 0x71374491U, 0xb5c0fbcfU, 0xe9b5dba5U,
    0x3956c25bU, 0x59f111f1U, 0x923f82a4U, 0xab1c5ed5U,
    0xd807aa98U, 0x12835b01U, 0x243185beU, 0x550c7dc3U,
    0x72be5d74U, 0x80deb1feU, 0x9bdc06a7U, 0xc19bf174U,
    0xe49b69c1U, 0xefbe4786U, 0x0fc19dc6U, 0x240ca1ccU,
    0x2de92c6fU, 0x4a7484aaU, 0x5cb0a9dcU, 0x76f988daU,
    0x983e5152U, 0xa831c66dU, 0xb00327c8U, 0xbf597fc7U,
    0xc6e00bf3U, 0xd5a79147U, 0x06ca6351U, 0x14292967U,
    0x27b70a85U, 0x2e1b2138U, 0x4d2c6dfcU, 0x53380d13U,
    0x650a7354U, 0x766a0abbU, 0x81c2c92eU, 0x92722c85U,
    0xa2bfe8a1U, 0xa81a664bU, 0xc24b8b70U, 0xc76c51a3U,
    0xd192e819U, 0xd6990624U, 0xf40e3585U, 0x106aa070U,
    0x19a4c116U, 0x1e376c08U, 0x2748774cU, 0x34b0bcb5U,
    0x391c0cb3U, 0x4ed8aa4aU, 0x5b9cca4fU, 0x682e6ff3U,
    0x748f82eeU, 0x78a5636fU, 0x84c87814U, 0x8cc70208U,
    0x90befffaU, 0xa4506cebU, 0xbef9a3f7U, 0xc67178f2U,
}};

class Sha256 final {
public:
    void update(std::span<const std::byte> bytes) {
        if (bytes.size() > static_cast<std::size_t>(
                std::numeric_limits<std::uint64_t>::max() - totalBytes_)) {
            throw std::overflow_error("SHA-256 input length is not representable");
        }
        totalBytes_ += static_cast<std::uint64_t>(bytes.size());
        for (const std::byte value : bytes) {
            buffer_[bufferSize_++] = std::to_integer<std::uint8_t>(value);
            if (bufferSize_ == buffer_.size()) {
                transform(buffer_);
                bufferSize_ = 0U;
            }
        }
    }

    [[nodiscard]] std::array<std::uint8_t, 32> finish() {
        if (totalBytes_ > std::numeric_limits<std::uint64_t>::max() / 8U) {
            throw std::overflow_error("SHA-256 bit length is not representable");
        }
        const std::uint64_t totalBits = totalBytes_ * 8U;
        buffer_[bufferSize_++] = 0x80U;
        if (bufferSize_ > 56U) {
            while (bufferSize_ < buffer_.size()) buffer_[bufferSize_++] = 0U;
            transform(buffer_);
            bufferSize_ = 0U;
        }
        while (bufferSize_ < 56U) buffer_[bufferSize_++] = 0U;
        for (std::size_t index = 0U; index < 8U; ++index) {
            const unsigned shift = static_cast<unsigned>((7U - index) * 8U);
            buffer_[56U + index] = static_cast<std::uint8_t>(
                (totalBits >> shift) & 0xffU);
        }
        transform(buffer_);

        std::array<std::uint8_t, 32> digest {};
        for (std::size_t word = 0U; word < state_.size(); ++word) {
            for (std::size_t byte = 0U; byte < 4U; ++byte) {
                const unsigned shift
                    = static_cast<unsigned>((3U - byte) * 8U);
                digest[word * 4U + byte] = static_cast<std::uint8_t>(
                    (state_[word] >> shift) & 0xffU);
            }
        }
        return digest;
    }

private:
    void transform(const std::array<std::uint8_t, 64>& block) {
        std::array<std::uint32_t, 64> words {};
        for (std::size_t index = 0U; index < 16U; ++index) {
            const std::size_t offset = index * 4U;
            words[index]
                = (static_cast<std::uint32_t>(block[offset]) << 24U)
                | (static_cast<std::uint32_t>(block[offset + 1U]) << 16U)
                | (static_cast<std::uint32_t>(block[offset + 2U]) << 8U)
                | static_cast<std::uint32_t>(block[offset + 3U]);
        }
        for (std::size_t index = 16U; index < words.size(); ++index) {
            const std::uint32_t value15 = words[index - 15U];
            const std::uint32_t value2 = words[index - 2U];
            const std::uint32_t sigma0 = std::rotr(value15, 7)
                ^ std::rotr(value15, 18) ^ (value15 >> 3U);
            const std::uint32_t sigma1 = std::rotr(value2, 17)
                ^ std::rotr(value2, 19) ^ (value2 >> 10U);
            words[index] = words[index - 16U] + sigma0
                + words[index - 7U] + sigma1;
        }

        std::uint32_t a = state_[0];
        std::uint32_t b = state_[1];
        std::uint32_t c = state_[2];
        std::uint32_t d = state_[3];
        std::uint32_t e = state_[4];
        std::uint32_t f = state_[5];
        std::uint32_t g = state_[6];
        std::uint32_t h = state_[7];
        for (std::size_t index = 0U; index < words.size(); ++index) {
            const std::uint32_t sum1 = std::rotr(e, 6)
                ^ std::rotr(e, 11) ^ std::rotr(e, 25);
            const std::uint32_t choice = (e & f) ^ ((~e) & g);
            const std::uint32_t temporary1 = h + sum1 + choice
                + kRoundConstants[index] + words[index];
            const std::uint32_t sum0 = std::rotr(a, 2)
                ^ std::rotr(a, 13) ^ std::rotr(a, 22);
            const std::uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
            const std::uint32_t temporary2 = sum0 + majority;
            h = g;
            g = f;
            f = e;
            e = d + temporary1;
            d = c;
            c = b;
            b = a;
            a = temporary1 + temporary2;
        }
        state_[0] += a;
        state_[1] += b;
        state_[2] += c;
        state_[3] += d;
        state_[4] += e;
        state_[5] += f;
        state_[6] += g;
        state_[7] += h;
    }

    std::array<std::uint32_t, 8> state_ {{
        0x6a09e667U,
        0xbb67ae85U,
        0x3c6ef372U,
        0xa54ff53aU,
        0x510e527fU,
        0x9b05688cU,
        0x1f83d9abU,
        0x5be0cd19U,
    }};
    std::array<std::uint8_t, 64> buffer_ {};
    std::size_t bufferSize_ = 0U;
    std::uint64_t totalBytes_ = 0U;
};

std::string toHex(const std::array<std::uint8_t, 32>& digest) {
    constexpr char kHexDigits[] = "0123456789abcdef";
    std::string result;
    result.resize(digest.size() * 2U);
    for (std::size_t index = 0U; index < digest.size(); ++index) {
        result[index * 2U] = kHexDigits[digest[index] >> 4U];
        result[index * 2U + 1U] = kHexDigits[digest[index] & 0x0fU];
    }
    return result;
}

} // namespace

std::string sha256Hex(std::span<const std::byte> bytes) {
    Sha256 hash;
    hash.update(bytes);
    return toHex(hash.finish());
}

std::string sha256Hex(std::string_view text) {
    return sha256Hex(std::as_bytes(std::span(text.data(), text.size())));
}

std::string sha256FileHex(
    const std::filesystem::path& path,
    std::uintmax_t maximumBytes) {
    if (maximumBytes == 0U) {
        throw std::invalid_argument("SHA-256 file size bound must be positive");
    }
    std::error_code sizeError;
    const std::uintmax_t size = std::filesystem::file_size(path, sizeError);
    if (sizeError) {
        throw std::runtime_error(
            "failed to inspect SHA-256 input file: " + path.string());
    }
    if (size > maximumBytes) {
        throw std::invalid_argument(
            "SHA-256 input file exceeds the configured size bound");
    }
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error(
            "failed to open SHA-256 input file: " + path.string());
    }
    Sha256 hash;
    std::array<char, 64U * 1024U> buffer {};
    std::uintmax_t hashedBytes = 0U;
    while (input) {
        input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        const std::streamsize count = input.gcount();
        if (count > 0) {
            const auto chunkBytes = static_cast<std::uintmax_t>(count);
            if (chunkBytes > maximumBytes - hashedBytes) {
                throw std::invalid_argument(
                    "SHA-256 input file exceeds the configured size bound");
            }
            hashedBytes += chunkBytes;
            hash.update(std::as_bytes(std::span(
                buffer.data(), static_cast<std::size_t>(count))));
        }
    }
    if (!input.eof()) {
        throw std::runtime_error(
            "failed while reading SHA-256 input file: " + path.string());
    }
    return toHex(hash.finish());
}

} // namespace holobench::project
