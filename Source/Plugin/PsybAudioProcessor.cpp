#include "PsybAudioProcessor.h"
#include "Dsp/ReportedLatency.h"
#include "UI/PsybAudioProcessorEditor.h"

#include <algorithm>

namespace
{
constexpr int kMidiEventCapacity = 2048;

bool keepsItsSlot (const psyb::MidiIn& event)
{
    return event.cc == 120 || event.cc == 123 || (event.cc < 0 && event.velocity <= 0);
}
} // namespace

PsybAudioProcessor::PsybAudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)
                          .withInput ("Sidechain", juce::AudioChannelSet::stereo(), false))
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

void PsybAudioProcessor::getStateInformation (juce::MemoryBlock&) {}
void PsybAudioProcessor::setStateInformation (const void*, int) {}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new PsybAudioProcessor();
}
