#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "PluginProcessor.h"
#include "ScreenReaderAnnouncer.h"

// Screen-reader-first editor. Every control has an accessible title and help
// text, sections are accessibility groups, and Tab order follows the visual
// sections top to bottom, column by column.
//
// Slider keys: arrows = small step (Shift = finer), Page Up/Down = large step,
// Home/End = minimum/maximum, Enter or F2 = type an exact value.
// Ctrl+S = save over the selected user preset (Save As for a factory one),
// Ctrl+Shift+S = Save As. F1 = speak help for the focused control.
//
// Mouse: double-click (or Alt+click) a slider to reset it to its default,
// click a slider's value to type one, hover any control for its help text.

namespace access808::ui
{
struct Theme
{
    juce::Colour background, section, sectionOutline, text, accent, control;

    static Theme dark();
    static Theme highContrast();
};

// Speaks text in the user's own screen reader voice: through OSARA inside
// REAPER, else a UIA notification, else JUCE's announcement. `interrupt`
// stops current speech (errors, and answers to a key the user just pressed).
void announce (const juce::String& text, bool interrupt = false);

// Where announce() sends speech; the open editor sets it. Message thread only.
using Speaker = std::function<void (const juce::String&, bool)>;
void setSpeaker (Speaker);

class AccessibleSlider : public juce::Slider
{
public:
    AccessibleSlider();
    ~AccessibleSlider() override;
    bool keyPressed (const juce::KeyPress&) override;

    // Sets the help text and the tooltip (the same text) on the slider and its
    // value box. JUCE gives screen readers a slider's tooltip as its help.
    void setHelp (const juce::String& help);
    void promptForValue();

private:
    void lookAndFeelChanged() override;
    void attachValueBox();
    void stepByProportion (double delta);

    // A click on the value text opens the typed-value prompt.
    struct ValueBoxClick : juce::MouseListener
    {
        explicit ValueBoxClick (AccessibleSlider& s) : owner (s) {}
        void mouseUp (const juce::MouseEvent& e) override;
        AccessibleSlider& owner;
    };

    ValueBoxClick valueBoxClick { *this };
    juce::Component::SafePointer<juce::Label> valueBox;
};

// Shows a control's tooltip with its name in front and, for sliders, the
// mouse hints. Controls keep the plain help text as their tooltip, because
// JUCE hands a slider's, combo box's or button's tooltip to screen readers as
// its help text; the decoration exists only on screen. JUCE hides the tooltip
// window itself from screen readers.
class HelpTooltipWindow : public juce::TooltipWindow
{
public:
    explicit HelpTooltipWindow (juce::Component* parent) : juce::TooltipWindow (parent, 700) {}
    juce::String getTipFor (juce::Component&) override;
};

// A titled box that is exposed as an accessibility group.
class Section : public juce::Component
{
public:
    Section (const juce::String& title, const Theme& theme);
    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override; // outlines the focused control
    void resized() override;

    void addRow (juce::Component& control, juce::Label* label);

    static constexpr int headerHeight = 32;
    static constexpr int rowHeight = 32;
    int getPreferredHeight() const { return headerHeight + rowHeight * (int) rows.size() + 8; }

    std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override;

private:
    struct Row { juce::Component* control; juce::Label* label; };
    std::vector<Row> rows;
    const Theme& theme;
};
} // namespace access808::ui

class Access808Editor : public juce::AudioProcessorEditor,
                        private juce::AudioProcessorListener,
                        private juce::FocusChangeListener
{
public:
    explicit Access808Editor (Access808Processor&);
    ~Access808Editor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;

private:
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ComboAttachment  = juce::AudioProcessorValueTreeState::ComboBoxAttachment;

    struct Control
    {
        std::unique_ptr<juce::Label> label;
        std::unique_ptr<juce::Component> component;
        std::unique_ptr<SliderAttachment> sliderAttachment;
        std::unique_ptr<ComboAttachment> comboAttachment;
    };

    access808::ui::Section& addSection (const juce::String& title, int column);
    void addParameter (access808::ui::Section&, const char* paramId);
    juce::Label& makeLabel (const juce::String& text);

    void refreshPresetBox();
    void loadPresetFromBox();
    void stepPreset (int delta);
    void savePreset();
    void importPresets();
    void exportPreset();
    void openPresetFolder();
    juce::File getLastPresetFolder() const;
    void setLastPresetFolder (const juce::File&);
    void showSaveDialog();
    void showRenameDialog();
    void deleteCurrentPreset();
    void showNameDialog (const juce::String& title, const juce::String& name, const juce::String& category,
                         std::function<void (const juce::String&, const juce::String&)> onOk);

    void applyTheme();
    void applyScale();
    void layoutContent();

    void speakFocusedHelp();
    void speakDuckStatus();
    void speak (const juce::String& text, bool interrupt);
    void parentHierarchyChanged() override;

    void audioProcessorParameterChanged (juce::AudioProcessor*, int index, float) override;
    void audioProcessorChanged (juce::AudioProcessor*, const ChangeDetails&) override;
    void globalFocusChanged (juce::Component* focused) override;

    Access808Processor& processor;
    access808::ScreenReaderAnnouncer announcer { *this };
    juce::LookAndFeel_V4 lookAndFeel; // declared before the components so it outlives them
    access808::ui::HelpTooltipWindow tooltipWindow { this }; // help text on mouse hover
    access808::ui::Theme theme = access808::ui::Theme::dark();
    std::unique_ptr<juce::PropertiesFile> settings;
    float scale = 1.0f;

    juce::Viewport viewport;
    juce::Component scaledHolder; // sized at the scaled size, for the viewport
    juce::Component content;      // laid out at 100%, scaled with a transform

    std::vector<std::unique_ptr<access808::ui::Section>> sections;
    std::vector<int> sectionColumns;
    std::vector<Control> controls;
    std::vector<std::unique_ptr<juce::Label>> extraLabels;

    juce::Label titleLabel, hintLabel; // painted header, not exposed to screen readers
    juce::ComboBox presetBox;
    juce::Component presetRow;     // the preset list with Previous and Next
    juce::Component presetButtons;
    juce::TextButton prevButton { "Previous" }, nextButton { "Next" };
    juce::TextButton saveButton { "Save" }, saveAsButton { "Save As" }, renameButton { "Rename" },
                     deleteButton { "Delete" }, importButton { "Import" }, exportButton { "Export" },
                     folderButton { "Open Folder" };
    std::unique_ptr<juce::FileChooser> fileChooser;
    juce::ComboBox sizeBox, themeBox;
    juce::TextButton duckCheckButton { "Check Ducking" }, helpButton { "Help for Focused Control (F1)" };
    juce::Component* lastFocusedControl = nullptr;
    juce::Slider* glideTimeSlider = nullptr;
    int glideSyncIndex = -1;

    static constexpr int numColumns = 3;
    static constexpr int columnWidth = 410;
    static constexpr int margin = 12;
    static constexpr int headerHeight = 34;
    static constexpr int fullWidth = -1; // addSection column for a section across all columns
    int contentWidth = 0, contentHeight = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Access808Editor)
};
