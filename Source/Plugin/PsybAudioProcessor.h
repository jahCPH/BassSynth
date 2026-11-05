#pragma once

#include "Engine/Sequencer.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <vector>

/** Empty synth shell. Display name is PsyRoot.
    Bundle id, plugin code, and the manufacturer code are name-neutral.
*/
class PsybAudioProcessor : public juce::AudioProcessor
{
public:
    PsybAudioProcessor();
    ~PsybAudioProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    const juce::String getName() const override;
    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;

    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int index, const juce::String& newName) override;

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState& parameters() { return apvts; }
    int duckState() const { return engine.duckState(); }
    float duckGain() const { return engine.duckGain(); }

private:
    juce::AudioProcessorValueTreeState apvts;
    psyb::PsyEngine engine;
    std::vector<psyb::MidiIn> midiEvents;
    bool lastLatch = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PsybAudioProcessor)
};
