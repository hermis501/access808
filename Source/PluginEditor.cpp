#include "PluginEditor.h"
#include "Presets.h"

using namespace access808;

namespace
{
    const float scaleChoices[] = { 1.0f, 1.25f, 1.5f, 1.75f, 2.0f };

   #if JUCE_MAC
    constexpr bool isMac = true;
   #else
    constexpr bool isMac = false;
   #endif
}

namespace access808::ui
{
// ============================================================================
// Theme and announcements

Theme Theme::dark()
{
    return { juce::Colour (0xff15161a), juce::Colour (0xff22242b), juce::Colour (0xff22242b),
             juce::Colours::white, juce::Colour (0xffffb000), juce::Colour (0xff30333c) };
}

Theme Theme::highContrast()
{
    return { juce::Colours::black, juce::Colours::black, juce::Colours::white,
             juce::Colours::white, juce::Colours::yellow, juce::Colour (0xff202020) };
}

namespace
{
    Speaker& currentSpeaker()
    {
        static Speaker speaker;
        return speaker;
    }
}

void setSpeaker (Speaker s) { currentSpeaker() = std::move (s); }

void announce (const juce::String& text, bool interrupt)
{
    if (auto& speaker = currentSpeaker())
        speaker (text, interrupt);
    else
        juce::AccessibilityHandler::postAnnouncement (text, interrupt ? juce::AccessibilityHandler::AnnouncementPriority::high
                                                                      : juce::AccessibilityHandler::AnnouncementPriority::medium);
}

// ============================================================================
// AccessibleSlider

AccessibleSlider::AccessibleSlider()
    : juce::Slider (juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight)
{
    setWantsKeyboardFocus (true);
    setTextBoxIsEditable (false); // the text box would be a second tab stop; Enter opens a prompt instead
    setTextBoxStyle (juce::Slider::TextBoxRight, true, 110, 24); // recreates the value box: see lookAndFeelChanged
    setScrollWheelEnabled (true);
}

AccessibleSlider::~AccessibleSlider()
{
    if (valueBox != nullptr)
        valueBox->removeMouseListener (&valueBoxClick);
}

void AccessibleSlider::lookAndFeelChanged()
{
    juce::Slider::lookAndFeelChanged(); // deletes and recreates the value box
    attachValueBox();
}

void AccessibleSlider::attachValueBox()
{
    if (valueBox != nullptr)
        valueBox->removeMouseListener (&valueBoxClick);
    valueBox = nullptr;

    for (auto* child : getChildren())
        if (auto* label = dynamic_cast<juce::Label*> (child))
            valueBox = label;

    if (valueBox != nullptr)
    {
        valueBox->addMouseListener (&valueBoxClick, false);
        valueBox->setMouseCursor (juce::MouseCursor::IBeamCursor);
        valueBox->setTooltip (getTooltip());
    }
}

void AccessibleSlider::setHelp (const juce::String& help)
{
    setHelpText (help);
    setTooltip (help);
    attachValueBox(); // copies the tooltip to the value box
}

void AccessibleSlider::ValueBoxClick::mouseUp (const juce::MouseEvent& e)
{
    if (e.mouseWasClicked() && ! e.mods.isPopupMenu() && owner.isEnabled())
        owner.promptForValue();
}

void AccessibleSlider::stepByProportion (double delta)
{
    const double current = valueToProportionOfLength (getValue());
    setValue (proportionOfLengthToValue (juce::jlimit (0.0, 1.0, current + delta)), juce::sendNotificationSync);
}

bool AccessibleSlider::keyPressed (const juce::KeyPress& key)
{
    const auto range = getRange();

    // Small step: 1% of the travel, but never less than one interval (so
    // whole-semitone controls move by one semitone).
    double small = 0.01;
    if (getInterval() > 0.0 && range.getLength() > 0.0)
    {
        const double intervalProportion = valueToProportionOfLength (range.getStart() + getInterval())
                                        - valueToProportionOfLength (range.getStart());
        small = juce::jmax (small, intervalProportion * 1.0001);
    }
    const double large = juce::jmax (0.1, small * 4.0);

    const auto mods = key.getModifiers();
    if (mods.isCtrlDown() || mods.isAltDown() || mods.isCommandDown())
        return juce::Slider::keyPressed (key);

    const auto code = key.getKeyCode();
    const double step = mods.isShiftDown() ? small * 0.25 : small;

    if (code == juce::KeyPress::upKey || code == juce::KeyPress::rightKey)   { stepByProportion (step);  return true; }
    if (code == juce::KeyPress::downKey || code == juce::KeyPress::leftKey)  { stepByProportion (-step); return true; }
    if (code == juce::KeyPress::pageUpKey)                                   { stepByProportion (large);  return true; }
    if (code == juce::KeyPress::pageDownKey)                                 { stepByProportion (-large); return true; }
    if (code == juce::KeyPress::homeKey) { setValue (getMinimum(), juce::sendNotificationSync); return true; }
    if (code == juce::KeyPress::endKey)  { setValue (getMaximum(), juce::sendNotificationSync); return true; }
    if (code == juce::KeyPress::returnKey || code == juce::KeyPress::F2Key)  { promptForValue(); return true; }

    return juce::Slider::keyPressed (key);
}

void AccessibleSlider::promptForValue()
{
    auto* window = new juce::AlertWindow ("Set " + getTitle(),
                                          "Type a value for " + getTitle() + ", then press Enter.",
                                          juce::MessageBoxIconType::NoIcon, this);
    window->addTextEditor ("value", getTextFromValue (getValue()), getTitle());
    window->addButton ("OK", 1, juce::KeyPress (juce::KeyPress::returnKey));
    window->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));

    if (auto* editor = window->getTextEditor ("value"))
    {
        editor->setTitle (getTitle() + " value");
        editor->selectAll();
    }

    juce::Component::SafePointer<AccessibleSlider> safeThis (this);
    juce::Component::SafePointer<juce::AlertWindow> safeWindow (window);

    window->enterModalState (true, juce::ModalCallbackFunction::create ([safeThis, safeWindow] (int result)
    {
        if (safeThis == nullptr)
            return;

        if (result == 1 && safeWindow != nullptr)
        {
            const auto text = safeWindow->getTextEditorContents ("value");
            safeThis->setValue (safeThis->getValueFromText (text), juce::sendNotificationSync);
            announce (safeThis->getTitle() + " set to " + safeThis->getTextFromValue (safeThis->getValue()));
        }
        safeThis->grabKeyboardFocus();
    }), true);
}

// ============================================================================
// HelpTooltipWindow

juce::String HelpTooltipWindow::getTipFor (juce::Component& c)
{
    const auto tip = juce::TooltipWindow::getTipFor (c);
    if (tip.isEmpty())
        return {};

    // A slider's value box or a combo box's text: describe the whole control.
    auto* owner = &c;
    if (auto* parent = c.getParentComponent())
        if (dynamic_cast<juce::Slider*> (parent) != nullptr || dynamic_cast<juce::ComboBox*> (parent) != nullptr)
            owner = parent;

    auto title = owner->getTitle();
    if (auto* button = dynamic_cast<juce::Button*> (owner); button != nullptr && title.isEmpty())
        title = button->getButtonText();
    if (auto* label = dynamic_cast<juce::Label*> (owner); label != nullptr && title.isEmpty())
        title = label->getText();

    auto text = title.isNotEmpty() ? title + ": " + tip : tip;
    if (dynamic_cast<juce::Slider*> (owner) != nullptr)
        text << "\nDouble-click to reset. Click the value to type one.";
    return text;
}

// ============================================================================
// Section

Section::Section (const juce::String& title, const Theme& t) : theme (t)
{
    setTitle (title);
    setFocusContainerType (FocusContainerType::focusContainer);
}

std::unique_ptr<juce::AccessibilityHandler> Section::createAccessibilityHandler()
{
    return std::make_unique<juce::AccessibilityHandler> (*this, juce::AccessibilityRole::group);
}

void Section::addRow (juce::Component& control, juce::Label* label)
{
    rows.push_back ({ &control, label });
    if (label != nullptr)
        addAndMakeVisible (*label);
    addAndMakeVisible (control);
}

void Section::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat().reduced (0.5f);
    g.setColour (theme.section);
    g.fillRoundedRectangle (bounds, 6.0f);
    g.setColour (theme.sectionOutline);
    g.drawRoundedRectangle (bounds, 6.0f, 1.5f);
    g.setColour (theme.accent);
    g.setFont (juce::FontOptions (19.0f, juce::Font::bold));
    g.drawText (getTitle(), getLocalBounds().removeFromTop (headerHeight).reduced (10, 0),
                juce::Justification::centredLeft);
}

void Section::paintOverChildren (juce::Graphics& g)
{
    // A visible focus ring, for sighted keyboard users; JUCE's default look
    // shows none on sliders.
    auto* focused = juce::Component::getCurrentlyFocusedComponent();
    if (focused == nullptr || ! isParentOf (focused))
        return;

    // Inside a combo box or a slider, outline the whole control.
    while (focused->getParentComponent() != nullptr && focused->getParentComponent() != this
           && (dynamic_cast<juce::ComboBox*> (focused->getParentComponent()) != nullptr
               || dynamic_cast<juce::Slider*> (focused->getParentComponent()) != nullptr))
        focused = focused->getParentComponent();

    const auto r = getLocalArea (focused, focused->getLocalBounds()).toFloat().expanded (2.0f);
    g.setColour (theme.accent);
    g.drawRoundedRectangle (r, 4.0f, 2.0f);
}

void Section::resized()
{
    auto area = getLocalBounds().reduced (10, 0);
    area.removeFromTop (headerHeight);

    for (auto& row : rows)
    {
        auto r = area.removeFromTop (rowHeight).reduced (0, 3);
        if (row.label != nullptr)
            row.label->setBounds (r.removeFromLeft (155));
        row.control->setBounds (r);
    }
}
} // namespace access808::ui

// ============================================================================
// Editor

Access808Editor::Access808Editor (Access808Processor& p)
    : AudioProcessorEditor (p), processor (p)
{
    setTitle ("Access808");
    setDescription ("Accessible 808 bass synthesizer with legato glide.");
    setLookAndFeel (&lookAndFeel);

    juce::PropertiesFile::Options opts;
    opts.applicationName = "Access808";
    opts.filenameSuffix = ".settings";
    opts.folderName = "Access808";
    opts.osxLibrarySubFolder = "Application Support";
    settings = std::make_unique<juce::PropertiesFile> (opts);
    if (settings->getIntValue ("theme", 0) == 1)
        theme = ui::Theme::highContrast();

    addAndMakeVisible (viewport);
    viewport.setViewedComponent (&scaledHolder, false);
    viewport.setScrollBarsShown (true, true);
    viewport.setTitle ("Access808 controls");
    scaledHolder.addAndMakeVisible (content);

    // Header for sighted users. Screen readers get the same through the window
    // title and F1, so these are not exposed.
    titleLabel.setText (juce::String ("Access808  ") + JucePlugin_VersionString, juce::dontSendNotification);
    titleLabel.setFont (juce::FontOptions (22.0f, juce::Font::bold));
    hintLabel.setText ("Hover a control for help. Double-click a slider to reset it, click its value to type one.",
                       juce::dontSendNotification);
    hintLabel.setFont (juce::FontOptions (15.0f));
    hintLabel.setJustificationType (juce::Justification::centredRight);
    for (auto* l : { &titleLabel, &hintLabel })
    {
        l->setAccessible (false);
        content.addAndMakeVisible (*l);
    }

    // Sections follow the parameter order, so the window and OSARA's parameter
    // list are learned once. ---- Across the top: Preset ----
    auto& presetSection = addSection ("Preset", fullWidth);
    presetBox.setTitle ("Preset");
    presetBox.setHelpText ("Factory and user presets. Arrow keys load the next or previous preset.");
    presetBox.onChange = [this] { loadPresetFromBox(); };
    for (auto* c : std::initializer_list<juce::Component*> { &presetBox, &prevButton, &nextButton })
        presetRow.addAndMakeVisible (*c);
    presetSection.addRow (presetRow, &makeLabel ("Preset"));

    const auto trash = juce::String (isMac ? "Trash" : "Recycle Bin");
    const std::pair<juce::TextButton*, const char*> buttonInfo[] = {
        { &prevButton,   "Load the previous preset in the list." },
        { &nextButton,   "Load the next preset in the list." },
        { &saveButton,   "Save the current sound over the selected user preset. For a factory preset, this asks for a new name. Shortcut Control+S." },
        { &saveAsButton, "Save the current sound as a new user preset, with a name and an optional category. Shortcut Control+Shift+S." },
        { &renameButton, "Rename the selected user preset or change its category." },
        { &deleteButton, nullptr },
        { &importButton, "Copy preset files, for example from a friend, into your user presets, then load the first one." },
        { &exportButton, "Save the current sound as a preset file anywhere, to share it or keep a backup." },
        { &folderButton, isMac ? "Open your user preset folder in Finder." : "Open your user preset folder in File Explorer." },
    };
    for (const auto& [button, help] : buttonInfo)
    {
        const auto text = help != nullptr ? juce::String (help)
                                          : "Delete the selected user preset. It goes to the " + trash + ".";
        button->setHelpText (text);
        button->setTooltip (text);
    }
    prevButton.setTitle ("Previous preset");
    nextButton.setTitle ("Next preset");
    importButton.setTitle ("Import presets");
    exportButton.setTitle ("Export preset");
    folderButton.setTitle ("Open preset folder");
    presetBox.setTooltip (presetBox.getHelpText());

    prevButton.onClick   = [this] { stepPreset (-1); };
    nextButton.onClick   = [this] { stepPreset (1); };
    saveButton.onClick   = [this] { savePreset(); };
    saveAsButton.onClick = [this] { showSaveDialog(); };
    renameButton.onClick = [this] { showRenameDialog(); };
    deleteButton.onClick = [this] { deleteCurrentPreset(); };
    importButton.onClick = [this] { importPresets(); };
    exportButton.onClick = [this] { exportPreset(); };
    folderButton.onClick = [this] { openPresetFolder(); };
    for (auto* b : { &saveButton, &saveAsButton, &renameButton, &deleteButton, &importButton, &exportButton, &folderButton })
        presetButtons.addAndMakeVisible (*b);
    presetSection.addRow (presetButtons, nullptr);
    refreshPresetBox();

    // ---- Column 1: Glide, Pitch ----
    auto& glide = addSection ("Glide", 0);
    for (auto id : { ids::glideMode, ids::glideTime, ids::glideSync, ids::glideTiming, ids::glideCurve,
                     ids::slideRetrigger, ids::notePriority })
        addParameter (glide, id);

    auto& pitch = addSection ("Pitch", 0);
    for (auto id : { ids::tune, ids::fine, ids::tuningRef, ids::pitchDrop, ids::pitchDropTime })
        addParameter (pitch, id);

    // ---- Column 2: Envelope, Tone, Saturation and Filter ----
    auto& env = addSection ("Amp Envelope", 1);
    for (auto id : { ids::attack, ids::hold, ids::decay, ids::decayCurve, ids::sustain, ids::release, ids::phaseReset })
        addParameter (env, id);

    auto& tone = addSection ("Tone", 1);
    for (auto id : { ids::waveShape, ids::subLevel, ids::clickType, ids::clickLevel, ids::clickTone,
                     ids::clickFollowsAttack })
        addParameter (tone, id);

    auto& sat = addSection ("Saturation and Filter", 1);
    for (auto id : { ids::saturationType, ids::drive, ids::driveCompensation, ids::saturationMix,
                     ids::oversampling, ids::cutoff, ids::resonance })
        addParameter (sat, id);

    // ---- Column 3: Modulation, Playing, Mix, Window ----
    auto& mod = addSection ("Modulation", 2);
    for (auto id : { ids::vibratoRate, ids::vibratoDepth, ids::vibratoDelay, ids::drift })
        addParameter (mod, id);

    auto& playing = addSection ("Playing", 2);
    for (auto id : { ids::velocitySens, ids::velocityToDrive, ids::velocityToClick, ids::bendRange })
        addParameter (playing, id);

    auto& mix = addSection ("Mix and Ducking", 2);
    for (auto id : { ids::stereoWidth, ids::duckAmount, ids::duckThreshold, ids::duckRelease, ids::output })
        addParameter (mix, id);
    duckCheckButton.setHelpText ("Says whether kick audio is reaching the sidechain input and how much the 808 is ducked. "
                                 "Play the kick, then press this.");
    duckCheckButton.setTooltip (duckCheckButton.getHelpText());
    duckCheckButton.onClick = [this] { speakDuckStatus(); };
    mix.addRow (duckCheckButton, nullptr);

    auto& settingsSection = addSection ("Window", 2);
    sizeBox.setTitle ("Window Size");
    sizeBox.setHelpText ("Makes the whole window larger.");
    for (int i = 0; i < (int) std::size (scaleChoices); ++i)
        sizeBox.addItem (juce::String (juce::roundToInt (scaleChoices[i] * 100.0f)) + "%", i + 1);
    themeBox.setTitle ("Theme");
    themeBox.setHelpText ("High contrast uses black, white and yellow.");
    themeBox.addItem ("Dark", 1);
    themeBox.addItem ("High contrast", 2);
    settingsSection.addRow (sizeBox, &makeLabel ("Window Size"));
    settingsSection.addRow (themeBox, &makeLabel ("Theme"));
    sizeBox.setTooltip (sizeBox.getHelpText());
    themeBox.setTooltip (themeBox.getHelpText());
    helpButton.setHelpText ("Speaks what the last focused control does. F1 does the same from any control.");
    helpButton.setTooltip (helpButton.getHelpText());
    helpButton.onClick = [this] { speakFocusedHelp(); };
    settingsSection.addRow (helpButton, nullptr);

    // Tab order: section by section in the order above (column by column).
    for (size_t i = 0; i < sections.size(); ++i)
        sections[i]->setExplicitFocusOrder ((int) i + 1);

    layoutContent();

    // Settings, applied after layout so the size is known.
    int scaleIndex = juce::jlimit (0, (int) std::size (scaleChoices) - 1, settings->getIntValue ("scale", 0));
    scale = scaleChoices[scaleIndex];
    sizeBox.setSelectedItemIndex (scaleIndex, juce::dontSendNotification);
    themeBox.setSelectedItemIndex (settings->getIntValue ("theme", 0), juce::dontSendNotification);
    sizeBox.onChange = [this]
    {
        const int idx = sizeBox.getSelectedItemIndex();
        scale = scaleChoices[juce::jlimit (0, (int) std::size (scaleChoices) - 1, idx)];
        settings->setValue ("scale", idx);
        settings->saveIfNeeded();
        applyScale();
        ui::announce ("Window size " + sizeBox.getText());
    };
    themeBox.onChange = [this]
    {
        const int idx = themeBox.getSelectedItemIndex();
        theme = idx == 1 ? ui::Theme::highContrast() : ui::Theme::dark();
        settings->setValue ("theme", idx);
        settings->saveIfNeeded();
        applyTheme();
    };

    ui::setSpeaker ([this] (const juce::String& text, bool interrupt) { speak (text, interrupt); });
    if (auto* sync = processor.apvts.getParameter (ids::glideSync))
        glideSyncIndex = sync->getParameterIndex();

    applyTheme();
    processor.addListener (this);
    juce::Desktop::getInstance().addFocusChangeListener (this);
    applyScale();
}

Access808Editor::~Access808Editor()
{
    ui::setSpeaker (nullptr);
    juce::Desktop::getInstance().removeFocusChangeListener (this);
    processor.removeListener (this);
    setLookAndFeel (nullptr);
}

juce::Label& Access808Editor::makeLabel (const juce::String& text)
{
    auto label = std::make_unique<juce::Label>();
    label->setText (text, juce::dontSendNotification);
    label->setFont (juce::FontOptions (16.0f));
    label->setAccessible (false); // the control carries the name itself; avoid reading it twice
    label->setMinimumHorizontalScale (0.7f);
    extraLabels.push_back (std::move (label));
    return *extraLabels.back();
}

ui::Section& Access808Editor::addSection (const juce::String& title, int column)
{
    sections.push_back (std::make_unique<ui::Section> (title, theme));
    sectionColumns.push_back (column);
    content.addAndMakeVisible (*sections.back());
    return *sections.back();
}

void Access808Editor::addParameter (ui::Section& section, const char* paramId)
{
    auto* param = processor.apvts.getParameter (paramId);
    jassert (param != nullptr);

    Control c;
    const auto name = param->getName (64);
    const auto help = getParameterHelp (paramId);

    if (auto* choice = dynamic_cast<juce::AudioParameterChoice*> (param))
    {
        auto box = std::make_unique<juce::ComboBox>();
        box->addItemList (choice->choices, 1);
        box->setTitle (name);
        box->setHelpText (help);
        box->setTooltip (help);
        c.comboAttachment = std::make_unique<ComboAttachment> (processor.apvts, paramId, *box);
        c.component = std::move (box);
    }
    else
    {
        auto slider = std::make_unique<ui::AccessibleSlider>();
        slider->setTitle (name);
        slider->setHelp (help);
        if (juce::String (paramId) == ids::glideTime)
            glideTimeSlider = slider.get();
        c.sliderAttachment = std::make_unique<SliderAttachment> (processor.apvts, paramId, *slider);
        // Double-click (or Alt+click) goes back to the parameter's default.
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (param))
            slider->setDoubleClickReturnValue (true, ranged->convertFrom0to1 (ranged->getDefaultValue()));
        c.component = std::move (slider);
    }

    auto& label = makeLabel (name);
    label.setTooltip (dynamic_cast<juce::SettableTooltipClient*> (c.component.get())->getTooltip());
    section.addRow (*c.component, &label);
    controls.push_back (std::move (c));
}

// ---- Presets ----

void Access808Editor::refreshPresetBox()
{
    auto& pm = processor.presets;
    pm.refresh();

    presetBox.clear (juce::dontSendNotification);
    const auto& entries = pm.getEntries();
    for (int i = 0; i < (int) entries.size(); ++i)
        presetBox.addItem (entries[(size_t) i].displayName, i + 1);

    const int idx = pm.indexOfDisplayName (pm.getCurrentName());
    if (idx >= 0)
        presetBox.setSelectedItemIndex (idx, juce::dontSendNotification);
    else
        presetBox.setText (pm.getCurrentName(), juce::dontSendNotification);
}

void Access808Editor::loadPresetFromBox()
{
    const int index = presetBox.getSelectedItemIndex();
    auto& pm = processor.presets;
    if (! juce::isPositiveAndBelow (index, (int) pm.getEntries().size()))
        return;

    const auto entry = pm.getEntries()[(size_t) index];
    juce::String error;
    if (entry.isFactory())
        processor.setCurrentProgram (entry.factoryIndex); // keeps the host's program in sync
    else
        error = pm.load (index);

    // The screen reader already reads the new list item, so only errors are announced.
    if (error.isNotEmpty())
        ui::announce (error, true);
}

void Access808Editor::stepPreset (int delta)
{
    const int n = (int) processor.presets.getEntries().size();
    if (n == 0)
        return;

    const int current = presetBox.getSelectedItemIndex();
    const int next = current < 0 ? (delta > 0 ? 0 : n - 1) : (current + delta + n) % n;
    presetBox.setSelectedItemIndex (next, juce::sendNotificationSync); // loads it
    // Focus stays on the button, so the screen reader doesn't read the list.
    ui::announce (presetBox.getText());
}

void Access808Editor::savePreset()
{
    const auto& entries = processor.presets.getEntries();
    const int idx = presetBox.getSelectedItemIndex();
    if (! juce::isPositiveAndBelow (idx, (int) entries.size()) || entries[(size_t) idx].isFactory())
    {
        showSaveDialog(); // a factory preset or a changed sound with no user preset: ask for a name
        return;
    }

    const auto error = processor.presets.saveOver (idx);
    refreshPresetBox();
    ui::announce (error.isEmpty() ? "Saved " + processor.presets.getCurrentName() : error, error.isNotEmpty());
}

juce::File Access808Editor::getLastPresetFolder() const
{
    // The folder last used to import or export, so the dialogs open there
    // again. Documents until then, or when that folder is gone.
    const auto path = settings->getValue ("lastPresetFolder");
    if (juce::File::isAbsolutePath (path) && juce::File (path).isDirectory())
        return juce::File (path);
    return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory);
}

void Access808Editor::setLastPresetFolder (const juce::File& folder)
{
    settings->setValue ("lastPresetFolder", folder.getFullPathName());
    settings->saveIfNeeded();
}

void Access808Editor::importPresets()
{
    fileChooser = std::make_unique<juce::FileChooser> ("Import Access808 presets", getLastPresetFolder(),
                                                       juce::String ("*") + PresetManager::extension);
    juce::Component::SafePointer<Access808Editor> safeThis (this);
    fileChooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles
                                  | juce::FileBrowserComponent::canSelectMultipleItems,
                              [safeThis] (const juce::FileChooser& chooser)
    {
        if (safeThis == nullptr)
            return;
        safeThis->importButton.grabKeyboardFocus();

        const auto files = chooser.getResults();
        if (files.isEmpty())
        {
            ui::announce ("Import cancelled");
            return;
        }

        safeThis->setLastPresetFolder (files.getFirst().getParentDirectory());
        const auto result = safeThis->processor.presets.importFiles (files);
        safeThis->refreshPresetBox();

        juce::String text;
        if (result.imported > 0)
        {
            const int idx = safeThis->processor.presets.indexOfDisplayName (result.firstDisplayName);
            if (idx >= 0)
                safeThis->presetBox.setSelectedItemIndex (idx, juce::sendNotificationSync);
            text << "Imported " << result.imported << (result.imported == 1 ? " preset" : " presets")
                 << ". Loaded " << result.firstDisplayName << ".";
        }
        if (! result.failed.isEmpty())
            text << (text.isEmpty() ? "" : " ") << "Not an Access808 preset, skipped: "
                 << result.failed.joinIntoString (", ") << ".";
        ui::announce (text, result.imported == 0);
    });
}

void Access808Editor::exportPreset()
{
    const auto start = getLastPresetFolder().getChildFile (processor.presets.getExportName() + PresetManager::extension);
    fileChooser = std::make_unique<juce::FileChooser> ("Export the current sound as a preset file", start,
                                                       juce::String ("*") + PresetManager::extension);
    juce::Component::SafePointer<Access808Editor> safeThis (this);
    fileChooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                                  | juce::FileBrowserComponent::warnAboutOverwriting,
                              [safeThis] (const juce::FileChooser& chooser)
    {
        if (safeThis == nullptr)
            return;
        safeThis->exportButton.grabKeyboardFocus();

        auto file = chooser.getResult();
        if (file == juce::File())
        {
            ui::announce ("Export cancelled");
            return;
        }
        if (! file.hasFileExtension (PresetManager::extension))
            file = file.withFileExtension (PresetManager::extension);

        safeThis->setLastPresetFolder (file.getParentDirectory());
        const auto error = safeThis->processor.presets.exportTo (file);
        ui::announce (error.isEmpty() ? "Exported " + file.getFileName() + " to " + file.getParentDirectory().getFullPathName()
                                      : error,
                      error.isNotEmpty());
    });
}

void Access808Editor::openPresetFolder()
{
    // Opens File Explorer or Finder, which the screen reader reads by itself.
    const auto dir = PresetManager::getUserPresetDirectory();
    if (! dir.createDirectory() || ! dir.startAsProcess())
        ui::announce ("Could not open the preset folder " + dir.getFullPathName() + ".", true);
}

void Access808Editor::showNameDialog (const juce::String& title, const juce::String& name, const juce::String& category,
                                      std::function<void (const juce::String&, const juce::String&)> onOk)
{
    auto* window = new juce::AlertWindow (title, "Type a name and an optional category, then press Enter.",
                                          juce::MessageBoxIconType::NoIcon, this);
    window->addTextEditor ("name", name, "Name");
    window->addTextEditor ("category", category, "Category");
    window->addButton ("OK", 1, juce::KeyPress (juce::KeyPress::returnKey));
    window->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    if (auto* e = window->getTextEditor ("name"))     { e->setTitle ("Preset name"); e->selectAll(); }
    if (auto* e = window->getTextEditor ("category")) e->setTitle ("Category, for example Trap or Drill");

    juce::Component::SafePointer<Access808Editor> safeThis (this);
    juce::Component::SafePointer<juce::AlertWindow> safeWindow (window);

    window->enterModalState (true, juce::ModalCallbackFunction::create ([safeThis, safeWindow, onOk] (int result)
    {
        if (safeThis == nullptr)
            return;
        if (result == 1 && safeWindow != nullptr)
            onOk (safeWindow->getTextEditorContents ("name"), safeWindow->getTextEditorContents ("category"));
        else
            ui::announce ("Cancelled");
        safeThis->presetBox.grabKeyboardFocus();
    }), true);
}

void Access808Editor::showSaveDialog()
{
    const auto& entries = processor.presets.getEntries();
    const int idx = presetBox.getSelectedItemIndex();
    juce::String name, category;
    if (juce::isPositiveAndBelow (idx, (int) entries.size()) && ! entries[(size_t) idx].isFactory())
    {
        name = entries[(size_t) idx].name;
        category = entries[(size_t) idx].category;
    }

    showNameDialog ("Save preset", name, category, [this] (const juce::String& n, const juce::String& c)
    {
        const auto error = processor.presets.saveUser (n, c);
        refreshPresetBox();
        ui::announce (error.isEmpty() ? "Saved " + processor.presets.getCurrentName() : error);
    });
}

void Access808Editor::showRenameDialog()
{
    const auto& entries = processor.presets.getEntries();
    const int idx = presetBox.getSelectedItemIndex();
    if (! juce::isPositiveAndBelow (idx, (int) entries.size()) || entries[(size_t) idx].isFactory())
    {
        ui::announce ("Factory presets cannot be renamed. Select a user preset first.", true);
        return;
    }

    const auto entry = entries[(size_t) idx];
    showNameDialog ("Rename preset", entry.name, entry.category, [this, idx] (const juce::String& n, const juce::String& c)
    {
        const auto error = processor.presets.renameUser (idx, n, c);
        refreshPresetBox();
        ui::announce (error.isEmpty() ? "Renamed to " + processor.presets.getCurrentName() : error);
    });
}

void Access808Editor::deleteCurrentPreset()
{
    const auto& entries = processor.presets.getEntries();
    const int idx = presetBox.getSelectedItemIndex();
    if (! juce::isPositiveAndBelow (idx, (int) entries.size()) || entries[(size_t) idx].isFactory())
    {
        ui::announce ("Factory presets cannot be deleted. Select a user preset first.", true);
        return;
    }

    const auto displayName = entries[(size_t) idx].displayName;
    juce::Component::SafePointer<Access808Editor> safeThis (this);
    juce::AlertWindow::showOkCancelBox (juce::MessageBoxIconType::QuestionIcon, "Delete preset",
                                        "Delete " + displayName + "? It goes to the "
                                            + juce::String (isMac ? "Trash." : "Recycle Bin."),
                                        "Delete", "Cancel", this,
                                        juce::ModalCallbackFunction::create ([safeThis, idx, displayName] (int result)
    {
        if (safeThis == nullptr)
            return;
        if (result == 1)
        {
            const auto error = safeThis->processor.presets.deleteUser (idx);
            safeThis->processor.presets.setCurrentName ("Init");
            safeThis->refreshPresetBox();
            ui::announce (error.isEmpty() ? "Deleted " + displayName : error);
        }
        safeThis->presetBox.grabKeyboardFocus();
    }));
}

bool Access808Editor::keyPressed (const juce::KeyPress& key)
{
    if (key == juce::KeyPress::F1Key)
    {
        speakFocusedHelp();
        return true;
    }
    if (key == juce::KeyPress ('s', juce::ModifierKeys::ctrlModifier | juce::ModifierKeys::shiftModifier, 0))
    {
        showSaveDialog();
        return true;
    }
    if (key == juce::KeyPress ('s', juce::ModifierKeys::ctrlModifier, 0))
    {
        savePreset();
        return true;
    }
    return false;
}

void Access808Editor::audioProcessorChanged (juce::AudioProcessor*, const ChangeDetails& details)
{
    if (! details.programChanged)
        return;

    juce::Component::SafePointer<Access808Editor> safeThis (this);
    juce::MessageManager::callAsync ([safeThis]
    {
        if (safeThis != nullptr)
            safeThis->refreshPresetBox();
    });
}

// ---- Speech, help and status ----

void Access808Editor::speak (const juce::String& text, bool interrupt)
{
    if (text.trim().isEmpty())
        return;

    // OSARA speaks exactly like the rest of REAPER; outside REAPER a UIA
    // notification reaches NVDA, JAWS and Narrator in the user's own voice.
    if (auto osara = processor.getOsaraOutputMessage())
    {
        osara (text.toRawUTF8());
        return;
    }
    announcer.announce (text, interrupt);
}

void Access808Editor::parentHierarchyChanged()
{
    // The notification window must exist before the first announcement, or a
    // screen reader that hasn't seen it yet may drop that message.
    if (isShowing())
        announcer.prepare();
}

void Access808Editor::speakFocusedHelp()
{
    // NVDA doesn't read accessibility help text on focus, so F1 speaks it.
    auto* c = lastFocusedControl;
    if (c == nullptr || ! content.isParentOf (c))
    {
        speak ("Move to a control first, then press F1 for its help. "
               "Arrow keys change a value, Page Up and Page Down in big steps, Home and End go to the ends, "
               "Enter or F2 types an exact value. Control+S saves the preset, Control+Shift+S saves it under a new name.", true);
        return;
    }

    // A combo box's popup or a slider's text box: use the owning control.
    while (c != nullptr && c->getHelpText().isEmpty() && c->getParentComponent() != &content)
        c = c->getParentComponent();
    if (c == nullptr)
        return;

    auto text = c->getTitle();
    if (auto* slider = dynamic_cast<juce::Slider*> (c))
        text << ", " << slider->getTextFromValue (slider->getValue());
    else if (auto* box = dynamic_cast<juce::ComboBox*> (c))
        text << ", " << box->getText();
    text << ". " << c->getHelpText();
    speak (text, true);
}

void Access808Editor::speakDuckStatus()
{
    const auto st = processor.getDuckStatus();
    juce::String text;
    if (! st.inputConnected)
        text = "The sidechain input is off. In REAPER, open the plugin pins and route the kick to inputs 1 and 2.";
    else if (! st.signalRecent)
        text = "No kick audio in the last 2 seconds. Send the kick track to this track's channels 1 and 2, then play.";
    else if (st.amount <= 0.0f)
        text = "Kick audio is arriving, but Duck Amount is 0%, so nothing is ducked.";
    else if (st.reductionDb < 0.5f)
        text = "Kick audio is arriving but too quiet to duck. Lower the Duck Threshold.";
    else
        text = "Kick audio is arriving. The 808 is ducked by up to " + juce::String (st.reductionDb, 1) + " dB.";
    speak (text, true);
}

void Access808Editor::audioProcessorParameterChanged (juce::AudioProcessor*, int index, float)
{
    // Glide Time's value text says when Glide Sync makes it inactive.
    if (index != glideSyncIndex || glideTimeSlider == nullptr)
        return;

    juce::Component::SafePointer<juce::Slider> safe (glideTimeSlider);
    juce::MessageManager::callAsync ([safe]
    {
        if (safe != nullptr)
            safe->updateText();
    });
}

// ---- Look, size and focus ----

void Access808Editor::applyTheme()
{
    auto& lf = lookAndFeel;
    lf.setColour (juce::ResizableWindow::backgroundColourId, theme.background);
    lf.setColour (juce::Label::textColourId, theme.text);
    lf.setColour (juce::Slider::thumbColourId, theme.accent);
    lf.setColour (juce::Slider::trackColourId, theme.accent.withAlpha (0.7f));
    lf.setColour (juce::Slider::backgroundColourId, theme.control);
    lf.setColour (juce::Slider::textBoxTextColourId, theme.text);
    lf.setColour (juce::Slider::textBoxOutlineColourId, theme.sectionOutline);
    lf.setColour (juce::ComboBox::backgroundColourId, theme.control);
    lf.setColour (juce::ComboBox::textColourId, theme.text);
    lf.setColour (juce::ComboBox::outlineColourId, theme.accent);
    lf.setColour (juce::ComboBox::arrowColourId, theme.accent);
    lf.setColour (juce::TextButton::buttonColourId, theme.control);
    lf.setColour (juce::TextButton::textColourOffId, theme.text);
    lf.setColour (juce::PopupMenu::backgroundColourId, theme.background);
    lf.setColour (juce::PopupMenu::textColourId, theme.text);
    lf.setColour (juce::PopupMenu::highlightedBackgroundColourId, theme.accent);
    lf.setColour (juce::PopupMenu::highlightedTextColourId, juce::Colours::black);
    lf.setColour (juce::ScrollBar::thumbColourId, theme.accent);
    lf.setColour (juce::TextEditor::backgroundColourId, theme.control);
    lf.setColour (juce::TextEditor::textColourId, theme.text);
    lf.setColour (juce::AlertWindow::backgroundColourId, theme.background);
    lf.setColour (juce::AlertWindow::textColourId, theme.text);
    lf.setColour (juce::TooltipWindow::backgroundColourId, theme.control);
    lf.setColour (juce::TooltipWindow::textColourId, theme.text);
    lf.setColour (juce::TooltipWindow::outlineColourId, theme.accent);
    titleLabel.setColour (juce::Label::textColourId, theme.accent);
    hintLabel.setColour (juce::Label::textColourId, theme.text.withAlpha (0.75f));

    for (auto& c : controls)
        c.component->sendLookAndFeelChange();
    sendLookAndFeelChange();
    repaint();
}

void Access808Editor::layoutContent()
{
    contentWidth = margin + numColumns * (columnWidth + margin);
    const int innerWidth = contentWidth - 2 * margin;

    // Header line, then full-width sections, then the columns below them.
    auto header = juce::Rectangle<int> (margin, margin / 2, innerWidth, headerHeight);
    titleLabel.setBounds (header.removeFromLeft (260));
    hintLabel.setBounds (header);

    int top = margin / 2 + headerHeight + margin / 2;
    for (size_t i = 0; i < sections.size(); ++i)
    {
        if (sectionColumns[i] != fullWidth)
            continue;
        const int h = sections[i]->getPreferredHeight();
        sections[i]->setBounds (margin, top, innerWidth, h);
        top += h + margin;
    }

    std::array<int, numColumns> y {};
    y.fill (top);

    for (size_t i = 0; i < sections.size(); ++i)
    {
        const int col = sectionColumns[i];
        if (col == fullWidth)
            continue;
        const int h = sections[i]->getPreferredHeight();
        sections[i]->setBounds (margin + col * (columnWidth + margin), y[(size_t) col], columnWidth, h);
        y[(size_t) col] += h + margin;
    }

    contentHeight = *std::max_element (y.begin(), y.end());
    content.setBounds (0, 0, contentWidth, contentHeight);

    // Preset row: the list, then Previous and Next.
    auto row = presetRow.getLocalBounds();
    nextButton.setBounds (row.removeFromRight (110).reduced (2, 0));
    prevButton.setBounds (row.removeFromRight (110).reduced (2, 0));
    presetBox.setBounds (row.withTrimmedRight (6));

    // Buttons, evenly spread.
    auto b = presetButtons.getLocalBounds();
    juce::TextButton* buttons[] = { &saveButton, &saveAsButton, &renameButton, &deleteButton,
                                    &importButton, &exportButton, &folderButton };
    const int w = b.getWidth() / (int) std::size (buttons);
    for (auto* button : buttons)
        button->setBounds (b.removeFromLeft (w).reduced (2, 0));
}

void Access808Editor::applyScale()
{
    content.setTransform (juce::AffineTransform::scale (scale));
    const int scaledW = juce::roundToInt (contentWidth * scale);
    const int scaledH = juce::roundToInt (contentHeight * scale);
    scaledHolder.setSize (scaledW, scaledH);

    // Never larger than the screen; the viewport scrolls the rest into view.
    juce::Rectangle<int> area (0, 0, 1920, 1080);
    if (auto* display = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay())
        area = display->userBounds.toNearestInt();
    const int maxW = juce::roundToInt (area.getWidth() * 0.95f);
    const int maxH = juce::roundToInt (area.getHeight() * 0.9f);
    const int scrollbar = viewport.getScrollBarThickness();
    const bool needsScroll = scaledW > maxW || scaledH > maxH;
    setSize (juce::jmin (maxW, scaledW + (needsScroll ? scrollbar : 0)),
             juce::jmin (maxH, scaledH + (needsScroll ? scrollbar : 0)));
}

void Access808Editor::globalFocusChanged (juce::Component* focused)
{
    // Remembered for F1 and the help button (which takes focus itself).
    if (focused != nullptr && content.isParentOf (focused) && focused != &helpButton)
        lastFocusedControl = focused;

    // Move the focus ring (drawn by the sections).
    for (auto& s : sections)
        s->repaint();

    // Keep the focused control visible when the window is scaled and scrolls.
    if (focused == nullptr || ! content.isParentOf (focused))
        return;

    const auto r = scaledHolder.getLocalArea (focused, focused->getLocalBounds()).expanded (20);
    auto view = viewport.getViewArea();
    int x = view.getX(), y = view.getY();
    if (r.getRight() > view.getRight())   x = r.getRight() - view.getWidth();
    if (r.getX() < x)                     x = r.getX();
    if (r.getBottom() > view.getBottom()) y = r.getBottom() - view.getHeight();
    if (r.getY() < y)                     y = r.getY();
    viewport.setViewPosition (juce::jmax (0, x), juce::jmax (0, y));
}

void Access808Editor::paint (juce::Graphics& g)
{
    g.fillAll (theme.background);
}

void Access808Editor::resized()
{
    viewport.setBounds (getLocalBounds());
}
