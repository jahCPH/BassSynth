#include "UI/PsybAudioProcessorEditor.h"

PsybAudioProcessorEditor::PsybAudioProcessorEditor (PsybAudioProcessor& p)
    : AudioProcessorEditor (&p), synth (p)
{
    auto addKnob = [this] (const char* id, const char* name)
    {
        auto knob = std::make_unique<Knob>();
        knob->label.setText (name, juce::dontSendNotification);
        knob->label.setColour (juce::Label::textColourId, juce::Colours::white);
        knob->slider.setSliderStyle (juce::Slider::LinearHorizontal);
        knob->slider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 64, 18);
        addAndMakeVisible (knob->label);
        addAndMakeVisible (knob->slider);
        knob->attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
            synth.parameters(), id, knob->slider);
        knobs.push_back (std::move (knob));
    };
    auto addToggle = [this] (const char* id, const char* name)
    {
        auto toggle = std::make_unique<Toggle>();
        toggle->button.setButtonText (name);
        toggle->button.setColour (juce::ToggleButton::textColourId, juce::Colours::white);
        addAndMakeVisible (toggle->button);
        toggle->attachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
            synth.parameters(), id, toggle->button);
        toggles.push_back (std::move (toggle));
    };

    addKnob ("r_cutoff", "CUTOFF");
    addKnob ("r_reso", "RESO");
    addKnob ("r_envamt", "ENV AMT");
    addKnob ("r_fatk", "FILT ATK");
    addKnob ("r_fdec", "FILT DEC");
    addKnob ("r_fsus", "FILT SUS");
    addKnob ("r_aatk", "AMP ATK");
    addKnob ("r_adec", "AMP DECAY");
    addKnob ("r_asus", "AMP SUS");
    addKnob ("r_arel", "AMP REL");
    addKnob ("r_drive", "DRIVE");
    addKnob ("b_sublevel", "SUB");
    addKnob ("b_output", "OUTPUT");
    addToggle ("r_phasereset", "PHASE RESET");
    addToggle ("r_filterreset", "FILTER RESET");
    addToggle ("q_latch", "LATCH");

    setSize (560, 520);
}

PsybAudioProcessorEditor::~PsybAudioProcessorEditor() = default;

void PsybAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff12141a));
}

void PsybAudioProcessorEditor::resized()
{
    auto area = getLocalBounds().reduced (12);
    const int rowH = 28;
    for (auto& knob : knobs)
    {
        auto row = area.removeFromTop (rowH);
        knob->label.setBounds (row.removeFromLeft (92));
        knob->slider.setBounds (row);
    }
    area.removeFromTop (8);
    for (auto& toggle : toggles)
        toggle->button.setBounds (area.removeFromTop (rowH));
}
