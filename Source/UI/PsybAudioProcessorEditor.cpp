#include "PsybAudioProcessorEditor.h"

PsybAudioProcessorEditor::PsybAudioProcessorEditor (PsybAudioProcessor& p)
    : AudioProcessorEditor (&p)
{
    setSize (480, 280);
}

PsybAudioProcessorEditor::~PsybAudioProcessorEditor() = default;

void PsybAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff101010));
    g.setColour (juce::Colours::white);
    g.setFont (juce::FontOptions (18.0f));
    g.drawText ("Psy.b", getLocalBounds(), juce::Justification::centred);
}

void PsybAudioProcessorEditor::resized() {}
