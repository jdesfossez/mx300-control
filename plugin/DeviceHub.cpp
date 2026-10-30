#include "DeviceHub.h"

namespace
{
constexpr double kRetryMs = 3000;

bool looksLikeMx300 (const juce::String& name) { return name.containsIgnoreCase ("MX300"); }

juce::PropertiesFile::Options settingsOptions()
{
    juce::PropertiesFile::Options o;
    o.applicationName = "MX300 Control";
    o.filenameSuffix = ".settings";
    o.folderName = "MX300 Control";
    o.osxLibrarySubFolder = "Application Support";
    return o;
}
} // namespace

DeviceHub::DeviceHub()
    : props (std::make_unique<juce::PropertiesFile> (settingsOptions()))
{
    deviceListConnection = juce::MidiDeviceListConnection::make ([this]
    {
        // a port appeared or disappeared
        if (currentSource == Source::Hardware)
        {
            bool stillThere = false;
            for (auto& d : juce::MidiOutput::getAvailableDevices())
                stillThere |= d.name == currentPortName;
            if (! stillThere)
                closeSource();
        }
        if (currentSource == Source::None)
            openBestSource();
    });
    openBestSource();
    startTimer (5);
}

DeviceHub::~DeviceHub()
{
    stopTimer();
    closeSource();
}

juce::Array<juce::MidiDeviceInfo> DeviceHub::availablePorts() const { return juce::MidiOutput::getAvailableDevices(); }
juce::String DeviceHub::preferredPort() const { return props->getValue ("port"); }
bool DeviceHub::demoMode() const
{
    if (juce::SystemStats::getEnvironmentVariable ("MX300_DEMO", {}) == "1")   // testing: force demo, keep settings
        return true;
    return props->getBoolValue ("demo", false);
}

void DeviceHub::setPreferredPort (const juce::String& id)
{
    props->setValue ("port", id);
    props->setValue ("demo", false);
    reconnect();
}

void DeviceHub::setDemoMode (bool on)
{
    props->setValue ("demo", on);
    reconnect();
}

void DeviceHub::reconnect()
{
    closeSource();
    openBestSource();
}

void DeviceHub::closeSource()
{
    deviceSession.disconnect();   // GUI mode off while the port is still open
    if (input)
        input->stop();
    input.reset();
    output.reset();
    demo.reset();
    currentSource = Source::None;
    currentPortName = {};
    std::lock_guard<std::mutex> l (incomingLock);
    incoming.clear();
}

void DeviceHub::openBestSource()
{
    lastConnectAttemptMs = nowMs();
    if (demoMode())
    {
        demo = std::make_unique<mx300::FakeUnit>();
        demo->toHost = [this] (const mx300::Bytes& f)
        {
            std::lock_guard<std::mutex> l (incomingLock);
            incoming.push_back (f);
        };
        currentSource = Source::Demo;
        currentPortName = "Demo unit";
        deviceSession.connect (nowMs());
        return;
    }

    const auto wanted = preferredPort();
    auto pick = [&] (const juce::Array<juce::MidiDeviceInfo>& list) -> juce::MidiDeviceInfo
    {
        for (auto& d : list)
            if (wanted.isNotEmpty() ? d.identifier == wanted : looksLikeMx300 (d.name))
                return d;
        return {};
    };
    auto out = pick (juce::MidiOutput::getAvailableDevices());
    if (out.identifier.isEmpty())
        return;
    // the input normally has the same name as the output (one bidirectional USB-MIDI port);
    // Windows and ALSA may name the two directions differently
    juce::MidiDeviceInfo in;
    const auto inputs = juce::MidiInput::getAvailableDevices();
    for (auto& d : inputs)
        if (d.name == out.name)
            in = d;
    for (auto& d : inputs)
        if (in.identifier.isEmpty() && looksLikeMx300 (d.name))
            in = d;
    if (in.identifier.isEmpty())
        return;

    output = juce::MidiOutput::openDevice (out.identifier);
    input = juce::MidiInput::openDevice (in.identifier, this);
    if (! output || ! input)
    {
        output.reset();
        input.reset();
        return;
    }
    input->start();
    currentSource = Source::Hardware;
    currentPortName = out.name;
    deviceSession.connect (nowMs());
}

void DeviceHub::sendSysex (const mx300::Bytes& f)
{
    lastOutgoingMs = nowMs();
    if (demo)
    {
        demo->receive (f);
        return;
    }
    if (output && f.size() >= 2)
        output->sendMessageNow (juce::MidiMessage::createSysExMessage (f.data() + 1, (int) f.size() - 2));
}

void DeviceHub::handleIncomingMidiMessage (juce::MidiInput*, const juce::MidiMessage& m)
{
    if (! m.isSysEx())
        return;
    const auto* raw = m.getRawData();   // includes F0 ... F7
    mx300::Bytes f (raw, raw + m.getRawDataSize());
    std::lock_guard<std::mutex> l (incomingLock);
    incoming.push_back (std::move (f));
}

void DeviceHub::timerCallback()
{
    std::vector<mx300::Bytes> frames;
    {
        std::lock_guard<std::mutex> l (incomingLock);
        frames.swap (incoming);
    }
    const double now = nowMs();
    for (auto& f : frames)
    {
        lastIncomingMs = now;
        deviceSession.handleFrame (f, now);
    }
    deviceSession.tick (now);

    using S = mx300::DeviceSession::State;
    if (currentSource == Source::None && now - lastConnectAttemptMs > kRetryMs)
        openBestSource();
    else if (currentSource == Source::Hardware && deviceSession.state() == S::NoResponse
             && now - lastConnectAttemptMs > kRetryMs)
    {
        lastConnectAttemptMs = now;
        deviceSession.connect (now);
    }
}
