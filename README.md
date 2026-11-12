# TB IC100-S (Mountain Lion VST)

A model of the **H/H IC100-S** guitar amplifier head as a VST 2.4 plugin, 64-bit Intel, for
**Mac OS X 10.8.5** (Pro Tools 11 through Blue Cat PatchWork). Non-commercial, personal use.

Built from the H/H drawing *IC100-S Circuit Diagram* (in `Docs/`), which includes the AO3103/7
power amp board. This is the IC100-**S**: a different circuit from the plain IC100, which is not
used anywhere here.

## The panel

Laid out like the IC100-S head's silver front panel:

| Upper band | Channel 1: **Sustain, Presence, Treble, Bass, Volume** — Channel 2: **Brilliance, Treble, Bass, Volume** |
|---|---|
| Switch row | **Sustain** slide, **Ch 1 Input** Normal/Bright rocker, **Ch 1** on rocker, **Studio/Stage** slide, **Ch 2 Input** Normal/Bright rocker, **Ch 2** on rocker, red **Mains** rocker |
| Plugin strip | **Speaker** (miked 2x12 or speaker terminals), **Input** ±12 dB, **Output** ±24 dB, 8 presets, readout |

The amp's two input jacks per channel are replaced by one input per channel with a
Normal/Bright rocker, and each channel has an on switch so the guitar can feed Ch 1, Ch 2 or
both. **Mains** off passes the dry signal (time-aligned). Every switch lights up on its engaged side.

## What is modelled (component level, 4x oversampled)

- **Inputs**: Normal (2K2 + 4u7) or Bright (2K2 + 4n7) into a 741 at x18 (47K/2K7), 100K bias.
- **Sustain** (Ch 1): a 741 at 43-67 dB of gain (150K over 68R + 680n + the 1K Sustain pot),
  fed through 4K7/47n, clipping on its ±16 V rails, then 22K/6u8/100n into the tone stack.
  S1 picks the clean or Sustain signal.
- **Tone stacks** (both channels): 6K8, Bass 25K log with 22n/220n, 1K; Treble 25K log
  between 10n and 220n; the Bass wiper joins via 2K2. Ch 1's stack is fed through 100R with
  47n and 4K7 || 10n at its top.
- **Volume** 25K log (Ch 1 has a 10n bright cap across it), 6u8 into a 741 whose feedback is a
  T of 47K + 47K (33p across) with 100n (Presence, Ch 1) or 47n (Brilliance, Ch 2), 3K9 and
  the 25K pot to ground.
- **Mixer**: inverting 741, 150K from each channel, 3K9 + 33K feedback; **Studio** shorts the
  33K (19.5 dB quieter than **Stage**). 470n out to the power amp.
- **Power amp** (AO3103/7): closed-loop gain, input coupling (470n + 0.22u into 16K), slew,
  clipping on ±47 V rails that sag on 5000u reservoirs, V-I current limit, the output network
  (L1 with 15R, Zobel 15R + 47n) into the owner's **8 ohm 2x12 with two 1977 H/H 12"
  speakers** (the same electrical speaker model as TB VS Musician), plus the cab's acoustic voice.

### Estimates (values the drawing doesn't fix or the scan doesn't show clearly)

| Constant | Value | Why |
|---|---|---|
| `kPaGain` | 22 | the power amp board's feedback network is hard to read on the scan; about 1 V rms at Power Amp In for full power |
| `kRailRs`, `kRailDrop`, `kIMax`, `kPaSlew` | 2.5 ohm, 3 V, 8 A, 2 V/us | supply source resistance, output stage losses, V-I limit, slew |
| L1 | 3 uH | not marked |
| Pot tapers | log for Bass/Treble/Volume (CCW end at the bottom); Presence/Brilliance log as a rheostat from the CW end | "LOG" and "A/LOG" on the drawing |
| Unused input jack | left open | the jack switching on the drawing is unclear; the plugin uses one input per channel anyway |

## Build and test

GitHub Actions builds and verifies the bundle on an Intel macOS runner (minimum OS 10.8.5,
only 10.8-era system libraries), loads it in a mini host, drives the panel offscreen and
publishes screenshots to the `gui-preview` branch. Locally on Linux:

    make -C Tests run vst-run      # DSP tests, preset store, plugin in a mini VST host
    make -C Tests asan             # the same under AddressSanitizer + UBSan

## Install

1. Actions → latest run → Artifacts → **TB-IC100S-mountain-lion-vst** → download.
2. Unzip (twice: the artifact zip holds `TB-IC100S-mountain-lion-vst.zip`).
3. Copy **TB IC100-S.vst** to `/Library/Audio/Plug-Ins/VST/`.
4. Rescan in Blue Cat PatchWork.

Presets live in `~/Library/Application Support/TB/TB IC100-S/Presets.txt`.
