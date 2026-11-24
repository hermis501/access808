#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

// Factory presets plus user presets saved as files in
//   Windows: %APPDATA%\Access808\Presets\<Category>\<Name>.a808preset
//   macOS:   ~/Library/Application Support/Access808/Presets/<Category>/<Name>.a808preset
// The combined list is what the editor's preset box shows. Host programs
// (REAPER's preset menu) expose the factory presets only, because VST3 hosts
// expect a fixed program list.

namespace access808
{
class PresetManager
{
public:
    struct Entry
    {
        juce::String displayName; // "Trap - Long Slide" or "User - Bass - My 808"
        juce::String category;    // empty for factory presets
        juce::String name;
        juce::File file;          // empty for factory presets
        int factoryIndex = -1;

        bool isFactory() const { return factoryIndex >= 0; }
    };

    explicit PresetManager (juce::AudioProcessorValueTreeState&);

    static juce::File getUserPresetDirectory();
    // Tests point the user folder somewhere temporary; an empty File restores it.
    static void setUserPresetDirectoryForTests (const juce::File&);
    static constexpr const char* extension = ".a808preset";

    void refresh();
    const std::vector<Entry>& getEntries() const { return entries; }
    int indexOfDisplayName (const juce::String& displayName) const;

    // All return an empty string on success, or an error message to announce.
    juce::String load (int index);
    juce::String saveUser (const juce::String& name, const juce::String& category);
    juce::String renameUser (int index, const juce::String& newName, const juce::String& newCategory);
    juce::String deleteUser (int index);
    // Overwrites the user preset at `index` with the current sound.
    juce::String saveOver (int index);
    // Writes the current sound to any file, for sharing.
    juce::String exportTo (const juce::File& file) const;
    // The file name Export suggests for the current preset, without extension.
    juce::String getExportName() const;

    // Copies preset files into the user folder. A name that is already taken
    // gets a number, so nothing is overwritten.
    struct ImportResult
    {
        int imported = 0;
        juce::String firstDisplayName; // of the first imported preset
        juce::StringArray failed;      // file names that are not Access808 presets
    };
    ImportResult importFiles (const juce::Array<juce::File>& files);

    // Applies a factory preset (defaults + overrides). Used by setCurrentProgram too.
    void applyFactory (int factoryIndex);

    juce::String getCurrentName() const { return currentName; }
    void setCurrentName (const juce::String& n) { currentName = n; }

private:
    static juce::String sanitise (const juce::String&);
    juce::File fileFor (const juce::String& name, const juce::String& category) const;
    juce::String writeCurrentState (const juce::File& file) const;
    static juce::String displayNameFor (const juce::String& name, const juce::String& category);

    juce::AudioProcessorValueTreeState& apvts;
    std::vector<Entry> entries;
    juce::String currentName { "Init" };
};
} // namespace access808
