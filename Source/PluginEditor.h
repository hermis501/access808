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
// Ctrl+S = save the current sound as a user preset. F1 = speak help for the
// focused control.

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
    bool keyPressed (const juce::KeyPress&) override;

private:
    void stepByProportion (double delta);
    void promptForValue();
};

// A titled box that is exposed as an accessibility group.
class Section : public juce::Component
{
public:
    Section (const juce::String& title, const Theme& theme);
    void paint (juce::Graphics&) override;
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

    juce::ComboBox presetBox;
    juce::Component presetButtons;
    juce::TextButton saveButton { "Save As" }, renameButton { "Rename" }, deleteButton { "Delete" };
    juce::ComboBox sizeBox, themeBox;
    juce::TextButton duckCheckButton { "Check Ducking" }, helpButton { "Help for Focused Control (F1)" };
    juce::Component* lastFocusedControl = nullptr;
    juce::Slider* glideTimeSlider = nullptr;
    int glideSyncIndex = -1;

    static constexpr int numColumns = 3;
    static constexpr int columnWidth = 380;
    static constexpr int margin = 12;
    int contentWidth = 0, contentHeight = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Access808Editor)
};
