#include <val/presentation/DockWorkspace.h>
#include <val/presentation/ShellTheme.h>

namespace val::presentation
{
WorkspacePanel::WorkspacePanel(PanelId panel, std::function<void()> collapseRequested, juce::Component* content)
    : panel_(panel), content_(content)
{
    setName(panelName(panel));
    heading_.setText(panelName(panel), juce::dontSendNotification);
    heading_.setJustificationType(juce::Justification::centredLeft);
    collapse_.setTitle("Collapse " + juce::String(panelName(panel)));
    collapse_.setTooltip("Collapse " + juce::String(panelName(panel)));
    collapse_.onClick = [callback = std::move(collapseRequested)] { if (callback) callback(); };
    collapse_.setWantsKeyboardFocus(true);
    addAndMakeVisible(heading_);
    if (panel != PanelId::patchCanvas)
        addAndMakeVisible(collapse_);
    if (content_)
        addAndMakeVisible(content_);
}

void WorkspacePanel::paint(juce::Graphics& graphics)
{
    graphics.fillAll(ShellTheme::surface());
    graphics.setColour(ShellTheme::border());
    graphics.drawHorizontalLine(36, 0.0f, static_cast<float>(getWidth()));
}

void WorkspacePanel::resized()
{
    auto bounds = getLocalBounds().reduced(6, 4);
    auto header = bounds.removeFromTop(30);
    if (panel_ != PanelId::patchCanvas)
    {
        collapse_.setBounds(header.removeFromRight(28).reduced(2));
        collapse_.setButtonText("-");
    }
    heading_.setBounds(header);
    if (content_)
        content_->setBounds(bounds);
}

void WorkspacePanel::setContent(juce::Component& content)
{
    if (content_ && content_->getParentComponent() == this)
        removeChildComponent(content_);
    content_ = &content;
    addAndMakeVisible(content);
    resized();
}

DockWorkspace::Divider::Divider(std::function<void(int)> moved) : moved_(std::move(moved))
{
    setName("Resize fixed workspace panel (Left/Right arrows)");
    setWantsKeyboardFocus(true);
    setMouseCursor(juce::MouseCursor::LeftRightResizeCursor);
}

void DockWorkspace::Divider::paint(juce::Graphics& graphics)
{
    graphics.fillAll(hasKeyboardFocus(false) || isMouseOver()
                         ? ShellTheme::accent() : ShellTheme::border());
}

void DockWorkspace::Divider::mouseDown(const juce::MouseEvent& event)
{
    grabKeyboardFocus();
    previous_ = event.getScreenX();
}

void DockWorkspace::Divider::mouseDrag(const juce::MouseEvent& event)
{
    const auto current = event.getScreenX();
    moved_(current - previous_);
    previous_ = current;
}

bool DockWorkspace::Divider::keyPressed(const juce::KeyPress& key)
{
    if (key == juce::KeyPress::leftKey)
        moved_(-12);
    else if (key == juce::KeyPress::rightKey)
        moved_(12);
    else
        return false;
    return true;
}

DockWorkspace::DockWorkspace(application::WorkspaceService& workspace)
    : workspace_(workspace), canvas_(workspace),
      leftDivider_([this](int delta)
      {
          leftRatio_ = juce::jlimit(0.16f, 0.34f, leftRatio_ + static_cast<float>(delta) / juce::jmax(1, getWidth()));
          resized();
      }),
      rightDivider_([this](int delta)
      {
          rightRatio_ = juce::jlimit(0.16f, 0.34f, rightRatio_ - static_cast<float>(delta) / juce::jmax(1, getWidth()));
          resized();
      })
{
    setName("Fixed audio workspace");
    leftDivider_.setName("Resize Module Browser width (Left/Right arrows)");
    rightDivider_.setName("Resize Property Inspector width (Left/Right arrows)");
    for (std::size_t index = 0; index < panels_.size(); ++index)
    {
        const auto id = static_cast<PanelId>(index);
        juce::Component* content = id == PanelId::patchCanvas ? static_cast<juce::Component*>(&canvas_)
            : id == PanelId::propertyInspector ? static_cast<juce::Component*>(&signalPreview_) : nullptr;
        panels_[index] = std::make_unique<WorkspacePanel>(id, [this, id]
        {
            setPanelVisible(id, false);
        }, content);
        addAndMakeVisible(*panels_[index]);
    }
    reopenBrowser_.setTitle("Show Module Browser");
    reopenBrowser_.setTooltip("Show Module Browser");
    reopenBrowser_.onClick = [this] { showPanel(PanelId::moduleBrowser); };
    reopenInspector_.setTitle("Show Property Inspector");
    reopenInspector_.setTooltip("Show Property Inspector");
    reopenInspector_.onClick = [this] { showPanel(PanelId::propertyInspector); };
    for (auto* control : {&reopenBrowser_, &reopenInspector_})
        control->setWantsKeyboardFocus(true);
    addAndMakeVisible(reopenBrowser_);
    addAndMakeVisible(reopenInspector_);
    addAndMakeVisible(leftDivider_);
    addAndMakeVisible(rightDivider_);
    signalPreview_.onOpenDetails = [this] { showSignalInspector(); };
    canvas_.onObservationTarget = [this](domain::GraphId source) { signalPreview_.followSelection(source); };
    visualizations_.bindCanvas(&canvas_);
}

DockWorkspace::~DockWorkspace()
{
    canvas_.onObservationTarget = {};
    visualizations_.bindCanvas(nullptr);
    signalWindow_.reset();
}

DockWorkspace::SignalInspectorWindow::SignalInspectorWindow(VisualizationSession& session)
    : juce::DocumentWindow("Signal Inspector", juce::Colour(0xff1a2532), juce::DocumentWindow::closeButton)
{
    setUsingNativeTitleBar(true);
    setContentOwned(new VisualizationWidgets(session, VisualizationHost::monitor), false);
    setResizable(true, true);
    setResizeLimits(640, 450, 1600, 1200);
    centreWithSize(960, 680);
}

void DockWorkspace::showSignalInspector()
{
    if (!signalWindow_)
    {
        signalWindow_ = std::make_unique<SignalInspectorWindow>(visualizations_);
        signalWindow_->setLookAndFeel(&getLookAndFeel());
        signalWindow_->view().setTarget(signalPreview_.target());
    }
    else
        signalWindow_->view().followSelection(signalPreview_.target());
    signalWindow_->setVisible(true);
    signalWindow_->toFront(true);
}

WorkspacePanel& DockWorkspace::panel(PanelId id) noexcept
{
    return *panels_[static_cast<std::size_t>(id)];
}

application::ProjectLayout DockWorkspace::projectLayout() const
{
    application::ProjectLayout value;
    value.visible = {true, layout_.placement(PanelId::moduleBrowser).visible, false,
                     layout_.placement(PanelId::propertyInspector).visible, false};
    value.selectedPanels.fill(-1);
    return value;
}

void DockWorkspace::restoreLayout(const application::ProjectLayout& value)
{
    layout_.setVisible(PanelId::patchCanvas, true);
    layout_.setVisible(PanelId::moduleBrowser, value.visible[1]);
    layout_.setVisible(PanelId::propertyInspector, value.visible[3]);
    leftRatio_ = 0.23f;
    rightRatio_ = 0.25f;
    resized();
}

void DockWorkspace::changed()
{
    if (onLayoutChanged)
        onLayoutChanged();
}

void DockWorkspace::showPanel(PanelId id)
{
    setPanelVisible(id, true);
    panel(id).grabKeyboardFocus();
}

void DockWorkspace::setPanelVisible(PanelId id, bool visible)
{
    if (id == PanelId::patchCanvas)
        visible = true;
    if (layout_.placement(id).visible == visible)
        return;
    layout_.setVisible(id, visible);
    resized();
    changed();
}

void DockWorkspace::resetLayout()
{
    layout_.reset();
    leftRatio_ = 0.23f;
    rightRatio_ = 0.25f;
    resized();
    showPanel(PanelId::patchCanvas);
}

void DockWorkspace::paint(juce::Graphics& graphics)
{
    graphics.fillAll(ShellTheme::background());
}

void DockWorkspace::resized()
{
    auto bounds = getLocalBounds();
    const bool browserVisible = layout_.placement(PanelId::moduleBrowser).visible;
    const bool inspectorVisible = layout_.placement(PanelId::propertyInspector).visible;
    const int railWidth = 28;
    const int leftWidth = browserVisible ? juce::roundToInt(static_cast<float>(getWidth()) * leftRatio_) : railWidth;
    const int rightWidth = inspectorVisible ? juce::roundToInt(static_cast<float>(getWidth()) * rightRatio_) : railWidth;
    panels_[static_cast<std::size_t>(PanelId::moduleBrowser)]->setVisible(browserVisible);
    panels_[static_cast<std::size_t>(PanelId::propertyInspector)]->setVisible(inspectorVisible);
    panels_[static_cast<std::size_t>(PanelId::patchCanvas)]->setVisible(true);
    reopenBrowser_.setVisible(!browserVisible);
    reopenInspector_.setVisible(!inspectorVisible);
    leftDivider_.setVisible(browserVisible);
    rightDivider_.setVisible(inspectorVisible);

    if (browserVisible)
    {
        panels_[1]->setBounds(bounds.removeFromLeft(leftWidth));
        leftDivider_.setBounds(bounds.removeFromLeft(6));
    }
    else
        reopenBrowser_.setBounds(bounds.removeFromLeft(railWidth).withHeight(34));

    if (inspectorVisible)
    {
        panels_[2]->setBounds(bounds.removeFromRight(rightWidth));
        rightDivider_.setBounds(bounds.removeFromRight(6));
    }
    else
        reopenInspector_.setBounds(bounds.removeFromRight(railWidth).withHeight(34));

    panels_[0]->setBounds(bounds);
}
}
