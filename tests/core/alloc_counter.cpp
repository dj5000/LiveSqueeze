// Replaces the global allocation functions in the test executable so tests can assert that the
// audio path performs no allocation.

#include <atomic>
#include <cstdlib>
#include <new>

#include "test_util.hpp"

namespace {
std::atomic<std::size_t> g_allocs{0};

void* allocate(std::size_t size) {
    g_allocs.fetch_add(1, std::memory_order_relaxed);
    if (void* p = std::malloc(size == 0 ? 1 : size)) {
        return p;
    }
    throw std::bad_alloc();
}
} // namespace

namespace lsqtest {
std::size_t allocCount() {
    return g_allocs.load(std::memory_order_relaxed);
}
} // namespace lsqtest

void* operator new(std::size_t size) {
    return allocate(size);
}
void* operator new[](std::size_t size) {
    return allocate(size);
}
void operator delete(void* p) noexcept {
    std::free(p);
}
void operator delete[](void* p) noexcept {
    std::free(p);
}
void operator delete(void* p, std::size_t) noexcept {
    std::free(p);
}
void operator delete[](void* p, std::size_t) noexcept {
    std::free(p);
}
