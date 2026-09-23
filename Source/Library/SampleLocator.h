#pragma once

#include <juce_core/juce_core.h>

// Finding and collecting sample files.
namespace batida
{

bool isAudioFileName (const juce::String& path); // .wav .aif .aiff .flac

// Searches the folders (and their subfolders) for a file with this name, and
// this size when it's known (so "kick.wav" finds the right kick.wav). Stops
// after looking at `maxFiles` files; `shouldStop` lets a search be cancelled.
juce::File findSample (const juce::String& fileName, juce::int64 bytes, const juce::Array<juce::File>& folders,
                       int maxFiles = 50000, std::function<bool()> shouldStop = {});

// Copies a sample into `folder`, reusing a copy that's already there (same
// name and size); a different file with the same name gets " 2", " 3"...
// Returns the copy, or an empty File if copying failed.
juce::File collectSample (const juce::File& sample, const juce::File& folder);

} // namespace batida
