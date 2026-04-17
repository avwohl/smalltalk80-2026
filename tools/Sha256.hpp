#pragma once
#include <array>
#include <cstdint>
#include <cstddef>
#include <string>

class Sha256 {
public:
    static std::array<uint8_t, 32> hash(const uint8_t* data, size_t len);
    static std::string hexDigest(const uint8_t* data, size_t len);

    Sha256();
    void update(const uint8_t* data, size_t len);
    std::array<uint8_t, 32> finalize();
    std::string hexFinalize();

private:
    uint32_t state_[8];
    uint8_t  block_[64];
    size_t   blockLen_;
    uint64_t totalLen_;

    void processBlock(const uint8_t* block);
};
