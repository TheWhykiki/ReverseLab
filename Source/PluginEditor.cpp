#include "PluginEditor.h"

namespace
{
const auto background = juce::Colour(0xff0b0f17);
const auto panel = juce::Colour(0xff141b28);
const auto accent = juce::Colour(0xff61e6c4);
const auto violet = juce::Colour(0xff8b7cf6);
const auto text = juce::Colour(0xffe8edf7);
const auto muted = juce::Colour(0xff8290a8);
}

ReverseLabLookAndFeel::ReverseLabLookAndFeel()
{
    setColour(juce::Slider::textBoxTextColourId, text);
    setColour(juce::Slider::textBoxBackgroundColourId, juce::Colour(0xff0f1520));
    setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour(juce::ComboBox::backgroundColourId, juce::Colour(0xff111925));
    setColour(juce::ComboBox::textColourId, text);
    setColour(juce::ComboBox::outlineColourId, juce::Colour(0xff273247));
    setColour(juce::PopupMenu::backgroundColourId, panel);
    setColour(juce::PopupMenu::textColourId, text);
    setColour(juce::ToggleButton::textColourId, text);
}

void ReverseLabLookAndFeel::drawRotarySlider(juce::Graphics& g, int x, int y, int w, int h,
                                              float position, float start, float end, juce::Slider&)
{
    const auto bounds = juce::Rectangle<float>(x, y, w, h).reduced(9.0f);
    const auto radius = juce::jmin(bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const auto centre = bounds.getCentre();
    const auto angle = start + position * (end - start);
    g.setColour(juce::Colour(0xff222c3d));
    g.fillEllipse(bounds.withSizeKeepingCentre(radius * 2.0f, radius * 2.0f));
    juce::Path track;
    track.addCentredArc(centre.x, centre.y, radius - 3.0f, radius - 3.0f, 0.0f, start, end, true);
    g.setColour(juce::Colour(0xff344158));
    g.strokePath(track, juce::PathStrokeType(4.0f, juce::PathStrokeType::curved));
    juce::Path active;
    active.addCentredArc(centre.x, centre.y, radius - 3.0f, radius - 3.0f, 0.0f, start, angle, true);
    g.setColour(accent);
    g.strokePath(active, juce::PathStrokeType(4.0f, juce::PathStrokeType::curved));
    juce::Path pointer;
    pointer.addRoundedRectangle(-2.0f, -radius + 7.0f, 4.0f, radius * 0.42f, 2.0f);
    g.setColour(text);
    g.fillPath(pointer, juce::AffineTransform::rotation(angle).translated(centre.x, centre.y));
}

ScopeComponent::ScopeComponent(ReverseLabAudioProcessor& p) : processor(p)
{
    setAccessible(true);
    setTitle("Reverse waveform and playback positions");
    startTimerHz(30);
}

void ScopeComponent::paint(juce::Graphics& g)
{
    auto area = getLocalBounds().toFloat();
    g.setColour(panel);
    g.fillRoundedRectangle(area, 12.0f);
    g.setColour(juce::Colour(0xff263147));
    for (int i = 1; i < 8; ++i)
    {
        const auto x = area.getX() + area.getWidth() * static_cast<float>(i) / 8.0f;
        g.drawVerticalLine(static_cast<int>(x), area.getY() + 8.0f, area.getBottom() - 8.0f);
    }
    juce::Path waveform;
    const auto write = processor.getScopeWriteIndex();
    for (int x = 0; x < static_cast<int>(area.getWidth()); ++x)
    {
        const auto index = (write + x * 256 / juce::jmax(1, static_cast<int>(area.getWidth()))) % 256;
        const auto value = juce::jlimit(-1.0f, 1.0f, processor.getScopeSample(index));
        const auto px = area.getX() + static_cast<float>(x);
        const auto py = area.getCentreY() - value * area.getHeight() * 0.40f;
        if (x == 0) waveform.startNewSubPath(px, py); else waveform.lineTo(px, py);
    }
    g.setColour(accent.withAlpha(0.9f));
    g.strokePath(waveform, juce::PathStrokeType(1.7f));
    const auto left = std::fmod(processor.getEnginePhase(0) / 48000.0f, 1.0f);
    const auto right = std::fmod(processor.getEnginePhase(1) / 48000.0f, 1.0f);
    g.setColour(accent); g.fillEllipse(area.getX() + left * area.getWidth() - 3.0f, area.getY() + 7.0f, 6.0f, 6.0f);
    g.setColour(violet); g.fillEllipse(area.getX() + right * area.getWidth() - 3.0f, area.getBottom() - 13.0f, 6.0f, 6.0f);
    g.setColour(muted); g.setFont(11.0f); g.drawText("REVERSE BUFFER  •  L / R", area.reduced(12.0f), juce::Justification::topLeft);
}

ReverseLabAudioProcessorEditor::ReverseLabAudioProcessorEditor(ReverseLabAudioProcessor& p)
    : AudioProcessorEditor(&p), processor(p), scope(p)
{
    setLookAndFeel(&lookAndFeel);
    setResizable(true, true);
    setResizeLimits(720, 460, 1440, 920);
    const auto savedSize = processor.getLastEditorSize();
    setSize(savedSize.x, savedSize.y);

    title.setText("ReverseLab", juce::dontSendNotification);
    title.setFont(juce::Font(juce::FontOptions(29.0f, juce::Font::bold)));
    title.setColour(juce::Label::textColourId, text);
    subtitle.setText("STEREO TIME REVERSAL", juce::dontSendNotification);
    subtitle.setFont(juce::Font(juce::FontOptions(11.0f, juce::Font::bold)));
    subtitle.setColour(juce::Label::textColourId, accent);
    latencyLabel.setColour(juce::Label::textColourId, muted);
    latencyLabel.setJustificationType(juce::Justification::centredRight);
    addAndMakeVisible(title); addAndMakeVisible(subtitle); addAndMakeVisible(latencyLabel); addAndMakeVisible(scope);

    auto addChoice = [this](juce::ComboBox& box, juce::Label& label, const juce::String& name)
    {
        box.addItemList(rl::params::subdivisionNames(), 1);
        box.setTitle(name); box.setDescription(name + " reverse segment length");
        label.setText(name, juce::dontSendNotification); label.setColour(juce::Label::textColourId, muted);
        label.setJustificationType(juce::Justification::centred);
        addAndMakeVisible(box); addAndMakeVisible(label);
    };
    addChoice(leftSize, leftSizeLabel, "LEFT SIZE"); addChoice(rightSize, rightSizeLabel, "RIGHT SIZE");
    leftSizeAttachment = std::make_unique<ComboAttachment>(processor.parameters, rl::params::leftSize, leftSize);
    rightSizeAttachment = std::make_unique<ComboAttachment>(processor.parameters, rl::params::rightSize, rightSize);

    const std::array<std::pair<const char*, const char*>, 9> knobSpecs {{
        { "SPEED", rl::params::speed }, { "CROSSFADE", rl::params::crossfade }, { "DRY / WET", rl::params::mix },
        { "FEEDBACK", rl::params::feedback }, { "HIGH-PASS", rl::params::highpass }, { "LOW-PASS", rl::params::lowpass },
        { "STEREO", rl::params::stereoOffset }, { "RANDOM", rl::params::random }, { "OUTPUT", rl::params::output }
    }};
    for (size_t i = 0; i < knobs.size(); ++i) configureKnob(knobs[i], knobSpecs[i].first, knobSpecs[i].second);

    configureButton(sync, "SYNC", rl::params::sync, syncAttachment);
    configureButton(link, "LINK L/R", rl::params::link, linkAttachment);
    configureButton(freeze, "FREEZE", rl::params::freeze, freezeAttachment);
    configureButton(retrigger, "RETRIGGER", rl::params::retrigger, retriggerAttachment);
    configureButton(bypass, "BYPASS", rl::params::bypass, bypassAttachment);

    presetLabel.setText("PRESET", juce::dontSendNotification); presetLabel.setColour(juce::Label::textColourId, muted);
    for (int i = 0; i < processor.getNumPrograms(); ++i) presetBox.addItem(processor.getProgramName(i), i + 1);
    presetBox.setSelectedId(processor.getCurrentProgram() + 1, juce::dontSendNotification);
    presetBox.onChange = [this] { processor.setCurrentProgram(presetBox.getSelectedId() - 1); };
    addAndMakeVisible(presetLabel); addAndMakeVisible(presetBox);
    startTimerHz(10);
}

ReverseLabAudioProcessorEditor::~ReverseLabAudioProcessorEditor()
{
    processor.setLastEditorSize(getWidth(), getHeight());
    setLookAndFeel(nullptr);
}

void ReverseLabAudioProcessorEditor::configureKnob(Knob& knob, const juce::String& name, const char* id)
{
    knob.slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    knob.slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 74, 18);
    auto* parameter = processor.parameters.getParameter(id);
    knob.slider.setDoubleClickReturnValue(true, parameter->convertFrom0to1(parameter->getDefaultValue()));
    knob.slider.setTitle(name); knob.slider.setDescription(name + " parameter");
    knob.label.setText(name, juce::dontSendNotification); knob.label.setColour(juce::Label::textColourId, muted);
    knob.label.setJustificationType(juce::Justification::centred);
    knob.attachment = std::make_unique<SliderAttachment>(processor.parameters, id, knob.slider);
    addAndMakeVisible(knob.slider); addAndMakeVisible(knob.label);
}

void ReverseLabAudioProcessorEditor::configureButton(juce::ToggleButton& button, const juce::String& name,
                                                       const char* id, std::unique_ptr<ButtonAttachment>& attachment)
{
    button.setButtonText(name); button.setTitle(name); button.setClickingTogglesState(true);
    attachment = std::make_unique<ButtonAttachment>(processor.parameters, id, button);
    addAndMakeVisible(button);
}

void ReverseLabAudioProcessorEditor::paint(juce::Graphics& g)
{
    g.fillAll(background);
    g.setGradientFill(juce::ColourGradient(juce::Colour(0xff172236), 0.0f, 0.0f,
                                           background, 0.0f, static_cast<float>(getHeight()), false));
    g.fillRect(getLocalBounds());
    auto lower = getLocalBounds().toFloat().withTrimmedTop(getHeight() * 0.49f).reduced(18.0f, 6.0f);
    g.setColour(panel.withAlpha(0.78f)); g.fillRoundedRectangle(lower, 15.0f);
}

void ReverseLabAudioProcessorEditor::resized()
{
    processor.setLastEditorSize(getWidth(), getHeight());
    auto area = getLocalBounds().reduced(18);
    auto header = area.removeFromTop(54);
    title.setBounds(header.removeFromLeft(210));
    subtitle.setBounds(header.removeFromLeft(190).withTrimmedTop(9));
    bypass.setBounds(header.removeFromRight(86).reduced(3));
    latencyLabel.setBounds(header.removeFromRight(230));
    scope.setBounds(area.removeFromTop(150));
    area.removeFromTop(10);

    auto timing = area.removeFromTop(86);
    auto presetArea = timing.removeFromLeft(170).reduced(4);
    presetLabel.setBounds(presetArea.removeFromTop(20)); presetBox.setBounds(presetArea.removeFromTop(30));
    auto left = timing.removeFromLeft(150).reduced(6);
    leftSizeLabel.setBounds(left.removeFromTop(20)); leftSize.setBounds(left.removeFromTop(32));
    auto right = timing.removeFromLeft(150).reduced(6);
    rightSizeLabel.setBounds(right.removeFromTop(20)); rightSize.setBounds(right.removeFromTop(32));
    sync.setBounds(timing.removeFromLeft(90).reduced(6)); link.setBounds(timing.removeFromLeft(110).reduced(6));
    freeze.setBounds(timing.removeFromLeft(100).reduced(6)); retrigger.setBounds(timing.reduced(6));

    auto knobArea = area.reduced(8, 2);
    const int columns = 9;
    const auto width = knobArea.getWidth() / columns;
    for (int i = 0; i < columns; ++i)
    {
        auto cell = knobArea.removeFromLeft(i == columns - 1 ? knobArea.getWidth() : width).reduced(3);
        knobs[(size_t) i].label.setBounds(cell.removeFromTop(22));
        knobs[(size_t) i].slider.setBounds(cell);
    }
}

void ReverseLabAudioProcessorEditor::timerCallback()
{
    const auto wantsSyncValues = processor.parameters.getRawParameterValue(rl::params::sync)->load() > 0.5f;
    if (wantsSyncValues != showingSyncValues)
    {
        showingSyncValues = wantsSyncValues;
        const auto leftSelection = leftSize.getSelectedId();
        const auto rightSelection = rightSize.getSelectedId();
        juce::StringArray names;
        if (showingSyncValues)
            names = rl::params::subdivisionNames();
        else
            for (int i = 0; i < 15; ++i)
            {
                const auto normalized = static_cast<double>(i) / 14.0;
                names.add(juce::String(20.0 * std::pow(200.0, normalized), 0) + " ms");
            }
        leftSize.clear(juce::dontSendNotification); rightSize.clear(juce::dontSendNotification);
        leftSize.addItemList(names, 1); rightSize.addItemList(names, 1);
        leftSize.setSelectedId(leftSelection, juce::dontSendNotification);
        rightSize.setSelectedId(rightSelection, juce::dontSendNotification);
    }
    const auto samples = processor.getCurrentLatencySamples();
    const auto ms = samples * 1000.0 / juce::jmax(1.0, processor.getSampleRate());
    latencyLabel.setText("LATENCY  " + juce::String(samples) + " smp  /  " + juce::String(ms, 1) + " ms",
                         juce::dontSendNotification);
}
