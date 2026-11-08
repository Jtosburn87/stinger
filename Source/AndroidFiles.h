#pragma once
#include <juce_core/juce_core.h>
#include <functional>

/** Android's file picker hands back content links, not files: these copy what was picked into a folder
    (the show's Audio folder) so the rest of Stinger can use ordinary files. Android only. */
namespace AndroidFiles
{
    /** Copies each link into folder, off the message thread (songs can be big). A file of the same name and size
        already there is used as it is. done gets the copies on the message thread, in the same order:
        juce::File() for one that couldn't be copied. */
    void copyIn (const juce::Array<juce::URL>& links, const juce::File& folder,
                 std::function<void (const juce::Array<juce::File>& copies)> done);
}
