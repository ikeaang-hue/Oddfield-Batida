#include "SampleLocator.h"

namespace batida
{

bool isAudioFileName (const juce::String& path)
{
    const auto ext = juce::File (path).getFileExtension().toLowerCase();
    return ext == ".wav" || ext == ".aif" || ext == ".aiff" || ext == ".flac";
}

juce::File findSample (const juce::String& fileName, juce::int64 bytes, const juce::Array<juce::File>& folders,
                       int maxFiles, std::function<bool()> shouldStop)
{
    int looked = 0;
    juce::File sameName; // a file with the right name but another size, if nothing better turns up
    for (const auto& folder : folders)
    {
        if (! folder.isDirectory())
            continue;
        for (const auto& entry : juce::RangedDirectoryIterator (folder, true, "*", juce::File::findFiles))
        {
            if (++looked > maxFiles || (shouldStop && (looked % 256) == 0 && shouldStop()))
                return sameName;
            const auto& f = entry.getFile();
            if (! f.getFileName().equalsIgnoreCase (fileName))
                continue;
            if (bytes < 0 || entry.getFileSize() == bytes)
                return f;
            if (sameName == juce::File())
                sameName = f;
        }
    }
    return sameName;
}

juce::File collectSample (const juce::File& sample, const juce::File& folder)
{
    if (! sample.existsAsFile() || ! folder.createDirectory())
        return {};
    if (sample.getParentDirectory() == folder)
        return sample;

    const auto name = sample.getFileNameWithoutExtension(), ext = sample.getFileExtension();
    for (int n = 1; n < 1000; ++n)
    {
        const auto target = folder.getChildFile (n == 1 ? name + ext : name + " " + juce::String (n) + ext);
        if (target.existsAsFile())
        {
            if (target.getSize() == sample.getSize() && target.hasIdenticalContentTo (sample))
                return target;
            continue;
        }
        return sample.copyFileTo (target) ? target : juce::File();
    }
    return {};
}

} // namespace batida
