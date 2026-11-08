#include "AndroidFiles.h"
#include <juce_events/juce_events.h>

namespace
{
    juce::File copyOne (const juce::URL& link, const juce::File& folder)
    {
        const auto document = juce::AndroidDocument::fromDocument (link);
        auto fileName = juce::File::createLegalFileName (document.getInfo().getName());
        if (fileName.isEmpty())
            fileName = "Imported audio.wav";
        folder.createDirectory();
        auto target = folder.getChildFile (fileName);
        if (target.existsAsFile() && target.getSize() == document.getInfo().getSizeInBytes())
            return target;   // the same file again: no second copy
        if (target.existsAsFile())
            target = target.getNonexistentSibling();

        auto in = document.createInputStream();
        if (in == nullptr)
            return {};
        bool copied = false;
        {
            juce::FileOutputStream out (target);
            copied = out.openedOk() && out.writeFromInputStream (*in, -1) >= 0;
            out.flush();
            copied = copied && out.getStatus().wasOk();
        }
        if (! copied)
        {
            target.deleteFile();
            return {};
        }
        return target;
    }
}

void AndroidFiles::copyIn (const juce::Array<juce::URL>& links, const juce::File& folder,
                           std::function<void (const juce::Array<juce::File>&)> done)
{
    juce::Thread::launch ([links, folder, done]
    {
        juce::Array<juce::File> copies;
        for (auto& link : links)
            copies.add (copyOne (link, folder));
        juce::MessageManager::callAsync ([done, copies] { if (done) done (copies); });
    });
}
