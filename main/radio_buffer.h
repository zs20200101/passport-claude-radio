#pragma once
#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>

// One network producer and one audio consumer. Storage outlives both tasks.
// Monotonic unsigned cursors allow wraparound without a shared count/lock.
class RadioBuffer {
public:
    RadioBuffer(uint8_t *storage, uint32_t capacity) : data_(storage), capacity_(capacity) {}
    uint32_t available() const {
        return written_.load(std::memory_order_acquire) - read_.load(std::memory_order_acquire);
    }
    uint32_t push(const uint8_t *source, uint32_t size) {
        const auto written = written_.load(std::memory_order_relaxed);
        const auto used = written - read_.load(std::memory_order_acquire);
        size = std::min(size, capacity_ - used);
        copy_in(source, written % capacity_, size);
        written_.store(written + size, std::memory_order_release);
        return size;
    }
    uint32_t pop(uint8_t *destination, uint32_t size) {
        const auto read = read_.load(std::memory_order_relaxed);
        size = std::min(size, written_.load(std::memory_order_acquire) - read);
        const auto offset = read % capacity_;
        const auto first = std::min(size, capacity_ - offset);
        std::memcpy(destination, data_ + offset, first);
        std::memcpy(destination + first, data_, size - first);
        read_.store(read + size, std::memory_order_release);
        return size;
    }
private:
    void copy_in(const uint8_t *source, uint32_t offset, uint32_t size) {
        const auto first = std::min(size, capacity_ - offset);
        std::memcpy(data_ + offset, source, first);
        std::memcpy(data_, source + first, size - first);
    }
    uint8_t *data_;
    uint32_t capacity_;
    std::atomic<uint32_t> written_{0}, read_{0};
};
