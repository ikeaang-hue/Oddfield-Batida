#include "Theme.h"

#import <AppKit/AppKit.h>

namespace theme
{
bool reduceMotion()
{
    // macOS System Settings > Accessibility > Display > Reduce motion; read at
    // most once a second.
    static bool reduce = false;
    static double checked = -1.0e9;
    const auto now = juce::Time::getMillisecondCounterHiRes();
    if (now - checked > 1000.0)
    {
        checked = now;
        reduce = [[NSWorkspace sharedWorkspace] accessibilityDisplayShouldReduceMotion];
    }
    return reduce;
}
} // namespace theme
