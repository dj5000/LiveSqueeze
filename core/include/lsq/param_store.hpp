#pragma once

#include <atomic>
#include <mutex>
#include <type_traits>

#include "lsq/params.hpp"

namespace lsq {

// Wait-free hand-off of a whole value from one writer thread to one reader thread (a "triple
// buffer"). The reader always sees a complete, consistent value and never blocks the writer or
// vice versa. Exactly one thread may call publish() and exactly one may call fetch().
template <class T> class TripleBuffer {
    static_assert(std::is_trivially_copyable_v<T>);

public:
    // Writer thread.
    void publish(const T& v) noexcept {
        buf_[write_] = v;
        const int old = middle_.exchange(write_ | kDirty, std::memory_order_acq_rel);
        write_ = old & 3;
    }

    // Reader thread. Returns true and fills `out` only if a new value arrived since the last call.
    bool fetch(T& out) noexcept {
        if ((middle_.load(std::memory_order_acquire) & kDirty) == 0) {
            return false;
        }
        const int old = middle_.exchange(read_, std::memory_order_acq_rel);
        read_ = old & 3;
        out = buf_[read_];
        return true;
    }

private:
    static constexpr int kDirty = 4;
    T buf_[3]{};
    int write_ = 0;              // owned by the writer
    int read_ = 2;               // owned by the reader
    std::atomic<int> middle_{1}; // index of the middle slot, plus the dirty bit
};

// The parameter channel between the GUI (writer) and the audio thread (reader). publish()
// sanitizes the values, so the audio thread can trust them.
//
// fetch() only reports values the reader has not seen yet, so a reader that starts later (a new
// Engine after the audio is restarted) takes the current settings from latest() instead. The
// writer side is serialized by a mutex; the audio thread only ever calls fetch(), which never
// locks.
class ParamStore {
public:
    void publish(Params p) noexcept {
        sanitize(p);
        std::lock_guard<std::mutex> lock(writeMu_);
        latest_ = p;
        buffer_.publish(p);
    }
    bool fetch(Params& out) noexcept { return buffer_.fetch(out); }

    // The most recently published value (defaults if nothing was published). Not for the audio
    // thread.
    Params latest() const {
        std::lock_guard<std::mutex> lock(writeMu_);
        return latest_;
    }

private:
    TripleBuffer<Params> buffer_;
    mutable std::mutex writeMu_;
    Params latest_{};
};

} // namespace lsq
