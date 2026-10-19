#include "PluginHost.h"

namespace
{
    constexpr int scanTimeoutMs = 60000;

    void addFormats (juce::AudioPluginFormatManager& fm)
    {
        fm.addFormat (new juce::VST3PluginFormat());
    }

    /** Scans one plugin file in a separate process, so a crashing plugin can't take Stinger down. */
    class OutOfProcessScanner : public juce::KnownPluginList::CustomScanner
    {
    public:
        bool findPluginTypesFor (juce::AudioPluginFormat&, juce::OwnedArray<juce::PluginDescription>& result,
                                 const juce::String& fileOrIdentifier) override
        {
            const juce::TemporaryFile temp ("scan.xml");
            juce::ChildProcess child;
            const juce::StringArray command { juce::File::getSpecialLocation (juce::File::currentExecutableFile).getFullPathName(),
                                              "--scan-plugin", fileOrIdentifier, temp.getFile().getFullPathName() };
            if (! child.start (command, 0))
                return false;
            if (! child.waitForProcessToFinish (scanTimeoutMs))
            {
                child.kill();   // hung (or waiting on a dialog nobody can see)
                return false;
            }
            if (child.getExitCode() != 0)
                return false;   // crashed, or nothing loadable in the file
            if (auto xml = juce::parseXML (temp.getFile()))
                for (auto* e : xml->getChildIterator())
                {
                    auto d = std::make_unique<juce::PluginDescription>();
                    if (d->loadFromXml (*e))
                        result.add (d.release());
                }
            return ! result.isEmpty();
        }
    };
}

PluginHost::PluginHost (juce::PropertiesFile& s) : settings (s)
{
    addFormats (formats);
    if (auto xml = settings.getXmlValue ("pluginList"))
        known.recreateFromXml (*xml);
    known.setCustomScanner (std::make_unique<OutOfProcessScanner>());
    known.addChangeListener (this);
}

PluginHost::~PluginHost()
{
    known.removeChangeListener (this);
}

void PluginHost::changeListenerCallback (juce::ChangeBroadcaster*)
{
    if (auto xml = known.createXml())
    {
        settings.setValue ("pluginList", xml.get());
        settings.saveIfNeeded();
    }
}

juce::Array<juce::PluginDescription> PluginHost::effects() const
{
    juce::Array<juce::PluginDescription> list;
    for (auto& d : known.getTypes())
        if (! d.isInstrument)
            list.add (d);
    std::sort (list.begin(), list.end(), [] (const auto& a, const auto& b)
    {
        const int maker = a.manufacturerName.compareIgnoreCase (b.manufacturerName);
        return maker != 0 ? maker < 0 : a.name.compareIgnoreCase (b.name) < 0;
    });
    return list;
}

std::unique_ptr<juce::AudioPluginInstance> PluginHost::create (const juce::PluginDescription& d, double sampleRate, int blockSize,
                                                               juce::String& error)
{
    return formats.createPluginInstance (d, sampleRate, blockSize, error);
}

juce::File PluginHost::deadMansPedalFile() const
{
    return settings.getFile().getSiblingFile ("PluginScanCrash.txt");
}

int PluginHost::runScanner (const juce::StringArray& args)
{
    const int i = args.indexOf ("--scan-plugin");
    if (i < 0 || args.size() < i + 3)
        return 2;
    const auto pluginFile = args[i + 1].unquoted();
    const juce::File out (args[i + 2].unquoted());

    juce::AudioPluginFormatManager fm;
    addFormats (fm);
    juce::OwnedArray<juce::PluginDescription> found;
    for (auto* format : fm.getFormats())
        if (format->fileMightContainThisPluginType (pluginFile))
            format->findAllTypesForFile (found, pluginFile);

    juce::XmlElement xml ("FOUND");
    for (auto* d : found)
        if (auto e = d->createXml())
            xml.addChildElement (e.release());
    xml.writeTo (out);
    return found.isEmpty() ? 1 : 0;
}
