#include <val/presentation/VisualizationWidgets.h>
#include <val/presentation/ShellTheme.h>
#include <val/presentation/CanvasView.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <stdexcept>

namespace val::presentation
{
namespace
{
const char* title(VisualizationKind kind)
{
    switch (kind)
    {
        case VisualizationKind::oscilloscope: return "Oscilloscope";
        case VisualizationKind::spectrum: return "Spectrum Analyzer";
        case VisualizationKind::peak: return "Peak Meter";
        case VisualizationKind::rms: return "RMS Meter";
        case VisualizationKind::guitarTuner: return "Guitar Tuner";
        case VisualizationKind::correlation: return "Correlation Meter";
    }
    return "";
}
juce::String db(float value)
{
    const auto level = audio::AnalyzerEngine::decibels(value);
    return std::isfinite(level) ? juce::String(level, 2) : juce::String("-inf");
}
struct TuningReading
{
    juce::String note;
    double cents{};
};
TuningReading tuningReading(double frequency)
{
    static constexpr std::array<const char*, 12> noteNames{
        "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    const auto midiNote = static_cast<int>(std::lround(69.0 + 12.0 * std::log2(frequency / 440.0)));
    const auto reference = 440.0 * std::pow(2.0, static_cast<double>(midiNote - 69) / 12.0);
    return {juce::String(noteNames[static_cast<std::size_t>((midiNote % 12 + 12) % 12)])
                + juce::String(midiNote / 12 - 1),
            1200.0 * std::log2(frequency / reference)};
}
juce::String signedCents(double cents)
{
    if (std::abs(cents) < 0.05)
        return "0.0 cents";
    return (cents > 0 ? "+" : "") + juce::String(cents, 1) + " cents " + (cents > 0 ? "sharp" : "flat");
}
}
VisualizationSession::VisualizationSession()
{
    sources_.reserve(136); views_.reserve(128);
    startTimerHz(30);
}
VisualizationSession::~VisualizationSession() { stopTimer(); jassert(views_.empty()); bindCanvas(nullptr); }
application::Result VisualizationSession::add(std::shared_ptr<audio::SignalCaptureBuffer> capture,
                                             juce::String label, audio::AnalyzerProfile profile)
{
    if (const auto result = engine_.add(capture, profile); !result.succeeded) return result;
    std::erase_if(sources_, [&capture](const auto& source)
    { return source.state == ObservationState::removed && source.id == capture->source(); });
    auto results = engine_.results(capture->source());
    const auto id = capture->source();
    sources_.push_back({id, std::move(capture), std::move(results), {}, std::move(label), ObservationState::waiting, 0, false, {}});
    for (auto* view : views_) view->resized();
    return application::Result::success();
}
VisualizationSession::Source* VisualizationSession::find(std::uint64_t id)
{
    for (auto& source : sources_) if (source.id == id) return &source;
    return nullptr;
}
void VisualizationSession::remove(std::uint64_t source)
{
    for (auto* view : views_) if (view->target() == source) view->releaseDemand();
    if (auto* record = find(source); record && record->embeddedDemand)
    {
        const auto result = engine_.demand(source, false); jassert(result.succeeded);
        record->embeddedDemand = false;
    }
    engine_.remove(source);
    if (auto* record = find(source))
    {
        record->state = ObservationState::removed; record->latest.reset(); record->results.reset(); record->capture.reset();
    }
    std::erase_if(sources_, [this](const auto& record)
    {
        return record.state == ObservationState::removed
            && std::none_of(views_.begin(), views_.end(), [&record](auto* view) { return view->target() == record.id; });
    });
}
void VisualizationSession::reset()
{
    for (auto* view : views_) { view->setTarget(0); view->setPinned(false); }
    const auto ids = sourceIds();
    for (const auto id : ids) remove(id);
    sources_.clear();
}
void VisualizationSession::bindCanvas(CanvasView* canvas)
{
    if (canvas_) canvas_->paintObservation = {};
    for (auto& source : sources_) if (source.embeddedDemand)
    {
        const auto result = engine_.demand(source.id, false); jassert(result.succeeded);
        source.embeddedDemand = false;
    }
    canvas_ = canvas;
    if (canvas_) canvas_->paintObservation = [this](juce::Graphics& g, domain::GraphId id, juce::Rectangle<float> area)
    {
        const auto* source = find(id);
        if (!source) return;
        if (source->error.isNotEmpty())
        {
            g.setColour(ShellTheme::warning());
            g.drawFittedText("Capture limit", area.toNearestInt(), juce::Justification::centred, 2);
            return;
        }
        if (!source->latest || !source->latest->scopeFrames) return;
        const auto& data = *source->latest;
        const bool stale = juce::Time::getMillisecondCounterHiRes() - source->received > 500;
        g.setColour(stale ? ShellTheme::textMuted() : ShellTheme::analyzer());
        juce::Path envelope;
        for (int pixel = 0; pixel < 48; ++pixel)
        {
            float low = 1, high = -1;
            for (int bin = pixel * 512 / 48; bin < (pixel + 1) * 512 / 48; ++bin)
            {
                low = std::min(low, data.scopeMinimum[0][bin]);
                high = std::max(high, data.scopeMaximum[0][bin]);
            }
            const auto x = area.getX() + area.getWidth() * static_cast<float>(pixel) / 48;
            const auto y = area.getCentreY() - juce::jlimit(-1.0f, 1.0f, high) * area.getHeight() * 0.45f;
            envelope.startNewSubPath(x, y);
            envelope.lineTo(x, std::max(y + 0.5f, area.getCentreY() - juce::jlimit(-1.0f, 1.0f, low) * area.getHeight() * 0.45f));
        }
        g.strokePath(envelope, juce::PathStrokeType(0.8f));
    };
}
void VisualizationSession::setState(std::uint64_t source, ObservationState state)
{
    if (auto* record = find(source))
    {
        record->state = state;
        if (state != ObservationState::processing) record->latest.reset();
    }
}
juce::StringArray VisualizationSession::labels() const
{
    juce::StringArray values;
    for (const auto& source : sources_) values.add(source.label + (source.state == ObservationState::removed ? " (removed)" : ""));
    return values;
}
std::vector<std::uint64_t> VisualizationSession::sourceIds() const
{
    std::vector<std::uint64_t> values;
    for (const auto& source : sources_) values.push_back(source.id);
    return values;
}
void VisualizationSession::attach(VisualizationWidgets& view)
{
    if (views_.size() == 128) throw std::length_error("Visualization host budget is 128 components. Close existing monitors before adding another.");
    views_.push_back(&view);
}
void VisualizationSession::detach(VisualizationWidgets& view)
{
    view.releaseDemand();
    std::erase(views_, &view);
}
void VisualizationSession::refresh()
{
    const auto now = juce::Time::getMillisecondCounterHiRes();
    for (auto& source : sources_)
    {
        const bool active = canvas_ && canvas_->observationVisible(source.id)
            && (source.state == ObservationState::waiting || source.state == ObservationState::processing);
        if (source.embeddedDemand != active)
        {
            const auto admitted = engine_.demand(source.id, active);
            source.error = admitted.succeeded ? juce::String{} : juce::String::fromUTF8(admitted.error.c_str());
            if (admitted.succeeded) source.embeddedDemand = active;
        }
        if (source.results)
        {
            if (source.results->latest(*scratch_))
            {
                source.latest = std::make_shared<audio::SignalAnalysis>(*scratch_); source.received = now;
                if (source.state == ObservationState::waiting) source.state = ObservationState::processing;
            }
        }
        if (canvas_ && active && now >= nextEmbedded_) canvas_->repaintObservation(source.id);
    }
    if (now >= nextEmbedded_) nextEmbedded_ = now + 100;
    for (auto* view : views_) view->tick(now);
}
VisualizationWidget::VisualizationWidget(VisualizationKind kind, VisualizationHost host) : kind_(kind), host_(host)
{
    setName(title(kind));
    setAccessible(true);
    refresh_.setTitle(juce::String(title(kind)) + " refresh rate");
    const int cap = host == VisualizationHost::embedded ? 15 : 30;
    for (int rate : {1, 5, 10, 15, 20, 30}) if (rate <= cap) refresh_.addItem(juce::String(rate) + " Hz", rate);
    rate_ = host == VisualizationHost::embedded ? 10 : host == VisualizationHost::inspector ? 20 : 30;
    refresh_.setSelectedId(rate_, juce::dontSendNotification);
    refresh_.onChange = [this]
    {
        const auto result = setRefreshHz(refresh_.getSelectedId());
        jassert(result.succeeded);
    };
    addAndMakeVisible(refresh_);
}
application::Result VisualizationWidget::setRefreshHz(int hz)
{
    if (hz < 1 || hz > (host_ == VisualizationHost::embedded ? 15 : 30))
        return application::Result::failure("Refresh must be 1-15 Hz for embedded or 1-30 Hz for inspector/monitor; 60 Hz is not qualified.");
    rate_ = hz;
    refresh_.setText(juce::String(hz) + " Hz", juce::dontSendNotification);
    next_ = 0;
    return application::Result::success();
}
void VisualizationWidget::resized() { refresh_.setBounds(getWidth() - 83, 4, 79, 24); }
void VisualizationWidget::update(std::shared_ptr<const audio::SignalAnalysis> result, juce::String state, double now, bool stale)
{
    if (now < next_) return;
    result_ = std::move(result); status_ = std::move(state); stale_ = stale;
    next_ = now + 1000.0 / rate_;
    auto description = juce::String(title(kind_)) + ". " + status_ + ". ";
    if (kind_ == VisualizationKind::rms)
        description << "Raw RMS, full scale sine minus 3.01 dBFS.";
    else if (kind_ == VisualizationKind::correlation)
        description << "Mean-removed stereo correlation, dimensionless.";
    else
        description << "Sample amplitude reference one equals zero dBFS.";
    if (kind_ == VisualizationKind::correlation)
    {
        if (!result_) description << " Waiting for stereo samples.";
        else if (result_->stamp.channels < 2) description << " Unavailable: choose a stereo source.";
        else if (!result_->correlationValid) description << " No measurable stereo variation in the analysis window.";
        else description << " Left/right correlation " << juce::String(result_->correlation, 3)
                         << " over " << juce::String(result_->rmsFrames / result_->stamp.sampleRate, 2) << " seconds.";
    }
    if (kind_ == VisualizationKind::guitarTuner && result_)
    {
        if (!result_->pitchReady) description << " Waiting for a stable pitch window.";
        else if (result_->pitchSilence) description << " No signal. Check routing and input gain.";
        else
        {
            description << " Channel " << (result_->stamp.firstChannel + result_->pitchChannel + 1)
                        << ". A4 reference 440 Hz. Confidence " << juce::String(result_->pitchConfidence * 100.0f, 0) << " percent.";
            if (result_->pitchValid && result_->pitchHz > 0)
            {
                const auto reading = tuningReading(result_->pitchHz);
                description << " " << reading.note << ", " << signedCents(reading.cents) << ".";
                if (stale_ || status_.startsWith("Frozen"))
                    description << " Last measurement is not live.";
                else
                    description << (std::abs(reading.cents) <= 5.0 ? " In tune." : " Tune toward center.");
            }
            else description << " No stable note; reduce noise or isolate one string.";
        }
    }
    setDescription(description);
    repaint();
}
void VisualizationWidget::paint(juce::Graphics& g)
{
    const auto began = std::chrono::steady_clock::now();
    g.fillAll(ShellTheme::input());
    g.setColour(ShellTheme::border()); g.drawRect(getLocalBounds());
    g.setColour(ShellTheme::textPrimary()); g.setFont(14.0f);
    g.drawText(title(kind_), 9, 4, getWidth() - 95, 25, juce::Justification::centredLeft);
    auto area = getLocalBounds().reduced(9).withTrimmedTop(31).withTrimmedBottom(43).toFloat();
    g.setFont(11.0f);
    const bool waiting = !result_ || (kind_ == VisualizationKind::guitarTuner && !result_->pitchReady)
        || (kind_ == VisualizationKind::spectrum && !result_->spectrumValid)
        || (kind_ == VisualizationKind::peak && !result_->peakValid)
        || (kind_ == VisualizationKind::rms && !result_->rmsValid)
        || (kind_ == VisualizationKind::correlation && !result_->correlationValid)
        || (kind_ != VisualizationKind::guitarTuner && result_->scopeFrames == 0);
    if (!area.isEmpty() && !waiting)
    {
        const auto& data = *result_;
        const auto opacity = stale_ ? 0.35f : 1.0f;
        if (kind_ == VisualizationKind::guitarTuner)
        {
            if (data.pitchSilence)
            {
                g.setColour(ShellTheme::textSecondary());
                g.setFont(18.0f);
                g.drawFittedText("No signal", area.toNearestInt(), juce::Justification::centred, 1);
            }
            else if (!data.pitchValid || data.pitchHz <= 0)
            {
                g.setColour(ShellTheme::warning());
                g.setFont(18.0f);
                g.drawFittedText("Uncertain signal", area.removeFromTop(area.getHeight() * 0.55f).toNearestInt(),
                                 juce::Justification::centred, 1);
                g.setFont(11.0f);
                g.drawFittedText("Isolate one string | confidence "
                                     + juce::String(data.pitchConfidence * 100.0f, 0) + "%",
                                 area.toNearestInt(), juce::Justification::centred, 1);
            }
            else
            {
                const auto reading = tuningReading(data.pitchHz);
                const auto inTune = !stale_ && !status_.startsWith("Frozen") && std::abs(reading.cents) <= 5.0;
                auto noteArea = area.removeFromTop(area.getHeight() * 0.56f);
                g.setColour(stale_ ? ShellTheme::textMuted() : ShellTheme::textPrimary());
                g.setFont(juce::FontOptions(std::min(32.0f, noteArea.getHeight() * 0.68f)));
                g.drawText(reading.note, noteArea.toNearestInt(), juce::Justification::centred);
                g.setFont(11.0f);
                g.setColour(stale_ ? ShellTheme::textMuted() : ShellTheme::textSecondary());
                g.drawText(juce::String(data.pitchHz, 1) + " Hz | " + signedCents(reading.cents)
                               + " | Ch " + juce::String(data.stamp.firstChannel + data.pitchChannel + 1)
                               + " | " + juce::String(data.pitchConfidence * 100.0f, 0) + "% confidence",
                           area.removeFromTop(17).toNearestInt(), juce::Justification::centred);
                auto scale = area.reduced(3.0f, 5.0f);
                const auto centre = scale.getCentreX();
                g.setColour(ShellTheme::border().withAlpha(0.35f));
                g.fillRoundedRectangle(scale.withHeight(8.0f).withY(scale.getCentreY() - 4.0f), 4.0f);
                g.setColour(ShellTheme::analyzer().withAlpha(opacity));
                g.drawVerticalLine(static_cast<int>(centre), scale.getY(), scale.getBottom());
                for (int step = -2; step <= 2; ++step)
                {
                    const auto x = centre + scale.getWidth() * static_cast<float>(step) / 4.0f;
                    g.drawVerticalLine(static_cast<int>(x), scale.getCentreY() - (step == 0 ? 8.0f : 4.0f),
                                       scale.getCentreY() + (step == 0 ? 8.0f : 4.0f));
                }
                const auto position = juce::jlimit(-1.0f, 1.0f, static_cast<float>(reading.cents / 50.0));
                const auto markerX = centre + position * scale.getWidth() * 0.5f;
                g.setColour(stale_ || status_.startsWith("Frozen") ? ShellTheme::textMuted()
                    : inTune ? ShellTheme::analyzer() : ShellTheme::warning());
                g.fillEllipse(markerX - 5.0f, scale.getCentreY() - 5.0f, 10.0f, 10.0f);
                g.setFont(10.0f);
                g.setColour(stale_ || status_.startsWith("Frozen")
                                ? ShellTheme::textMuted() : ShellTheme::textPrimary());
                g.drawText(stale_ || status_.startsWith("Frozen") ? "STALE MEASUREMENT"
                               : inTune ? "IN TUNE" : "TUNE TOWARD CENTER",
                           scale.withTrimmedTop(12.0f).toNearestInt(), juce::Justification::centredTop);
            }
        }
        else if (kind_ == VisualizationKind::correlation)
        {
            const auto centre = area.getCentreX();
            const auto track = area.withHeight(18.0f).withY(area.getCentreY() - 9.0f);
            g.setColour(ShellTheme::raised());
            g.fillRoundedRectangle(track, 4.0f);
            g.setColour(ShellTheme::border());
            g.drawVerticalLine(static_cast<int>(centre), area.getY(), area.getBottom());
            for (int tick = 0; tick <= 4; ++tick)
            {
                const auto x = area.getX() + area.getWidth() * static_cast<float>(tick) / 4.0f;
                g.drawVerticalLine(static_cast<int>(x), track.getY() - 4.0f, track.getBottom() + 4.0f);
            }
            const auto extent = std::abs(static_cast<float>(data.correlation)) * area.getWidth() * 0.5f;
            const auto bar = data.correlation < 0
                ? juce::Rectangle<float>(centre - extent, track.getY(), extent, track.getHeight())
                : juce::Rectangle<float>(centre, track.getY(), extent, track.getHeight());
            g.setColour(ShellTheme::audio().withAlpha(opacity));
            g.fillRoundedRectangle(bar, 4.0f);
            g.setColour(ShellTheme::textPrimary());
            g.setFont(juce::FontOptions(22.0f));
            g.drawText(juce::String(data.correlation, 3), area.withHeight(32.0f).toNearestInt(),
                       juce::Justification::centred);
            g.setFont(11.0f);
            g.setColour(ShellTheme::textSecondary());
            g.drawText("-1   Out of phase", static_cast<int>(area.getX()), static_cast<int>(track.getBottom() + 8.0f),
                       static_cast<int>(area.getWidth() * 0.4f), 18, juce::Justification::left);
            g.drawText("Uncorrelated", static_cast<int>(centre - area.getWidth() * 0.2f),
                       static_cast<int>(track.getBottom() + 8.0f), static_cast<int>(area.getWidth() * 0.4f), 18,
                       juce::Justification::centred);
            g.drawText("+1   In phase", static_cast<int>(area.getRight() - area.getWidth() * 0.4f),
                       static_cast<int>(track.getBottom() + 8.0f), static_cast<int>(area.getWidth() * 0.4f), 18,
                       juce::Justification::right);
        }
        else if (kind_ == VisualizationKind::oscilloscope)
        {
            g.setColour(ShellTheme::border().withAlpha(0.35f));
            for (int i = 0; i <= 4; ++i)
                g.drawHorizontalLine(static_cast<int>(area.getY() + area.getHeight() * static_cast<float>(i) / 4), area.getX(), area.getRight());
            for (int channel = 0; channel < data.stamp.channels; ++channel)
            {
                g.setColour(ShellTheme::audio().withAlpha(channel == 0 ? opacity : opacity * 0.68f));
                juce::Path envelope;
                const int pixels = juce::jlimit(1, 512, static_cast<int>(area.getWidth()));
                for (int pixel = 0; pixel < pixels; ++pixel)
                {
                    float minimum = 1.0f, maximum = -1.0f;
                    for (int bin = pixel * 512 / pixels; bin < (pixel + 1) * 512 / pixels; ++bin)
                    {
                        minimum = std::min(minimum, data.scopeMinimum[channel][bin]);
                        maximum = std::max(maximum, data.scopeMaximum[channel][bin]);
                    }
                    const auto x = area.getX() + area.getWidth() * static_cast<float>(pixel) / static_cast<float>(pixels);
                    const auto y1 = area.getCentreY() - juce::jlimit(-1.0f, 1.0f, maximum) * area.getHeight() * 0.45f;
                    const auto y2 = area.getCentreY() - juce::jlimit(-1.0f, 1.0f, minimum) * area.getHeight() * 0.45f;
                    envelope.startNewSubPath(x, y1);
                    envelope.lineTo(x, std::max(y1 + 0.5f, y2));
                }
                g.strokePath(envelope, juce::PathStrokeType(1.0f));
            }
            g.setColour(ShellTheme::textSecondary());
            g.drawText("+1", static_cast<int>(area.getX()), static_cast<int>(area.getY()), 24, 16, juce::Justification::left);
            g.drawText("-1", static_cast<int>(area.getX()), static_cast<int>(area.getBottom()) - 16, 24, 16, juce::Justification::left);
        }
        else if (kind_ == VisualizationKind::spectrum)
        {
            for (int level = 0; level >= -120; level -= 30)
            {
                const auto y = area.getY() + area.getHeight() * static_cast<float>(-level) / 120;
                g.setColour(ShellTheme::border().withAlpha(0.35f)); g.drawHorizontalLine(static_cast<int>(y), area.getX(), area.getRight());
                g.setColour(ShellTheme::textSecondary()); g.drawText(juce::String(level), static_cast<int>(area.getX()), static_cast<int>(y), 30, 13, juce::Justification::left);
            }
            for (int channel = 0; channel < data.stamp.channels; ++channel)
            {
                juce::Path path;
                for (int bin = 1; bin <= data.fftSize / 2; ++bin)
                {
                    const auto x = area.getX() + area.getWidth() * static_cast<float>(std::log(static_cast<double>(bin))
                                                                                   / std::log(data.fftSize / 2.0));
                    const auto y = area.getBottom() - area.getHeight() * (120 + juce::jlimit(-120.0f, 0.0f, data.amplitudeDb[channel][bin])) / 120;
                    if (bin == 1) path.startNewSubPath(x, y); else path.lineTo(x, y);
                }
                g.setColour(ShellTheme::audio().withAlpha(channel == 0 ? opacity : opacity * 0.68f));
                g.strokePath(path, juce::PathStrokeType(1.3f));
            }
        }
        else
        {
            for (int channel = 0; channel < data.stamp.channels; ++channel)
            {
                auto bar = area.withTrimmedTop(static_cast<float>(channel) * area.getHeight() / data.stamp.channels)
                               .withHeight(area.getHeight() / data.stamp.channels).reduced(0, 7);
                const auto amplitude = kind_ == VisualizationKind::peak ? data.displayedPeak[channel] : data.rms[channel];
                const auto fraction = (60 + juce::jlimit(-60.0f, 0.0f, audio::AnalyzerEngine::decibels(amplitude))) / 60;
                g.setColour(ShellTheme::raised()); g.fillRect(bar);
                g.setColour((data.overRange[channel] ? ShellTheme::error() : ShellTheme::analyzer()).withAlpha(opacity));
                g.fillRect(bar.withWidth(bar.getWidth() * fraction));
                if (kind_ == VisualizationKind::peak)
                {
                    const auto held = (60 + juce::jlimit(-60.0f, 0.0f, audio::AnalyzerEngine::decibels(data.heldPeak[channel]))) / 60;
                    g.setColour(ShellTheme::textPrimary());
                    g.drawVerticalLine(static_cast<int>(bar.getX() + bar.getWidth() * held), bar.getY(), bar.getBottom());
                }
                g.setColour(ShellTheme::textPrimary());
                const auto reported = kind_ == VisualizationKind::peak ? data.peak[channel] : data.rms[channel];
                g.drawText("Ch " + juce::String(data.stamp.firstChannel + channel + 1) + "   " + db(reported) + " dBFS",
                           bar.toNearestInt(), juce::Justification::centred);
            }
        }
    }
    g.setColour(stale_ ? ShellTheme::warning() : ShellTheme::textSecondary());
    if (waiting) g.drawFittedText(result_ ? (kind_ == VisualizationKind::correlation
                                                    ? result_->stamp.channels < 2 ? "Requires a stereo source"
                                                        : "No stereo variation in the analysis window"
                                                : kind_ == VisualizationKind::guitarTuner
                                                    ? "Preparing pitch window" : "Preparing contiguous analysis window")
                                          : status_.isEmpty() ? "Waiting for contiguous samples" : status_,
                                area.toNearestInt(), juce::Justification::centred, 3);
    juce::String caption = kind_ == VisualizationKind::oscilloscope ? "Min/max envelope | amplitude +/-1"
        : kind_ == VisualizationKind::spectrum ? "Hann | peak-amplitude dBFS | log Hz"
        : kind_ == VisualizationKind::peak ? "50 ms sample peak (not true peak) | hold 1 s"
        : kind_ == VisualizationKind::guitarTuner ? "Chromatic pitch | equal temperament | A4 reference 440 Hz"
        : kind_ == VisualizationKind::correlation ? "Mean-removed L/R correlation [-1,+1] over the RMS window"
        : "Raw RMS | 0 dBFS = amplitude 1 | no AES17 offset";
    if (result_ && kind_ == VisualizationKind::spectrum)
        caption = "N=" + juce::String(result_->fftSize) + " | bin " + juce::String(result_->binHz, 2) + " Hz | dominant " + juce::String(result_->dominantHz[0], 1) + " Hz";
    g.drawFittedText(caption, 8, getHeight() - 43, getWidth() - 16, 18, juce::Justification::centredLeft, 1);
    g.drawFittedText(status_, 8, getHeight() - 24, getWidth() - 16, 18, juce::Justification::centredLeft, 1);
    lastPaintMs_ = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - began).count();
    maxPaintMs_ = std::max(lastPaintMs_, maxPaintMs_);
}
VisualizationWidgets::VisualizationWidgets(VisualizationSession& session, VisualizationHost host) : session_(session), host_(host)
{
    setName(host == VisualizationHost::monitor ? "Signal Inspector" : "Signal preview");
    target_.setTitle("Observation source (scheduled runtime sources only)");
    target_.setTextWhenNothingSelected("No target - select a running source");
    target_.onChange = [this]
    {
        const auto ids = session_.sourceIds();
        const auto index = target_.getSelectedItemIndex();
        if (index >= 0 && static_cast<std::size_t>(index) < ids.size()) setTarget(ids[static_cast<std::size_t>(index)]);
    };
    pin_.setTooltip("Pin target; selection changes no longer retarget this view.");
    freeze_.setTooltip("Freeze a bounded display copy; stop this view's capture demand.");
    pin_.setToggleState(host == VisualizationHost::monitor, juce::dontSendNotification);
    freeze_.onClick = [this] { setFrozen(freeze_.getToggleState()); };
    details_.onClick = [this] { if (onOpenDetails) onOpenDetails(); };
    reset_.onClick = [this]
    {
        const auto result = session_.engine_.resetHold(source_);
        if (!result.succeeded) admissionError_ = juce::String::fromUTF8(result.error.c_str());
    };
    for (auto* child : std::initializer_list<juce::Component*>{&target_, &pin_, &freeze_, &details_, &reset_, &status_})
        addAndMakeVisible(child);
    addAndMakeVisible(generatorInfo_);
    generatorInfo_.setFont(juce::FontOptions(12.0f));
    generatorInfo_.setColour(juce::Label::textColourId, ShellTheme::textPrimary());
    status_.setText("No target. Structural nodes are not automatically processing.", juce::dontSendNotification);
    status_.setFont(juce::FontOptions(11.0f));
    for (std::size_t i = 0; i < plots_.size(); ++i)
    {
        plots_[i] = std::make_unique<VisualizationWidget>(static_cast<VisualizationKind>(i), host);
        if (host == VisualizationHost::monitor || (host == VisualizationHost::embedded && i == 0)) addAndMakeVisible(*plots_[i]);
        else tabs_.addTab(title(static_cast<VisualizationKind>(i)), ShellTheme::surface(), plots_[i].get(), false);
    }
    if (host == VisualizationHost::inspector) addAndMakeVisible(tabs_);
    if (host == VisualizationHost::embedded)
        for (auto* child : std::initializer_list<juce::Component*>{&target_, &pin_, &freeze_, &details_, &reset_, &status_})
            child->setVisible(false);
    details_.setVisible(host == VisualizationHost::inspector);
    session_.attach(*this);
    resized();
}
VisualizationWidgets::~VisualizationWidgets() { session_.detach(*this); tabs_.clearTabs(); }
void VisualizationWidgets::resized()
{
    if (host_ == VisualizationHost::embedded)
    {
        if (plots_[0]) plots_[0]->setBounds(getLocalBounds());
        return;
    }
    const auto selected = target_.getSelectedId();
    target_.clear(juce::dontSendNotification);
    target_.addItemList(session_.labels(), 1);
    target_.setSelectedId(selected, juce::dontSendNotification);
    auto bounds = getLocalBounds().reduced(4);
    target_.setBounds(bounds.removeFromTop(26));
    auto controls = bounds.removeFromTop(25);
    pin_.setBounds(controls.removeFromLeft(47)); freeze_.setBounds(controls.removeFromLeft(67));
    reset_.setBounds(controls.removeFromLeft(80)); details_.setBounds(controls);
    generatorInfo_.setVisible(generatorInfoData_.summary.isNotEmpty());
    if (generatorInfo_.isVisible())
        generatorInfo_.setBounds(bounds.removeFromTop(27).reduced(4, 1));
    status_.setBounds(bounds.removeFromBottom(32));
    if (host_ == VisualizationHost::monitor)
    {
        const auto rowHeight = bounds.getHeight() / 3;
        for (std::size_t rowIndex = 0; rowIndex < 3; ++rowIndex)
        {
            auto row = bounds.removeFromTop(rowIndex == 2 ? bounds.getHeight() : rowHeight);
            plots_[rowIndex * 2]->setBounds(row.removeFromLeft(row.getWidth() / 2).reduced(3));
            plots_[rowIndex * 2 + 1]->setBounds(row.reduced(3));
        }
    }
    else tabs_.setBounds(bounds);
}
void VisualizationWidgets::paint(juce::Graphics& g) { g.fillAll(ShellTheme::surface()); }
void VisualizationWidgets::releaseDemand()
{
    if (!demand_) return;
    const auto result = session_.engine_.demand(source_, false);
    if (!result.succeeded) status_.setText(result.error, juce::dontSendNotification);
    demand_ = false;
}
void VisualizationWidgets::setTarget(std::uint64_t source)
{
    if (source_ == source) return;
    releaseDemand(); source_ = source; frozenResult_.reset(); resumeAfterSequence_ = 0; admissionError_.clear();
    generatorInfoData_ = {};
    generatorInfo_.setText({}, juce::dontSendNotification);
    generatorInfo_.setVisible(false);
    freeze_.setToggleState(false, juce::dontSendNotification);
    const auto ids = session_.sourceIds();
    const auto found = std::find(ids.begin(), ids.end(), source);
    target_.setSelectedId(found == ids.end() ? 0 : static_cast<int>(found - ids.begin()) + 1, juce::dontSendNotification);
    for (auto& plot : plots_) plot->update({}, "Waiting for selected source", 1.0e20, false);
    // Reset rate deadlines after the forced old-target clear.
    for (auto& plot : plots_) { const auto result = plot->setRefreshHz(plot->refreshHz()); jassert(result.succeeded); }
    tick(juce::Time::getMillisecondCounterHiRes());
}
void VisualizationWidgets::followSelection(std::uint64_t source) { if (!pinned()) setTarget(source); }
void VisualizationWidgets::setGeneratorContext(GeneratorDisplayInfo info)
{
    generatorInfoData_ = std::move(info);
    generatorInfo_.setVisible(generatorInfoData_.summary.isNotEmpty());
    generatorInfo_.setText(generatorInfoData_.summary, juce::dontSendNotification);
    resized();
}
void VisualizationWidgets::setPinned(bool pinned) { pin_.setToggleState(pinned, juce::dontSendNotification); }
void VisualizationWidgets::setFrozen(bool frozen)
{
    freeze_.setToggleState(frozen, juce::dontSendNotification);
    if (frozen)
    {
        if (auto* source = session_.find(source_); source && source->latest)
            frozenResult_ = std::make_shared<audio::SignalAnalysis>(*source->latest);
        releaseDemand();
    }
    else
    {
        frozenResult_.reset();
        if (const auto* source = session_.find(source_); source && source->latest)
            resumeAfterSequence_ = source->latest->stamp.sequence;
    }
    tick(juce::Time::getMillisecondCounterHiRes());
}
bool VisualizationWidgets::visibleInHierarchy() const
{
    if (!isShowing()) return false;
    if (const auto* peer = getPeer(); peer && peer->isMinimised()) return false;
    auto visible = getLocalBounds();
    auto* child = static_cast<const juce::Component*>(this);
    for (auto* parent = getParentComponent(); parent; parent = parent->getParentComponent())
    {
        visible = visible.translated(child->getX(), child->getY()).getIntersection(parent->getLocalBounds());
        child = parent;
    }
    return !visible.isEmpty();
}
void VisualizationWidgets::visibilityChanged()
{
    if (!visibleInHierarchy()) releaseDemand();
}
void VisualizationWidgets::tick(double now)
{
    auto* source = session_.find(source_);
    const bool active = visibleInHierarchy() && !frozen() && source
        && (source->state == ObservationState::waiting || source->state == ObservationState::processing);
    if (active && !demand_)
    {
        const auto result = session_.engine_.demand(source_, true);
        demand_ = result.succeeded;
        admissionError_ = result.succeeded ? juce::String{} : juce::String::fromUTF8(result.error.c_str());
    }
    else if (!active) releaseDemand();
    if (!visibleInHierarchy()) return;
    auto result = frozen() ? frozenResult_ : source ? source->latest : nullptr;
    if (!frozen() && result && result->stamp.sequence <= resumeAfterSequence_) result.reset();
    bool stale = source && source->received > 0 && now - source->received > 500;
    juce::String state = !source_ ? "No target" : !source ? "Unsupported / not scheduled"
        : source->state == ObservationState::removed ? "Target removed"
        : source->state == ObservationState::unsupported ? "Unsupported source"
        : source->state == ObservationState::stopped ? "Audio stopped"
        : frozen() ? "Frozen (capture demand paused)"
        : !result ? "Waiting for samples" : stale ? "Stale - no new samples" : result->gap || result->captureDrops ? "Degraded - capture gaps" : "Live";
    if (source && (source->state == ObservationState::removed || source->state == ObservationState::stopped
                   || source->state == ObservationState::unsupported)) result.reset();
    reset_.setEnabled(result != nullptr && !frozen());
    if (admissionError_.isNotEmpty()) { state = admissionError_; result.reset(); }
    else if (source && source->error.isNotEmpty()) state = source->error;
    if (result)
    {
        state += " | " + juce::String(result->stamp.sampleRate, 0) + " Hz | drops " + juce::String(result->captureDrops);
        if (result->nonFinite) state += " | invalid samples";
        if (generatorInfoData_.durationSeconds > 0 && result->stamp.sampleRate > 0
            && generatorInfoData_.transport != 0)
        {
            const auto elapsed = static_cast<double>(result->stamp.nodeFrame) / result->stamp.sampleRate;
            const auto duration = generatorInfoData_.durationSeconds;
            const auto progress = generatorInfoData_.transport == 2 ? std::fmod(elapsed, duration)
                : std::min(elapsed, duration);
            const auto percent = 100.0 * progress / duration;
            const auto stateText = generatorInfoData_.transport == 1 && elapsed >= duration
                ? juce::String("Complete") : juce::String(percent, 1) + "%";
            generatorInfo_.setText(generatorInfoData_.summary + " | " + stateText,
                                    juce::dontSendNotification);
        }
        else generatorInfo_.setText(generatorInfoData_.summary, juce::dontSendNotification);
        status_.setText("Source " + juce::String(result->stamp.source) + " | epoch " + juce::String(result->stamp.epoch)
            + " | revision " + juce::String(result->stamp.revision) + " | " + juce::String(result->analysisMilliseconds, 3) + " ms analysis"
            + " | UI received " + juce::String(std::max(0.0, now - (source ? source->received : now)), 0) + " ms ago",
            juce::dontSendNotification);
    }
    else
    {
        status_.setText(state, juce::dontSendNotification);
        if (generatorInfo_.isVisible())
            generatorInfo_.setText(generatorInfoData_.summary, juce::dontSendNotification);
    }
    for (auto& plot : plots_) if (plot->isShowing()) plot->update(result, state, now, stale);
}
}
