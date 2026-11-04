#include "PsybAudioProcessor.h"
#include "Dsp/ReportedLatency.h"
#include "UI/PsybAudioProcessorEditor.h"

#include <algorithm>

PsybAudioProcessor::PsybAudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)
                          .withInput ("Sidechain", juce::AudioChannelSet::stereo(), false))
{
}

PsybAudioProcessor::~PsybAudioProcessor() = default;

void PsybAudioProcessor::prepareToPlay (double, int)
{
    engine.loadInit();
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

    psyb::MidiIn events[256];
    int eventCount = 0;
    for (const auto meta : midi)
    {
        if (eventCount >= 256)
            break;

        const auto msg = meta.getMessage();
        auto& event = events[eventCount];
        event = {};
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

        ++eventCount;
    }

    if (output.getNumSamples() > 0)
        engine.process (block, events, eventCount, output.getWritePointer (0));

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
