#include "Show.h"
#include "PluginHost.h"

// ShowController: each button's 3 plugin slots.

CueFx* ShowController::getFx (int id) const
{
    auto it = fx.find (id);
    return it == fx.end() || it->second->isEmpty() ? nullptr : it->second.get();
}

CueFx& ShowController::fxFor (int id)
{
    auto& p = fx[id];
    if (p == nullptr)
    {
        p = std::make_shared<CueFx>();
        p->prepare (engine.getSampleRate(), engine.getBlockSize());
    }
    return *p;
}

void ShowController::attachFx (PlayPlan& plan) const
{
    plan.fx = getFx (plan.cueId);
}

bool ShowController::setPlugin (int id, int slot, const juce::PluginDescription& description, juce::String& error)
{
    if (! show.isValid (id) || slot < 0 || slot >= CueFx::numSlots || host == nullptr)
        return false;
    auto plugin = host->create (description, engine.getSampleRate(), engine.getBlockSize(), error);
    if (plugin == nullptr)
        return false;
    pluginsGoing (fxFor (id), slot);
    fxFor (id).setSlot (slot, std::move (plugin), description);
    engine.setCueFx (id, getFx (id));   // already playing: through the plugin from now on
    auto& data = show.cue (id).plugins[(size_t) slot];
    if (auto xml = description.createXml())
        data.description = xml->toString (juce::XmlElement::TextFormat().singleLine().withoutHeader());
    data.state.clear();
    data.bypassed = false;
    dirty = true;
    sendChangeMessage();
    return true;
}

void ShowController::setPluginForTest (int id, int slot, std::unique_ptr<juce::AudioProcessor> p)
{
    juce::PluginDescription d;
    d.name = p != nullptr ? p->getName() : juce::String();
    fxFor (id).setSlot (slot, std::move (p), d);
    engine.setCueFx (id, getFx (id));
}

void ShowController::removePlugin (int id, int slot)
{
    if (! show.isValid (id) || slot < 0 || slot >= CueFx::numSlots)
        return;
    if (auto it = fx.find (id); it != fx.end())
    {
        pluginsGoing (*it->second, slot);
        it->second->setSlot (slot, nullptr, {});
        engine.setCueFx (id, getFx (id));
    }
    show.cue (id).plugins[(size_t) slot] = {};
    dirty = true;
    sendChangeMessage();
}

void ShowController::setPluginBypassed (int id, int slot, bool bypassed)
{
    if (! show.isValid (id) || slot < 0 || slot >= CueFx::numSlots)
        return;
    if (auto it = fx.find (id); it != fx.end())
        it->second->setBypassed (slot, bypassed);
    show.cue (id).plugins[(size_t) slot].bypassed = bypassed;
    dirty = true;
    sendChangeMessage();
}

void ShowController::storePluginStates()
{
    for (auto& [id, chain] : fx)
        if (show.isValid (id))
            for (int s = 0; s < CueFx::numSlots; ++s)
                if (chain->getPlugin (s) != nullptr)
                {
                    show.cue (id).plugins[(size_t) s].state = chain->getState (s);
                    show.cue (id).plugins[(size_t) s].bypassed = chain->isBypassed (s);
                }
}

void ShowController::createPlugins (int id)
{
    if (! show.isValid (id) || host == nullptr)
        return;
    for (int s = 0; s < CueFx::numSlots; ++s)
    {
        const auto& data = show.cue (id).plugins[(size_t) s];
        if (data.isEmpty())
            continue;
        juce::PluginDescription d;
        auto xml = juce::parseXML (data.description);
        if (xml == nullptr || ! d.loadFromXml (*xml))
            continue;
        juce::String error;
        auto plugin = host->create (d, engine.getSampleRate(), engine.getBlockSize(), error);
        if (plugin == nullptr)
        {
            missingPlugins.addIfNotAlreadyThere (d.name + (d.manufacturerName.isNotEmpty() ? " (" + d.manufacturerName + ")" : juce::String()));
            continue;
        }
        auto& chain = fxFor (id);
        chain.setSlot (s, std::move (plugin), d);
        chain.setState (s, data.state);
        chain.setBypassed (s, data.bypassed);
    }
}

void ShowController::retireFx (int id)
{
    if (auto it = fx.find (id); it != fx.end())
    {
        pluginsGoing (*it->second);
        retiredFx.push_back (it->second);
        fx.erase (it);
    }
}

void ShowController::pluginsGoing (CueFx& chain, int onlySlot)
{
    if (! onPluginGoing)
        return;
    for (int s = 0; s < CueFx::numSlots; ++s)
        if (onlySlot < 0 || s == onlySlot)
            if (auto* p = chain.getPlugin (s))
                onPluginGoing (p);
}
