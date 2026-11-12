// ic100s_test.cpp
// Offline checks for the TB IC100-S DSP. Builds on Linux or macOS:
//   make -C Tests run      (writes demo WAVs to Tests/renders)
//
// 1. Small-signal behaviour of the preamp: every tone control moves its band
//    the right way, Volumes are monotonic, Bright input, Studio/Stage
// 2. Sustain: compression, level, Sustain knob
// 3. Channel switches and the mains switch (dry pass-through, exact)
// 4. Power amp: clipping level, current limit, supply sag and recovery
// 5. Stability sweep: control settings x 10 mV-4 V x 41 Hz-15 kHz, plus loud noise
// 6. Automation of every control while playing
// 7. CPU per second of audio
// 8. Demo renders (32-bit float WAV)

#include "../Source/DSP/IC100SAmp.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <ctime>
#include <string>
#include <vector>

using namespace hhic;

static const double kPiT = 3.14159265358979323846;
static const int kSr = 44100;
static int failures = 0;

static void check (bool ok, const char* what)
{
    std::printf ("  %s  %s\n", ok ? "ok  " : "FAIL", what);
    if (! ok) ++failures;
}

static bool allFinite (const std::vector<float>& x, double limit)
{
    for (size_t i = 0; i < x.size(); ++i)
        if (! (x[i] == x[i]) || std::fabs (x[i]) > limit) return false;
    return true;
}

static double peakFrom (const std::vector<float>& x, size_t from)
{
    double p = 0.0;
    for (size_t i = from; i < x.size(); ++i) p = std::max (p, (double) std::fabs (x[i]));
    return p;
}

static double rmsFrom (const std::vector<float>& x, size_t from)
{
    double s = 0.0;
    for (size_t i = from; i < x.size(); ++i) s += (double) x[i] * x[i];
    return std::sqrt (s / (double) (x.size() - from));
}

static std::vector<float> sine (double f, double amp, double sec)
{
    std::vector<float> v ((size_t) (sec * kSr));
    for (size_t i = 0; i < v.size(); ++i) v[i] = (float) (amp * std::sin (2.0 * kPiT * f * i / kSr));
    return v;
}

static std::vector<float> run (IC100SAmp& a, const std::vector<float>& in)
{
    std::vector<float> out (in.size());
    const int block = 256;
    for (size_t pos = 0; pos < in.size(); pos += block)
    {
        const int n = (int) ((in.size() - pos) < (size_t) block ? (in.size() - pos) : block);
        a.process (&in[pos], &out[pos], n);
    }
    return out;
}

// Preamp small-signal gains (dB) at f, run at the oversampled rate:
// Ch 1 output, Ch 2 output, mixer output.
struct PreGains { double ch1, ch2, mix; };

static PreGains pre (IC100SAmp& a, double f, double amp = 0.01)
{
    a.resetPreamp();
    const double fs = a.oversampledRate();
    const int n = (int) (fs * 0.5);
    double p1 = 0.0, p2 = 0.0, pm = 0.0;
    for (int i = 0; i < n; ++i)
    {
        double c1, c2, m;
        a.probePreamp (amp * std::sin (2.0 * kPiT * f * i / fs), c1, c2, m);
        if (i > n / 2)
        {
            p1 = std::max (p1, std::fabs (c1));
            p2 = std::max (p2, std::fabs (c2));
            pm = std::max (pm, std::fabs (m));
        }
    }
    PreGains g;
    g.ch1 = 20.0 * std::log10 (p1 / amp + 1e-30);
    g.ch2 = 20.0 * std::log10 (p2 / amp + 1e-30);
    g.mix = 20.0 * std::log10 (pm / amp + 1e-30);
    return g;
}

static void writeWav (const std::string& path, const std::vector<float>& x, int sr)
{
    FILE* f = std::fopen (path.c_str(), "wb");
    if (! f) { std::printf ("  could not write %s\n", path.c_str()); return; }
    const unsigned int dataBytes = (unsigned int) (x.size() * 4);
    const unsigned int riff = 36 + dataBytes;
    const unsigned short fmt = 3, ch = 1, bits = 32, align = 4;
    const unsigned int rate = (unsigned int) sr, bps = rate * 4, fmtLen = 16;
    std::fwrite ("RIFF", 1, 4, f); std::fwrite (&riff, 4, 1, f);
    std::fwrite ("WAVEfmt ", 1, 8, f); std::fwrite (&fmtLen, 4, 1, f);
    std::fwrite (&fmt, 2, 1, f); std::fwrite (&ch, 2, 1, f);
    std::fwrite (&rate, 4, 1, f); std::fwrite (&bps, 4, 1, f);
    std::fwrite (&align, 2, 1, f); std::fwrite (&bits, 2, 1, f);
    std::fwrite ("data", 1, 4, f); std::fwrite (&dataBytes, 4, 1, f);
    std::fwrite (&x[0], 4, x.size(), f);
    std::fclose (f);
}

// Karplus-Strong plucked string as a stand-in for a DI guitar track.
static void pluck (std::vector<float>& buf, size_t start, double freq, double amp, double dur)
{
    const int period = (int) (kSr / freq + 0.5);
    std::vector<double> line (period);
    unsigned int seed = (unsigned int) (freq * 1000.0);
    for (int i = 0; i < period; ++i)
    {
        seed = seed * 1664525u + 1013904223u;
        line[i] = ((seed >> 8) / 16777216.0 * 2.0 - 1.0) * amp;
    }
    const size_t n = (size_t) (dur * kSr);
    int pos = 0;
    for (size_t i = 0; i < n && start + i < buf.size(); ++i)
    {
        const int nx = (pos + 1) % period;
        const double v = line[pos];
        line[pos] = 0.4985 * (line[pos] + line[nx]);
        pos = nx;
        buf[start + i] += (float) v;
    }
}

static std::vector<float> riff (double amp)
{
    std::vector<float> g ((size_t) (6.0 * kSr), 0.0f);
    const double notes[] = { 82.4, 110.0, 146.8, 110.0, 82.4, 123.5, 164.8, 98.0 };
    for (int k = 0; k < 8; ++k)
    {
        pluck (g, (size_t) (k * 0.5 * kSr), notes[k], amp, 0.9);
        pluck (g, (size_t) (k * 0.5 * kSr), notes[k] * 1.5, amp * 0.6, 0.9);
    }
    pluck (g, (size_t) (4.2 * kSr), 196.0, amp, 1.8);
    pluck (g, (size_t) (4.2 * kSr), 246.9, amp * 0.8, 1.8);
    pluck (g, (size_t) (4.2 * kSr), 293.7, amp * 0.7, 1.8);
    return g;
}

//==============================================================================
static void testPreamp()
{
    std::printf ("1. Preamp small-signal behaviour\n");

    {
        IC100SAmp a;
        a.prepare (kSr);
        check (a.circuitsBuilt(), "every netlist built with all its nodes");
    }

    // Bass, Treble, Presence on Ch 1; Bass, Treble, Brilliance on Ch 2
    for (int ch = 0; ch < 2; ++ch)
    {
        double lo[3], hi[3], tl[3], th[3], pr[3];
        for (int s = 0; s < 3; ++s)
        {
            const double v = s * 0.5;
            IC100SAmp b, t, p;
            if (ch == 0) { b.setCh1Bass (v); t.setCh1Treble (v); p.setPresence (v); }
            else { b.setCh2On (true); b.setCh2Bass (v); t.setCh2On (true); t.setCh2Treble (v);
                   p.setCh2On (true); p.setBrilliance (v); }
            b.prepare (kSr); t.prepare (kSr); p.prepare (kSr);
            const PreGains b100 = pre (b, 100.0), b1k = pre (b, 1000.0);
            const PreGains t5k = pre (t, 5000.0), t100 = pre (t, 100.0);
            const PreGains p3k = pre (p, 3000.0);
            lo[s] = ch == 0 ? b100.ch1 : b100.ch2;
            hi[s] = ch == 0 ? b1k.ch1 : b1k.ch2;
            th[s] = ch == 0 ? t5k.ch1 : t5k.ch2;
            tl[s] = ch == 0 ? t100.ch1 : t100.ch2;
            pr[s] = ch == 0 ? p3k.ch1 : p3k.ch2;
        }
        char msg[160];
        std::printf ("    Ch %d bass 0/5/10 @100 Hz: %.1f %.1f %.1f dB (1 kHz %.1f..%.1f)\n", ch + 1, lo[0], lo[1], lo[2], hi[0], hi[2]);
        std::snprintf (msg, sizeof (msg), "Ch %d Bass raises 100 Hz by > 12 dB, monotonic, 1 kHz moves < 3 dB", ch + 1);
        check (lo[2] - lo[0] > 12.0 && lo[1] > lo[0] && lo[2] > lo[1] && std::fabs (hi[2] - hi[0]) < 3.0, msg);
        std::printf ("    Ch %d treble 0/5/10 @5 kHz: %.1f %.1f %.1f dB (100 Hz %.1f..%.1f)\n", ch + 1, th[0], th[1], th[2], tl[0], tl[2]);
        std::snprintf (msg, sizeof (msg), "Ch %d Treble raises 5 kHz by > 15 dB, monotonic, 100 Hz moves < 3 dB", ch + 1);
        check (th[2] - th[0] > 15.0 && th[1] > th[0] && th[2] > th[1] && std::fabs (tl[2] - tl[0]) < 3.0, msg);
        std::printf ("    Ch %d %s 0/5/10 @3 kHz: %.1f %.1f %.1f dB\n", ch + 1, ch == 0 ? "presence" : "brilliance", pr[0], pr[1], pr[2]);
        std::snprintf (msg, sizeof (msg), "Ch %d %s lifts 3 kHz by > 5 dB, monotonic", ch + 1, ch == 0 ? "Presence" : "Brilliance");
        check (pr[2] - pr[0] > 5.0 && pr[1] >= pr[0] && pr[2] > pr[1], msg);
    }

    // Volumes
    for (int ch = 0; ch < 2; ++ch)
    {
        double prev = -1e9;
        bool mono = true;
        double g0 = 0.0, g10 = 0.0;
        for (int s = 0; s <= 10; ++s)
        {
            IC100SAmp a;
            if (ch == 0) a.setCh1Volume (s / 10.0);
            else { a.setCh2On (true); a.setCh2Volume (s / 10.0); }
            a.prepare (kSr);
            const PreGains g = pre (a, 1000.0);
            const double v = ch == 0 ? g.ch1 : g.ch2;
            if (v <= prev) mono = false;
            prev = v;
            if (s == 0) g0 = v;
            if (s == 10) g10 = v;
        }
        char msg[96];
        std::printf ("    Ch %d volume 0 -> 10 @1 kHz: %.1f -> %.1f dB\n", ch + 1, g0, g10);
        std::snprintf (msg, sizeof (msg), "Ch %d Volume monotonic over 0..10, range > 50 dB", ch + 1);
        check (mono && g10 - g0 > 50.0, msg);
    }

    // Bright inputs: bass cut, top end kept
    for (int ch = 0; ch < 2; ++ch)
    {
        IC100SAmp n, b;
        n.setCh2On (true); b.setCh2On (true);
        if (ch == 0) b.setCh1Bright (true); else b.setCh2Bright (true);
        n.prepare (kSr); b.prepare (kSr);
        const PreGains n100 = pre (n, 100.0), b100 = pre (b, 100.0), n6k = pre (n, 6000.0), b6k = pre (b, 6000.0);
        const double d100 = (ch == 0 ? n100.ch1 - b100.ch1 : n100.ch2 - b100.ch2);
        const double d6k = (ch == 0 ? n6k.ch1 - b6k.ch1 : n6k.ch2 - b6k.ch2);
        std::printf ("    Ch %d Bright vs Normal: 100 Hz %+.1f dB, 6 kHz %+.1f dB\n", ch + 1, -d100, -d6k);
        char msg[96];
        std::snprintf (msg, sizeof (msg), "Ch %d Bright input cuts 100 Hz by > 10 dB, 6 kHz within 2 dB", ch + 1);
        check (d100 > 10.0 && std::fabs (d6k) < 2.0, msg);
    }

    // Studio / Stage: 3K9 vs 36K9 over 150K
    {
        IC100SAmp st, su;
        su.setStage (false);
        st.prepare (kSr); su.prepare (kSr);
        const double d = pre (st, 1000.0).mix - pre (su, 1000.0).mix;
        std::printf ("    Stage - Studio at the mixer: %.2f dB (36K9/3K9 = 19.51 dB)\n", d);
        check (std::fabs (d - 19.51) < 0.5, "Studio/Stage switch changes the mixer gain by 19.5 dB");
    }

    // Mixer: both channels add
    {
        IC100SAmp one, both;
        both.setCh2On (true);
        one.prepare (kSr); both.prepare (kSr);
        const PreGains g = pre (both, 400.0);
        std::printf ("    mixer at 400 Hz: Ch 1 %.1f, Ch 2 %.1f, mixed %.1f dB\n", g.ch1, g.ch2, g.mix);
        check (g.mix > std::max (g.ch1, g.ch2) - 12.5, "mixer sums both channels at about -12 dB each (Stage)");
    }
}

static void testSustain()
{
    std::printf ("2. Sustain\n");
    double outLo = 0.0, outHi = 0.0, clean = 0.0;
    {
        IC100SAmp a;
        a.prepare (kSr);
        clean = std::pow (10.0, pre (a, 200.0, 0.01).ch1 / 20.0) * 0.01;
    }
    double lvl[3];
    for (int s = 0; s < 3; ++s)
    {
        IC100SAmp a;
        a.setSustainOn (true);
        a.setSustain (s * 0.5);
        a.prepare (kSr);
        lvl[s] = std::pow (10.0, pre (a, 200.0, 0.002).ch1 / 20.0) * 0.002;
        if (s == 1)
        {
            outLo = std::pow (10.0, pre (a, 200.0, 0.01).ch1 / 20.0) * 0.01;
            outHi = std::pow (10.0, pre (a, 200.0, 1.0).ch1 / 20.0) * 1.0;
        }
    }
    std::printf ("    Ch 1 out at 200 Hz: clean 10 mV in -> %.3f V; Sustain on 10 mV -> %.3f V, 1 V -> %.3f V\n", clean, outLo, outHi);
    check (20.0 * std::log10 (outHi / outLo) < 3.0, "Sustain compresses: 40 dB more input, < 3 dB more output");
    check (outLo > clean * 2.0, "Sustain on is louder than clean at a light touch");
    std::printf ("    2 mV in, Sustain 0/5/10: %.3f %.3f %.3f V\n", lvl[0], lvl[1], lvl[2]);
    check (lvl[2] > lvl[1] && lvl[1] > lvl[0], "Sustain knob raises the drive (more output from a decaying note)");
}

static void testSwitches()
{
    std::printf ("3. Channel and mains switches\n");
    // Ch 2 off: its knobs do nothing; Ch 1 off as well: silence
    {
        IC100SAmp a, b;
        b.setCh2Volume (1.0); b.setBrilliance (1.0);
        a.prepare (kSr); b.prepare (kSr);
        const std::vector<float> in = sine (330.0, 0.1, 0.5);
        const std::vector<float> oa = run (a, in), ob = run (b, in);
        double d = 0.0;
        for (size_t i = 0; i < oa.size(); ++i) d = std::max (d, (double) std::fabs (oa[i] - ob[i]));
        check (d < 1.0e-6, "Ch 2 switched off: its controls have no effect");

        IC100SAmp c;
        c.setCh1On (false);
        c.prepare (kSr);
        const std::vector<float> oc = run (c, in);
        check (peakFrom (oc, oc.size() / 2) < 1.0e-6, "both channels off: silence");

        IC100SAmp e;
        e.setCh1On (false); e.setCh2On (true);
        e.prepare (kSr);
        const std::vector<float> oe = run (e, in);
        check (peakFrom (oe, oe.size() / 2) > 0.01, "Ch 2 alone plays");
    }
    // Mains off: the input, delayed by the reported latency, exactly
    {
        IC100SAmp a;
        a.setPower (false);
        a.prepare (kSr);
        std::vector<float> in = sine (440.0, 0.3, 0.3);
        const std::vector<float> o = run (a, in);
        const int L = IC100SAmp::latencySamples();
        double d = 0.0;
        for (size_t i = (size_t) L; i < o.size(); ++i) d = std::max (d, (double) std::fabs (o[i] - in[i - L]));
        std::printf ("    latency %d samples\n", L);
        check (d < 1.0e-6, "mains off passes the dry input, delayed by the reported latency");
    }
    // Switching while playing: no NaN, no runaway
    {
        IC100SAmp a;
        a.prepare (kSr);
        const std::vector<float> in = sine (196.0, 0.3, 3.0);
        std::vector<float> out (in.size());
        const int block = 128;
        for (size_t pos = 0, k = 0; pos < in.size(); pos += block, ++k)
        {
            if (k % 37 == 0) a.setSustainOn ((k / 37) % 2 == 1);
            if (k % 53 == 0) a.setCh2On ((k / 53) % 2 == 1);
            if (k % 61 == 0) a.setStage ((k / 61) % 2 == 0);
            if (k % 71 == 0) a.setCh1Bright ((k / 71) % 2 == 1);
            if (k % 83 == 0) a.setPower ((k / 83) % 3 != 2);
            if (k % 97 == 0) a.setSpeaker ((k / 97) % 2 == 0);
            const int n = (int) std::min ((size_t) block, in.size() - pos);
            a.process (&in[pos], &out[pos], n);
        }
        check (allFinite (out, 4.0), "every switch toggled while playing: finite, bounded");
    }
}

static void testPowerAmp()
{
    std::printf ("4. Power amp\n");
    IC100SAmp a;
    a.setCh1Volume (1.0);
    a.setSpeaker (false);
    a.prepare (kSr);
    const std::vector<float> in = sine (220.0, 0.3, 1.0);
    a.clearPeakAmps();
    const std::vector<float> o = run (a, in);
    const double vpk = peakFrom (o, o.size() / 2) * 40.0;
    std::printf ("    flat out: %.1f V peak at the speaker, %.2f A peak, rails %.1f V\n", vpk, a.peakSpeakerAmps(), a.railPositive());
    check (vpk > 38.0 && vpk < 47.0, "clips at 38-47 V peak into the 8 ohm 2x12 (about 100 W)");
    check (a.peakSpeakerAmps() < 8.01, "speaker current within the V-I limit");
    check (a.railPositive() < 47.5, "rails sag under full load");
    std::vector<float> quiet (kSr, 0.0f);
    run (a, quiet);
    check (a.railPositive() > 46.0, "rails recover after the note");

    // Small signal stays well under clipping at a moderate setting
    IC100SAmp b;
    b.setSpeaker (false);
    b.prepare (kSr);
    const std::vector<float> ob = run (b, sine (220.0, 0.05, 0.5));
    const double vb = peakFrom (ob, ob.size() / 2) * 40.0;
    std::printf ("    50 mV guitar, Volume 5: %.2f V peak\n", vb);
    check (vb > 0.1 && vb < 30.0, "moderate settings play cleanly below clipping");
}

static void testStability()
{
    std::printf ("5. Stability sweep\n");
    unsigned int seed = 12345u;
    bool ok = true;
    const double amps[] = { 0.01, 0.3, 1.5, 4.0 };
    const double freqs[] = { 41.0, 330.0, 2500.0, 15000.0 };
    int runs = 0;
    for (int k = 0; k < 24; ++k)
    {
        IC100SAmp a;
        double v[9];
        for (int i = 0; i < 9; ++i)
        {
            seed = seed * 1664525u + 1013904223u;
            v[i] = (k < 2) ? (double) k : (seed >> 8) / 16777216.0;
        }
        a.setSustain (v[0]); a.setPresence (v[1]); a.setCh1Treble (v[2]); a.setCh1Bass (v[3]);
        a.setCh1Volume (v[4]); a.setBrilliance (v[5]); a.setCh2Treble (v[6]); a.setCh2Bass (v[7]);
        a.setCh2Volume (v[8]);
        a.setSustainOn (k % 2 == 0); a.setCh2On (k % 3 != 0); a.setStage (k % 4 != 1);
        a.setCh1Bright (k % 5 == 0); a.setCh2Bright (k % 7 == 0); a.setSpeaker (k % 2 == 1);
        a.prepare (k % 3 == 0 ? 96000.0 : kSr);
        for (int ai = 0; ai < 4; ++ai)
            for (int fi = 0; fi < 4; ++fi)
            {
                const std::vector<float> o = run (a, sine (freqs[fi], amps[ai], 0.15));
                if (! allFinite (o, 3.0)) ok = false;
                ++runs;
            }
        std::vector<float> noise ((size_t) (0.3 * kSr));
        for (size_t i = 0; i < noise.size(); ++i)
        {
            seed = seed * 1664525u + 1013904223u;
            noise[i] = (float) (((seed >> 8) / 16777216.0 * 2.0 - 1.0) * 4.0);
        }
        if (! allFinite (run (a, noise), 3.0)) ok = false;
    }
    std::printf ("    %d sine runs + 24 loud noise runs\n", runs);
    check (ok, "no NaN, no runaway, output under +10 dBFS for every setting");
}

static void testAutomation()
{
    std::printf ("6. Automation\n");
    IC100SAmp a;
    a.setCh2On (true);
    a.prepare (kSr);
    const std::vector<float> in = riff (0.3);
    std::vector<float> out (in.size());
    const int block = 64;
    for (size_t pos = 0, k = 0; pos < in.size(); pos += block, ++k)
    {
        const double t = (double) pos / kSr;
        const double s = 0.5 + 0.5 * std::sin (t * 7.0);
        a.setSustain (s); a.setPresence (1.0 - s); a.setCh1Treble (s); a.setCh1Bass (1.0 - s);
        a.setCh1Volume (0.3 + 0.5 * s); a.setBrilliance (s); a.setCh2Treble (1.0 - s);
        a.setCh2Bass (s); a.setCh2Volume (0.6 - 0.4 * s);
        a.setInputGainDb (-12.0 + 24.0 * s); a.setOutputGainDb (-6.0 * s);
        const int n = (int) std::min ((size_t) block, in.size() - pos);
        a.process (&in[pos], &out[pos], n);
    }
    check (allFinite (out, 3.0), "every knob swept while playing: finite, bounded");
}

static void testCpu()
{
    std::printf ("7. CPU\n");
    IC100SAmp a;
    a.setCh2On (true);
    a.setSustainOn (true);
    a.prepare (kSr);
    const std::vector<float> in = riff (0.3);
    const std::clock_t t0 = std::clock();
    run (a, in);
    const double sec = (double) (std::clock() - t0) / CLOCKS_PER_SEC;
    const double perSec = sec / ((double) in.size() / kSr);
    std::printf ("    %.1f ms per second of audio (both channels and Sustain on, one side)\n", perSec * 1000.0);
#if defined(__SANITIZE_ADDRESS__)
    std::printf ("    (sanitizer build: timing not checked)\n");
#else
    check (perSec < 0.5, "runs well within real time");
#endif
}

static void renders (const char* dir)
{
    std::printf ("8. Demo renders\n");
    const std::vector<float> g = riff (0.25);
    struct R { const char* name; double sus; bool susOn; double vol1; bool ch2; double vol2; bool stage; bool bright; double pres; };
    const R rs[] = {
        { "ch1_clean",        0.5, false, 0.4, false, 0.0, true,  false, 0.5 },
        { "ch1_bright_input", 0.5, false, 0.4, false, 0.0, true,  true,  0.5 },
        { "ch1_cranked",      0.5, false, 1.0, false, 0.0, true,  false, 0.6 },
        { "ch1_sustain",      0.8, true,  0.5, false, 0.0, true,  false, 0.5 },
        { "ch2_clean",        0.5, false, 0.0, true,  0.5, true,  false, 0.5 },
        { "both_channels",    0.5, false, 0.4, true,  0.4, true,  false, 0.5 },
        { "ch1_studio",       0.5, false, 0.8, false, 0.0, false, false, 0.5 },
    };
    for (size_t i = 0; i < sizeof (rs) / sizeof (rs[0]); ++i)
    {
        IC100SAmp a;
        a.setSustain (rs[i].sus); a.setSustainOn (rs[i].susOn); a.setCh1Volume (rs[i].vol1);
        a.setCh1On (rs[i].vol1 > 0.0); a.setCh2On (rs[i].ch2); a.setCh2Volume (rs[i].vol2);
        a.setStage (rs[i].stage); a.setCh1Bright (rs[i].bright); a.setPresence (rs[i].pres);
        a.prepare (kSr);
        const std::vector<float> o = run (a, g);
        const std::string path = std::string (dir) + "/" + rs[i].name + ".wav";
        writeWav (path, o, kSr);
        std::printf ("    %-18s peak %.3f  rms %.3f\n", rs[i].name, peakFrom (o, 0), rmsFrom (o, 0));
    }
    writeWav (std::string (dir) + "/dry_guitar.wav", g, kSr);
}

int main (int argc, char** argv)
{
    const char* dir = argc > 1 ? argv[1] : "renders";
    testPreamp();
    testSustain();
    testSwitches();
    testPowerAmp();
    testStability();
    testAutomation();
    testCpu();
    renders (dir);
    std::printf ("\n%s (%d failure%s)\n", failures ? "FAILED" : "ALL PASSED", failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
