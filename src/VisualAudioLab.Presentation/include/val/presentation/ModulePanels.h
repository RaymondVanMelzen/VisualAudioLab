#pragma once

#include <val/application/WorkspaceService.h>
#include <val/audio/AudioNode.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include <memory>
#include <string>

namespace val::presentation
{
/**
 * Searchable, keyboard-selectable built-in module catalog, with no graph/runtime ownership.
 * onInsert receives the displayed type name. The host maps the four visualization names to
 * audio.type "Analyzer" and its chosen visualization kind; other names are audio.type values.
 * Construct, use and destroy on the JUCE message thread.
 */
class ModuleCatalog final : public juce::Component
{
public:
    ModuleCatalog();
    ~ModuleCatalog() override;
    void resized() override;
    std::function<void(std::string type)> onInsert;

private:
    /** Owns the searchable JUCE list model and its child controls. */
    class Impl;
    std::unique_ptr<Impl> impl_;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ModuleCatalog)
};

/**
 * Message-thread inspector borrowing a stable WorkspaceService; the service must outlive it.
 * Resolves explicit audio.type only. Persisted base, bypass and successfully compiled source edits
 * use setParameter and its undo history. Overrides/automation are transient runtime-only actions.
 * The host supplies runtime/transport admission, reports failures with setStatusMessage, and calls
 * refresh after undo/redo, patch replacement or externally edited selected-node content.
 * Unsaved script drafts survive refresh and selection changes, keyed by patch/node identity.
 * A saved-source change while a draft exists is labelled; compiling explicitly replaces saved text.
 * Drafts are editor-instance memory only: project Save excludes them and editor destruction loses
 * them. Loading source replaces the current draft; loading a project with reused patch/node IDs may
 * retain an earlier draft with a saved-source conflict label. Revert to saved source explicitly
 * discards the selected draft; only Compile / Apply persists source.
 * No devices, transport, rendering thread, or audio processing is owned by this component.
 */
class ModuleEditor final : public juce::Component
{
public:
    explicit ModuleEditor(application::WorkspaceService& workspace);
    ~ModuleEditor() override;
    void resized() override;
    void setNode(std::uint64_t id);
    [[nodiscard]] std::uint64_t node() const noexcept;
    [[nodiscard]] std::uint64_t selected() const noexcept { return node(); }
    void refresh();
    void setStatusMessage(const juce::String& message);
    /** Runnable mono default for newly inserted Script nodes; no external file dependency. */
    [[nodiscard]] static std::string defaultScriptSource();

    /** Called after an accepted persisted edit; the host decides whether to republish its graph. */
    std::function<void()> onChanged;
    /** Transient actions only; the host syncs persisted base/bypass changes through onChanged. */
    std::function<void(std::uint64_t, std::uint16_t, float, audio::ParameterAction)> onRuntimeParameter;
    /**
     * Automation target and finite nonnegative seconds ahead. Host maps current epoch, revision
     * and sample position, and surfaces admission failures using setStatusMessage.
     * Scheduled values do not change the document's saved base.
     */
    std::function<void(std::uint64_t, std::uint16_t, float, double)> onScheduleParameter;

private:
    /** Owns the scrollable editor, processor metadata and lifetime-safe asynchronous source loader. */
    class Impl;
    std::unique_ptr<Impl> impl_;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ModuleEditor)
};
}
