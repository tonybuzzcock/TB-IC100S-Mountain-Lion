// Params.h
// Parameter table shared by the VST wrapper and the editor.
//
// Order follows the IC100-S front panel: the nine knobs left to right, then
// the switch row left to right (with the owner's one-input-per-channel layout:
// a Normal/Bright rocker and an On switch for each channel in place of the
// input jacks), the mains switch, then the plugin-only controls.

#ifndef TBIC_PARAMS_H
#define TBIC_PARAMS_H

namespace tbic
{

enum Param
{
    // Knobs, left to right
    kSustain = 0, kPresence, kCh1Treble, kCh1Bass, kCh1Volume,
    kBrilliance, kCh2Treble, kCh2Bass, kCh2Volume,
    // Switch row, left to right
    kSustainOn, kCh1Bright, kCh1On, kStage, kCh2Bright, kCh2On, kMains,
    // Plugin only (bottom strip of the editor)
    kSpeaker, kInput, kOutput,
    kNumParams
};

enum Kind { kKnob, kSwitch, kBrightSw, kStageSw, kDb12, kDb24 };

const char* const kParamNames[kNumParams] = {
    "Sustain", "Presence", "1 Treble", "1 Bass", "1 Volume",
    "Brillnce", "2 Treble", "2 Bass", "2 Volume",
    "Sus On", "1 Input", "Ch 1 On", "Stu/Stg", "2 Input", "Ch 2 On", "Mains",
    "Speaker", "Input", "Output"
};

const int kParamKind[kNumParams] = {
    kKnob, kKnob, kKnob, kKnob, kKnob,
    kKnob, kKnob, kKnob, kKnob,
    kSwitch, kBrightSw, kSwitch, kStageSw, kBrightSw, kSwitch, kSwitch,
    kSwitch, kDb12, kDb24
};

const float kDefaults[kNumParams] = {
    0.5f, 0.5f, 0.5f, 0.5f, 0.5f,
    0.5f, 0.5f, 0.5f, 0.5f,
    0.0f, 0.0f, 1.0f, 1.0f, 0.0f, 0.0f, 1.0f,
    1.0f, 0.5f, 0.5f
};

const double kInputRangeDb  = 12.0;
const double kOutputRangeDb = 24.0;

inline bool isOn (float v) { return v >= 0.5f; }

} // namespace tbic

#endif
