#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

// An inline text field for renaming: Return or clicking away keeps the text,
// Escape cancels. `holder` (owned by the parent) keeps it alive while open.
inline void startRename (std::unique_ptr<juce::TextEditor>& holder, juce::Component& parent, juce::Rectangle<int> area,
                         const juce::String& current, std::function<void (const juce::String&)> commit)
{
    holder = std::make_unique<juce::TextEditor>();
    auto* ed = holder.get();
    ed->setText (current, false);
    ed->setFont (juce::FontOptions (14.0f));
    ed->setJustification (juce::Justification::centredLeft);
    ed->setBounds (area);

    juce::Component::SafePointer<juce::Component> safeParent (&parent);
    auto finish = [safeParent, &holder, ed, commit] (bool keep)
    {
        if (safeParent == nullptr || holder.get() != ed)
            return;
        if (keep && commit)
            commit (ed->getText());
        juce::MessageManager::callAsync ([safeParent, &holder, ed]
        {
            if (safeParent != nullptr && holder.get() == ed)
                holder.reset();
        });
    };
    ed->onReturnKey = [finish] { finish (true); };
    ed->onFocusLost = [finish] { finish (true); };
    ed->onEscapeKey = [finish] { finish (false); };

    parent.addAndMakeVisible (*ed);
    ed->selectAll();
    ed->grabKeyboardFocus();
}
