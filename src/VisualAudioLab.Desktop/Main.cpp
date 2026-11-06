#include <val/application/Logger.h>
#include <val/application/SettingsService.h>
#include <val/infrastructure/FileLogger.h>
#include <val/infrastructure/LocalSettingsStore.h>
#include <val/infrastructure/LocalProjectStore.h>
#include <val/infrastructure/AudioBackends.h>
#include <val/presentation/MainComponent.h>
#include <val/presentation/BuildInfo.h>
#include <val/presentation/WindowPlacement.h>
#include <ValResources.h>
#include <iostream>

namespace val::desktop
{
namespace
{
std::filesystem::path nativePath(const juce::File& file)
{
    const auto utf8 = file.getFullPathName().toStdString();
    return std::filesystem::path(std::u8string(utf8.begin(), utf8.end()));
}

/** Owns the native top-level shell; application service lifetimes enclose its content. */
class MainWindow final : public juce::DocumentWindow
{
public:
    MainWindow(application::WorkspaceService& workspace, application::ProjectSession& project,
               const application::Settings& settings)
        : juce::DocumentWindow("Visual Audio Lab", juce::Colour(0xff20252b), allButtons)
    {
        setIcon(juce::ImageCache::getFromMemory(val_resources::VAL_256x256_png,
                                                val_resources::VAL_256x256_pngSize));
        setUsingNativeTitleBar(true);
        setResizable(true, false);
        setResizeLimits(640, 380, 7680, 4320);
        content_ = new presentation::MainComponent(workspace, &project, [](application::AudioBackendKind kind) -> std::unique_ptr<application::IAudioBackend>
        {
            switch (kind)
            {
                case application::AudioBackendKind::asio: return std::make_unique<infrastructure::AsioBackend>();
                case application::AudioBackendKind::wasapiExclusive: return std::make_unique<infrastructure::WasapiExclusiveBackend>();
                case application::AudioBackendKind::wasapiShared: return std::make_unique<infrastructure::WasapiSharedBackend>();
                case application::AudioBackendKind::coreAudio: return std::make_unique<infrastructure::CoreAudioBackend>();
                case application::AudioBackendKind::directSound: return std::make_unique<infrastructure::DirectSoundBackend>();
            }
            return {};
        });
        setContentOwned(content_, false);
        content_->setInspectorVisible(settings.inspectorVisible);
        content_->runtime().restoreSetup(settings.audioSetup);
        const auto& displays = juce::Desktop::getInstance().getDisplays();
        if (const auto* display = displays.getPrimaryDisplay())
        {
            const auto area = display->userArea;
            auto desired = juce::Rectangle<int>(area.getX(), area.getY(),
                                                 settings.windowWidth, settings.windowHeight);
            const auto frame = presentation::windowFrameSize(*this);
            auto placement = presentation::fitWindowToUserArea(desired, area, frame);
            frame.addTo(placement.bounds);
            placement.bounds.setCentre(area.getCentre());
            frame.subtractFrom(placement.bounds);
            setResizeLimits(placement.minimumWidth, placement.minimumHeight,
                            placement.maximumWidth, placement.maximumHeight);
            workArea_ = area;
            frameSize_ = frame;
            setBounds(placement.bounds);
        }
        else
        {
            setResizeLimits(640, 380, 7680, 4320);
            centreWithSize(settings.windowWidth, settings.windowHeight);
        }
        setVisible(true);
        placementReady_ = true;
        fitToCurrentDisplayIfNeeded();
    }

    void closeButtonPressed() override { juce::JUCEApplication::getInstance()->systemRequestedQuit(); }
    presentation::MainComponent& content() noexcept { return *content_; }
    void moved() override
    {
        fitToCurrentDisplayIfNeeded();
    }

    void resized() override
    {
        juce::DocumentWindow::resized();
        fitToCurrentDisplayIfNeeded();
    }

private:
    void fitToDisplay(const juce::Displays::Display& display, juce::Rectangle<int> desired,
                      juce::BorderSize<int> frame)
    {
        const auto placement = presentation::fitWindowToUserArea(desired, display.userArea, frame);
        const juce::ScopedValueSetter<bool> fitting(fittingPlacement_, true);
        workArea_ = display.userArea;
        frameSize_ = frame;
        setResizeLimits(placement.minimumWidth, placement.minimumHeight,
                        placement.maximumWidth, placement.maximumHeight);
        if (getBounds() != placement.bounds)
            setBounds(placement.bounds);
    }

    void fitToCurrentDisplayIfNeeded()
    {
        if (!placementReady_ || fittingPlacement_ || isFullScreen())
            return;

        const auto frame = presentation::windowFrameSize(*this);
        auto outerBounds = getBounds();
        frame.addTo(outerBounds);
        const auto& displays = juce::Desktop::getInstance().getDisplays();
        if (const auto* display = displays.getDisplayForRect(outerBounds);
            display && (display->userArea != workArea_ || frame != frameSize_))
            fitToDisplay(*display, getBounds(), frame);
    }

    presentation::MainComponent* content_{};
    juce::Rectangle<int> workArea_;
    juce::BorderSize<int> frameSize_;
    bool placementReady_{}, fittingPlacement_{};
};

/** Explicit composition root for the UI shell, local adapters and application-owned services. */
class DesktopApplication final : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override { return "Visual Audio Lab"; }
    const juce::String getApplicationVersion() override { return val::presentation::productVersion; }
    bool moreThanOneInstanceAllowed() override { return false; }

    void initialise(const juce::String& commandLine) override
    {
        if (commandLine.isNotEmpty())
        {
            std::cerr << "This skeleton accepts no command-line arguments.\n";
            setApplicationReturnValue(2);
            quit();
            return;
        }
        const auto root = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                              .getChildFile("VisualAudioLab");
        logger_ = std::make_unique<infrastructure::FileLogger>(nativePath(root.getChildFile("Logs/shell.log")));
        store_ = std::make_unique<infrastructure::LocalSettingsStore>(nativePath(root.getChildFile("settings.json")));
        settings_ = std::make_unique<application::SettingsService>(*store_);
        const auto loaded = settings_->load();
        projectStore_ = std::make_unique<infrastructure::LocalProjectStore>(nativePath(root.getChildFile("recent-projects.json")));
        project_ = std::make_unique<application::ProjectSession>(workspace_, *projectStore_);
        window_ = std::make_unique<MainWindow>(workspace_, *project_, settings_->current());
        if (!loaded.succeeded)
        {
            window_->content().showStatus(juce::String::fromUTF8(loaded.error.c_str())
                                          + " Using temporary defaults; original will not be overwritten.");
            log(application::LogLevel::warning, "settings.load", "Preferences unavailable; preserving original.");
        }
        log(application::LogLevel::info, "lifecycle", "Application started with explicit audio transport; no automatic device probing/playback.");
    }

    void systemRequestedQuit() override
    {
        if (quitting_)
            return;
        if (window_)
            window_->content().requestClose([this] { finishQuit(); });
        else
            finishQuit();
    }

    void finishQuit()
    {
        quitting_ = true;
        if (window_ && settings_)
        {
            const application::Settings value{window_->getWidth(), window_->getHeight(),
                                               window_->content().inspectorVisible(), window_->content().runtime().setupPreference()};
            const auto saved = settings_->save(value);
            if (!saved.succeeded)
            {
                log(application::LogLevel::error, "settings.save", "Preferences could not be saved.");
                juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
                    "Settings not saved", juce::String::fromUTF8(saved.error.c_str())
                    + "\nThe application will close without changing the existing settings.",
                    "Close", window_.get(), juce::ModalCallbackFunction::create([](int)
                    {
                        juce::JUCEApplication::getInstance()->quit();
                    }));
                return;
            }
        }
        quit();
    }

    void shutdown() override
    {
        window_.reset();
        project_.reset();
        projectStore_.reset();
        if (logger_)
            log(application::LogLevel::info, "lifecycle", "Application skeleton stopped.");
        settings_.reset();
        store_.reset();
        logger_.reset();
    }

    void anotherInstanceStarted(const juce::String& commandLine) override
    {
        if (window_)
        {
            window_->toFront(true);
            if (commandLine.isNotEmpty())
                window_->content().showStatus("This skeleton accepts no command-line arguments.");
        }
    }

private:
    void log(application::LogLevel level, std::string_view category, std::string_view message)
    {
        const auto result = logger_->write(level, category, message);
        if (!result.succeeded)
        {
            std::cerr << result.error << '\n';
            if (window_)
                window_->content().showStatus(juce::String::fromUTF8(result.error.c_str()));
        }
    }

    std::unique_ptr<infrastructure::FileLogger> logger_;
    std::unique_ptr<infrastructure::LocalSettingsStore> store_;
    std::unique_ptr<application::SettingsService> settings_;
    application::WorkspaceService workspace_;
    std::unique_ptr<infrastructure::LocalProjectStore> projectStore_;
    std::unique_ptr<application::ProjectSession> project_;
    std::unique_ptr<MainWindow> window_;
    bool quitting_{false};
};
}
}

START_JUCE_APPLICATION(val::desktop::DesktopApplication)
