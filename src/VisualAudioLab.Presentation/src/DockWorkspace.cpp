#include <val/presentation/DockWorkspace.h>
#include <val/presentation/ShellTheme.h>

namespace val::presentation
{
WorkspacePanel::WorkspacePanel(PanelId panel, std::function<void(int)> dockRequested, juce::Component* content)
    : panel_(panel), content_(content)
{
    setName(panelName(panel));
    setWantsKeyboardFocus(true);
    heading_.setText(panelName(panel), juce::dontSendNotification);
    heading_.setMouseCursor(juce::MouseCursor::DraggingHandCursor);
    heading_.setTooltip("Drag this heading into a dock, or use the Dock selector.");
    heading_.addMouseListener(this, false);
    destination_.setTitle(juce::String(panelName(panel)) + " dock destination");
    destination_.setTooltip("Move this panel to a fixed dock, or hide it.");
    destination_.addItem("Dock: Left", 1);
    destination_.addItem("Dock: Centre", 2);
    destination_.addItem("Dock: Right", 3);
    destination_.addItem("Dock: Bottom", 4);
    if (panel != PanelId::patchCanvas)
        destination_.addItem("Hide panel", 5);
    destination_.onChange = [this, callback = std::move(dockRequested)]
    {
        callback(destination_.getSelectedId());
    };

    juce::String description;
    switch (panel)
    {
        case PanelId::patchCanvas:
            description = "Patch Canvas content is supplied by the desktop host.";
            break;
        case PanelId::moduleBrowser:
            description = "Module catalog is supplied by the desktop host.";
            break;
        case PanelId::projectExplorer:
            description = "Project Explorer\nUse File for project files and the patch selector to navigate graphs.";
            break;
        case PanelId::propertyInspector:
            description = "Select a placed node (module instance or routing utility) to inspect properties or signals.";
            break;
        case PanelId::audioSettings:
            description = "Audio Setup is supplied by the desktop runtime host. Hide this panel when configuration is complete.";
            break;
    }
    description_.setText(description, juce::dontSendNotification);
    description_.setJustificationType(juce::Justification::centred);
    for (auto* child : std::initializer_list<juce::Component*>{&heading_, &destination_, &description_})
        addAndMakeVisible(child);
    if (content_)
    {
        addAndMakeVisible(content_);
        description_.setVisible(false);
    }

}

WorkspacePanel::~WorkspacePanel()
{
    heading_.removeMouseListener(this);
}
void WorkspacePanel::setContent(juce::Component& content)
{
    if (content_ && content_->getParentComponent() == this) removeChildComponent(content_);
    content_ = &content; addAndMakeVisible(content_);
    description_.setVisible(false);
    resized();
}

void WorkspacePanel::paint(juce::Graphics& graphics)
{
    graphics.fillAll(ShellTheme::surface());
    if (panel_ == PanelId::patchCanvas)
    {
        graphics.setColour(juce::Colour(0xff303948));
        for (int x = 12; x < getWidth(); x += 24)
            for (int y = 80; y < getHeight(); y += 24)
                graphics.fillRect(x, y, 1, 1);
    }
    graphics.setColour(ShellTheme::border());
    graphics.drawHorizontalLine(68, 0.0f, static_cast<float>(getWidth()));
}

void WorkspacePanel::resized()
{
    auto bounds = getLocalBounds().reduced(8);
    heading_.setBounds(bounds.removeFromTop(24));
    destination_.setBounds(bounds.removeFromTop(28));
    bounds.removeFromTop(12);
    if (content_)
    {
        content_->setBounds(bounds);
        return;
    }
    description_.setBounds(bounds);
}

void WorkspacePanel::mouseDrag(const juce::MouseEvent& event)
{
    if (event.originalComponent == &heading_ && event.getDistanceFromDragStart() > 5)
        if (auto* container = juce::DragAndDropContainer::findParentDragContainerFor(this))
            if (!container->isDragAndDropActive())
                container->startDragging(static_cast<int>(panel_), this);
}

void WorkspacePanel::setDockPosition(DockPosition position)
{
    destination_.setSelectedId(static_cast<int>(position) + 1, juce::dontSendNotification);
}

DockWorkspace::DockArea::DockArea(DockWorkspace& workspace, DockPosition position)
    : juce::TabbedComponent(juce::TabbedButtonBar::TabsAtTop),
      workspace_(workspace), position_(position)
{
    setName(juce::StringArray{"Left", "Centre", "Right", "Bottom"}[static_cast<int>(position)]
            + " workspace dock");
    setTabBarDepth(30);
}

bool DockWorkspace::DockArea::isInterestedInDragSource(const SourceDetails& details)
{
    return workspace_.accepts(details);
}

void DockWorkspace::DockArea::itemDropped(const SourceDetails& details)
{
    if (workspace_.accepts(details))
        workspace_.movePanel(static_cast<WorkspacePanel*>(details.sourceComponent.get())->panelId(), position_);
}

void DockWorkspace::DockArea::paint(juce::Graphics& graphics)
{
    juce::TabbedComponent::paint(graphics);
    if (getNumTabs() == 0)
    {
        graphics.setColour(ShellTheme::border());
        graphics.drawRect(getLocalBounds());
        graphics.setColour(juce::Colours::lightgrey);
        graphics.drawFittedText("Drop panel here", getLocalBounds().reduced(4),
                                juce::Justification::centred, 2);
    }
}

DockWorkspace::Divider::Divider(bool vertical, std::function<void(int)> moved)
    : vertical_(vertical), moved_(std::move(moved))
{
    setWantsKeyboardFocus(true);
    setMouseCursor(vertical ? juce::MouseCursor::LeftRightResizeCursor
                            : juce::MouseCursor::UpDownResizeCursor);
}

void DockWorkspace::Divider::paint(juce::Graphics& graphics)
{
    graphics.fillAll(hasKeyboardFocus(false) || isMouseOver()
                         ? ShellTheme::accent() : ShellTheme::border());
}

void DockWorkspace::Divider::mouseDown(const juce::MouseEvent& event)
{
    grabKeyboardFocus();
    previous_ = vertical_ ? event.getScreenX() : event.getScreenY();
}

void DockWorkspace::Divider::mouseDrag(const juce::MouseEvent& event)
{
    const auto current = vertical_ ? event.getScreenX() : event.getScreenY();
    moved_(current - previous_);
    previous_ = current;
}

bool DockWorkspace::Divider::keyPressed(const juce::KeyPress& key)
{
    if (key == (vertical_ ? juce::KeyPress::leftKey : juce::KeyPress::upKey))
        moved_(-12);
    else if (key == (vertical_ ? juce::KeyPress::rightKey : juce::KeyPress::downKey))
        moved_(12);
    else
        return false;
    return true;
}

DockWorkspace::DockWorkspace(application::WorkspaceService& workspace)
    : canvas_(workspace), left_(*this, DockPosition::left), centre_(*this, DockPosition::centre),
      right_(*this, DockPosition::right), bottom_(*this, DockPosition::bottom),
      leftDivider_(true, [this](int delta)
      {
          leftRatio_ = juce::jlimit(0.18f, 0.30f, leftRatio_ + static_cast<float>(delta) / juce::jmax(1, getWidth()));
          resized();
      }),
      rightDivider_(true, [this](int delta)
      {
          rightRatio_ = juce::jlimit(0.18f, 0.30f, rightRatio_ - static_cast<float>(delta) / juce::jmax(1, getWidth()));
          resized();
      }),
      bottomDivider_(false, [this](int delta)
      {
          const auto height = static_cast<float>(juce::jmax(1, getHeight()));
          const auto minimum = static_cast<float>(juce::jmin(190, getHeight() / 2)) / height;
          bottomRatio_ = juce::jlimit(minimum, 0.5f,
              static_cast<float>(bottom_.getHeight() - delta) / height);
          resized();
      })
{
    setName("Dockable workspace");
    leftDivider_.setName("Resize left dock (Left/Right arrows)");
    rightDivider_.setName("Resize right dock (Left/Right arrows)");
    bottomDivider_.setName("Resize bottom dock (Up/Down arrows)");
    for (std::size_t index = 0; index < panels_.size(); ++index)
    {
        const auto id = static_cast<PanelId>(index);
        juce::Component* content = id == PanelId::patchCanvas ? static_cast<juce::Component*>(&canvas_)
            : id == PanelId::propertyInspector ? static_cast<juce::Component*>(&signalPreview_) : nullptr;
        panels_[index] = std::make_unique<WorkspacePanel>(id, [this, id](int destination)
        {
            if (destination == 5)
                setPanelVisible(id, false);
            else if (destination >= 1 && destination <= 4)
                movePanel(id, static_cast<DockPosition>(destination - 1));
        }, content);
    }
    for (auto* child : std::initializer_list<juce::Component*>{
             &left_, &centre_, &right_, &bottom_, &leftDivider_, &rightDivider_, &bottomDivider_})
        addAndMakeVisible(child);
    rebuild();
    signalPreview_.onOpenDetails = [this] { showSignalInspector(); };
    canvas_.onObservationTarget = [this](domain::GraphId source) { signalPreview_.followSelection(source); };
    visualizations_.bindCanvas(&canvas_);
}

DockWorkspace::~DockWorkspace()
{
    canvas_.onObservationTarget = {};
    visualizations_.bindCanvas(nullptr);
    signalWindow_.reset();
    for (auto* dock : {&left_, &centre_, &right_, &bottom_})
        dock->clearTabs();
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
    else signalWindow_->view().followSelection(signalPreview_.target());
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
    for (std::size_t i = 0; i < panels_.size(); ++i)
    {
        const auto& placement = layout_.placement(static_cast<PanelId>(i));
        value.docks[i] = static_cast<int>(placement.position);
        value.visible[i] = placement.visible;
    }
    value.ratios = {leftRatio_, rightRatio_, bottomRatio_};
    const std::array<const DockArea*, 4> docks{&left_, &centre_, &right_, &bottom_};
    value.selectedPanels.fill(-1);
    for (std::size_t dock = 0; dock < docks.size(); ++dock)
        for (std::size_t panelIndex = 0; panelIndex < panels_.size(); ++panelIndex)
            if (docks[dock]->getCurrentContentComponent() == panels_[panelIndex].get())
                value.selectedPanels[dock] = static_cast<int>(panelIndex);
    return value;
}

void DockWorkspace::restoreLayout(const application::ProjectLayout& value)
{
    for (std::size_t i = 0; i < panels_.size(); ++i)
    {
        const auto panelId = static_cast<PanelId>(i);
        layout_.move(panelId, static_cast<DockPosition>(value.docks[i]));
        layout_.setVisible(panelId, value.visible[i]);
    }
    leftRatio_ = static_cast<float>(value.ratios[0]);
    rightRatio_ = static_cast<float>(value.ratios[1]);
    bottomRatio_ = static_cast<float>(value.ratios[2]);
    rebuild();
    const std::array<DockArea*, 4> docks{&left_, &centre_, &right_, &bottom_};
    for (std::size_t dock = 0; dock < docks.size(); ++dock)
        for (int tab = 0; tab < docks[dock]->getNumTabs(); ++tab)
        {
            const auto selected = value.selectedPanels[dock];
            if (selected >= 0 && docks[dock]->getTabContentComponent(tab) == panels_[selected].get())
                docks[dock]->setCurrentTabIndex(tab);
        }
    resized();
}

DockWorkspace::DockArea& DockWorkspace::area(DockPosition position) noexcept
{
    switch (position)
    {
        case DockPosition::left: return left_;
        case DockPosition::centre: return centre_;
        case DockPosition::right: return right_;
        case DockPosition::bottom: return bottom_;
    }
    return centre_;
}

void DockWorkspace::rebuild()
{
    std::array<juce::Component*, 4> selected{};
    const std::array<DockArea*, 4> docks{&left_, &centre_, &right_, &bottom_};
    for (std::size_t index = 0; index < docks.size(); ++index)
    {
        selected[index] = docks[index]->getCurrentContentComponent();
        docks[index]->clearTabs();
    }
    for (std::size_t index = 0; index < panels_.size(); ++index)
    {
        const auto id = static_cast<PanelId>(index);
        const auto& placement = layout_.placement(id);
        panels_[index]->setDockPosition(placement.position);
        if (placement.visible)
            area(placement.position).addTab(panelName(id), ShellTheme::surface(), panels_[index].get(), false);
    }
    for (std::size_t index = 0; index < docks.size(); ++index)
        for (int tab = 0; tab < docks[index]->getNumTabs(); ++tab)
            if (docks[index]->getTabContentComponent(tab) == selected[index])
                docks[index]->setCurrentTabIndex(tab);
    resized();
}

void DockWorkspace::changed()
{
    if (onLayoutChanged)
        onLayoutChanged();
}

void DockWorkspace::showPanel(PanelId id)
{
    if (!layout_.placement(id).visible)
    {
        layout_.setVisible(id, true);
        rebuild();
    }
    auto& dock = area(layout_.placement(id).position);
    for (int index = 0; index < dock.getNumTabs(); ++index)
        if (dock.getTabContentComponent(index) == &panel(id))
            dock.setCurrentTabIndex(index);
    panel(id).grabKeyboardFocus();
    changed();
}

void DockWorkspace::setPanelVisible(PanelId id, bool visible)
{
    if (layout_.placement(id).visible == visible || (id == PanelId::patchCanvas && !visible))
        return;
    layout_.setVisible(id, visible);
    rebuild();
    changed();
}

void DockWorkspace::movePanel(PanelId id, DockPosition position)
{
    layout_.move(id, position);
    rebuild();
    showPanel(id);
}

void DockWorkspace::resetLayout()
{
    layout_.reset();
    leftRatio_ = 0.22f;
    rightRatio_ = 0.24f;
    bottomRatio_ = 0.26f;
    rebuild();
    showPanel(PanelId::patchCanvas);
}

bool DockWorkspace::accepts(const juce::DragAndDropTarget::SourceDetails& details) const
{
    for (const auto& view : panels_)
        if (details.sourceComponent.get() == view.get())
            return true;
    return false;
}

void DockWorkspace::paint(juce::Graphics& graphics)
{
    graphics.fillAll(ShellTheme::background());
}

void DockWorkspace::resized()
{
    auto bounds = getLocalBounds();
    // Keep an empty centre available as a drop target; empty peripheral docks collapse.
    const bool hasBottom = bottom_.getNumTabs() > 0;
    const bool hasLeft = left_.getNumTabs() > 0;
    const bool hasRight = right_.getNumTabs() > 0;
    bottom_.setVisible(hasBottom);
    left_.setVisible(hasLeft);
    right_.setVisible(hasRight);
    bottomDivider_.setVisible(hasBottom);
    leftDivider_.setVisible(hasLeft);
    rightDivider_.setVisible(hasRight);
    if (hasBottom)
    {
        const auto height = juce::jmin(bounds.getHeight() / 2,
            juce::jmax(190, juce::roundToInt(static_cast<float>(getHeight()) * bottomRatio_)));
        bottom_.setBounds(bounds.removeFromBottom(height));
        bottomDivider_.setBounds(bounds.removeFromBottom(6));
    }
    const int leftWidth = juce::roundToInt(static_cast<float>(getWidth()) * leftRatio_);
    const int rightWidth = juce::roundToInt(static_cast<float>(getWidth()) * rightRatio_);
    if (hasLeft)
    {
        left_.setBounds(bounds.removeFromLeft(leftWidth));
        leftDivider_.setBounds(bounds.removeFromLeft(6));
    }
    if (hasRight)
    {
        right_.setBounds(bounds.removeFromRight(rightWidth));
        rightDivider_.setBounds(bounds.removeFromRight(6));
    }
    centre_.setBounds(bounds);
}
}
