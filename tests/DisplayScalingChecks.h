// SPDX-License-Identifier: MIT
// Copyright (c) 2026 sh4
#pragma once
#include "DisplayScaling.h"

namespace deka {
inline void CheckDisplayScaling() {
    struct ManualExample {
        int requested;
        SIZE reference, current;
        int expected;
    };

    const ManualExample manualExamples[] = {
        {1024, {3840, 2160}, {3840, 2160}, 1024}, {1024, {3840, 2160}, {1920, 1080}, 512},
        {1024, {3840, 2160}, {1280, 720}, 336},   {1024, {3840, 2160}, {2560, 1440}, 688},
        {1024, {3840, 2160}, {2048, 1536}, 544},  {1024, {3840, 2160}, {2160, 3840}, 576},
        {1024, {3840, 2160}, {7680, 4320}, 1024}, {512, {1920, 1080}, {3840, 2160}, 1024},
        {512, {1920, 1080}, {1920, 2160}, 512},   {512, {1920, 1080}, {3840, 1080}, 512},
        {512, {1920, 1080}, {2560, 1440}, 688},   {64, {3840, 2160}, {640, 480}, 64},
        {512, {3840, 2160}, {0, 0}, 512},         {512, {0, 2160}, {1920, 1080}, 512},
        {512, {3840, 2160}, {1920, -1}, 512},
    };
    for (const auto& example : manualExamples) {
        Require(ScaleIconSize(example.requested, example.reference, example.current) ==
                    example.expected,
                "Display scaling produced an unexpected icon size");
    }

    // Smallest active monitor work-area selection, independent of enumeration order.
    struct MonitorExample {
        std::vector<SIZE> displays;
        SIZE expected;
    };

    const MonitorExample monitorExamples[] = {
        {{}, {0, 0}},
        {{{0, 0}, {-1920, 1080}, {1920, 0}, {-1, -1}}, {0, 0}},
        {{{0, 0}, {3840, 2100}, {1920, -1}, {2560, 1400}, {0, 1080}}, {2560, 1400}},
        {{{3840, 2100}, {1920, 1040}}, {1920, 1040}},
        {{{2560, 1440}, {1080, 1920}}, {1080, 1920}},
        {{{1080, 1920}, {2560, 1440}}, {1080, 1920}},
        {{{1920, 1080}, {1440, 2560}}, {1920, 1080}},
        {{{1920, 1080}, {1080, 1920}}, {1080, 1920}},
        {{{1080, 1920}, {1920, 1080}}, {1080, 1920}},
        {{{1600, 900}, {1200, 1200}}, {1600, 900}},
        {{{1200, 1200}, {1600, 900}}, {1600, 900}},
        {{{60000, 40000}, {50000, 40000}}, {50000, 40000}},
    };
    for (const auto& example : monitorExamples) {
        SIZE chosen = ChooseSmallestDisplaySize(example.displays);
        Require(chosen.cx == example.expected.cx && chosen.cy == example.expected.cy,
                "Smallest monitor selection produced an unexpected size");
    }

    // Equal-area landscape/portrait tie-breaker picks the tighter size so icons fit both.
    int equalAreaSize = CalculateAutomaticIconSize(
        ChooseSmallestDisplaySize(std::vector<SIZE>{{1920, 1080}, {1080, 1920}}));
    Require(equalAreaSize == 880, "Equal-area tie-breaker automatic size mismatch");
    Require(MeasureDesktopGridCapacity(SIZE{1080, 1920}, equalAreaSize).total == 2 &&
                MeasureDesktopGridCapacity(SIZE{1920, 1080}, equalAreaSize).total == 2,
            "Equal-area automatic size must fit 2 icons on both orientations");

    // Automatic sizing: 16px steps, [64, 4096], targeting 2-3 grid capacity.
    struct AutomaticExample {
        SIZE display;
        int expectedSize;
        int expectedCapacity;
        bool checkMaximality;
    };

    const AutomaticExample automaticExamples[] = {
        {{1280, 720}, 576, 2, true},     {{1366, 768}, 624, 2, true},
        {{1920, 1080}, 896, 2, true},    {{1920, 1040}, 896, 2, true},
        {{2560, 1440}, 1216, 2, true},   {{2560, 1400}, 1216, 2, true},
        {{3840, 2160}, 1856, 2, true},   {{7680, 4320}, 3776, 2, true},
        {{1080, 1920}, 880, 2, true},    {{1440, 2560}, 1200, 2, true},
        {{2160, 3840}, 1840, 2, true},   {{3440, 1440}, 1344, 2, true},
        {{5120, 1440}, 1344, 3, true},   {{1440, 5120}, 1376, 3, true},
        {{256, 200}, 64, 2, true},       {{200, 150}, 64, 0, false},
        {{160, 120}, 64, 0, false},      {{9600, 5400}, 4096, 2, false},
        {{16000, 9000}, 4096, 6, false}, {{0, 0}, 64, 0, false},
        {{-1920, 1080}, 64, 0, false},   {{1920, 0}, 64, 0, false},
    };
    for (const auto& example : automaticExamples) {
        int size = CalculateAutomaticIconSize(example.display);
        Require(size == example.expectedSize, "Automatic icon sizing produced an unexpected size");
        Require(size >= MinIconSize && size <= MaxAutomaticIconSize && size % IconSizeStep == 0,
                "Automatic icon size violated step or min/max bounds");
        auto capacity = MeasureDesktopGridCapacity(example.display, size);
        Require(capacity.total == example.expectedCapacity,
                "Automatic icon size produced an unexpected grid capacity");
        if (example.checkMaximality) {
            Require(capacity.total >= 2 && capacity.total <= 3,
                    "Automatic icon size did not target 2-3 grid capacity");
            Require(MeasureDesktopGridCapacity(example.display, size + IconSizeStep).total < 2,
                    "Automatic icon size did not choose the largest 16px step");
        }
    }
}
} // namespace deka
