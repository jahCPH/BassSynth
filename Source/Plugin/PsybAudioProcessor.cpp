#include "PsybAudioProcessor.h"
#include "Dsp/ReportedLatency.h"
#include "UI/PsybAudioProcessorEditor.h"

#include <algorithm>
#include <cmath>

namespace
{
constexpr int kMidiEventCapacity = 2048;

bool keepsItsSlot (const psyb::MidiIn& event)
{
    return event.cc == 120 || event.cc == 123 || (event.cc < 0 && event.velocity <= 0);
}
juce::AudioProcessorValueTreeState::ParameterLayout parameterLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;
    auto hz = [] (float min, float max, float skew)
    {
        return NormalisableRange<float> (min, max, 0.01f, skew);
    };
    auto add = [&] (const String& id, const String& name, NormalisableRange<float> range, float def)
    {
        layout.add (std::make_unique<AudioParameterFloat> (ParameterID { id, 1 }, name, range, def));
    };

    add ("r_cutoff", "Cutoff", hz (20.f, 20000.f, 0.3f), 900.f);
    add ("r_reso", "Reso", { 0.f, 100.f }, 20.f);
    add ("r_envamt", "Env Amt", { -100.f, 100.f }, 60.f);
    add ("r_fdec", "Filter Decay", hz (5.f, 4000.f, 0.3f), 60.f);
    add ("r_fatk", "Filter Attack", hz (0.5f, 200.f, 0.3f), 0.5f);
    add ("r_fsus", "Filter Sustain", { 0.f, 100.f }, 0.f);
    add ("r_aatk", "Amp Attack", hz (0.5f, 200.f, 0.3f), 0.5f);
    add ("r_adec", "Amp Decay", hz (5.f, 4000.f, 0.3f), 70.f);
    add ("r_asus", "Amp Sustain", { 0.f, 100.f }, 0.f);
    add ("r_arel", "Amp Release", hz (2.f, 2000.f, 0.3f), 12.f);
    add ("r_drive", "Drive", { 0.f, 100.f }, 15.f);
    add ("b_sublevel", "Sub", { 0.f, 100.f }, 70.f);
    add ("b_output", "Output", { -24.f, 6.f }, 0.f);
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { "r_phasereset", 1 }, "Phase Reset", true));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { "r_filterreset", 1 }, "Filter Reset", true));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { "q_latch", 1 }, "Latch", false));
    return layout;
}

} // namespace

PsybAudioProcessor::PsybAudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)
                          .withInput ("Sidechain", juce::AudioChannelSet::stereo(), false))
    , apvts (*this, nullptr, "PsyRoot", parameterLayout())
{
    midiEvents.reserve (static_cast<size_t> (kMidiEventCapacity));
}

PsybAudioProcessor::~PsybAudioProcessor() = default;

void PsybAudioProcessor::prepareToPlay (double, int)
{
    engine.loadInit();
    midiEvents.reserve (static_cast<size_t> (kMidiEventCapacity));
    setLatencySamples (psyb::hostReportedLatencySamples());
}

void PsybAudioProcessor::releaseResources() {}

bool PsybAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

    // Sidechain is optional. Disabled by default; the host may enable stereo.
    const auto sidechain = layouts.getMainInputChannelSet();
    return sidechain == juce::AudioChannelSet::disabled()
        || sidechain == juce::AudioChannelSet::stereo();
}

void PsybAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;

    auto output = getBusBuffer (buffer, false, 0);
    output.clear();

    auto raw = [this] (const char* id) { return apvts.getRawParameterValue (id)->load(); };
    psyb::RollParams params;
    params.cutoffHz = raw ("r_cutoff");
    params.reso = raw ("r_reso") / 100.f;
    params.envAmt = raw ("r_envamt") / 100.f;
    params.fdecMs = raw ("r_fdec");
    params.fatkMs = raw ("r_fatk");
    params.fsus = raw ("r_fsus") / 100.f;
    params.aatkMs = raw ("r_aatk");
    params.adecMs = raw ("r_adec");
    params.asus = raw ("r_asus") / 100.f;
    params.arelMs = raw ("r_arel");
    params.drive = raw ("r_drive") / 100.f;
    params.sub = raw ("b_sublevel") / 100.f;
    params.outputDb = raw ("b_output");
    params.phaseReset = raw ("r_phasereset") >= 0.5f;
    params.filterReset = raw ("r_filterreset") >= 0.5f;
    engine.setRollParams (params);
    const bool latch = raw ("q_latch") >= 0.5f;
    if (latch != lastLatch)
    {
        engine.setLatch (latch);
        lastLatch = latch;
    }

    psyb::Block block;
    block.sampleRate = getSampleRate();
    block.bpm = 120.0;
    block.ppqStart = 0.0;
    block.numSamples = output.getNumSamples();
    block.playing = false;

    if (auto* head = getPlayHead())
    {
        if (const auto pos = head->getPosition())
        {
            if (const auto bpm = pos->getBpm())
                block.bpm = *bpm;
            if (const auto ppq = pos->getPpqPosition())
                block.ppqStart = *ppq;
            block.playing = pos->getIsPlaying();
        }
    }

    midiEvents.clear();
    for (const auto meta : midi)
    {
        const auto msg = meta.getMessage();
        psyb::MidiIn event {};
        event.sample = meta.samplePosition;
        event.channel = std::clamp (msg.getChannel() - 1, 0, 15);

        if (msg.isAllSoundOff())
            event.cc = 120;
        else if (msg.isAllNotesOff())
            event.cc = 123;
        else if (msg.isNoteOn())
        {
            event.note = msg.getNoteNumber();
            event.velocity = msg.getVelocity();
        }
        else if (msg.isNoteOff())
        {
            event.note = msg.getNoteNumber();
            event.velocity = 0;
        }
        else if (msg.isController())
        {
            event.cc = msg.getControllerNumber();
            event.velocity = msg.getControllerValue();
        }
        else
            continue;

        // Stay inside the reserved capacity. A full block keeps note-offs
        // and the all-notes/all-sound controllers, and drops a note-on.
        if (midiEvents.size() < midiEvents.capacity()
            && static_cast<int> (midiEvents.size()) < kMidiEventCapacity)
        {
            midiEvents.push_back (event);
            continue;
        }

        if (! keepsItsSlot (event))
            continue;

        for (int i = static_cast<int> (midiEvents.size()) - 1; i >= 0; --i)
        {
            if (keepsItsSlot (midiEvents[static_cast<size_t> (i)]))
                continue;
            midiEvents[static_cast<size_t> (i)] = event;
            break;
        }
    }

    if (output.getNumSamples() > 0)
        engine.process (block,
                        midiEvents.empty() ? nullptr : midiEvents.data(),
                        static_cast<int> (midiEvents.size()),
                        output.getWritePointer (0));

    if (output.getNumChannels() > 1)
        output.copyFrom (1, 0, output, 0, 0, output.getNumSamples());
}

juce::AudioProcessorEditor* PsybAudioProcessor::createEditor()
{
    return new PsybAudioProcessorEditor (*this);
}

bool PsybAudioProcessor::hasEditor() const { return true; }

const juce::String PsybAudioProcessor::getName() const { return "PsyRoot"; }

bool PsybAudioProcessor::acceptsMidi() const { return true; }
bool PsybAudioProcessor::producesMidi() const { return false; }
bool PsybAudioProcessor::isMidiEffect() const { return false; }
double PsybAudioProcessor::getTailLengthSeconds() const { return 0.08; }

int PsybAudioProcessor::getNumPrograms() { return 1; }
int PsybAudioProcessor::getCurrentProgram() { return 0; }
void PsybAudioProcessor::setCurrentProgram (int) {}
const juce::String PsybAudioProcessor::getProgramName (int) { return {}; }
void PsybAudioProcessor::changeProgramName (int, const juce::String&) {}

void PsybAudioProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, dest);
}

void PsybAudioProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new PsybAudioProcessor();
}
