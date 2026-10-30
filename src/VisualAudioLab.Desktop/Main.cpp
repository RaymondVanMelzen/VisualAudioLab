#include <val/application/Logger.h>
#include <val/application/SettingsService.h>
#include <val/infrastructure/FileLogger.h>
#include <val/infrastructure/LocalSettingsStore.h>
#include <val/presentation/MainComponent.h>
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
    MainWindow(application::WorkspaceService& workspace, const application::Settings& settings)
        : juce::DocumentWindow("Visual Audio Lab", juce::Colour(0xff20252b), allButtons)
    {
        setUsingNativeTitleBar(true);
        setResizable(true, false);
        setResizeLimits(800, 600, 7680, 4320);
        content_ = new presentation::MainComponent(workspace);
        setContentOwned(content_, false);
        content_->setInspectorVisible(settings.inspectorVisible);
        centreWithSize(settings.windowWidth, settings.windowHeight);
        setVisible(true);
    }

    void closeButtonPressed() override { juce::JUCEApplication::getInstance()->systemRequestedQuit(); }
    presentation::MainComponent& content() noexcept { return *content_; }

private:
    presentation::MainComponent* content_{};
};

/** Explicit composition root for the UI shell, local adapters and application-owned services. */
class DesktopApplication final : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override { return "Visual Audio Lab"; }
    const juce::String getApplicationVersion() override { return "0.0.1"; }
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
        window_ = std::make_unique<MainWindow>(workspace_, settings_->current());
        if (!loaded.succeeded)
        {
            window_->content().showStatus(juce::String::fromUTF8(loaded.error.c_str())
                                          + " Using temporary defaults; original will not be overwritten.");
            log(application::LogLevel::warning, "settings.load", "Preferences unavailable; preserving original.");
        }
        log(application::LogLevel::info, "lifecycle", "Application skeleton started; no audio capabilities.");
    }

    void systemRequestedQuit() override
    {
        if (quitting_)
            return;
        quitting_ = true;
        if (window_ && settings_)
        {
            const application::Settings value{window_->getWidth(), window_->getHeight(),
                                               window_->content().inspectorVisible()};
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
    std::unique_ptr<MainWindow> window_;
    bool quitting_{false};
};
}
}

START_JUCE_APPLICATION(val::desktop::DesktopApplication)
