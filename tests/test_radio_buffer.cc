#include "radio_buffer.h"
#include <cassert>
#include <vector>
#include <random>
#include <deque>
#include <thread>
int main() {
    uint8_t storage[128] = {}, source[67], result[73];
    RadioBuffer buffer(storage, sizeof(storage));
    std::deque<uint8_t> expected;
    std::mt19937 random(11);
    uint32_t sequence = 0;
    // Vary chunk sizes, repeatedly fill, drain and wrap the real byte buffer.
    for (int i = 0; i < 30000; ++i) {
        if (random() % 2) {
            const uint32_t size = random() % sizeof(source);
            for (uint32_t j = 0; j < size; ++j) source[j] = (sequence + j) % 251;
            const auto accepted = buffer.push(source, size);
            for (uint32_t j = 0; j < accepted; ++j) expected.push_back(source[j]);
            sequence += accepted;
        } else {
            const auto read = buffer.pop(result, random() % sizeof(result));
            assert(read <= expected.size());
            for (uint32_t j = 0; j < read; ++j) {
                assert(result[j] == expected.front()); expected.pop_front();
            }
        }
        assert(buffer.available() == expected.size());
    }
    while (!expected.empty()) {
        const auto read = buffer.pop(result, sizeof(result));
        assert(read);
        for (uint32_t j = 0; j < read; ++j) {
            assert(result[j] == expected.front()); expected.pop_front();
        }
    }
    assert(buffer.pop(result, sizeof(result)) == 0);
    constexpr uint32_t total = 400000;
    std::thread producer([&] {
        uint32_t position = 0;
        uint8_t block[57];
        while (position < total) {
            const auto size = std::min<uint32_t>(sizeof(block), total - position);
            for (uint32_t j = 0; j < size; ++j) block[j] = (position + j) % 251;
            position += buffer.push(block, size);
            std::this_thread::yield();
        }
    });
    uint32_t position = 0;
    while (position < total) {
        const auto size = buffer.pop(result, sizeof(result));
        for (uint32_t j = 0; j < size; ++j) assert(result[j] == (position + j) % 251);
        position += size;
        std::this_thread::yield();
    }
    producer.join();
    assert(buffer.available() == 0);
}
