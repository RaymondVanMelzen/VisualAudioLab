#include <val/audio/AnalyzerEngine.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <numbers>
#include <stdexcept>

namespace val::audio
{
using Clock = std::chrono::steady_clock;
void AnalysisResults::publish(const SignalAnalysis& result) noexcept
{
    for (std::size_t i = 0; i < pages_.size(); ++i)
    {
        auto& page = pages_[(next_ + i) % pages_.size()];
        auto state = page.state.load(std::memory_order_acquire);
        if (state != Ownership::free && state != Ownership::ready) continue;
        if (!page.state.compare_exchange_strong(state, Ownership::writing, std::memory_order_acq_rel)) continue;
        if (state == Ownership::ready) drops_.fetch_add(1, std::memory_order_relaxed);
        page.payload = result;
        page.state.store(Ownership::ready, std::memory_order_release);
        next_ = (next_ + i + 1) % pages_.size();
        return;
    }
    drops_.fetch_add(1, std::memory_order_relaxed);
}
bool AnalysisResults::latest(SignalAnalysis& result) noexcept
{
    bool found = false;
    for (auto& page : pages_)
    {
        auto state = Ownership::ready;
        if (!page.state.compare_exchange_strong(state, Ownership::reading, std::memory_order_acq_rel)) continue;
        if (page.payload.stamp.sequence > lastSequence_ && (!found || page.payload.stamp.sequence > result.stamp.sequence))
        {
            result = page.payload;
            found = true;
        }
        page.state.store(Ownership::free, std::memory_order_release);
    }
    if (found) lastSequence_ = result.stamp.sequence;
    return found;
}
static_assert(std::atomic<unsigned>::is_always_lock_free);
struct AnalyzerEngine::State
{
    std::shared_ptr<SignalCaptureBuffer> capture;
    AnalyzerProfile profile;
    std::array<std::vector<float>, 2> history;
    std::vector<float> window;
    std::vector<std::complex<double>> fft, twiddles;
    std::array<std::vector<double>, 2> averagePower;
    std::shared_ptr<AnalysisResults> results{std::make_shared<AnalysisResults>()};
    SignalAnalysis current;
    SignalCaptureBuffer::Slot packet;
    std::uint64_t count{}, segment{}, expected{}, revision{}, epoch{}, jobs{}, invalidSamples{};
    std::size_t position{}, hop{};
    int demands{}, rmsLength{}, peakLength{};
    std::array<double, 2> squares{}, sums{};
    std::array<float, 2> held{}, displayed{};
    std::array<std::uint64_t, 2> holdUntil{};
    double sumWindow{}, windowSquares{}, maxMs{};
    float holdDecay{}, displayDecay{};
    bool continuity{}, averaged{};

    State(std::shared_ptr<SignalCaptureBuffer> buffer, AnalyzerProfile settings)
        : capture(std::move(buffer)), profile(settings)
    {
        rmsLength = static_cast<int>(std::ceil(capture->sampleRate() * profile.rmsSeconds));
        peakLength = static_cast<int>(std::ceil(capture->sampleRate() * 0.05));
        holdDecay = static_cast<float>(std::pow(10.0, -12.0 / (20 * capture->sampleRate())));
        displayDecay = static_cast<float>(std::pow(10.0, -20.0 / (20 * capture->sampleRate())));
        const auto capacity = std::max({rmsLength, peakLength, profile.fftSize, 4096});
        for (auto& channel : history) channel.resize(static_cast<std::size_t>(capacity));
        window.resize(static_cast<std::size_t>(profile.fftSize));
        fft.resize(window.size());
        twiddles.resize(window.size() / 2);
        for (auto& power : averagePower) power.resize(window.size() / 2 + 1);
        for (std::size_t i = 0; i < window.size(); ++i)
        {
            window[i] = static_cast<float>(0.5 - 0.5 * std::cos(2 * std::numbers::pi * static_cast<double>(i)
                                                            / static_cast<double>(window.size())));
            sumWindow += window[i];
            windowSquares += static_cast<double>(window[i]) * window[i];
        }
        for (std::size_t i = 0; i < twiddles.size(); ++i)
            twiddles[i] = std::polar(1.0, -2 * std::numbers::pi * static_cast<double>(i) / static_cast<double>(window.size()));
    }
    std::size_t memory() const noexcept
    {
        std::size_t bytes = sizeof(*this) + sizeof(AnalysisResults) + capture->memoryBytes();
        for (const auto& channel : history) bytes += channel.capacity() * sizeof(float);
        for (const auto& power : averagePower) bytes += power.capacity() * sizeof(double);
        return bytes + window.capacity() * sizeof(float) + (fft.capacity() + twiddles.capacity()) * sizeof(std::complex<double>);
    }
    void reset()
    {
        ++segment;
        count = position = hop = 0;
        squares = {}; sums = {}; held = {}; displayed = {}; holdUntil = {};
        averaged = false;
        current.spectrumValid = current.rmsValid = current.peakValid = current.nonFinite = false;
        current.scopeFrames = 0;
        current.overRange = {};
        current.dominantHz = {};
    }
    float past(int channel, std::size_t distance) const noexcept
    {
        const auto size = history[channel].size();
        return history[channel][(position + size - distance) % size];
    }
    void transform()
    {
        const auto n = window.size();
        for (int channel = 0; channel < capture->channels(); ++channel)
        {
            for (std::size_t i = 0; i < n; ++i)
                fft[i] = static_cast<double>(past(channel, n - i)) * window[i];
            for (std::size_t i = 1, j = 0; i < n; ++i)
            {
                auto bit = n >> 1;
                for (; j & bit; bit >>= 1) j ^= bit;
                j ^= bit;
                if (i < j) std::swap(fft[i], fft[j]);
            }
            for (std::size_t length = 2; length <= n; length <<= 1)
                for (std::size_t start = 0; start < n; start += length)
                    for (std::size_t i = 0; i < length / 2; ++i)
                    {
                        const auto even = fft[start + i];
                        const auto odd = fft[start + i + length / 2] * twiddles[i * n / length];
                        fft[start + i] = even + odd;
                        fft[start + i + length / 2] = even - odd;
                    }
            const auto alpha = profile.spectrumAverageSeconds == 0 ? 1.0
                : -std::expm1(-static_cast<double>(n / 2) / (capture->sampleRate() * profile.spectrumAverageSeconds));
            double greatest = -1;
            std::size_t dominant = 0;
            for (std::size_t bin = 0; bin <= n / 2; ++bin)
            {
                const double factor = bin == 0 || bin == n / 2 ? 1.0 : 2.0;
                const auto power = std::norm(fft[bin]);
                auto& average = averagePower[channel][bin];
                average = averaged ? average + alpha * (power - average) : power;
                current.amplitudeDb[channel][bin] = AnalyzerEngine::decibels(
                    static_cast<float>(factor * std::sqrt(average) / sumWindow));
                const auto density = factor * average / (capture->sampleRate() * windowSquares);
                current.densityDb[channel][bin] = density > 0 ? static_cast<float>(10 * std::log10(density))
                                                              : -std::numeric_limits<float>::infinity();
                if (average > greatest) { greatest = average; dominant = bin; }
            }
            current.dominantHz[channel] = static_cast<double>(dominant) * capture->sampleRate() / static_cast<double>(n);
        }
        averaged = true;
        current.spectrumValid = true;
    }
    void analyze()
    {
        const auto& stamp = packet.stamp;
        const bool gap = !continuity || stamp.epoch != epoch || stamp.revision != revision || stamp.firstFrame != expected;
        if (gap) reset();
        continuity = true;
        epoch = stamp.epoch; revision = stamp.revision;
        expected = stamp.firstFrame + static_cast<std::uint64_t>(stamp.frames);
        current.gap = gap;
        for (int frame = 0; frame < stamp.frames; ++frame)
        {
            bool invalid = false;
            for (int channel = 0; channel < stamp.channels; ++channel)
                invalid |= !std::isfinite(packet.samples[channel * SignalCaptureBuffer::slotFrames + frame])
                    || std::abs(packet.samples[channel * SignalCaptureBuffer::slotFrames + frame]) > 1.0e6f;
            if (invalid)
            {
                ++invalidSamples;
                reset();
                current.nonFinite = true; current.gap = true;
                continue;
            }
            for (int channel = 0; channel < stamp.channels; ++channel)
            {
                const auto x = packet.samples[channel * SignalCaptureBuffer::slotFrames + frame];
                const auto old = count >= static_cast<std::uint64_t>(rmsLength)
                    ? past(channel, static_cast<std::size_t>(rmsLength)) : 0.0f;
                squares[channel] += static_cast<double>(x) * x - static_cast<double>(old) * old;
                sums[channel] += x - static_cast<double>(old);
                history[channel][position] = x;
                const auto amplitude = std::abs(x);
                if (amplitude >= 1.0f) ++current.overRange[channel];
                if (amplitude >= held[channel])
                {
                    held[channel] = amplitude;
                    holdUntil[channel] = count + 1 + static_cast<std::uint64_t>(stamp.sampleRate);
                }
                else if (count + 1 > holdUntil[channel]) held[channel] = std::max(amplitude, held[channel] * holdDecay);
                displayed[channel] = std::max(amplitude, displayed[channel] * displayDecay);
            }
            position = (position + 1) % history[0].size();
            ++count; ++hop;
            if (count >= static_cast<std::uint64_t>(profile.fftSize)
                && (hop >= static_cast<std::size_t>(profile.fftSize / 2)))
            {
                transform();
                hop = 0;
            }
        }
        current.stamp = stamp;
        current.segment = segment;
        current.invalidSamples = invalidSamples;
        current.captureDrops = capture->drops();
        current.fftSize = profile.fftSize;
        current.rmsFrames = rmsLength;
        current.coveredFrames = static_cast<int>(std::min<std::uint64_t>(count, static_cast<std::uint64_t>(rmsLength)));
        current.binHz = capture->sampleRate() / profile.fftSize;
        current.coherentGain = sumWindow / profile.fftSize;
        current.enbwBins = profile.fftSize * windowSquares / (sumWindow * sumWindow);
        current.enbwHz = current.enbwBins * current.binHz;
        current.rmsValid = count >= static_cast<std::uint64_t>(rmsLength);
        current.peakValid = count >= static_cast<std::uint64_t>(peakLength);
        current.scopeFrames = static_cast<int>(std::min<std::uint64_t>(count, 4096));
        auto scopeEnd = std::size_t{0};
        current.triggered = profile.trigger == AnalyzerProfile::Trigger::free;
        if (!current.triggered && current.scopeFrames > profile.preTriggerFrames + 512)
        {
            for (int distance = 512; distance < current.scopeFrames - profile.preTriggerFrames; ++distance)
            {
                const auto before = past(0, static_cast<std::size_t>(distance + 1));
                const auto after = past(0, static_cast<std::size_t>(distance));
                if ((profile.trigger == AnalyzerProfile::Trigger::rising && before < profile.triggerLevel && after >= profile.triggerLevel)
                    || (profile.trigger == AnalyzerProfile::Trigger::falling && before > profile.triggerLevel && after <= profile.triggerLevel))
                {
                    scopeEnd = static_cast<std::size_t>(distance - 512);
                    current.triggered = true;
                    current.scopeFrames = std::min(current.scopeFrames - static_cast<int>(scopeEnd), 512 + profile.preTriggerFrames);
                    break;
                }
            }
        }
        const auto peakCount = static_cast<int>(std::min<std::uint64_t>(count, static_cast<std::uint64_t>(peakLength)));
        AnalyzerSnapshot meters;
        meters.windows = ++jobs;
        for (int channel = 0; channel < stamp.channels; ++channel)
        {
            float peak = 0, minimum = std::numeric_limits<float>::infinity(), maximum = -minimum;
            for (int i = 1; i <= peakCount; ++i) peak = std::max(peak, std::abs(past(channel, static_cast<std::size_t>(i))));
            current.peak[channel] = peak;
            current.heldPeak[channel] = held[channel]; current.displayedPeak[channel] = displayed[channel];
            current.rms[channel] = current.rmsValid ? static_cast<float>(std::sqrt(std::max(0.0, squares[channel]) / rmsLength)) : 0;
            current.mean[channel] = current.rmsValid ? static_cast<float>(sums[channel] / rmsLength) : 0;
            for (int pixel = 0; pixel < 512; ++pixel)
            {
                const int start = pixel * current.scopeFrames / 512, end = (pixel + 1) * current.scopeFrames / 512;
                float low = std::numeric_limits<float>::infinity(), high = -low;
                for (int i = start; i < std::max(start + 1, end) && i < current.scopeFrames; ++i)
                {
                    const auto sample = past(channel, scopeEnd + static_cast<std::size_t>(current.scopeFrames - i));
                    low = std::min(low, sample); high = std::max(high, sample);
                }
                if (current.scopeFrames == 0) low = high = 0;
                current.scopeMinimum[channel][pixel] = low; current.scopeMaximum[channel][pixel] = high;
                minimum = std::min(minimum, low); maximum = std::max(maximum, high);
            }
            current.minimum[channel] = minimum; current.maximum[channel] = maximum;
            meters.peak[channel] = peak; meters.rms[channel] = current.rms[channel];
        }
        current.resultVersion = jobs;
        capture->publishMeters(meters);
    }
};

AnalyzerEngine::AnalyzerEngine(bool startWorker) : threaded_(startWorker)
{
    sources_.reserve(8);
    if (threaded_) worker_ = std::jthread([this](std::stop_token stop) { run(stop); });
}
AnalyzerEngine::~AnalyzerEngine()
{
    if (worker_.joinable()) { worker_.request_stop(); worker_.join(); }
    for (auto& source : sources_) source->capture->retire();
}
application::Result AnalyzerEngine::validate(const AnalyzerProfile& profile, double rate)
{
    if (!std::isfinite(rate) || rate < 8000 || rate > 384000 || profile.fftSize < 1024 || profile.fftSize > 8192
        || (profile.fftSize & (profile.fftSize - 1)) || !std::isfinite(profile.rmsSeconds)
        || profile.rmsSeconds < 0.01 || profile.rmsSeconds > 1 || !std::isfinite(profile.spectrumAverageSeconds)
        || profile.spectrumAverageSeconds < 0 || profile.spectrumAverageSeconds > 2
        || !std::isfinite(profile.triggerLevel) || std::abs(profile.triggerLevel) > 1
        || profile.preTriggerFrames < 0 || profile.preTriggerFrames > 2048)
        return application::Result::failure("Analysis requires power-of-two FFT 1024-8192, RMS 10-1000 ms, averaging 0-2 s and bounded trigger.");
    return application::Result::success();
}
application::Result AnalyzerEngine::add(std::shared_ptr<SignalCaptureBuffer> capture, AnalyzerProfile profile)
{
    if (!capture) return application::Result::failure("Cannot observe an absent capture source.");
    if (capture->retired()) return application::Result::failure("Capture generation was retired. Prepare a new transport before reattaching.");
    if (const auto result = validate(profile, capture->sampleRate()); !result.succeeded) return result;
    std::lock_guard lock(registry_);
    int channels = capture->channels();
    double samplesPerSecond = capture->channels() * capture->sampleRate();
    for (const auto& state : sources_)
    {
        if (state->capture->source() == capture->source())
            return application::Result::failure("Source already admitted. Share its existing results rather than adding another analyzer.");
        channels += state->capture->channels();
        samplesPerSecond += state->capture->channels() * state->capture->sampleRate();
    }
    if (sources_.size() == 8 || channels > 16)
        return application::Result::failure("Observation capacity exhausted (8 sources/16 channels). Unpin a monitor before adding another.");
    if (samplesPerSecond > 768000)
        return application::Result::failure("Observation copy bandwidth exceeds 3.072 MB/s. Reduce captured channels or sample rate.");
    auto state = std::make_unique<State>(std::move(capture), profile);
    std::size_t memory = state->memory();
    for (const auto& existing : sources_) memory += existing->memory();
    if (memory > 64 * 1024 * 1024)
        return application::Result::failure("Observation exceeds the 64 MiB memory budget.");
    sources_.push_back(std::move(state));
    return application::Result::success();
}
void AnalyzerEngine::remove(std::uint64_t source)
{
    std::lock_guard lock(registry_);
    std::erase_if(sources_, [source](const auto& state)
    {
        if (state->capture->source() != source) return false;
        state->capture->retire();
        return true;
    });
}
application::Result AnalyzerEngine::demand(std::uint64_t source, bool active)
{
    std::lock_guard lock(registry_);
    for (auto& state : sources_) if (state->capture->source() == source)
    {
        if (!active && state->demands == 0) return application::Result::failure("Observation demand is already detached.");
        if (active && state->demands == 108) return application::Result::failure("Observation view limit is 108 shared hosts per source.");
        state->demands += active ? 1 : -1;
        state->capture->setEnabled(state->demands > 0);
        return application::Result::success();
    }
    return application::Result::failure("Observation source was removed.");
}
bool AnalyzerEngine::latest(std::uint64_t source, SignalAnalysis& result)
{
    std::unique_lock lock(registry_, std::try_to_lock);
    if (!lock.owns_lock()) return false;
    for (auto& state : sources_) if (state->capture->source() == source) return state->results->latest(result);
    return false;
}
std::shared_ptr<AnalysisResults> AnalyzerEngine::results(std::uint64_t source)
{
    std::lock_guard lock(registry_);
    for (auto& state : sources_) if (state->capture->source() == source) return state->results;
    return {};
}
AnalyzerDiagnostics AnalyzerEngine::diagnostics() const
{
    std::lock_guard lock(registry_);
    AnalyzerDiagnostics result;
    for (const auto& state : sources_)
    {
        result.jobs += state->jobs; result.publicationDrops += state->results->drops();
        result.lastJobMilliseconds = std::max(result.lastJobMilliseconds, state->current.analysisMilliseconds);
        result.maxJobMilliseconds = std::max(result.maxJobMilliseconds, state->maxMs);
        result.memoryBytes += state->memory();
    }
    result.sources = sources_.size();
    return result;
}
std::size_t AnalyzerEngine::round()
{
    std::lock_guard lock(registry_);
    std::size_t jobs = 0;
    for (auto& state : sources_)
    {
        const auto* packet = state->capture->acquire();
        if (!packet) continue;
        state->packet = *packet;
        state->capture->release();
        const auto start = Clock::now();
        state->analyze();
        state->current.analysisMilliseconds = std::chrono::duration<double, std::milli>(Clock::now() - start).count();
        state->maxMs = std::max(state->maxMs, state->current.analysisMilliseconds);
        state->results->publish(state->current);
        ++jobs;
    }
    return jobs;
}
std::size_t AnalyzerEngine::pump()
{
    if (threaded_) throw std::logic_error("Manual ingestion cannot compete with the shared worker.");
    return round();
}
void AnalyzerEngine::run(std::stop_token stop)
{
    while (!stop.stop_requested())
    {
        const auto jobs = round();
        std::this_thread::sleep_for(std::chrono::milliseconds(jobs ? 1 : 3));
    }
}
float AnalyzerEngine::decibels(float amplitude) noexcept
{
    return amplitude > 0 ? 20 * std::log10(amplitude) : -std::numeric_limits<float>::infinity();
}
application::Result AnalyzerEngine::resetHold(std::uint64_t source)
{
    std::lock_guard lock(registry_);
    for (auto& state : sources_) if (state->capture->source() == source)
    {
        state->held = {}; state->holdUntil = {}; state->current.heldPeak = {};
        return application::Result::success();
    }
    return application::Result::failure("Cannot reset hold: observation source was removed.");
}
}
