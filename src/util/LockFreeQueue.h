#pragma once

#include <atomic>
#include <vector>
#include <optional>

namespace OrchFaust {

template <typename T, size_t Capacity = 1024>
class LockFreeSPSCQueue {
public:
    LockFreeSPSCQueue() : write_idx(0), read_idx(0) {
        buffer.resize(Capacity);
    }

    bool push(const T& item) {
        size_t current_write = write_idx.load(std::memory_order_relaxed);
        size_t current_read = read_idx.load(std::memory_order_acquire);

        if ((current_write + 1) % Capacity == current_read) {
            // Queue is full
            return false;
        }

        buffer[current_write] = item;
        write_idx.store((current_write + 1) % Capacity, std::memory_order_release);
        return true;
    }

    bool push(T&& item) {
        size_t current_write = write_idx.load(std::memory_order_relaxed);
        size_t current_read = read_idx.load(std::memory_order_acquire);

        if ((current_write + 1) % Capacity == current_read) {
            // Queue is full
            return false;
        }

        buffer[current_write] = std::move(item);
        write_idx.store((current_write + 1) % Capacity, std::memory_order_release);
        return true;
    }

    std::optional<T> pop() {
        size_t current_read = read_idx.load(std::memory_order_relaxed);
        size_t current_write = write_idx.load(std::memory_order_acquire);

        if (current_read == current_write) {
            // Queue is empty
            return std::nullopt;
        }

        T item = std::move(buffer[current_read]);
        read_idx.store((current_read + 1) % Capacity, std::memory_order_release);
        return item;
    }

    bool empty() const {
        return read_idx.load(std::memory_order_relaxed) == write_idx.load(std::memory_order_relaxed);
    }

private:
    std::vector<T> buffer;
    std::atomic<size_t> write_idx;
    std::atomic<size_t> read_idx;
};

} // namespace OrchFaust
