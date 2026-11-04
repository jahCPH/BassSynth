#pragma once

#include "Plugin/PsybAudioProcessor.h"

/** Placeholder window. The PsyRoot label is a display string, not the plugin id. */
class PsybAudioProcessorEditor : public juce::AudioProcessorEditor
{
public:
    explicit PsybAudioProcessorEditor (PsybAudioProcessor&);
    ~PsybAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PsybAudioProcessorEditor)
};
