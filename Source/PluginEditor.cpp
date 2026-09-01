#include "PluginEditor.h"
#include <cmath>

namespace
{
const auto background = juce::Colour(0xff080d14);
const auto panel = juce::Colour(0xff101823);
const auto raised = juce::Colour(0xff172230);
const auto border = juce::Colour(0xff2b3848);
const auto accent = juce::Colour(0xff67e3c0);
const auto violet = juce::Colour(0xffa07cf3);
const auto text = juce::Colour(0xfff0eee8);
const auto muted = juce::Colour(0xff8e99aa);

juce::Rectangle<int> contentBounds(juce::Component& component)
{
    const auto margin = component.getWidth() < 820 ? 12 : 16;
    return component.getLocalBounds().reduced(margin);
}

void drawDirectionArrow(juce::Graphics& g, juce::Rectangle<float> bounds, juce::Colour colour)
{
    const auto y = bounds.getCentreY();
    const auto left = bounds.getX();
    const auto right = bounds.getRight();
    juce::Path arrow;
    arrow.startNewSubPath(right, y);
    arrow.lineTo(left + 8.0f, y);
    arrow.startNewSubPath(left + 8.0f, y);
    arrow.lineTo(left + 18.0f, y - 9.0f);
    arrow.startNewSubPath(left + 8.0f, y);
    arrow.lineTo(left + 18.0f, y + 9.0f);
    g.setColour(colour);
    g.strokePath(arrow, juce::PathStrokeType(2.2f, juce::PathStrokeType::curved,
                                             juce::PathStrokeType::rounded));
}
}

ReverseLabLookAndFeel::ReverseLabLookAndFeel()
{
    setColour(juce::Slider::textBoxTextColourId, text);
    setColour(juce::Slider::textBoxBackgroundColourId, background.brighter(0.06f));
    setColour(juce::Slider::textBoxOutlineColourId, border);
    setColour(juce::ComboBox::backgroundColourId, panel);
    setColour(juce::ComboBox::textColourId, text);
    setColour(juce::ComboBox::outlineColourId, border);
    setColour(juce::PopupMenu::backgroundColourId, panel);
    setColour(juce::PopupMenu::textColourId, text);
    setColour(juce::ToggleButton::textColourId, text);
}

void ReverseLabLookAndFeel::drawRotarySlider(juce::Graphics& g, int x, int y, int w, int h,
                                              float position, float start, float end, juce::Slider& slider)
{
    const auto sizeControl = slider.getName().containsIgnoreCase("size");
    const auto bounds = juce::Rectangle<float>(x, y, w, h).reduced(sizeControl ? 8.0f : 7.0f);
    const auto diameter = juce::jmin(bounds.getWidth(), bounds.getHeight());
    const auto circle = bounds.withSizeKeepingCentre(diameter, diameter);
    const auto radius = diameter * 0.5f;
    const auto centre = circle.getCentre();
    const auto angle = start + position * (end - start);
    const auto activeColour = slider.getName().startsWithIgnoreCase("right") ? violet : accent;

    g.setColour(background.withAlpha(0.72f)); g.fillEllipse(circle);
    g.setColour(border.withAlpha(0.85f)); g.drawEllipse(circle, sizeControl ? 2.0f : 1.0f);
    juce::Path track;
    track.addCentredArc(centre.x, centre.y, radius - 4.0f, radius - 4.0f, 0.0f, start, end, true);
    g.setColour(juce::Colour(0xff344052));
    g.strokePath(track, juce::PathStrokeType(sizeControl ? 6.0f : 4.0f, juce::PathStrokeType::curved));
    juce::Path activePath;
    activePath.addCentredArc(centre.x, centre.y, radius - 4.0f, radius - 4.0f, 0.0f, start, angle, true);
    g.setColour(activeColour);
    g.strokePath(activePath, juce::PathStrokeType(sizeControl ? 6.0f : 4.0f, juce::PathStrokeType::curved));
    juce::Path pointer;
    pointer.addRoundedRectangle(-1.8f, -radius + 9.0f, 3.6f, radius * 0.34f, 1.8f);
    g.setColour(text);
    g.fillPath(pointer, juce::AffineTransform::rotation(angle).translated(centre.x, centre.y));
}

void ReverseLabLookAndFeel::drawToggleButton(juce::Graphics& g, juce::ToggleButton& button,
                                              bool highlighted, bool down)
{
    auto bounds = button.getLocalBounds().toFloat().reduced(1.0f);
    const auto active = button.getToggleState();
    const auto activeColour = button.getButtonText().containsIgnoreCase("retrigger") ? violet : accent;
    auto fill = active ? activeColour.withAlpha(down ? 0.30f : 0.18f) : raised.withAlpha(0.72f);
    if (highlighted) fill = fill.brighter(0.08f);
    g.setColour(fill); g.fillRoundedRectangle(bounds, 6.0f);
    g.setColour(active ? activeColour : border);
    g.drawRoundedRectangle(bounds, 6.0f, active || highlighted ? 1.7f : 1.0f);
    g.setColour(active ? activeColour : text);
    g.setFont(juce::Font(juce::FontOptions(button.getHeight() > 42 ? 15.0f : 12.0f, juce::Font::bold)));
    g.drawFittedText(button.getButtonText(), button.getLocalBounds().reduced(6), juce::Justification::centred, 1);
}

void ReverseLabLookAndFeel::drawComboBox(juce::Graphics& g, int width, int height, bool,
                                          int, int, int, int, juce::ComboBox&)
{
    const auto bounds = juce::Rectangle<float>(0.0f, 0.0f, static_cast<float>(width),
                                                static_cast<float>(height)).reduced(0.5f);
    g.setColour(panel); g.fillRoundedRectangle(bounds, 6.0f);
    g.setColour(border); g.drawRoundedRectangle(bounds, 6.0f, 1.0f);
    juce::Path chevron;
    const auto cx = static_cast<float>(width - 15), cy = static_cast<float>(height) * 0.5f;
    chevron.startNewSubPath(cx - 4.0f, cy - 2.0f);
    chevron.lineTo(cx, cy + 2.0f); chevron.lineTo(cx + 4.0f, cy - 2.0f);
    g.setColour(muted); g.strokePath(chevron, juce::PathStrokeType(1.6f));
}

ScopeComponent::ScopeComponent(ReverseLabAudioProcessor& p) : processor(p)
{
    setAccessible(true);
    setTitle("Stereo reverse waveform with left and right playback positions");
    setDescription("Live reverse buffer overview");
    startTimerHz(30);
}

void ScopeComponent::paint(juce::Graphics& g)
{
    auto area = getLocalBounds().toFloat();
    g.setColour(panel); g.fillRoundedRectangle(area, 10.0f);
    g.setColour(border); g.drawRoundedRectangle(area.reduced(0.5f), 10.0f, 1.0f);
    auto plot = area.reduced(14.0f, 10.0f);
    auto caption = plot.removeFromTop(18.0f);
    g.setColour(text); g.setFont(juce::Font(juce::FontOptions(11.0f, juce::Font::bold)));
    g.drawText("REVERSE BUFFER", caption.removeFromLeft(110.0f), juce::Justification::centredLeft);
    g.setColour(accent); g.drawText("L", caption.removeFromLeft(14.0f), juce::Justification::centred);
    g.setColour(muted); g.drawText("/", caption.removeFromLeft(10.0f), juce::Justification::centred);
    g.setColour(violet); g.drawText("R", caption.removeFromLeft(14.0f), juce::Justification::centred);
    const auto centre = plot.getCentreY();
    g.setColour(border.withAlpha(0.75f));
    for (int i = 0; i <= 8; ++i)
    {
        const auto px = plot.getX() + plot.getWidth() * static_cast<float>(i) / 8.0f;
        g.drawVerticalLine(static_cast<int>(px), plot.getY(), plot.getBottom());
    }
    g.drawHorizontalLine(static_cast<int>(centre), plot.getX(), plot.getRight());
    const auto write = processor.getScopeWriteIndex();
    juce::Path leftWave, rightWave;
    for (int x = 0; x < static_cast<int>(plot.getWidth()); ++x)
    {
        const auto index = (write + x * 256 / juce::jmax(1, static_cast<int>(plot.getWidth()))) % 256;
        const auto value = juce::jlimit(-1.0f, 1.0f, processor.getScopeSample(index));
        const auto px = plot.getX() + static_cast<float>(x);
        const auto amplitude = plot.getHeight() * 0.18f;
        const auto leftY = centre - plot.getHeight() * 0.23f - value * amplitude;
        const auto rightY = centre + plot.getHeight() * 0.23f + value * amplitude;
        if (x == 0) { leftWave.startNewSubPath(px, leftY); rightWave.startNewSubPath(px, rightY); }
        else { leftWave.lineTo(px, leftY); rightWave.lineTo(px, rightY); }
    }
    g.setColour(accent.withAlpha(0.90f)); g.strokePath(leftWave, juce::PathStrokeType(1.6f));
    g.setColour(violet.withAlpha(0.88f)); g.strokePath(rightWave, juce::PathStrokeType(1.6f));
    drawDirectionArrow(g, plot.withSizeKeepingCentre(plot.getWidth() * 0.16f, 22.0f)
                              .translated(0.0f, -plot.getHeight() * 0.23f), accent.withAlpha(0.82f));
    drawDirectionArrow(g, plot.withSizeKeepingCentre(plot.getWidth() * 0.16f, 22.0f)
                              .translated(0.0f, plot.getHeight() * 0.23f), violet.withAlpha(0.82f));
    const auto left = processor.getEnginePhase(0), right = processor.getEnginePhase(1);
    const auto leftX = plot.getX() + left * plot.getWidth(), rightX = plot.getX() + right * plot.getWidth();
    g.setColour(accent); g.fillRect(leftX - 1.0f, plot.getY(), 2.0f, plot.getHeight() * 0.48f);
    g.setColour(violet); g.fillRect(rightX - 1.0f, centre, 2.0f, plot.getHeight() * 0.50f);
}

ReverseLabAudioProcessorEditor::ReverseLabAudioProcessorEditor(ReverseLabAudioProcessor& p)
    : AudioProcessorEditor(&p), pluginProcessor(p), scope(p)
{
    setLookAndFeel(&lookAndFeel);
    setResizable(true, true);
    setResizeLimits(720, 460, 1440, 920);
    const auto savedSize = pluginProcessor.getLastEditorSize();
    setSize(savedSize.x, savedSize.y);
    title.setText("ReverseLab", juce::dontSendNotification);
    title.setFont(juce::Font(juce::FontOptions(27.0f, juce::Font::bold)));
    title.setColour(juce::Label::textColourId, text);
    subtitle.setText("STUDIO INSTRUMENT", juce::dontSendNotification);
    subtitle.setFont(juce::Font(juce::FontOptions(10.5f, juce::Font::bold)));
    subtitle.setColour(juce::Label::textColourId, accent);
    latencyLabel.setColour(juce::Label::textColourId, muted);
    latencyLabel.setJustificationType(juce::Justification::centredRight);
    addAndMakeVisible(title); addAndMakeVisible(subtitle); addAndMakeVisible(latencyLabel); addAndMakeVisible(scope);
    configureSizeControl(leftSize, leftSizeLabel, "LEFT SIZE", rl::params::leftSize, leftSizeAttachment);
    configureSizeControl(rightSize, rightSizeLabel, "RIGHT SIZE", rl::params::rightSize, rightSizeAttachment);
    configureFreeControl(leftFreeTime, "LEFT FREE TIME", rl::params::leftFreeMs, leftFreeAttachment);
    configureFreeControl(rightFreeTime, "RIGHT FREE TIME", rl::params::rightFreeMs, rightFreeAttachment);
    const std::array<std::pair<const char*, const char*>, 10> knobSpecs {{
        { "SPEED", rl::params::speed }, { "CROSSFADE", rl::params::crossfade }, { "DRY / WET", rl::params::mix },
        { "FEEDBACK", rl::params::feedback }, { "HIGH-PASS", rl::params::highpass }, { "LOW-PASS", rl::params::lowpass },
        { "OFFSET", rl::params::stereoOffset }, { "RANDOM", rl::params::random }, { "OUTPUT", rl::params::output },
        { "SEED", rl::params::seed }
    }};
    for (size_t i = 0; i < knobs.size(); ++i) configureKnob(knobs[i], knobSpecs[i].first, knobSpecs[i].second);
    configureButton(sync, "SYNC", rl::params::sync, syncAttachment);
    configureButton(link, "LINK L/R", rl::params::link, linkAttachment);
    configureButton(freeze, "FREEZE", rl::params::freeze, freezeAttachment);
    configureButton(retrigger, "RETRIGGER", rl::params::retrigger, retriggerAttachment);
    configureButton(bypass, "BYPASS", rl::params::bypass, bypassAttachment);
    freeze.setDescription("Hold the current reverse buffer");
    retrigger.setDescription("Restart the reverse section momentarily");
    presetLabel.setText("PRESET", juce::dontSendNotification);
    presetLabel.setColour(juce::Label::textColourId, muted);
    presetLabel.setFont(juce::Font(juce::FontOptions(9.5f, juce::Font::bold)));
    for (int i = 0; i < pluginProcessor.getNumPrograms(); ++i)
        presetBox.addItem(pluginProcessor.getProgramName(i), i + 1);
    presetBox.setSelectedId(pluginProcessor.getCurrentProgram() + 1, juce::dontSendNotification);
    presetBox.setTitle("Factory preset");
    presetBox.onChange = [this] { pluginProcessor.setCurrentProgram(presetBox.getSelectedId() - 1); };
    addAndMakeVisible(presetLabel); addAndMakeVisible(presetBox);
    startTimerHz(10);
    showingSyncValues = pluginProcessor.parameters.getRawParameterValue(rl::params::sync)->load() > 0.5f;
    leftSize.setVisible(showingSyncValues); rightSize.setVisible(showingSyncValues);
    leftFreeTime.setVisible(!showingSyncValues); rightFreeTime.setVisible(!showingSyncValues);
}

void ReverseLabAudioProcessorEditor::configureFreeControl(juce::Slider& slider, const juce::String& name,
                                                            const char* id,
                                                            std::unique_ptr<SliderAttachment>& attachment)
{
    slider.setName(name);
    slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 92, 22);
    slider.setTitle(name);
    slider.setDescription(name + " from 20 to 4000 milliseconds");
    slider.textFromValueFunction = [](double value) { return juce::String(value, value < 100.0 ? 1 : 0) + " ms"; };
    attachment = std::make_unique<SliderAttachment>(pluginProcessor.parameters, id, slider);
    addAndMakeVisible(slider);
}

ReverseLabAudioProcessorEditor::~ReverseLabAudioProcessorEditor()
{
    pluginProcessor.setLastEditorSize(getWidth(), getHeight());
    setLookAndFeel(nullptr);
}

void ReverseLabAudioProcessorEditor::configureSizeControl(juce::Slider& slider, juce::Label& label,
                                                           const juce::String& name, const char* id,
                                                           std::unique_ptr<SliderAttachment>& attachment)
{
    slider.setName(name);
    slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 92, 22);
    slider.setRange(0.0, 14.0, 1.0);
    slider.setTitle(name);
    slider.setDescription(name + " reverse segment length");
    slider.textFromValueFunction = [this](double value)
    {
        const auto index = juce::jlimit(0, 14, static_cast<int>(std::round(value)));
        if (pluginProcessor.parameters.getRawParameterValue(rl::params::sync)->load() > 0.5f)
            return rl::params::subdivisionNames()[index];
        const auto normalized = static_cast<double>(index) / 14.0;
        return juce::String(20.0 * std::pow(200.0, normalized), 0) + " ms";
    };
    label.setText(name, juce::dontSendNotification);
    label.setColour(juce::Label::textColourId, name.startsWith("RIGHT") ? violet : accent);
    label.setFont(juce::Font(juce::FontOptions(12.0f, juce::Font::bold)));
    label.setJustificationType(juce::Justification::centred);
    attachment = std::make_unique<SliderAttachment>(pluginProcessor.parameters, id, slider);
    addAndMakeVisible(slider); addAndMakeVisible(label);
}

void ReverseLabAudioProcessorEditor::configureKnob(Knob& knob, const juce::String& name, const char* id)
{
    knob.slider.setName(name);
    knob.slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    knob.slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 72, 18);
    auto* parameter = pluginProcessor.parameters.getParameter(id);
    knob.slider.setDoubleClickReturnValue(true, parameter->convertFrom0to1(parameter->getDefaultValue()));
    knob.slider.setTitle(name); knob.slider.setDescription(name + " parameter");
    knob.label.setText(name, juce::dontSendNotification);
    knob.label.setColour(juce::Label::textColourId, muted);
    knob.label.setFont(juce::Font(juce::FontOptions(9.5f, juce::Font::bold)));
    knob.label.setJustificationType(juce::Justification::centred);
    knob.attachment = std::make_unique<SliderAttachment>(pluginProcessor.parameters, id, knob.slider);
    if (juce::String(id) == rl::params::highpass)
        knob.slider.textFromValueFunction = [](double value)
        {
            return value <= 20.01 ? juce::String("Off") : juce::String(value, 0) + " Hz";
        };
    else if (juce::String(id) == rl::params::lowpass)
        knob.slider.textFromValueFunction = [](double value)
        {
            return value >= 19999.0 ? juce::String("Off") : juce::String(value, 0) + " Hz";
        };
    knob.slider.updateText();
    addAndMakeVisible(knob.slider); addAndMakeVisible(knob.label);
}

void ReverseLabAudioProcessorEditor::configureButton(juce::ToggleButton& button, const juce::String& name,
                                                       const char* id, std::unique_ptr<ButtonAttachment>& attachment)
{
    button.setButtonText(name); button.setTitle(name); button.setClickingTogglesState(true);
    attachment = std::make_unique<ButtonAttachment>(pluginProcessor.parameters, id, button);
    addAndMakeVisible(button);
}

void ReverseLabAudioProcessorEditor::paint(juce::Graphics& g)
{
    g.fillAll(background);
    auto area = contentBounds(*this);
    area.removeFromTop(juce::jlimit(42, 56, static_cast<int>(getHeight() * 0.09f)));
    area.removeFromTop(juce::jlimit(92, 138, static_cast<int>(getHeight() * 0.22f)) + 7);
    auto timing = area.removeFromTop(juce::jlimit(102, 150, static_cast<int>(getHeight() * 0.24f)));
    g.setColour(panel.withAlpha(0.82f)); g.fillRoundedRectangle(timing.toFloat(), 10.0f);
    g.setColour(border); g.drawRoundedRectangle(timing.toFloat().reduced(0.5f), 10.0f, 1.0f);
    area.removeFromTop(7 + juce::jlimit(40, 52, static_cast<int>(getHeight() * 0.085f)) + 7);
    const std::array<std::pair<juce::String, float>, 4> groups {{
        { "MOTION", 0.22f }, { "MIX", 0.33f }, { "TONE", 0.22f }, { "STEREO", 0.23f }
    }};
    auto remaining = area;
    for (size_t i = 0; i < groups.size(); ++i)
    {
        const auto width = i == groups.size() - 1 ? remaining.getWidth() : static_cast<int>(area.getWidth() * groups[i].second);
        auto group = remaining.removeFromLeft(width).reduced(2, 0);
        g.setColour(panel.withAlpha(0.82f)); g.fillRoundedRectangle(group.toFloat(), 9.0f);
        g.setColour(border); g.drawRoundedRectangle(group.toFloat().reduced(0.5f), 9.0f, 1.0f);
        g.setColour(i == 2 ? violet : accent);
        g.setFont(juce::Font(juce::FontOptions(10.0f, juce::Font::bold)));
        g.drawText(groups[i].first, group.removeFromTop(18), juce::Justification::centred);
    }
}

void ReverseLabAudioProcessorEditor::resized()
{
    pluginProcessor.setLastEditorSize(getWidth(), getHeight());
    auto area = contentBounds(*this);
    const auto compact = getWidth() < 820;
    auto header = area.removeFromTop(juce::jlimit(42, 56, static_cast<int>(getHeight() * 0.09f)));
    title.setBounds(header.removeFromLeft(compact ? 150 : 185));
    subtitle.setVisible(!compact);
    if (!compact) subtitle.setBounds(header.removeFromLeft(130).withTrimmedTop(5));
    bypass.setBounds(header.removeFromRight(compact ? 66 : 76).reduced(2, 7));
    latencyLabel.setBounds(header.removeFromRight(compact ? 116 : 150));
    link.setBounds(header.removeFromRight(compact ? 66 : 78).reduced(2, 7));
    sync.setBounds(header.removeFromRight(compact ? 58 : 66).reduced(2, 7));
    auto presetArea = header.reduced(4, 0);
    presetLabel.setBounds(presetArea.removeFromTop(15)); presetBox.setBounds(presetArea.removeFromTop(30));
    scope.setBounds(area.removeFromTop(juce::jlimit(92, 138, static_cast<int>(getHeight() * 0.22f))));
    area.removeFromTop(7);
    auto timing = area.removeFromTop(juce::jlimit(102, 150, static_cast<int>(getHeight() * 0.24f))).reduced(8, 3);
    auto left = timing.removeFromLeft(timing.getWidth() / 2).reduced(8, 0), right = timing.reduced(8, 0);
    leftSizeLabel.setBounds(left.removeFromTop(18)); leftSize.setBounds(left); leftFreeTime.setBounds(left);
    rightSizeLabel.setBounds(right.removeFromTop(18)); rightSize.setBounds(right); rightFreeTime.setBounds(right);
    area.removeFromTop(7);
    auto performance = area.removeFromTop(juce::jlimit(40, 52, static_cast<int>(getHeight() * 0.085f)));
    freeze.setBounds(performance.removeFromLeft((performance.getWidth() - 6) / 2));
    performance.removeFromLeft(6); retrigger.setBounds(performance); area.removeFromTop(7);
    auto layoutGroup = [this](juce::Rectangle<int> group, std::initializer_list<int> indices)
    {
        group.reduce(4, 2); group.removeFromTop(18);
        const auto count = static_cast<int>(indices.size()); auto remaining = group; int position = 0;
        for (const auto index : indices)
        {
            const auto width = position == count - 1 ? remaining.getWidth() : group.getWidth() / count;
            auto cell = remaining.removeFromLeft(width).reduced(1, 0);
            knobs[(size_t) index].label.setBounds(cell.removeFromTop(16)); knobs[(size_t) index].slider.setBounds(cell);
            ++position;
        }
    };
    auto bottom = area;
    auto motion = bottom.removeFromLeft(static_cast<int>(area.getWidth() * 0.22f));
    auto mix = bottom.removeFromLeft(static_cast<int>(area.getWidth() * 0.33f));
    auto tone = bottom.removeFromLeft(static_cast<int>(area.getWidth() * 0.22f));
    layoutGroup(motion, { 0, 1 }); layoutGroup(mix, { 2, 3, 8 });
    layoutGroup(tone, { 4, 5 }); layoutGroup(bottom, { 6, 7, 9 });
}

void ReverseLabAudioProcessorEditor::timerCallback()
{
    const auto wantsSyncValues = pluginProcessor.parameters.getRawParameterValue(rl::params::sync)->load() > 0.5f;
    if (wantsSyncValues != showingSyncValues)
    {
        showingSyncValues = wantsSyncValues;
        leftSize.setVisible(showingSyncValues); rightSize.setVisible(showingSyncValues);
        leftFreeTime.setVisible(!showingSyncValues); rightFreeTime.setVisible(!showingSyncValues);
        leftSize.updateText(); rightSize.updateText(); leftFreeTime.updateText(); rightFreeTime.updateText();
    }
    const auto linked = pluginProcessor.parameters.getRawParameterValue(rl::params::link)->load() > 0.5f;
    rightSize.setEnabled(!linked);
    rightSize.setInterceptsMouseClicks(!linked, !linked);
    rightSize.setWantsKeyboardFocus(!linked);
    rightFreeTime.setEnabled(!linked);
    rightFreeTime.setInterceptsMouseClicks(!linked, !linked);
    rightFreeTime.setWantsKeyboardFocus(!linked);
    rightSize.setAlpha(linked ? 0.42f : 1.0f);
    rightFreeTime.setAlpha(linked ? 0.42f : 1.0f);
    rightSizeLabel.setAlpha(linked ? 0.56f : 1.0f);
    rightSizeLabel.setText(linked ? "RIGHT SIZE · LINKED" : "RIGHT SIZE", juce::dontSendNotification);
    const auto samples = pluginProcessor.getCurrentLatencySamples();
    const auto ms = samples * 1000.0 / juce::jmax(1.0, pluginProcessor.getSampleRate());
    latencyLabel.setText("LATENCY  " + juce::String(samples) + " smp  /  " + juce::String(ms, 1) + " ms",
                         juce::dontSendNotification);
}
