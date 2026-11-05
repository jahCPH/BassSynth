#include "UI/PsybAudioProcessorEditor.h"

#include <algorithm>

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
    addKnob ("r_level", "ROLL LEVEL");
    addKnob ("b_sublevel", "SUB");
    addKnob ("b_output", "OUTPUT");
    addKnob ("s_cutoff", "SLIDE CUT");
    addKnob ("s_reso", "SLIDE RESO");
    addKnob ("s_glide_seq", "GLIDE");
    addKnob ("s_glide_ms", "GLIDE MS");
    addKnob ("s_accent", "ACCENT");
    addKnob ("s_envamt", "SLIDE ENV");
    addKnob ("s_fdec", "SLIDE FDEC");
    addKnob ("s_adec", "SLIDE ADEC");
    addKnob ("s_drive", "SLIDE DRIVE");
    addKnob ("s_level", "SLIDE LEVEL");
    addToggle ("r_on", "ROLL ON");
    addToggle ("r_phasereset", "PHASE RESET");
    addToggle ("r_filterreset", "FILTER RESET");
    addToggle ("s_on", "SLIDE ON");
    addToggle ("s_phasereset", "SLIDE PHASE");
    addToggle ("s_filterreset", "SLIDE FILTER");
    addToggle ("q_latch", "LATCH");
    addKnob ("b_duck_depth", "DUCK DEPTH");
    addKnob ("b_duck_release", "DUCK REL");
    addKnob ("b_duck_hold", "DUCK HOLD");
    addKnob ("b_duck_note", "DUCK NOTE");
    addToggle ("b_duck_passthru", "DUCK THRU");
    addToggle ("b_duck_target_r", "DUCK ROLL");
    addToggle ("b_duck_target_s", "DUCK SLIDE");
    addToggle ("b_duck_target_f", "DUCK FX");

    subOwner.addItemList ({ "SUB ROLL", "SUB SLIDE", "SUB OFF" }, 1);
    addAndMakeVisible (subOwner);
    subOwnerAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
        synth.parameters(), "b_subowner", subOwner);

    duckSource.addItemList ({ "DUCK INTERNAL", "DUCK MIDI", "DUCK AUDIO" }, 1);
    addAndMakeVisible (duckSource);
    duckSourceAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
        synth.parameters(), "b_duck_src", duckSource);
    duckCurve.addItemList ({ "DUCK LIN", "DUCK EXP" }, 1);
    addAndMakeVisible (duckCurve);
    duckCurveAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
        synth.parameters(), "b_duck_curve", duckCurve);
    juce::StringArray channels { "DUCK CH ANY" };
    for (int ch = 1; ch <= 16; ++ch)
        channels.add ("DUCK CH " + juce::String (ch));
    duckChannel.addItemList (channels, 1);
    addAndMakeVisible (duckChannel);
    duckChannelAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
        synth.parameters(), "b_duck_ch", duckChannel);

    duckReadout.setColour (juce::Label::textColourId, juce::Colours::white);
    addAndMakeVisible (duckReadout);
    startTimerHz (30);

    setSize (640, 1280);
}

PsybAudioProcessorEditor::~PsybAudioProcessorEditor()
{
    stopTimer();
}

void PsybAudioProcessorEditor::timerCallback()
{
    static const char* names[] = { "ACTIVE", "NO BUS", "SILENT", "WAITING", "STOPPED" };
    const int state = std::clamp (synth.duckState(), 0, 4);
    duckReadout.setText ("DUCK " + juce::String (names[state]) + "  " + juce::String (synth.duckGain(), 2),
                         juce::dontSendNotification);
}

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
        knob->label.setBounds (row.removeFromLeft (110));
        knob->slider.setBounds (row);
    }
    area.removeFromTop (8);
    for (auto& toggle : toggles)
        toggle->button.setBounds (area.removeFromTop (rowH));
    subOwner.setBounds (area.removeFromTop (rowH));
    duckSource.setBounds (area.removeFromTop (rowH));
    duckCurve.setBounds (area.removeFromTop (rowH));
    duckChannel.setBounds (area.removeFromTop (rowH));
    duckReadout.setBounds (area.removeFromTop (rowH));
}
