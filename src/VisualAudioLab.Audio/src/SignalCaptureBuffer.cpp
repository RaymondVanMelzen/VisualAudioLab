#include <val/audio/SignalCaptureBuffer.h>
#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace val::audio
{
static_assert(std::atomic<std::uint64_t>::is_always_lock_free);
static_assert(std::atomic<std::uint32_t>::is_always_lock_free);
static_assert(std::atomic<bool>::is_always_lock_free);
SignalCaptureBuffer::SignalCaptureBuffer(std::uint64_t source, double rate, int channels, int firstChannel)
    : source_(source), rate_(rate), channels_(channels), firstChannel_(firstChannel)
{
    if (!source || !std::isfinite(rate) || rate < 8000 || rate > 384000 || channels < 1 || channels > 2
        || firstChannel < 0 || firstChannel + channels > 32)
        throw std::invalid_argument("Capture requires stable source, 8-384 kHz, one/two selected channels within a 32-channel bus.");
}
void SignalCaptureBuffer::drop() noexcept
{
    const auto value = drops_.load(std::memory_order_relaxed);
    if (value != std::numeric_limits<std::uint64_t>::max()) drops_.store(value + 1, std::memory_order_relaxed);
}
void SignalCaptureBuffer::capture(const float* planar, int stride, int frames, std::uint64_t revision,
                                 std::uint64_t epoch, std::uint64_t firstFrame) noexcept
{
    if (!enabled_.load(std::memory_order_acquire) || frames == 0) return;
    if (!planar || frames < 0 || frames > 4096 || stride < frames) { drop(); return; }
    for (int offset = 0; offset < frames; offset += slotFrames)
    {
        const auto write = write_.load(std::memory_order_relaxed);
        if (write - read_.load(std::memory_order_acquire) >= slots) { drop(); continue; }
        auto& page = pages_[write % slots];
        const auto count = std::min(slotFrames, frames - offset);
        page.stamp = {source_, revision, epoch, firstFrame + static_cast<std::uint64_t>(offset),
                      write + 1, drops_.load(std::memory_order_relaxed), rate_, channels_, count, firstChannel_};
        for (int channel = 0; channel < channels_; ++channel)
            std::copy_n(planar + (firstChannel_ + channel) * stride + offset, count, page.samples.data() + channel * slotFrames);
        captured_.store(captured_.load(std::memory_order_relaxed) + count, std::memory_order_relaxed);
        write_.store(write + 1, std::memory_order_release);
    }
}
const SignalCaptureBuffer::Slot* SignalCaptureBuffer::acquire() const noexcept
{
    const auto read = read_.load(std::memory_order_relaxed);
    return read == write_.load(std::memory_order_acquire) ? nullptr : &pages_[read % slots];
}
void SignalCaptureBuffer::release() noexcept
{
    read_.store(read_.load(std::memory_order_relaxed) + 1, std::memory_order_release);
}
void SignalCaptureBuffer::publishMeters(const AnalyzerSnapshot& snapshot) noexcept
{
    for (int channel = 0; channel < channels_; ++channel)
    {
        peaks_[channel].store(std::bit_cast<std::uint32_t>(snapshot.peak[channel]), std::memory_order_relaxed);
        rms_[channel].store(std::bit_cast<std::uint32_t>(snapshot.rms[channel]), std::memory_order_relaxed);
    }
    windows_.store(snapshot.windows, std::memory_order_release);
}
AnalyzerSnapshot SignalCaptureBuffer::meters() const noexcept
{
    AnalyzerSnapshot result;
    result.windows = windows_.load(std::memory_order_acquire);
    for (int channel = 0; channel < channels_; ++channel)
    {
        result.peak[channel] = std::bit_cast<float>(peaks_[channel].load(std::memory_order_relaxed));
        result.rms[channel] = std::bit_cast<float>(rms_[channel].load(std::memory_order_relaxed));
    }
    return result;
}
}
