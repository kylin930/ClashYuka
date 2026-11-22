#include "clashyuka/runtime/sha256.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>

namespace clashyuka::runtime {
namespace {

constexpr std::uint32_t kRoundConstants[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1,
    0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
    0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786,
    0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147,
    0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
    0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b,
    0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a,
    0x5b9cca4f, 0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
    0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
};

std::uint32_t RotateRight(std::uint32_t value, int bits) {
    return (value >> bits) | (value << (32 - bits));
}

class Sha256Context {
public:
    Sha256Context()
        : state_{0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f,
                 0x9b05688c, 0x1f83d9ab, 0x5be0cd19} {}

    void Update(const unsigned char* data, std::size_t length) {
        total_ += length;
        while (length > 0) {
            const std::size_t copy = std::min(length, buffer_.size() - bufferLength_);
            std::memcpy(buffer_.data() + bufferLength_, data, copy);
            bufferLength_ += copy;
            data += copy;
            length -= copy;
            if (bufferLength_ == buffer_.size()) {
                Transform(buffer_.data());
                bufferLength_ = 0;
            }
        }
    }

    std::string Final() {
        const std::uint64_t totalBits = total_ * 8;
        buffer_[bufferLength_++] = 0x80;
        if (bufferLength_ > 56) {
            std::memset(buffer_.data() + bufferLength_, 0, buffer_.size() - bufferLength_);
            Transform(buffer_.data());
            bufferLength_ = 0;
        }
        std::memset(buffer_.data() + bufferLength_, 0, 56 - bufferLength_);
        for (int i = 0; i < 8; ++i) {
            buffer_[56 + i] = static_cast<unsigned char>(totalBits >> (56 - i * 8));
        }
        Transform(buffer_.data());

        static constexpr char kHex[] = "0123456789abcdef";
        std::string output;
        output.reserve(64);
        for (const std::uint32_t word : state_) {
            for (int shift = 28; shift >= 0; shift -= 4) {
                output.push_back(kHex[(word >> shift) & 0x0F]);
            }
        }
        return output;
    }

private:
    void Transform(const unsigned char* block) {
        std::uint32_t schedule[64];
        for (int i = 0; i < 16; ++i) {
            schedule[i] = (static_cast<std::uint32_t>(block[i * 4]) << 24) |
                          (static_cast<std::uint32_t>(block[i * 4 + 1]) << 16) |
                          (static_cast<std::uint32_t>(block[i * 4 + 2]) << 8) |
                          static_cast<std::uint32_t>(block[i * 4 + 3]);
        }
        for (int i = 16; i < 64; ++i) {
            const std::uint32_t s0 = RotateRight(schedule[i - 15], 7) ^
                                     RotateRight(schedule[i - 15], 18) ^
                                     (schedule[i - 15] >> 3);
            const std::uint32_t s1 = RotateRight(schedule[i - 2], 17) ^
                                     RotateRight(schedule[i - 2], 19) ^
                                     (schedule[i - 2] >> 10);
            schedule[i] = schedule[i - 16] + s0 + schedule[i - 7] + s1;
        }

        std::uint32_t a = state_[0];
        std::uint32_t b = state_[1];
        std::uint32_t c = state_[2];
        std::uint32_t d = state_[3];
        std::uint32_t e = state_[4];
        std::uint32_t f = state_[5];
        std::uint32_t g = state_[6];
        std::uint32_t h = state_[7];
        for (int i = 0; i < 64; ++i) {
            const std::uint32_t s1 = RotateRight(e, 6) ^ RotateRight(e, 11) ^ RotateRight(e, 25);
            const std::uint32_t choice = (e & f) ^ (~e & g);
            const std::uint32_t temp1 = h + s1 + choice + kRoundConstants[i] + schedule[i];
            const std::uint32_t s0 = RotateRight(a, 2) ^ RotateRight(a, 13) ^ RotateRight(a, 22);
            const std::uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
            const std::uint32_t temp2 = s0 + majority;
            h = g;
            g = f;
            f = e;
            e = d + temp1;
            d = c;
            c = b;
            b = a;
            a = temp1 + temp2;
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

    std::array<std::uint32_t, 8> state_;
    std::array<unsigned char, 64> buffer_{};
    std::size_t bufferLength_ = 0;
    std::uint64_t total_ = 0;
};

}  // namespace

std::string Sha256Hex(std::string_view data) {
    Sha256Context context;
    context.Update(reinterpret_cast<const unsigned char*>(data.data()), data.size());
    return context.Final();
}

std::string Sha256File(const std::string& path, std::string* error) {
    const std::filesystem::path file(std::u8string(path.begin(), path.end()));
    std::ifstream input(file, std::ios::binary);
    if (!input) {
        if (error != nullptr) {
            *error = "could not open the file for hashing";
        }
        return {};
    }

    Sha256Context context;
    std::array<char, 64 * 1024> buffer{};
    while (input) {
        input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        const std::streamsize count = input.gcount();
        if (count > 0) {
            context.Update(reinterpret_cast<const unsigned char*>(buffer.data()),
                           static_cast<std::size_t>(count));
        }
    }
    if (input.bad()) {
        if (error != nullptr) {
            *error = "could not read the file for hashing";
        }
        return {};
    }
    return context.Final();
}

}  // namespace clashyuka::runtime
