#include "PresetManager.h"
#include "Presets.h"

namespace access808
{
PresetManager::PresetManager (juce::AudioProcessorValueTreeState& s) : apvts (s)
{
    refresh();
}

namespace
{
    juce::File& testDirectory()
    {
        static juce::File dir;
        return dir;
    }
}

void PresetManager::setUserPresetDirectoryForTests (const juce::File& dir) { testDirectory() = dir; }

juce::File PresetManager::getUserPresetDirectory()
{
    if (testDirectory() != juce::File())
        return testDirectory();

    // Windows: %APPDATA%\Access808\Presets. macOS: ~/Library/Application
    // Support/Access808/Presets (JUCE's user data folder there is ~/Library).
    auto base = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory);
   #if JUCE_MAC
    base = base.getChildFile ("Application Support");
   #endif
    return base.getChildFile ("Access808").getChildFile ("Presets");
}

juce::String PresetManager::sanitise (const juce::String& s)
{
    return juce::File::createLegalFileName (s.trim()).trim();
}

juce::File PresetManager::fileFor (const juce::String& name, const juce::String& category) const
{
    auto dir = getUserPresetDirectory();
    if (category.isNotEmpty())
        dir = dir.getChildFile (category);
    return dir.getChildFile (name + extension);
}

void PresetManager::refresh()
{
    entries.clear();

    const auto& factory = getFactoryPresets();
    for (int i = 0; i < (int) factory.size(); ++i)
        entries.push_back ({ factory[(size_t) i].name, {}, factory[(size_t) i].name, {}, i });

    const auto root = getUserPresetDirectory();
    if (! root.isDirectory())
        return;

    std::vector<Entry> user;
    for (const auto& f : root.findChildFiles (juce::File::findFiles, true, juce::String ("*") + extension))
    {
        const auto parent = f.getParentDirectory();
        const auto category = parent == root ? juce::String() : parent.getRelativePathFrom (root);
        const auto name = f.getFileNameWithoutExtension();

        juce::String display ("User - ");
        if (category.isNotEmpty())
            display << category << " - ";
        display << name;

        user.push_back ({ display, category, name, f, -1 });
    }

    std::sort (user.begin(), user.end(), [] (const Entry& a, const Entry& b)
    {
        return a.displayName.compareNatural (b.displayName) < 0;
    });
    entries.insert (entries.end(), user.begin(), user.end());
}

int PresetManager::indexOfDisplayName (const juce::String& displayName) const
{
    for (size_t i = 0; i < entries.size(); ++i)
        if (entries[i].displayName == displayName)
            return (int) i;
    return -1;
}

void PresetManager::applyFactory (int factoryIndex)
{
    const auto& presets = getFactoryPresets();
    if (! juce::isPositiveAndBelow (factoryIndex, (int) presets.size()))
        return;

    for (auto* param : apvts.processor.getParameters())
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (param))
            ranged->setValueNotifyingHost (ranged->getDefaultValue());

    for (const auto& [id, value] : presets[(size_t) factoryIndex].values)
        if (auto* param = apvts.getParameter (id))
            param->setValueNotifyingHost (param->convertTo0to1 (value));

    currentName = presets[(size_t) factoryIndex].name;
}

juce::String PresetManager::load (int index)
{
    if (! juce::isPositiveAndBelow (index, (int) entries.size()))
        return "No preset selected.";

    const auto& e = entries[(size_t) index];
    if (e.isFactory())
    {
        applyFactory (e.factoryIndex);
        currentName = e.displayName;
        return {};
    }

    auto xml = juce::XmlDocument::parse (e.file);
    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType()))
        return "Could not read preset file " + e.file.getFileName() + ".";

    // Set each parameter through the host so automation and undo see the change.
    const auto state = juce::ValueTree::fromXml (*xml);
    for (auto* param : apvts.processor.getParameters())
    {
        auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (param);
        if (ranged == nullptr)
            continue;

        const auto child = state.getChildWithProperty ("id", ranged->getParameterID());
        const float value = child.isValid() ? (float) child.getProperty ("value", 0.0f)
                                            : ranged->convertFrom0to1 (ranged->getDefaultValue());
        ranged->setValueNotifyingHost (ranged->convertTo0to1 (value));
    }

    currentName = e.displayName;
    return {};
}

juce::String PresetManager::displayNameFor (const juce::String& name, const juce::String& category)
{
    return "User - " + (category.isNotEmpty() ? category + " - " : juce::String()) + name;
}

juce::String PresetManager::writeCurrentState (const juce::File& file) const
{
    if (! file.getParentDirectory().createDirectory())
        return "Could not create the folder " + file.getParentDirectory().getFullPathName() + ".";

    auto state = apvts.copyState();
    state.removeProperty ("program", nullptr);
    state.removeProperty ("presetName", nullptr);
    auto xml = state.createXml();
    if (xml == nullptr || ! xml->writeTo (file))
        return "Could not write the preset file " + file.getFileName() + ".";
    return {};
}

juce::String PresetManager::saveUser (const juce::String& rawName, const juce::String& rawCategory)
{
    const auto name = sanitise (rawName);
    const auto category = sanitise (rawCategory);
    if (name.isEmpty())
        return "Preset name is empty.";

    if (auto error = writeCurrentState (fileFor (name, category)); error.isNotEmpty())
        return error;

    refresh();
    const auto idx = indexOfDisplayName (displayNameFor (name, category));
    if (idx >= 0)
        currentName = entries[(size_t) idx].displayName;
    return {};
}

juce::String PresetManager::saveOver (int index)
{
    if (! juce::isPositiveAndBelow (index, (int) entries.size()) || entries[(size_t) index].isFactory())
        return "Factory presets cannot be overwritten. Use Save As to save a copy.";

    const auto entry = entries[(size_t) index];
    if (auto error = writeCurrentState (entry.file); error.isNotEmpty())
        return error;

    refresh();
    currentName = entry.displayName;
    return {};
}

juce::String PresetManager::exportTo (const juce::File& file) const
{
    return writeCurrentState (file);
}

juce::String PresetManager::getExportName() const
{
    for (const auto& e : entries)
        if (e.displayName == currentName)
            return sanitise (e.name);
    return sanitise (currentName).isNotEmpty() ? sanitise (currentName) : juce::String ("Access808 preset");
}

PresetManager::ImportResult PresetManager::importFiles (const juce::Array<juce::File>& files)
{
    ImportResult result;
    const auto root = getUserPresetDirectory();

    for (const auto& f : files)
    {
        auto xml = juce::XmlDocument::parse (f);
        if (xml == nullptr || ! xml->hasTagName (apvts.state.getType()))
        {
            result.failed.add (f.getFileName());
            continue;
        }

        auto name = sanitise (f.getFileNameWithoutExtension());
        if (name.isEmpty())
            name = "Imported preset";

        auto dest = root.getChildFile (name + extension);
        for (int n = 2; dest.exists(); ++n)
            dest = root.getChildFile (name + " (" + juce::String (n) + ")" + extension);

        if (! root.createDirectory() || ! f.copyFileTo (dest))
        {
            result.failed.add (f.getFileName());
            continue;
        }

        if (result.imported++ == 0)
            result.firstDisplayName = displayNameFor (dest.getFileNameWithoutExtension(), {});
    }

    refresh();
    return result;
}

juce::String PresetManager::renameUser (int index, const juce::String& rawName, const juce::String& rawCategory)
{
    if (! juce::isPositiveAndBelow (index, (int) entries.size()) || entries[(size_t) index].isFactory())
        return "Factory presets cannot be renamed. Save a copy with Save As instead.";

    const auto name = sanitise (rawName);
    const auto category = sanitise (rawCategory);
    if (name.isEmpty())
        return "Preset name is empty.";

    const auto oldFile = entries[(size_t) index].file;
    const auto newFile = fileFor (name, category);
    if (newFile.exists() && newFile != oldFile)
        return "A preset with that name already exists.";
    if (! newFile.getParentDirectory().createDirectory() || ! oldFile.moveFileTo (newFile))
        return "Could not rename the preset file.";

    // Remove an emptied category folder.
    const auto oldDir = oldFile.getParentDirectory();
    if (oldDir != getUserPresetDirectory() && oldDir.getNumberOfChildFiles (juce::File::findFilesAndDirectories) == 0)
        oldDir.deleteFile();

    refresh();
    const auto idx = indexOfDisplayName (displayNameFor (name, category));
    if (idx >= 0)
        currentName = entries[(size_t) idx].displayName;
    return {};
}

juce::String PresetManager::deleteUser (int index)
{
    if (! juce::isPositiveAndBelow (index, (int) entries.size()) || entries[(size_t) index].isFactory())
        return "Factory presets cannot be deleted.";

    const auto file = entries[(size_t) index].file;
    if (! file.moveToTrash() && ! file.deleteFile())
        return "Could not delete the preset file.";

    const auto dir = file.getParentDirectory();
    if (dir != getUserPresetDirectory() && dir.getNumberOfChildFiles (juce::File::findFilesAndDirectories) == 0)
        dir.deleteFile();

    refresh();
    return {};
}
} // namespace access808
