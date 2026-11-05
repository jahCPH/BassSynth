#include "PsybAudioProcessor.h"
#include "Dsp/AlgorithmVersion.h"
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
    add ("r_level", "Roll Level", { -60.f, 6.f, 0.1f }, 0.f);
    add ("b_sublevel", "Sub", { 0.f, 100.f }, 70.f);
    add ("b_output", "Output", { -24.f, 6.f }, 0.f);
    add ("s_cutoff", "Slide Cutoff", hz (20.f, 20000.f, 0.3f), 1200.f);
    add ("s_reso", "Slide Reso", { 0.f, 100.f }, 45.f);
    add ("s_envamt", "Slide Env", { -100.f, 100.f }, 50.f);
    add ("s_fatk", "Slide Filt Atk", hz (0.5f, 200.f, 0.3f), 0.5f);
    add ("s_fdec", "Slide Filt Dec", hz (5.f, 4000.f, 0.3f), 250.f);
    add ("s_fsus", "Slide Filt Sus", { 0.f, 100.f }, 0.f);
    add ("s_aatk", "Slide Amp Atk", hz (0.5f, 200.f, 0.3f), 0.5f);
    add ("s_adec", "Slide Amp Dec", hz (5.f, 4000.f, 0.3f), 300.f);
    add ("s_asus", "Slide Amp Sus", { 0.f, 100.f }, 0.f);
    add ("s_arel", "Slide Amp Rel", hz (2.f, 2000.f, 0.3f), 40.f);
    add ("s_drive", "Slide Drive", { 0.f, 100.f }, 15.f);
    add ("s_accent", "Accent", { 0.f, 100.f }, 50.f);
    add ("s_glide_seq", "Glide", { 0.f, 100.f }, 40.f);
    add ("s_glide_ms", "Glide ms", { 0.f, 2000.f, 0.1f }, 60.f);
    add ("s_level", "Slide Level", { -60.f, 6.f, 0.1f }, 0.f);
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { "r_on", 1 }, "Roll On", true));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { "r_phasereset", 1 }, "Phase Reset", true));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { "r_filterreset", 1 }, "Filter Reset", true));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { "s_on", 1 }, "Slide On", false));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { "s_phasereset", 1 }, "Slide Phase Reset", true));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { "s_filterreset", 1 }, "Slide Filter Reset", false));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { "q_latch", 1 }, "Latch", false));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { "b_subowner", 1 }, "Sub Owner",
                                                        StringArray { "ROLL", "SLIDE", "OFF" }, 0));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { "b_duck_src", 1 }, "Duck Source",
                                                        StringArray { "INTERNAL", "MIDI", "AUDIO" }, 0));
    add ("b_duck_depth", "Duck Depth", { 0.f, 100.f, 0.1f }, 45.f);
    add ("b_duck_release", "Duck Release", { 10.f, 500.f, 0.1f }, 120.f);
    add ("b_duck_hold", "Duck Hold", { 0.f, 50.f, 0.1f }, 0.f);
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { "b_duck_curve", 1 }, "Duck Curve",
                                                        StringArray { "LIN", "EXP" }, 1));
    add ("b_duck_note", "Duck Note", { 0.f, 127.f, 1.f }, 36.f);
    StringArray channels { "ANY" };
    for (int ch = 1; ch <= 16; ++ch)
        channels.add (String (ch));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { "b_duck_ch", 1 }, "Duck Channel", channels, 0));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { "b_duck_passthru", 1 }, "Duck Passthru", false));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { "b_duck_target_r", 1 }, "Duck Roll", true));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { "b_duck_target_s", 1 }, "Duck Slide", true));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { "b_duck_target_f", 1 }, "Duck FX", false));
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
        || sidechain == juce::AudioChannelSet::mono()
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
    engine.setEngineAudible (0, raw ("r_on") >= 0.5f);
    engine.setEngineLevelDb (0, raw ("r_level"));

    psyb::RollParams slide;
    slide.cutoffHz = raw ("s_cutoff");
    slide.reso = raw ("s_reso") / 100.f;
    slide.envAmt = raw ("s_envamt") / 100.f;
    slide.fatkMs = raw ("s_fatk");
    slide.fdecMs = raw ("s_fdec");
    slide.fsus = raw ("s_fsus") / 100.f;
    slide.aatkMs = raw ("s_aatk");
    slide.adecMs = raw ("s_adec");
    slide.asus = raw ("s_asus") / 100.f;
    slide.arelMs = raw ("s_arel");
    slide.drive = raw ("s_drive") / 100.f;
    slide.sub = raw ("b_sublevel") / 100.f;
    slide.phaseReset = raw ("s_phasereset") >= 0.5f;
    slide.filterReset = raw ("s_filterreset") >= 0.5f;
    engine.setSlideParams (slide);
    engine.setSlideAccent (raw ("s_accent") / 100.f);
    engine.setEngineLevelDb (1, raw ("s_level"));
    const bool slideOn = raw ("s_on") >= 0.5f;
    engine.setEngineAudible (1, slideOn);
    auto slideLane = engine.lane (1);
    slideLane.enabled = slideOn;
    engine.setLane (1, slideLane);
    engine.setLaneGlide (1, raw ("s_glide_seq") / 100.f, raw ("s_glide_ms"));
    if (auto* owner = dynamic_cast<juce::AudioParameterChoice*> (apvts.getParameter ("b_subowner")))
        engine.setSubOwner (static_cast<psyb::SubOwner> (owner->getIndex()));

    if (auto* src = dynamic_cast<juce::AudioParameterChoice*> (apvts.getParameter ("b_duck_src")))
        engine.setDuckSource (static_cast<psyb::DuckSource> (src->getIndex()));
    engine.setDuckDepth (raw ("b_duck_depth"));
    engine.setDuckReleaseMs (raw ("b_duck_release"));
    engine.setDuckHoldMs (raw ("b_duck_hold"));
    if (auto* curve = dynamic_cast<juce::AudioParameterChoice*> (apvts.getParameter ("b_duck_curve")))
        engine.setDuckCurve (static_cast<psyb::DuckCurve> (curve->getIndex()));
    engine.setDuckNote (static_cast<int> (std::lround (raw ("b_duck_note"))));
    if (auto* channel = dynamic_cast<juce::AudioParameterChoice*> (apvts.getParameter ("b_duck_ch")))
        engine.setDuckChannel (channel->getIndex());
    engine.setDuckPassthru (raw ("b_duck_passthru") >= 0.5f);
    engine.setDuckTarget (0, raw ("b_duck_target_r") >= 0.5f);
    engine.setDuckTarget (1, raw ("b_duck_target_s") >= 0.5f);
    engine.setDuckTarget (2, raw ("b_duck_target_f") >= 0.5f);

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

    const float* sidechain[2] = { nullptr, nullptr };
    int sidechainChannels = 0;
    if (getBusCount (true) > 0)
    {
        auto side = getBusBuffer (buffer, true, 0);
        sidechainChannels = std::min (2, side.getNumChannels());
        for (int c = 0; c < sidechainChannels; ++c)
            sidechain[c] = side.getReadPointer (c);
    }

    if (output.getNumSamples() > 0)
        engine.process (block,
                        midiEvents.empty() ? nullptr : midiEvents.data(),
                        static_cast<int> (midiEvents.size()),
                        output.getWritePointer (0),
                        sidechainChannels > 0 ? sidechain : nullptr,
                        sidechainChannels);

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
    {
        xml->setAttribute ("algo_r", engine.storedAlgorithmVersion (0));
        xml->setAttribute ("algo_s", engine.storedAlgorithmVersion (1));
        xml->setAttribute ("algo_f", engine.storedAlgorithmVersion (2));
        copyXmlToBinary (*xml, dest);
    }
}

void PsybAudioProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
    {
        psyb::AlgorithmVersionFields fields;
        fields.hasRoll = xml->hasAttribute ("algo_r");
        fields.hasSlide = xml->hasAttribute ("algo_s");
        fields.hasFx = xml->hasAttribute ("algo_f");
        fields.roll = xml->getIntAttribute ("algo_r", 1);
        fields.slide = xml->getIntAttribute ("algo_s", 1);
        fields.fx = xml->getIntAttribute ("algo_f", 1);
        const auto versions = psyb::resolveAlgorithmVersions (fields);
        engine.setStoredAlgorithmVersion (0, versions.roll);
        engine.setStoredAlgorithmVersion (1, versions.slide);
        engine.setStoredAlgorithmVersion (2, versions.fx);
        apvts.replaceState (juce::ValueTree::fromXml (*xml));
    }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new PsybAudioProcessor();
}
