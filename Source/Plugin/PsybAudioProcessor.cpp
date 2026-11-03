#include "PsybAudioProcessor.h"
#include "Dsp/ReportedLatency.h"
#include "Engine/Engine.h"
#include "UI/PsybAudioProcessorEditor.h"

PsybAudioProcessor::PsybAudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)
                          .withInput ("Sidechain", juce::AudioChannelSet::stereo(), false))
{
}

PsybAudioProcessor::~PsybAudioProcessor() = default;

void PsybAudioProcessor::prepareToPlay (double, int)
{
    juce::ignoreUnused (psyb::EngineSlot {});
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
    juce::ignoreUnused (midi);
    buffer.clear();
}

juce::AudioProcessorEditor* PsybAudioProcessor::createEditor()
{
    return new PsybAudioProcessorEditor (*this);
}

bool PsybAudioProcessor::hasEditor() const { return true; }

const juce::String PsybAudioProcessor::getName() const { return "Psy.b"; }

bool PsybAudioProcessor::acceptsMidi() const { return true; }
bool PsybAudioProcessor::producesMidi() const { return false; }
bool PsybAudioProcessor::isMidiEffect() const { return false; }
double PsybAudioProcessor::getTailLengthSeconds() const { return 0.0; }

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
