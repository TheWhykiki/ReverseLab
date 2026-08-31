#include <juce_core/juce_core.h>
#include "ReverseEngine.h"
#include <cmath>

class ReverseEngineTests final : public juce::UnitTest
{
public:
    ReverseEngineTests() : UnitTest("ReverseEngine", "DSP") {}

    void runTest() override
    {
        beginTest("Reverse chunks remain finite at maximum feedback");
        rl::ReverseEngine engine;
        engine.prepare(48000.0, 48000);
        rl::EngineSettings settings;
        settings.leftLength = settings.rightLength = 64;
        settings.feedback = 0.95f;
        settings.crossfade = 0.04f;
        for (int i = 0; i < 100000; ++i)
        {
            const auto input = i == 0 ? 1.0f : 0.0f;
            const auto left = engine.processSample(0, input, settings);
            const auto right = engine.processSample(1, input, settings);
            expect(std::isfinite(left) && std::isfinite(right));
            expectWithinAbsoluteError(left, juce::jlimit(-4.0f, 4.0f, left), 0.0001f);
            engine.advance();
        }

        beginTest("A captured ramp is read in descending order");
        engine.reset();
        settings.feedback = 0.0f;
        settings.crossfade = 0.0f;
        settings.speed = 1.0f;
        std::array<float, 192> output {};
        for (int i = 0; i < 192; ++i)
        {
            output[(size_t) i] = engine.processSample(0, static_cast<float>(i) / 192.0f, settings);
            (void) engine.processSample(1, 0.0f, settings);
            engine.advance();
        }
        int descending = 0;
        for (int i = 130; i < 190; ++i)
            if (output[(size_t) i] < output[(size_t) (i - 1)]) ++descending;
        expect(descending > 50, "Most samples in a complete reverse segment should descend");

        beginTest("Freeze stops replacing the captured material");
        engine.reset();
        settings.freeze = false;
        for (int i = 0; i < 128; ++i)
        {
            (void) engine.processSample(0, 0.25f, settings);
            (void) engine.processSample(1, 0.25f, settings);
            engine.advance();
        }
        settings.freeze = true;
        float energy = 0.0f;
        for (int i = 0; i < 256; ++i)
        {
            energy += std::abs(engine.processSample(0, 0.0f, settings));
            (void) engine.processSample(1, 0.0f, settings);
            engine.advance();
        }
        expect(energy > 1.0f);

        beginTest("Seeded randomisation is deterministic");
        auto render = [](uint32_t seed)
        {
            rl::ReverseEngine e; e.prepare(48000.0, 8192); e.setSeed(seed);
            rl::EngineSettings s; s.leftLength = s.rightLength = 80; s.randomAmount = 0.8f;
            std::array<float, 400> result {};
            for (int i = 0; i < 400; ++i)
            {
                result[(size_t) i] = e.processSample(0, std::sin(i * 0.1f), s);
                (void) e.processSample(1, 0.0f, s); e.advance();
            }
            return result;
        };
        const auto a = render(12345), b = render(12345), c = render(54321);
        expect(a == b);
        expect(a != c);
    }
};

static ReverseEngineTests reverseEngineTests;

int main()
{
    juce::UnitTestRunner runner;
    runner.runAllTests();
    int failures = 0;
    for (int i = 0; i < runner.getNumResults(); ++i)
        if (auto* result = runner.getResult(i)) failures += result->failures;
    return failures == 0 ? 0 : 1;
}
