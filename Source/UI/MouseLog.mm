#include "MouseLog.h"

#include "Library/Library.h"

#import <AppKit/AppKit.h>

namespace
{
juce::File logFile()
{
    return juce::File::getSpecialLocation (juce::File::userHomeDirectory)
        .getChildFile ("Library/Logs/Oddfield/Batida mouse.log");
}

void write (const juce::String& line)
{
    const auto f = logFile();
    f.getParentDirectory().createDirectory();
    if (f.getSize() > 2 * 1024 * 1024)
        f.deleteFile();
    f.appendText (juce::Time::getCurrentTime().formatted ("%H:%M:%S") + juce::String::formatted (".%03d  ", juce::Time::getCurrentTime().getMilliseconds())
                  + line + "\n");
}

class Log final : public MouseLog, private juce::MouseListener
{
public:
    explicit Log (juce::Component& e) : editor (e)
    {
        write ("---- editor open: process " + juce::String ([[[NSProcessInfo processInfo] processName] UTF8String])
               + ", app class " + juce::String ([NSStringFromClass ([NSApp class]) UTF8String]));
        juce::Desktop::getInstance().addGlobalMouseListener (this);
        auto* self = this;
        monitor = [NSEvent addLocalMonitorForEventsMatchingMask: NSEventMaskLeftMouseDown | NSEventMaskLeftMouseUp
                                                         handler: ^NSEvent* (NSEvent* ev) { self->raw (ev); return ev; }];
        juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<juce::Component> (&editor)]
        {
            if (safe != nullptr)
                if (auto* peer = safe->getPeer())
                {
                    auto* view = (NSView*) peer->getNativeHandle();
                    auto* window = [view window];
                    write (juce::String ("peer: view ") + juce::String ((int) [view frame].size.width) + " x "
                           + juce::String ((int) [view frame].size.height) + ", window #" + juce::String ((int) [window windowNumber])
                           + " visible " + juce::String ((int) [window isVisible]) + " key " + juce::String ((int) [window isKeyWindow])
                           + " frame " + juce::String ((int) [window frame].origin.x) + "," + juce::String ((int) [window frame].origin.y));
                }
        });
    }

    ~Log() override
    {
        [NSEvent removeMonitor: monitor];
        juce::Desktop::getInstance().removeGlobalMouseListener (this);
        write ("---- editor closed");
    }

private:
    void raw (NSEvent* ev)
    {
        auto* peer = editor.getPeer();
        auto* view = peer != nullptr ? (NSView*) peer->getNativeHandle() : nil;
        const auto inWindow = [ev locationInWindow];
        const auto screen = [ev window] != nil ? [[ev window] convertPointToScreen: inWindow] : inWindow;
        const auto front = [NSWindow windowNumberAtPoint: screen belowWindowWithWindowNumber: 0];
        juce::String line = [ev type] == NSEventTypeLeftMouseDown ? "down" : "up  ";
        line << " event window #" << (int) [ev windowNumber] << " at " << (int) inWindow.x << "," << (int) inWindow.y
             << "; front window there #" << (int) front;
        if (view != nil)
        {
            const auto local = [view convertPoint: inWindow fromView: nil];
            const juce::Point<int> p ((int) local.x, [view isFlipped] ? (int) local.y : (int) ([view frame].size.height - local.y));
            line << "; our window #" << (int) [[view window] windowNumber] << ", peer contains " << (int) peer->contains (p, true)
                 << " at " << p.x << "," << p.y;
        }
        write (line);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        write ("  -> JUCE mouseDown on " + juce::String (typeid (*e.eventComponent).name()) + " " + e.eventComponent->getComponentID());
    }

    juce::Component& editor;
    id monitor = nil;
};
} // namespace

std::unique_ptr<MouseLog> MouseLog::start (juce::Component& editor)
{
    if (! batida::Library::kReviewBuild)
        return nullptr;
    return std::make_unique<Log> (editor);
}
