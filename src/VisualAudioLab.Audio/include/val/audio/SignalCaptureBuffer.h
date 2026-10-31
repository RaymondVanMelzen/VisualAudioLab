#pragma once

#include <val/application/Result.h>
#include <array>
#include <atomic>
#include <cstdint>
#include <memory>

namespace val::audio
{
/** Stable source and stream provenance copied with each owned observation slot. */
struct CaptureStamp
{
    std::uint64_t source{}, revision{}, epoch{}, firstFrame{}, sequence{}, drops{};
    double sampleRate{};
    int channels{}, frames{}, firstChannel{};
};

/** Worker-published legacy meter observation; raw RMS reference is amplitude 1. */
struct AnalyzerSnapshot
{
    std::array<float, 32> peak{}, rms{};
    std::uint64_t windows{};
};

/**
 * Four fixed owned SPSC slots, each up to 2048 frames and two admitted channels.
 * The sole audio producer only copies/publishes; full queues drop incoming slots.
 * The sole ingestion consumer releases before analysis. No shared pointer is copied on audio.
 * Allocate/destroy off audio after producer quiescence and consumer removal.
 */
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable: 4324) // Intentional cache-line separation of the two SPSC cursors.
#endif
class SignalCaptureBuffer final
{
public:
    static constexpr int slotFrames = 2048, slots = 4;
    /** One exclusively producer/consumer-owned page; never exposes a borrowed host pointer. */
    struct Slot
    {
        CaptureStamp stamp;
        std::array<float, slotFrames * 2> samples{};
    };
    SignalCaptureBuffer(std::uint64_t source, double sampleRate, int channels, int firstChannel = 0);
    /** Bounded to two slots/4096 frames. Disabled demand skips all sample copies. */
    void capture(const float* planar, int stride, int frames, std::uint64_t revision,
                 std::uint64_t epoch, std::uint64_t firstFrame) noexcept;
    [[nodiscard]] const Slot* acquire() const noexcept;
    void release() noexcept;
    /** Demand may change on control/UI; this never owns/destroys callback resources. */
    void setEnabled(bool enabled) noexcept { enabled_.store(enabled && !retired_.load(std::memory_order_acquire), std::memory_order_release); }
    /** Permanently detach this generation; prepared plans may retain it until their off-thread retirement. */
    void retire() noexcept { retired_.store(true, std::memory_order_release); enabled_.store(false, std::memory_order_release); }
    [[nodiscard]] bool retired() const noexcept { return retired_.load(std::memory_order_acquire); }
    [[nodiscard]] bool enabled() const noexcept { return enabled_.load(std::memory_order_acquire); }
    [[nodiscard]] std::uint64_t drops() const noexcept { return drops_.load(std::memory_order_relaxed); }
    [[nodiscard]] std::uint64_t capturedFrames() const noexcept { return captured_.load(std::memory_order_relaxed); }
    [[nodiscard]] std::uint64_t source() const noexcept { return source_; }
    [[nodiscard]] double sampleRate() const noexcept { return rate_; }
    [[nodiscard]] int channels() const noexcept { return channels_; }
    [[nodiscard]] int firstChannel() const noexcept { return firstChannel_; }
    [[nodiscard]] std::size_t memoryBytes() const noexcept { return sizeof(*this); }
    /** Worker-only publication and arbitrary reader observation of independent lock-free fields. */
    void publishMeters(const AnalyzerSnapshot& snapshot) noexcept;
    [[nodiscard]] AnalyzerSnapshot meters() const noexcept;
private:
    void drop() noexcept;
    std::uint64_t source_;
    double rate_;
    int channels_, firstChannel_;
    std::array<Slot, slots> pages_;
    alignas(64) std::atomic<std::uint64_t> write_{};
    alignas(64) std::atomic<std::uint64_t> read_{};
    std::atomic<bool> enabled_{false};
    std::atomic<bool> retired_{false};
    std::atomic<std::uint64_t> drops_{}, captured_{};
    std::array<std::atomic<std::uint32_t>, 2> peaks_{}, rms_{};
    std::atomic<std::uint64_t> windows_{};
};
#if defined(_MSC_VER)
#pragma warning(pop)
#endif
}
