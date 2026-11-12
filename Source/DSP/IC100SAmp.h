// IC100SAmp.h
// TB IC100-S: model of the H/H IC100-S guitar amplifier (drawing "IC100-S
// Circuit Diagram", power amp board AO3103/7), the whole amplifier, one mono path.
//
//   guitar -+-> Ch 1: Normal/Bright input -> 741 (x18) -+-> clean -----------+
//           |                                           +-> Sustain 741 -----+- S1 -> tone stack
//           |         -> Volume -> 741 with Presence (T-network feedback) -> 150K --+
//           +-> Ch 2: Normal/Bright input -> 741 (x18) -> tone stack -> Volume     |
//                     -> 741 with Brilliance (T-network feedback) -> 150K ----------+
//   mixer 741 (3K9 + 33K, Studio/Stage shorts the 33K) -> 470n
//   -> AO3103/7 100 W power amp (+/-47 V) -> 8 ohm 2x12 (two 1977 H/H 12")
//
// Every op-amp stage and passive network is modelled from its component values
// (the Netlist class, 4x oversampled). The power amp is modelled from its
// behaviour (gain, rails that sag, slew, current limit) driving the electrical
// loudspeaker load; the cabinet's acoustic voice is a fixed filter set.
//
// Calibration: 1.0 at the input is 1 V peak from the guitar. Output 1.0 is
// 40 V peak at the speaker terminals (100 W into 8 ohms); with Speaker on, the
// cab-miked signal is scaled to match at 1 kHz.
// Plain C++98, no allocation or locks in process(). One instance per channel.

#ifndef TBIC_IC100SAMP_H
#define TBIC_IC100SAMP_H

#include "Netlist.h"
#include "Oversampler4x.h"
#include "Biquad.h"

namespace hhic
{

class IC100SAmp
{
public:
    IC100SAmp();

    void prepare (double sampleRate);
    void reset();

    // Front panel, left to right. Knobs are 0..1 (0 to 10 on the dial).
    // Changes are smoothed.
    void setSustain (double v);
    void setPresence (double v);
    void setCh1Treble (double v);
    void setCh1Bass (double v);
    void setCh1Volume (double v);
    void setBrilliance (double v);
    void setCh2Treble (double v);
    void setCh2Bass (double v);
    void setCh2Volume (double v);

    void setSustainOn (bool on);        // S1 (or the footswitch)
    void setCh1Bright (bool bright);    // which Ch 1 input jack the guitar is in
    void setStage (bool stage);         // Studio / Stage switch: Stage = 33K in circuit
    void setCh2Bright (bool bright);
    void setCh1On (bool on);            // plugin: feed Ch 1
    void setCh2On (bool on);            // plugin: feed Ch 2
    void setPower (bool on);            // mains switch: off = the dry signal passes

    // Plugin-only controls
    void setSpeaker (bool on);
    void setInputGainDb (double dB);
    void setOutputGainDb (double dB);

    void process (const float* in, float* out, int numSamples);

    static int latencySamples() { return Oversampler4x::latencySamples(); }

    // For tests
    double railPositive() const { return railPos; }
    double railNegative() const { return railNeg; }
    double peakSpeakerAmps() const { return peakAmps; }
    void   clearPeakAmps() { peakAmps = 0.0; }
    bool   currentLimitHit() const { return climHit; }
    bool   circuitsBuilt() const { return built; }

    // Probe: process one oversampled sample of the preamp alone and return
    // (Ch 1 output, Ch 2 output, mixer output). Used by the small-signal tests.
    void   probePreamp (double vIn, double& ch1, double& ch2, double& mix);
    void   resetPreamp();
    double oversampledRate() const { return fsOs; }

    static double logTaper (double x);

private:
    enum Knob { kSus, kPres, kT1, kB1, kV1, kBril, kT2, kB2, kV2, kNumKnobs };

    void buildCircuits();
    void applyPots();
    void applySwitches();
    double processCh1 (double g);
    double processCh2 (double g);
    double mixer (double v1, double v2);
    double powerAmp (double vIn);

    double fsBase, fsOs;
    bool built;

    // Circuits
    Netlist in1, sus, tone1, out1, in2, tone2, out2, mix, speaker;
    int in1Out, susOut, tone1Out, out1Out, in2Out, tone2Out, out2Out, mixOut, spkOut, spkCone;
    int eN1, eB1, eN2, eB2;                 // Normal / Bright input paths (2K2 or open)
    int eSusPot;                            // Sustain 1K lin (680n end to wiper)
    int eSelClean, eSelSus;                 // S1: clean or Sustain to the tone stack
    int eBass1Top, eBass1Bot, eTreb1Top, eTreb1Bot, eVol1Top, eVol1Bot, ePres;
    int eBass2Top, eBass2Bot, eTreb2Top, eTreb2Bot, eVol2Top, eVol2Bot, eBril;
    int eStudio;                            // Studio/Stage contacts across the mixer's 33K

    // Power amp
    Biquad paInHP, paGbwLP;
    double railPos, railNeg, vOutPrev, peakAmps, slewStep;
    bool climHit;

    Oversampler4x osMain, osSpkDown;

    // Speaker acoustics
    double coneVPrev;
    Biquad cab[6];
    double cabNorm;
    double spkMix;       // 0 = speaker terminals, 1 = miked speaker (crossfaded)

    // Dry path for the mains switch (delayed by the oversampler latency)
    enum { kDryLen = 64 };
    double dry[kDryLen];
    int dryPos;
    double powerMix;     // 1 = amp, 0 = dry

    // Controls
    double knobTarget[kNumKnobs], knobCur[kNumKnobs];
    bool sustainOn, bright1, bright2, stage, ch1On, ch2On, powerOn, speakerOn;
    bool switchesApplied;
    bool appSus, appB1, appB2, appStage;
    double ch1Gain, ch2Gain;           // smoothed channel feeds (click-free on/off)
    bool ch1Running, ch2Running;
    double inGainT, inGainC, outGainT, outGainC;
    double smoothCoeff;
    int potCountdown;
};

} // namespace hhic

#endif
