// IC100SAmp.cpp
// Component values are from the H/H drawing "IC100-S Circuit Diagram" (issue 5,
// redrawn), including the AO3103/7 power amp board drawn on the same sheet.
// The preamp parts have no reference numbers on the drawing; comments name
// them by value and position instead.

#include "IC100SAmp.h"

#include <cmath>
#include <cstring>

namespace hhic
{

namespace
{
    const int kPotUpdateInterval = 16;

    // 741 op-amps on the +/-16 V rails (PL16Z zeners fed through 1K5 from +/-47 V)
    const double kGbw741   = 1.0e6;    // Hz
    const double kSlew741  = 0.5;      // V/us
    const double kSwing741 = 14.0;     // V
    const double kKnee741  = 0.8;      // V
    const double kRout741  = 75.0;     // ohms, open-loop output resistance

    const double kPickupR  = 6000.0;   // guitar pickup DC resistance (guitar volume on full)
    const double kPotMin   = 10.0;     // wiper contact / end resistance
    const double kOpen     = 1.0e9;    // a switch contact that is open
    const double kClosed   = 0.05;     // a switch contact that is closed

    // Power amp (AO3103/7). The board's feedback network is hard to read on the
    // scan, so its closed-loop gain is an estimate (see the README).
    const double kPaGain     = 22.0;       // estimate: about 1 V rms at Power Amp In for full power
    const double kRailNoLoad = 48.0;       // V: 34-0-34 V winding, KBH06 bridge; "+/-47 V" on the drawing
    const double kRailC      = 5000.0e-6;  // reservoirs, 5000u 50 V each
    const double kRailRs     = 2.5;        // transformer + bridge source resistance (estimate)
    const double kRailDrop   = 3.0;        // V lost in TIP29C/30C + 2N3773 and the 0R33 emitter resistors
    const double kRailKnee   = 1.5;        // V
    const double kPaSlew     = 2.0;        // V/us at the output (estimate)
    const double kIMax       = 8.0;        // A, BC207B/BC204B V-I limiters, roughly (estimate)
    const double kIQuiet     = 0.05;       // A per rail at idle

    const double kVFullScale = 40.0;       // V peak at the speaker = 1.0 out

    inline double clamp01 (double x) { return x < 0.0 ? 0.0 : (x > 1.0 ? 1.0 : x); }
    inline double dbToGain (double dB) { return std::pow (10.0, dB / 20.0); }
    inline double potR (double r) { return r < kPotMin ? kPotMin : r; }
}

// Log ("audio") taper: 10% of the track at mid-rotation.
double IC100SAmp::logTaper (double x)
{
    const double k = 4.394449154672439;
    return (std::exp (k * x) - 1.0) / (std::exp (k) - 1.0);
}

IC100SAmp::IC100SAmp()
    : fsBase (44100.0), fsOs (176400.0), built (false),
      railPos (kRailNoLoad), railNeg (kRailNoLoad), vOutPrev (0.0), peakAmps (0.0),
      slewStep (1.0), climHit (false),
      coneVPrev (0.0), cabNorm (1.0), spkMix (1.0),
      dryPos (0), powerMix (1.0),
      sustainOn (false), bright1 (false), bright2 (false), stage (true),
      ch1On (true), ch2On (false), powerOn (true), speakerOn (true),
      switchesApplied (false), appSus (false), appB1 (false), appB2 (false), appStage (true),
      ch1Gain (1.0), ch2Gain (0.0), ch1Running (true), ch2Running (false),
      inGainT (1.0), inGainC (1.0), outGainT (1.0), outGainC (1.0),
      smoothCoeff (0.001), potCountdown (0)
{
    const double defaults[kNumKnobs] = { 0.5, 0.5, 0.5, 0.5, 0.5, 0.5, 0.5, 0.5, 0.5 };
    for (int i = 0; i < kNumKnobs; ++i)
        knobTarget[i] = knobCur[i] = defaults[i];
    std::memset (dry, 0, sizeof (dry));
    buildCircuits();
}

//==============================================================================
// The circuits

void IC100SAmp::buildCircuits()
{
    // ---- Input stage, either channel: Normal (2K2 + 4u7) and Bright (2K2 + 4n7)
    // jacks into a non-inverting 741, 47K over 2K7 (x18.4), 100K bias to ground.
    // The guitar is in one jack; the other path is left open.
    for (int ch = 0; ch < 2; ++ch)
    {
        Netlist& n = ch == 0 ? in1 : in2;
        n.clear();
        const int s = n.addNode(), a = n.addNode(), b = n.addNode(), p = n.addNode(),
                  m = n.addNode(), o = n.addNode();
        n.addInput (s, kPickupR);
        const int eN = n.addR (s, a, 2200.0);     // Normal 2K2
        n.addC (a, p, 4.7e-6);                    // 4u7
        const int eB = n.addR (s, b, kOpen);      // Bright 2K2
        n.addC (b, p, 4.7e-9);                    // 4n7
        n.addR (p, 0, 100000.0);                  // 100K
        n.addR (o, m, 47000.0);                   // 47K feedback
        n.addR (m, 0, 2700.0);                    // 2K7
        n.setOpAmp (p, m, o, kGbw741, kSlew741, kSwing741, kKnee741);
        if (ch == 0) { in1Out = o; eN1 = eN; eB1 = eB; }
        else         { in2Out = o; eN2 = eN; eB2 = eB; }
    }

    // ---- Ch 1 Sustain: 741 fed from the input stage through 4K7 (47n to ground),
    // 150K feedback, 68R + 680n + the Sustain pot (1K lin, wiper to ground) to
    // ground. It clips against its +/-16 V rails.
    {
        Netlist& n = sus; n.clear();
        const int s = n.addNode(), p = n.addNode(), m = n.addNode(), x = n.addNode(),
                  y = n.addNode(), o = n.addNode();
        n.addInput (s, kRout741);
        n.addR (s, p, 4700.0);                    // 4K7
        n.addC (p, 0, 47.0e-9);                   // 47n
        n.addR (o, m, 150000.0);                  // 150K
        n.addR (m, x, 68.0);                      // 68R
        n.addC (x, y, 680.0e-9);                  // 680n
        eSusPot = n.addR (y, 0, 500.0);           // Sustain 1K lin, 680n end to wiper
        n.setOpAmp (p, m, o, kGbw741, kSlew741, kSwing741, kKnee741);
        susOut = o;
    }

    // ---- Ch 1 selector, tone stack and Volume (passive). Clean: input stage
    // via 6u8. Sustain: 22K, 6u8, 100n to ground. S1 (or the footswitch) picks
    // one; then 47n to ground, 100R into the stack, whose top has 4K7 || 10n to
    // ground. Bass: 6K8, 25K log (22n top-to-wiper, 220n wiper-to-bottom), 1K
    // to ground; its wiper through 2K2 to the Treble wiper. Treble: 25K log
    // fed by 10n from the stack top, 220n to ground. Volume 25K log with a 10n
    // bright cap top-to-wiper, then 6u8 into the next 741 (100K to ground).
    {
        Netlist& n = tone1; n.clear();
        const int c = n.addNode(), s1 = n.addNode(), s = n.addNode(), f = n.addNode(),
                  T = n.addNode(), a = n.addNode(), w = n.addNode(), b = n.addNode(),
                  tt = n.addNode(), m = n.addNode(), tb = n.addNode(), vw = n.addNode(),
                  p = n.addNode(), ci = n.addNode(), si = n.addNode();
        n.addInput (ci, kRout741);                // clean, from the input stage
        n.addInput (si, kRout741);                // from the Sustain 741
        n.addC (ci, c, 6.8e-6);                   // 6u8
        n.addR (si, s1, 22000.0);                 // 22K
        n.addC (s1, s, 6.8e-6);                   // 6u8
        n.addC (s, 0, 100.0e-9);                  // 100n
        eSelClean = n.addR (c, f, kClosed);       // S1 off: clean
        eSelSus   = n.addR (s, f, kOpen);         // S1 on: Sustain
        n.addC (f, 0, 47.0e-9);                   // 47n
        n.addR (f, T, 100.0);                     // 100R
        n.addR (T, 0, 4700.0);                    // 4K7
        n.addC (T, 0, 10.0e-9);                   // 10n
        n.addR (T, a, 6800.0);                    // 6K8
        eBass1Top = n.addR (a, w, 12500.0);       // Bass 25K log
        eBass1Bot = n.addR (w, b, 12500.0);
        n.addC (a, w, 22.0e-9);                   // 22n
        n.addC (w, b, 220.0e-9);                  // 220n
        n.addR (b, 0, 1000.0);                    // 1K
        n.addR (w, m, 2200.0);                    // 2K2
        n.addC (T, tt, 10.0e-9);                  // 10n
        eTreb1Top = n.addR (tt, m, 12500.0);      // Treble 25K log
        eTreb1Bot = n.addR (m, tb, 12500.0);
        n.addC (tb, 0, 220.0e-9);                 // 220n
        eVol1Top = n.addR (m, vw, 12500.0);       // Volume 25K log
        eVol1Bot = n.addR (vw, 0, 12500.0);
        n.addC (m, vw, 10.0e-9);                  // 10n bright cap
        n.addC (vw, p, 6.8e-6);                   // 6u8
        n.addR (p, 0, 100000.0);                  // 100K
        tone1Out = p;
    }

    // ---- Ch 1 output stage: non-inverting 741, 6K8 + 4u7 to ground, feedback
    // a T of 47K + 47K with 33p across, its middle to ground through 100n,
    // 3K9 and the Presence pot (25K A/log, wired as a variable resistor).
    // ---- Ch 2 output stage: the same with 47n and Brilliance.
    for (int ch = 0; ch < 2; ++ch)
    {
        Netlist& n = ch == 0 ? out1 : out2;
        n.clear();
        const int p = n.addNode(), m = n.addNode(), x = n.addNode(), X = n.addNode(),
                  y1 = n.addNode(), y2 = n.addNode(), o = n.addNode();
        n.addInput (p, 1.0);                      // the + input follows the Volume network
        n.addR (m, x, 6800.0);                    // 6K8
        n.addC (x, 0, 4.7e-6);                    // 4u7
        n.addR (m, X, 47000.0);                   // 47K
        n.addR (X, o, 47000.0);                   // 47K
        n.addC (m, o, 33.0e-12);                  // 33p
        n.addC (X, y1, ch == 0 ? 100.0e-9 : 47.0e-9);   // 100n (Ch 1) / 47n (Ch 2)
        n.addR (y1, y2, 3900.0);                  // 3K9
        const int ep = n.addR (y2, 0, 25000.0);   // Presence / Brilliance 25K A/log
        n.setOpAmp (p, m, o, kGbw741, kSlew741, kSwing741, kKnee741);
        if (ch == 0) { out1Out = o; ePres = ep; }
        else         { out2Out = o; eBril = ep; }
    }

    // ---- Ch 2 tone stack and Volume: the input stage's 6u8 straight into the
    // same stack (no Sustain, no 4K7/10n at the top), Volume with no bright cap.
    {
        Netlist& n = tone2; n.clear();
        const int ci = n.addNode(), T = n.addNode(), a = n.addNode(), w = n.addNode(),
                  b = n.addNode(), tt = n.addNode(), m = n.addNode(), tb = n.addNode(),
                  vw = n.addNode(), p = n.addNode();
        n.addInput (ci, kRout741);
        n.addC (ci, T, 6.8e-6);                   // 6u8
        n.addR (T, a, 6800.0);                    // 6K8
        eBass2Top = n.addR (a, w, 12500.0);       // Bass 25K log
        eBass2Bot = n.addR (w, b, 12500.0);
        n.addC (a, w, 22.0e-9);                   // 22n
        n.addC (w, b, 220.0e-9);                  // 220n
        n.addR (b, 0, 1000.0);                    // 1K
        n.addR (w, m, 2200.0);                    // 2K2
        n.addC (T, tt, 10.0e-9);                  // 10n
        eTreb2Top = n.addR (tt, m, 12500.0);      // Treble 25K log
        eTreb2Bot = n.addR (m, tb, 12500.0);
        n.addC (tb, 0, 220.0e-9);                 // 220n
        eVol2Top = n.addR (m, vw, 12500.0);       // Volume 25K log
        eVol2Bot = n.addR (vw, 0, 12500.0);
        n.addC (vw, p, 6.8e-6);                   // 6u8
        n.addR (p, 0, 100000.0);                  // 100K
        tone2Out = p;
    }

    // ---- Mixer: inverting 741, both channels in through 150K, + to ground via
    // 100R, feedback 3K9 + 33K; the Studio/Stage switch shorts the 33K on Studio.
    // Output through 470n to the Power Amp In jack (C1 0.22u, R1 1K, R2 15K on
    // the power amp board: see paInHP).
    {
        Netlist& n = mix; n.clear();
        const int m = n.addNode(), q = n.addNode(), o = n.addNode();
        n.addInput (m, 150000.0);                 // Ch 1 150K
        n.addInput (m, 150000.0);                 // Ch 2 150K
        n.addR (m, q, 3900.0);                    // 3K9
        n.addR (q, o, 33000.0);                   // 33K
        eStudio = n.addR (q, o, kOpen);           // Studio contacts
        n.setOpAmp (0, m, o, kGbw741, kSlew741, kSwing741, kKnee741);
        mixOut = o;
    }

    // ---- Output network and loudspeaker load ----
    // L1 (value not marked) with R27 15R across it, Zobel R31 15R + C11 0.047u.
    // Two 1977 H/H 12" drivers wired as an 8 ohm pair (the owner's 2x12), the
    // same electrical model as TB VS Musician: Re, lossy voice-coil inductance,
    // and the motional impedance as a parallel R-L-C at the in-cab resonance.
    {
        Netlist& n = speaker; n.clear();
        const int a = n.addNode(), o = n.addNode(), z = n.addNode(), p = n.addNode(), q = n.addNode();
        n.addInput (a, 0.01);                     // power amp output (feedback holds this node)
        n.addL (a, o, 3.0e-6);                    // L1 (typical value)
        n.addR (a, o, 15.0);                      // R27
        n.addR (o, z, 15.0);                      // R31 Zobel
        n.addC (z, 0, 0.047e-6);                  // C11
        const double re = 6.4, fs = 90.0, qms = 3.5, qes = 0.55;
        const double ws = 2.0 * kPi * fs;
        const double cmes = qes / (ws * re);
        const double lces = 1.0 / (ws * ws * cmes);
        const double res = re * qms / qes;
        n.addR (o, p, re);                        // voice coil resistance
        n.addL (p, q, 0.5e-3);                    // voice coil inductance
        n.addR (p, q, 20.0);                      // eddy-current losses
        n.addR (q, 0, res);                       // motional: mechanical losses
        n.addL (q, 0, lces);                      //           suspension compliance
        n.addC (q, 0, cmes);                      //           moving mass
        spkOut = o;
        spkCone = q;
    }

    // Every node must have been created (addNode returns 0 when a netlist is full).
    built = tone1Out > 0 && tone2Out > 0 && out1Out > 0 && mixOut > 0 && spkCone > 0
         && eStudio >= 0 && eVol1Bot >= 0 && eVol2Bot >= 0 && eSusPot >= 0;
}

//==============================================================================
void IC100SAmp::prepare (double sr)
{
    fsBase = sr;
    fsOs = sr * Oversampler4x::kFactor;
    smoothCoeff = 1.0 - std::exp (-1.0 / (0.02 * sr));

    in1.prepare (fsOs); in2.prepare (fsOs); sus.prepare (fsOs);
    tone1.prepare (fsOs); tone2.prepare (fsOs);
    out1.prepare (fsOs); out2.prepare (fsOs);
    mix.prepare (fsOs); speaker.prepare (fsOs);

    // Power amp input: the mixer's 470n in series with C1 0.22u into R1 1K + R2 15K;
    // the 741's gain-bandwidth over the closed-loop gain.
    {
        const double cs = 1.0 / (1.0 / 470.0e-9 + 1.0 / 0.22e-6);
        paInHP.setHighPass1 (fsOs, 1.0 / (2.0 * kPi * cs * 16000.0));
    }
    paGbwLP.setLowPass1 (fsOs, kGbw741 / kPaGain * 1.5);
    slewStep = kPaSlew * 1.0e6 / fsOs;

    // Loudspeaker cone breakup and cabinet (base rate, after the physical low
    // end): the same 2x12 voicing as TB VS Musician.
    cab[0].setPeak (fsBase, 180.0, 1.0, 2.0);
    cab[1].setPeak (fsBase, 1500.0, 1.0, 2.5);
    cab[2].setPeak (fsBase, 2700.0, 1.1, 8.0);
    cab[3].setPeak (fsBase, 3800.0, 1.8, 5.0);
    const double lpF = fsBase * 0.45 < 5000.0 ? fsBase * 0.45 : 5000.0;
    cab[4].setLowPass (fsBase, lpF, 0.7);
    cab[5].setLowPass (fsBase, lpF * 1.15, 1.0);

    // Normalise the miked speaker so 1 kHz matches the speaker-terminal level
    // (a lower tone at very low host rates, where 1 kHz is above Nyquist).
    {
        const double fNorm = fsBase * 0.2 < 1000.0 ? fsBase * 0.2 : 1000.0;
        speaker.reset();
        for (int i = 0; i < 6; ++i) cab[i].reset();
        double prev = 0.0, peak = 0.0;
        const int nBase = (int) (0.12 * fsBase);
        const int F = Oversampler4x::kFactor;
        for (int i = 0; i < nBase; ++i)
        {
            double last = 0.0;
            for (int k = 0; k < F; ++k)
            {
                const double tt = (double) (i * F + k) / fsOs;
                const double vin = std::sin (2.0 * kPi * fNorm * tt);
                speaker.process (&vin);
                const double vc = speaker.v (spkCone);
                last = (vc - prev) * fsOs;
                prev = vc;
            }
            double y = last;
            for (int b = 0; b < 6; ++b) y = cab[b].process (y);
            if (i > nBase / 2 && std::fabs (y) > peak) peak = std::fabs (y);
        }
        cabNorm = peak > 0.0 ? 1.0 / peak : 1.0;
    }

    reset();
}

void IC100SAmp::reset()
{
    for (int i = 0; i < kNumKnobs; ++i) knobCur[i] = knobTarget[i];
    inGainC = inGainT;
    outGainC = outGainT;
    applySwitches();
    applyPots();

    resetPreamp();
    speaker.reset();
    osMain.reset(); osSpkDown.reset();
    spkMix = speakerOn ? 1.0 : 0.0;
    powerMix = powerOn ? 1.0 : 0.0;
    ch1Gain = ch1On ? 1.0 : 0.0;
    ch2Gain = ch2On ? 1.0 : 0.0;
    ch1Running = ch1On;
    ch2Running = ch2On;
    std::memset (dry, 0, sizeof (dry));
    dryPos = 0;

    paInHP.reset(); paGbwLP.reset();
    for (int i = 0; i < 6; ++i) cab[i].reset();

    railPos = railNeg = kRailNoLoad;
    vOutPrev = 0.0; coneVPrev = 0.0;
    climHit = false;
    potCountdown = 0;
}

void IC100SAmp::resetPreamp()
{
    in1.reset(); in2.reset(); sus.reset();
    tone1.reset(); tone2.reset(); out1.reset(); out2.reset(); mix.reset();
}

//==============================================================================
// Pots and switches

void IC100SAmp::applyPots()
{
    const double R25 = 25000.0;

    // Sustain: 1K linear between the 680n and the grounded wiper; clockwise
    // shortens it (more gain into the clipping 741).
    sus.setR (eSusPot, potR (1000.0 * (1.0 - knobCur[kSus])));

    // Tone stacks. Log tracks measured from the counter-clockwise end: Bass and
    // Treble from their bottom (220n) ends, Volume from ground.
    for (int ch = 0; ch < 2; ++ch)
    {
        Netlist& n = ch == 0 ? tone1 : tone2;
        const double bass = knobCur[ch == 0 ? kB1 : kB2];
        const double treb = knobCur[ch == 0 ? kT1 : kT2];
        const double vol  = knobCur[ch == 0 ? kV1 : kV2];
        const double rb = R25 * logTaper (bass), rt = R25 * logTaper (treb), rv = R25 * logTaper (vol);
        n.setR (ch == 0 ? eBass1Bot : eBass2Bot, potR (rb));
        n.setR (ch == 0 ? eBass1Top : eBass2Top, potR (R25 - rb));
        n.setR (ch == 0 ? eTreb1Bot : eTreb2Bot, potR (rt));
        n.setR (ch == 0 ? eTreb1Top : eTreb2Top, potR (R25 - rt));
        n.setR (ch == 0 ? eVol1Bot : eVol2Bot, potR (rv));
        n.setR (ch == 0 ? eVol1Top : eVol2Top, potR (R25 - rv));
    }

    // Presence / Brilliance: 25K "A" (log) pot used as a variable resistor between
    // its wiper and clockwise end, so clockwise takes resistance out (more treble
    // lift), with most of the lift in the top half of the dial.
    out1.setR (ePres, potR (R25 * (1.0 - logTaper (knobCur[kPres]))));
    out2.setR (eBril, potR (R25 * (1.0 - logTaper (knobCur[kBril]))));

    sus.rebuild(); tone1.rebuild(); tone2.rebuild(); out1.rebuild(); out2.rebuild();
}

void IC100SAmp::applySwitches()
{
    in1.setR (eN1, bright1 ? kOpen : 2200.0);
    in1.setR (eB1, bright1 ? 2200.0 : kOpen);
    in2.setR (eN2, bright2 ? kOpen : 2200.0);
    in2.setR (eB2, bright2 ? 2200.0 : kOpen);
    tone1.setR (eSelClean, sustainOn ? kOpen : kClosed);
    tone1.setR (eSelSus, sustainOn ? kClosed : kOpen);
    mix.setR (eStudio, stage ? kOpen : kClosed);
    in1.rebuild(); in2.rebuild(); tone1.rebuild(); mix.rebuild();
    appSus = sustainOn; appB1 = bright1; appB2 = bright2; appStage = stage;
    switchesApplied = true;
}

//==============================================================================
// Setters

void IC100SAmp::setSustain (double v)      { knobTarget[kSus]  = clamp01 (v); }
void IC100SAmp::setPresence (double v)     { knobTarget[kPres] = clamp01 (v); }
void IC100SAmp::setCh1Treble (double v)    { knobTarget[kT1]   = clamp01 (v); }
void IC100SAmp::setCh1Bass (double v)      { knobTarget[kB1]   = clamp01 (v); }
void IC100SAmp::setCh1Volume (double v)    { knobTarget[kV1]   = clamp01 (v); }
void IC100SAmp::setBrilliance (double v)   { knobTarget[kBril] = clamp01 (v); }
void IC100SAmp::setCh2Treble (double v)    { knobTarget[kT2]   = clamp01 (v); }
void IC100SAmp::setCh2Bass (double v)      { knobTarget[kB2]   = clamp01 (v); }
void IC100SAmp::setCh2Volume (double v)    { knobTarget[kV2]   = clamp01 (v); }
void IC100SAmp::setSustainOn (bool on)     { sustainOn = on; }
void IC100SAmp::setCh1Bright (bool b)      { bright1 = b; }
void IC100SAmp::setStage (bool s)          { stage = s; }
void IC100SAmp::setCh2Bright (bool b)      { bright2 = b; }
void IC100SAmp::setCh1On (bool on)         { ch1On = on; }
void IC100SAmp::setCh2On (bool on)         { ch2On = on; }
void IC100SAmp::setPower (bool on)         { powerOn = on; }
void IC100SAmp::setSpeaker (bool on)       { speakerOn = on; }
void IC100SAmp::setInputGainDb (double dB) { inGainT = dbToGain (dB); }
void IC100SAmp::setOutputGainDb (double dB){ outGainT = dbToGain (dB); }

//==============================================================================
// Signal path

double IC100SAmp::processCh1 (double g)
{
    in1.process (&g);
    const double v = in1.v (in1Out);
    double s = 0.0;
    if (sustainOn)
    {
        sus.process (&v);
        s = sus.v (susOut);
    }
    const double ins[2] = { v, s };
    tone1.process (ins);
    const double t = tone1.v (tone1Out);
    out1.process (&t);
    return out1.v (out1Out);
}

double IC100SAmp::processCh2 (double g)
{
    in2.process (&g);
    const double v = in2.v (in2Out);
    tone2.process (&v);
    const double t = tone2.v (tone2Out);
    out2.process (&t);
    return out2.v (out2Out);
}

double IC100SAmp::mixer (double v1, double v2)
{
    const double ins[2] = { v1, v2 };
    mix.process (ins);
    return mix.v (mixOut);
}

void IC100SAmp::probePreamp (double vIn, double& c1, double& c2, double& m)
{
    c1 = processCh1 (vIn);
    c2 = processCh2 (vIn);
    m = mixer (c1, c2);
}

double IC100SAmp::powerAmp (double vIn)
{
    // Closed-loop gain with the input coupling high-pass and the 741's bandwidth.
    double v = paGbwLP.process (paInHP.process (vIn) * kPaGain);

    // Slew rate
    double d = v - vOutPrev;
    if (d > slewStep) d = slewStep; else if (d < -slewStep) d = -slewStep;
    v = vOutPrev + d;

    // Clipping against the sagging rails
    const double lim = (v >= 0.0 ? railPos : railNeg) - kRailDrop;
    const double lin = lim - kRailKnee;
    const double av = std::fabs (v);
    if (av > lin)
    {
        const double y = lin + kRailKnee * (1.0 - std::exp (-(av - lin) / kRailKnee));
        v = v < 0.0 ? -y : y;
    }

    // V-I current limiting into the actual speaker load
    double i0, gIn;
    speaker.inputCurrentTerms (i0, gIn);
    double i = i0 + gIn * v;
    climHit = false;
    if (i > kIMax)       { v = (kIMax - i0) / gIn;  i = kIMax;  climHit = true; }
    else if (i < -kIMax) { v = (-kIMax - i0) / gIn; i = -kIMax; climHit = true; }

    speaker.process (&v);
    vOutPrev = v;
    if (std::fabs (i) > peakAmps) peakAmps = std::fabs (i);

    // Reservoir capacitors: each rail supplies its half of the load and is
    // recharged through the transformer and bridge.
    const double dt = 1.0 / fsOs;
    const double iPos = kIQuiet + (i > 0.0 ? i : 0.0);
    const double iNeg = kIQuiet + (i < 0.0 ? -i : 0.0);
    const double chP = railPos < kRailNoLoad ? (kRailNoLoad - railPos) / kRailRs : 0.0;
    const double chN = railNeg < kRailNoLoad ? (kRailNoLoad - railNeg) / kRailRs : 0.0;
    railPos += (chP - iPos) * dt / kRailC;
    railNeg += (chN - iNeg) * dt / kRailC;
    if (railPos < 5.0) railPos = 5.0;
    if (railNeg < 5.0) railNeg = 5.0;

    return speaker.v (spkOut);
}

void IC100SAmp::process (const float* in, float* out, int numSamples)
{
    const int F = Oversampler4x::kFactor;
    const double a = smoothCoeff;
    const double aBlock = 1.0 - std::pow (1.0 - a, (double) kPotUpdateInterval);

    double up[F], term[F], spk[F];

    for (int n = 0; n < numSamples; ++n)
    {
        if (--potCountdown <= 0)
        {
            potCountdown = kPotUpdateInterval;
            if (sustainOn != appSus || bright1 != appB1 || bright2 != appB2 || stage != appStage)
            {
                if (sustainOn && ! appSus) sus.reset();   // Sustain 741 idles while switched out
                applySwitches();
            }
            bool moved = false;
            for (int k = 0; k < kNumKnobs; ++k)
            {
                const double d = knobTarget[k] - knobCur[k];
                if (std::fabs (d) > 1.0e-5)
                {
                    knobCur[k] += d * aBlock;
                    if (std::fabs (knobTarget[k] - knobCur[k]) < 1.0e-5) knobCur[k] = knobTarget[k];
                    moved = true;
                }
            }
            if (moved) applyPots();

            // A channel switched back on starts from rest.
            if (ch1On && ! ch1Running) { in1.reset(); sus.reset(); tone1.reset(); out1.reset(); ch1Running = true; }
            if (ch2On && ! ch2Running) { in2.reset(); tone2.reset(); out2.reset(); ch2Running = true; }
            if (! ch1On && ch1Gain < 1.0e-4) ch1Running = false;
            if (! ch2On && ch2Gain < 1.0e-4) ch2Running = false;
        }

        inGainC  += (inGainT - inGainC) * a;
        outGainC += (outGainT - outGainC) * a;
        ch1Gain  += ((ch1On ? 1.0 : 0.0) - ch1Gain) * a;
        ch2Gain  += ((ch2On ? 1.0 : 0.0) - ch2Gain) * a;

        const double x = (double) in[n] * inGainC;
        osMain.upsample (x, up);

        for (int k = 0; k < F; ++k)
        {
            const double v1 = ch1Running ? processCh1 (up[k]) * ch1Gain : 0.0;
            const double v2 = ch2Running ? processCh2 (up[k]) * ch2Gain : 0.0;
            const double vs = powerAmp (mixer (v1, v2));

            // Both outputs run all the time so the Speaker switch can crossfade cleanly.
            const double vc = speaker.v (spkCone);
            spk[k] = (vc - coneVPrev) * fsOs;   // cone acceleration ~ radiated pressure
            coneVPrev = vc;
            term[k] = vs;
        }

        const double yTerm = osMain.downsample (term);
        double yAc = osSpkDown.downsample (spk);
        for (int b = 0; b < 6; ++b) yAc = cab[b].process (yAc);
        yAc *= cabNorm;
        spkMix += ((speakerOn ? 1.0 : 0.0) - spkMix) * a;
        const double y = (yTerm + (yAc - yTerm) * spkMix) * (outGainC / kVFullScale);

        // Mains switch: off passes the input straight through, delayed to line up with the amp.
        dry[dryPos] = (double) in[n];
        int rd = dryPos - latencySamples();
        if (rd < 0) rd += kDryLen;
        const double dryOut = dry[rd];
        dryPos = (dryPos + 1) % kDryLen;
        powerMix += ((powerOn ? 1.0 : 0.0) - powerMix) * a;

        double o = dryOut + (y - dryOut) * powerMix;
        if (! (std::fabs (o) < 1.0e6)) { reset(); o = 0.0; }   // NaN or infinity: start again from rest
        out[n] = (float) o;
    }
}

} // namespace hhic
