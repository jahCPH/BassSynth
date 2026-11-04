#pragma once

#include "Plugin/PsybAudioProcessor.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <memory>
#include <vector>

class PsybAudioProcessorEditor : public juce::AudioProcessorEditor
{
public:
    explicit PsybAudioProcessorEditor (PsybAudioProcessor&);
    ~PsybAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    struct Knob
    {
        juce::Label label;
        juce::Slider slider;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    };

    struct Toggle
    {
        juce::ToggleButton button;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> attachment;
    };

    PsybAudioProcessor& synth;
    std::vector<std::unique_ptr<Knob>> knobs;
    std::vector<std::unique_ptr<Toggle>> toggles;
    juce::ComboBox subOwner;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> subOwnerAttachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PsybAudioProcessorEditor)
};
