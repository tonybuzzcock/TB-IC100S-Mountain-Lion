// PanelView.mm
// The custom panel for TB IC100-S: an original design laid out like the
// IC100-S head's silver front panel. The upper band holds the nine knobs left
// to right as on the amp (Channel 1: Sustain, Presence, Treble, Bass, Volume;
// Channel 2: Brilliance, Treble, Bass, Volume) with the model name at the
// right. The lower band follows the amp's switch row: the TB electronic logo
// where the amp has its maker's mark, the Sustain slide switch, Channel 1's
// input (a Normal/Bright rocker in place of the two jacks) and its On rocker,
// the Studio/Stage slide switch, the same pair for Channel 2, and the red
// mains rocker. Every switch lights up on its engaged side. A strip of plugin
// controls runs along the bottom (Speaker, Input, Output, presets, readout).
//
// Cocoa, manual retain/release, and only APIs that exist in Mac OS X 10.8.5.
// Everything is drawn in code; no image files.

#import <Cocoa/Cocoa.h>

#include "Editor.h"
#include "Params.h"
#include "Presets.h"
#include "TBLogo.h"

#include <cmath>
#include <cstdio>
#include <cstring>

using namespace tbic;

namespace
{

enum CtlType { kCtlKnob, kCtlRocker, kCtlSlide, kCtlSmallKnob };

struct Ctl
{
    int param;
    int type;
    CGFloat cx, cy, r;     // r: knob radius (rockers and slides use fixed sizes)
    const char* label;     // name on the panel (also the readout's name)
    const char* posA;      // off / left / bottom legend
    const char* posB;      // on / right / top legend
    bool red;              // rocker lit red (mains) rather than amber
};

// Layout (flipped coordinates, origin top left)
const CGFloat kLedgeY = 148.0;      // between the knob band and the switch band
const CGFloat kStripTop = 228.0;    // the plugin strip below the faceplate
const CGFloat kKnobY = 92.0;
const CGFloat kSwitchY = 194.0;
const CGFloat kRockerW = 20.0, kRockerH = 32.0;
const CGFloat kSlideHalfW = 24.0, kSlideHalfH = 11.0;

const Ctl kCtls[] = {
    // Channel 1
    { kSustain,    kCtlKnob,    170, kKnobY, 22, "SUSTAIN",    0, 0, false },
    { kPresence,   kCtlKnob,    258, kKnobY, 22, "PRESENCE",   0, 0, false },
    { kCh1Treble,  kCtlKnob,    346, kKnobY, 22, "TREBLE",     0, 0, false },
    { kCh1Bass,    kCtlKnob,    434, kKnobY, 22, "BASS",       0, 0, false },
    { kCh1Volume,  kCtlKnob,    522, kKnobY, 22, "VOLUME",     0, 0, false },
    // Channel 2
    { kBrilliance, kCtlKnob,    690, kKnobY, 22, "BRILLIANCE", 0, 0, false },
    { kCh2Treble,  kCtlKnob,    778, kKnobY, 22, "TREBLE",     0, 0, false },
    { kCh2Bass,    kCtlKnob,    866, kKnobY, 22, "BASS",       0, 0, false },
    { kCh2Volume,  kCtlKnob,    954, kKnobY, 22, "VOLUME",     0, 0, false },
    // Switch row
    { kSustainOn,  kCtlSlide,   300, kSwitchY, 0, "SUSTAIN",    "OFF", "ON", false },
    { kCh1Bright,  kCtlRocker,  420, kSwitchY, 0, "CH 1 INPUT", "NORMAL", "BRIGHT", false },
    { kCh1On,      kCtlRocker,  520, kSwitchY, 0, "CH 1",       "OFF", "ON", false },
    { kStage,      kCtlSlide,   660, kSwitchY, 0, "",           "STUDIO", "STAGE", false },
    { kCh2Bright,  kCtlRocker,  800, kSwitchY, 0, "CH 2 INPUT", "NORMAL", "BRIGHT", false },
    { kCh2On,      kCtlRocker,  900, kSwitchY, 0, "CH 2",       "OFF", "ON", false },
    { kMains,      kCtlRocker, 1110, kSwitchY, 0, "MAINS",      "OFF", "ON", true },
    // Plugin strip
    { kSpeaker,    kCtlRocker,   60, 278, 0,  "SPEAKER", "OFF", "ON", false },
    { kInput,      kCtlSmallKnob,170, 276, 15, "INPUT",   "-12", "+12", false },
    { kOutput,     kCtlSmallKnob,250, 276, 15, "OUTPUT",  "-24", "+24", false },
};
const int kNumCtls = (int) (sizeof (kCtls) / sizeof (kCtls[0]));

// Preset section in the strip: a SAVE button and slots 1..8, then the readout
const CGFloat kStripLabelY = 244.0;
const NSRect kSaveRect = { { 312.0, 264.0 }, { 50.0, 26.0 } };
const CGFloat kSlotX0 = 372.0, kSlotPitch = 31.0, kSlotW = 27.0, kSlotY = 264.0, kSlotH = 26.0;
const NSRect kReadoutRect = { { 650.0, 252.0 }, { 530.0, 50.0 } };

inline NSRect slotRect (int k) { return NSMakeRect (kSlotX0 + k * kSlotPitch, kSlotY, kSlotW, kSlotH); }

// Pointer angle (radians, 0 = up, clockwise positive) for a 0..1 value.
inline CGFloat valueAngle (CGFloat v) { return (CGFloat) ((-150.0 + 300.0 * v) * M_PI / 180.0); }

// Point at angle a (0 = up, clockwise) and radius r around (cx, cy) in flipped coordinates.
inline NSPoint polar (CGFloat cx, CGFloat cy, CGFloat r, CGFloat a)
{
    return NSMakePoint (cx + r * std::sin (a), cy - r * std::cos (a));
}

NSColor* rgb (int r, int g, int b) { return [NSColor colorWithCalibratedRed: r / 255.0 green: g / 255.0 blue: b / 255.0 alpha: 1.0]; }

NSFont* boldFont (CGFloat size)
{
    NSFont* f = [NSFont fontWithName: @"Helvetica-Bold" size: size];
    return f ? f : [NSFont boldSystemFontOfSize: size];
}

NSFont* plainFont (CGFloat size)
{
    NSFont* f = [NSFont fontWithName: @"Helvetica" size: size];
    return f ? f : [NSFont systemFontOfSize: size];
}

NSDictionary* textAttrs (NSFont* font, NSColor* color, CGFloat kern)
{
    return [NSDictionary dictionaryWithObjectsAndKeys:
              font, NSFontAttributeName, color, NSForegroundColorAttributeName,
              [NSNumber numberWithDouble: kern], NSKernAttributeName, nil];
}

NSSize textSize (const char* s, NSFont* font, CGFloat kern = 0.0)
{
    return [[NSString stringWithUTF8String: s] sizeWithAttributes: textAttrs (font, [NSColor blackColor], kern)];
}

// align: 0 centre, -1 left edge at x, +1 right edge at x
void drawText (const char* s, NSFont* font, NSColor* color, CGFloat x, CGFloat cy, int align = 0, CGFloat kern = 0.0)
{
    NSString* str = [NSString stringWithUTF8String: s];
    NSDictionary* attrs = textAttrs (font, color, kern);
    const NSSize sz = [str sizeWithAttributes: attrs];
    const CGFloat w = sz.width - kern;
    const CGFloat x0 = align == 0 ? x - w * 0.5 : (align < 0 ? x : x - w);
    [str drawAtPoint: NSMakePoint (x0, cy - sz.height * 0.5) withAttributes: attrs];
}

// The glow colours: amber for the switches, red for the mains rocker
NSColor* glowColour (bool red) { return red ? rgb (255, 60, 40) : rgb (255, 176, 48); }

} // namespace

//==============================================================================
// Font size for one line in the readout: the usual size, or smaller so the
// whole text fits (a 40-character preset name never needs scrolling)
static CGFloat fitFontSize (const char* s, CGFloat size, CGFloat maxW)
{
    if (! s || ! *s) return size;
    NSString* str = [NSString stringWithUTF8String: s];
    if (! str) return size;
    NSDictionary* a = [NSDictionary dictionaryWithObject: boldFont (size) forKey: NSFontAttributeName];
    const CGFloat w = [str sizeWithAttributes: a].width;
    if (w <= maxW || w <= 0.0) return size;
    const CGFloat f = size * maxW / w;
    return f < 7.0 ? 7.0 : f;
}

@interface TBIC100SPanelView : NSView <NSTextFieldDelegate>
{
    EditorHost* host;
    float shown[kNumParams];
    NSTimer* timer;
    int dragCtl;
    CGFloat dragStartY, dragStartValue;
    int readoutParam;
    bool saveMode;
    int activePreset;
    bool presetUsed[kNumPresetSlots];
    char status[96];
    NSTextField* nameField;   // naming a preset, over the readout
    NSTextField* retiredField;// a finished one, taken away once its own event is over
    int namingSlot;
}
- (id) initWithFrame: (NSRect) frame host: (EditorHost*) h;
- (void) shutdown;
@end

@implementation TBIC100SPanelView

- (id) initWithFrame: (NSRect) frame host: (EditorHost*) h
{
    self = [super initWithFrame: frame];
    if (self)
    {
        host = h;
        for (int i = 0; i < kNumParams; ++i) shown[i] = host->getParam (i);
        dragCtl = -1;
        readoutParam = -1;
        saveMode = false;
        activePreset = -1;
        status[0] = 0;
        nameField = nil;
        retiredField = nil;
        namingSlot = -1;
        host->refreshPresets();
        for (int k = 0; k < kNumPresetSlots; ++k) presetUsed[k] = host->presetExists (k);
        timer = [[NSTimer timerWithTimeInterval: 1.0 / 20.0 target: self selector: @selector (tick:)
                                       userInfo: nil repeats: YES] retain];
        [[NSRunLoop currentRunLoop] addTimer: timer forMode: NSDefaultRunLoopMode];
        [[NSRunLoop currentRunLoop] addTimer: timer forMode: NSEventTrackingRunLoopMode];
    }
    return self;
}

- (void) shutdown
{
    [NSObject cancelPreviousPerformRequestsWithTarget: self];
    namingSlot = -1;
    [self dropRetired];
    if (nameField)
    {
        [nameField setDelegate: nil];
        [nameField setTarget: nil];
        [nameField removeFromSuperview];
        [nameField release];
        nameField = nil;
    }
    if (timer) { [timer invalidate]; [timer release]; timer = nil; }
    host = 0;
}

- (void) dealloc
{
    [self shutdown];
    [super dealloc];
}

- (BOOL) isFlipped { return YES; }
- (BOOL) isOpaque { return YES; }
- (BOOL) acceptsFirstMouse: (NSEvent*) e { (void) e; return YES; }

// Host-side changes (automation, presets) redraw the panel.
- (void) tick: (NSTimer*) t
{
    (void) t;
    if (! host) return;
    bool changed = false;
    for (int i = 0; i < kNumParams; ++i)
    {
        const float v = host->getParam (i);
        if (v != shown[i]) { shown[i] = v; changed = true; }
    }
    if (changed) [self setNeedsDisplay: YES];
}

//------------------------------------------------------------------------------
// Drawing

- (void) drawKnobScale: (const Ctl&) c
{
    NSColor* ink = [NSColor blackColor];
    for (int i = 0; i <= 10; ++i)
    {
        const CGFloat a = valueAngle (i / 10.0);
        NSBezierPath* t = [NSBezierPath bezierPath];
        [t moveToPoint: polar (c.cx, c.cy, c.r + 4.0, a)];
        [t lineToPoint: polar (c.cx, c.cy, c.r + (i % 5 == 0 ? 10.0 : 8.0), a)];
        [t setLineWidth: i % 5 == 0 ? 1.5 : 1.1];
        [ink set];
        [t stroke];
        char num[4];
        std::snprintf (num, sizeof (num), "%d", i);
        const NSPoint p = polar (c.cx, c.cy, c.r + 16.0, a);
        drawText (num, plainFont (8.5), ink, p.x, p.y);
    }
}

// The faceplate, lettering, scales and the strip
- (void) drawBackground
{
    const NSRect b = [self bounds];

    NSColor* ink = [NSColor blackColor];

    // Opaque silver faceplate: the knob band, a ledge, the switch band a shade darker
    NSGradient* face = [[[NSGradient alloc] initWithStartingColor: rgb (222, 224, 227)
                                                      endingColor: rgb (190, 194, 198)] autorelease];
    [face drawInRect: NSMakeRect (0, 0, b.size.width, kLedgeY) angle: 90.0];
    NSGradient* low = [[[NSGradient alloc] initWithStartingColor: rgb (212, 214, 217)
                                                     endingColor: rgb (182, 186, 190)] autorelease];
    [low drawInRect: NSMakeRect (0, kLedgeY, b.size.width, kStripTop - kLedgeY) angle: 90.0];
    // Brushed grain
    unsigned seed = 1100u;
    for (int k = 0; k < 260; ++k)
    {
        seed = seed * 1664525u + 1013904223u; const CGFloat y = (seed >> 8) / 16777216.0 * kStripTop;
        seed = seed * 1664525u + 1013904223u; const double q = (seed >> 8) / 16777216.0;
        [[NSColor colorWithCalibratedWhite: q < 0.5 ? 1.0 : 0.0 alpha: 0.025 + 0.03 * q] set];
        NSRectFillUsingOperation (NSMakeRect (0, y, b.size.width, 1.0), NSCompositeSourceOver);
    }
    // The ledge between the bands: shadow under the upper band, highlight on the lower
    [rgb (96, 98, 102) set];
    NSRectFill (NSMakeRect (0, kLedgeY - 1.0, b.size.width, 1.5));
    [rgb (244, 245, 247) set];
    NSRectFill (NSMakeRect (0, kLedgeY + 0.5, b.size.width, 1.0));
    [[NSColor colorWithCalibratedWhite: 0.0 alpha: 0.10] set];
    NSRectFillUsingOperation (NSMakeRect (0, kLedgeY + 1.5, b.size.width, 3.0), NSCompositeSourceOver);

    // Fine border around the faceplate
    NSBezierPath* border = [NSBezierPath bezierPathWithRect: NSInsetRect (NSMakeRect (0, 0, b.size.width, kStripTop), 5.5, 5.5)];
    [rgb (70, 72, 75) set];
    [border setLineWidth: 1.0];
    [border stroke];

    // Channel titles and knob names
    drawText ("CHANNEL 1", boldFont (12.0), ink, 346.0, 19.0, 0, 1.0);
    drawText ("CHANNEL 2", boldFont (12.0), ink, 822.0, 19.0, 0, 1.0);
    {
        NSBezierPath* div = [NSBezierPath bezierPath];
        [div moveToPoint: NSMakePoint (606.5, 22.0)];
        [div lineToPoint: NSMakePoint (606.5, 132.0)];
        [rgb (60, 62, 65) set];
        [div setLineWidth: 1.0];
        [div stroke];
    }
    for (int i = 0; i < kNumCtls; ++i)
    {
        const Ctl& c = kCtls[i];
        if (c.type != kCtlKnob) continue;
        drawText (c.label, boldFont (10.0), ink, c.cx, 40.0, 0, 0.4);
        [self drawKnobScale: c];
    }

    // The model name at the right, in plain lettering
    drawText ("IC100-S", boldFont (24.0), ink, 1108.0, 84.0, 0, 1.0);
    {
        NSBezierPath* rule = [NSBezierPath bezierPath];
        [rule moveToPoint: NSMakePoint (1060.0, 102.5)];
        [rule lineToPoint: NSMakePoint (1156.0, 102.5)];
        [rgb (60, 62, 65) set];
        [rule setLineWidth: 1.0];
        [rule stroke];
    }

    // Switch row: the TB electronic logo where the amp has its maker's mark
    tblogo::drawTBLogo (NSMakeRect (16.0, 160.0, 104.0, 60.0));
    drawText ("PROFESSIONAL", boldFont (9.0), ink, 128.0, 186.0, -1, 0.6);
    drawText ("POWER AMPLIFIER", boldFont (9.0), ink, 128.0, 199.0, -1, 0.6);

    // Switch names
    for (int i = 0; i < kNumCtls; ++i)
    {
        const Ctl& c = kCtls[i];
        if (c.cy > kStripTop || c.label[0] == 0) continue;
        if (c.type == kCtlRocker)
            drawText (c.label, boldFont (9.5), ink, c.cx, c.cy - kRockerH * 0.5 - 13.0, 0, 0.4);
        else if (c.type == kCtlSlide)
            drawText (c.label, boldFont (9.5), ink, c.cx, c.cy - kSlideHalfH - 14.0, 0, 0.4);
    }

    // Plugin strip, a slightly darker silver
    NSGradient* strip = [[[NSGradient alloc] initWithStartingColor: rgb (196, 199, 203)
                                                       endingColor: rgb (166, 170, 175)] autorelease];
    [strip drawInRect: NSMakeRect (0, kStripTop, b.size.width, b.size.height - kStripTop) angle: 90.0];
    [rgb (40, 40, 42) set];
    NSRectFill (NSMakeRect (0, kStripTop, b.size.width, 2.0));
    [rgb (238, 240, 242) set];
    NSRectFill (NSMakeRect (0, kStripTop + 2.0, b.size.width, 1.0));
}

// Grey knurled knob with a red position line.
- (void) drawKnobAt: (NSPoint) ctr radius: (CGFloat) r value: (CGFloat) v
{
    [NSGraphicsContext saveGraphicsState];
    NSShadow* sh = [[[NSShadow alloc] init] autorelease];
    [sh setShadowOffset: NSMakeSize (1.5, -2.5)];
    [sh setShadowBlurRadius: 4.0];
    [sh setShadowColor: [NSColor colorWithCalibratedWhite: 0.0 alpha: 0.45]];
    [sh set];
    [rgb (90, 93, 97) set];
    [[NSBezierPath bezierPathWithOvalInRect: NSMakeRect (ctr.x - r, ctr.y - r, 2 * r, 2 * r)] fill];
    [NSGraphicsContext restoreGraphicsState];

    const int ridges = 40;
    for (int k = 0; k < ridges; ++k)
    {
        const CGFloat a0 = (CGFloat) (2.0 * M_PI * k / ridges);
        const CGFloat a1 = (CGFloat) (2.0 * M_PI * (k + 0.5) / ridges);
        NSBezierPath* w = [NSBezierPath bezierPath];
        [w moveToPoint: ctr];
        [w lineToPoint: polar (ctr.x, ctr.y, r, a0)];
        [w lineToPoint: polar (ctr.x, ctr.y, r, a1)];
        [w closePath];
        const CGFloat lit = 0.5 + 0.5 * std::cos (a0 + 0.8);
        [[NSColor colorWithCalibratedWhite: 0.40 + 0.22 * lit alpha: 1.0] set];
        [w fill];
    }

    const CGFloat rc = r * 0.78;
    NSGradient* cap = [[[NSGradient alloc] initWithStartingColor: rgb (172, 175, 179)
                                                     endingColor: rgb (104, 107, 111)] autorelease];
    NSBezierPath* capPath = [NSBezierPath bezierPathWithOvalInRect: NSMakeRect (ctr.x - rc, ctr.y - rc, 2 * rc, 2 * rc)];
    [cap drawInBezierPath: capPath angle: -60.0];
    [rgb (70, 72, 75) set];
    [capPath setLineWidth: 0.8];
    [capPath stroke];

    const CGFloat a = valueAngle (v);
    NSBezierPath* line = [NSBezierPath bezierPath];
    [line moveToPoint: polar (ctr.x, ctr.y, r * 0.18, a)];
    [line lineToPoint: polar (ctr.x, ctr.y, r * 0.98, a)];
    [line setLineWidth: r > 18.0 ? 3.0 : 2.4];
    [line setLineCapStyle: NSRoundLineCapStyle];
    [rgb (200, 30, 30) set];
    [line stroke];
}

// A legend that glows when its side of the switch is engaged, grey when not.
- (void) drawLegend: (const char*) s x: (CGFloat) x y: (CGFloat) y align: (int) align lit: (bool) lit red: (bool) red
{
    [NSGraphicsContext saveGraphicsState];
    if (lit)
    {
        NSShadow* g = [[[NSShadow alloc] init] autorelease];
        [g setShadowOffset: NSMakeSize (0.0, 0.0)];
        [g setShadowBlurRadius: 6.0];
        [g setShadowColor: glowColour (red)];
        [g set];
        drawText (s, boldFont (9.5), [NSColor blackColor], x, y, align, 0.3);
        drawText (s, boldFont (9.5), [NSColor blackColor], x, y, align, 0.3);
    }
    else
        drawText (s, plainFont (9.0), rgb (118, 120, 124), x, y, align, 0.3);
    [NSGraphicsContext restoreGraphicsState];
}

// Lights the engaged part of a switch from inside, with a soft glow round it.
- (void) fillLit: (NSBezierPath*) p red: (bool) red vertical: (bool) vertical
{
    [NSGraphicsContext saveGraphicsState];
    NSShadow* glow = [[[NSShadow alloc] init] autorelease];
    [glow setShadowOffset: NSMakeSize (0.0, 0.0)];
    [glow setShadowBlurRadius: 12.0];
    [glow setShadowColor: glowColour (red)];
    [glow set];
    [glowColour (red) set];
    [p fill];
    [p fill];
    [NSGraphicsContext restoreGraphicsState];
    NSGradient* g = red
        ? [[[NSGradient alloc] initWithColorsAndLocations:
              rgb (255, 210, 196), 0.0, rgb (255, 96, 70), 0.35, rgb (224, 34, 24), 0.8, rgb (176, 20, 14), 1.0, nil] autorelease]
        : [[[NSGradient alloc] initWithColorsAndLocations:
              rgb (255, 244, 214), 0.0, rgb (255, 206, 110), 0.35, rgb (244, 160, 36), 0.8, rgb (206, 124, 18), 1.0, nil] autorelease];
    [g drawInBezierPath: p angle: vertical ? 90.0 : -90.0];
    [(red ? rgb (110, 12, 8) : rgb (120, 70, 8)) set];
    [p setLineWidth: 0.8];
    [p stroke];
}

// Rocker switch, vertical: press the top half for ON (Bright, Ch On, Mains On).
// The engaged half is raised and lit; the other half is dark.
- (void) drawRocker: (const Ctl&) c
{
    const bool on = isOn (shown[c.param]);
    const CGFloat w = kRockerW, h = kRockerH;
    const NSRect bezel = NSMakeRect (c.cx - w * 0.5 - 4, c.cy - h * 0.5 - 4, w + 8, h + 8);
    NSBezierPath* bz = [NSBezierPath bezierPathWithRoundedRect: bezel xRadius: 4 yRadius: 4];
    [NSGraphicsContext saveGraphicsState];
    NSShadow* sh = [[[NSShadow alloc] init] autorelease];
    [sh setShadowOffset: NSMakeSize (1.0, -2.0)];
    [sh setShadowBlurRadius: 3.0];
    [sh setShadowColor: [NSColor colorWithCalibratedWhite: 0.0 alpha: 0.45]];
    [sh set];
    [rgb (30, 31, 33) set];
    [bz fill];
    [NSGraphicsContext restoreGraphicsState];

    const NSRect top = NSMakeRect (c.cx - w * 0.5, c.cy - h * 0.5, w, h * 0.5);
    const NSRect bot = NSMakeRect (c.cx - w * 0.5, c.cy, w, h * 0.5);
    const NSRect litR = on ? NSInsetRect (top, -1.0, -1.0) : NSInsetRect (bot, -1.0, -1.0);
    const NSRect darkR = on ? bot : top;

    NSGradient* dark = [[[NSGradient alloc] initWithStartingColor: rgb (38, 39, 41) endingColor: rgb (58, 60, 63)] autorelease];
    [dark drawInRect: darkR angle: on ? 90.0 : -90.0];
    [self fillLit: [NSBezierPath bezierPathWithRoundedRect: litR xRadius: 2 yRadius: 2] red: c.red vertical: true];

    // Markings: a bar on the top (on) half, a ring on the bottom (off) half
    [(on ? rgb (90, 50, 6) : rgb (150, 152, 156)) set];
    if (c.red && on) [rgb (90, 10, 6) set];
    NSRectFill (NSMakeRect (c.cx - 1.0, c.cy - h * 0.5 + 4, 2.0, 8.0));
    [(on ? rgb (150, 152, 156) : (c.red ? rgb (90, 10, 6) : rgb (90, 50, 6))) set];
    NSBezierPath* ring = [NSBezierPath bezierPathWithOvalInRect: NSMakeRect (c.cx - 3.5, c.cy + h * 0.5 - 12, 7, 7)];
    [ring setLineWidth: 1.5];
    [ring stroke];

    // Legends to the right: the top one for ON, the bottom one for OFF
    const CGFloat lx = c.cx + w * 0.5 + 8.0;
    [self drawLegend: c.posB x: lx y: c.cy - h * 0.25 align: -1 lit: on red: c.red];
    [self drawLegend: c.posA x: lx y: c.cy + h * 0.25 align: -1 lit: ! on red: c.red];
}

// Two-position slide switch: the lever's engaged half is raised and lit.
// Right = on (Sustain on, Stage).
- (void) drawSlide: (const Ctl&) c
{
    const bool right = isOn (shown[c.param]);
    const NSRect slot = NSMakeRect (c.cx - kSlideHalfW, c.cy - kSlideHalfH, 2 * kSlideHalfW, 2 * kSlideHalfH);
    NSBezierPath* sp = [NSBezierPath bezierPathWithRoundedRect: slot xRadius: 3 yRadius: 3];
    [rgb (20, 20, 22) set];
    [sp fill];
    [rgb (80, 82, 86) set];
    [sp setLineWidth: 1.0];
    [sp stroke];

    const NSRect lever = NSInsetRect (slot, 3.0, 3.0);
    const CGFloat half = lever.size.width * 0.5;
    const NSRect flat = right ? NSMakeRect (lever.origin.x, lever.origin.y, half, lever.size.height)
                              : NSMakeRect (lever.origin.x + half, lever.origin.y, half, lever.size.height);
    const NSRect raised = right ? NSMakeRect (lever.origin.x + half - 2.0, lever.origin.y - 1.5, half + 2.0, lever.size.height + 3.0)
                                : NSMakeRect (lever.origin.x, lever.origin.y - 1.5, half + 2.0, lever.size.height + 3.0);
    NSGradient* fg = [[[NSGradient alloc] initWithStartingColor: rgb (56, 58, 61) endingColor: rgb (36, 37, 40)] autorelease];
    [fg drawInRect: flat angle: 90.0];
    [self fillLit: [NSBezierPath bezierPathWithRoundedRect: raised xRadius: 2 yRadius: 2] red: false vertical: false];
    // Grip ridges on the raised half
    [[NSColor colorWithCalibratedWhite: 0.0 alpha: 0.25] set];
    for (int i = -1; i <= 1; ++i)
        NSRectFillUsingOperation (NSMakeRect (NSMidX (raised) + i * 4.0 - 0.5, NSMinY (raised) + 4.0, 1.0, raised.size.height - 8.0), NSCompositeSourceOver);

    [self drawLegend: c.posA x: c.cx - kSlideHalfW - 7.0 y: c.cy align: 1 lit: ! right red: false];
    [self drawLegend: c.posB x: c.cx + kSlideHalfW + 7.0 y: c.cy align: -1 lit: right red: false];
}

- (void) drawReadout
{
    const NSRect box = kReadoutRect;
    NSBezierPath* p = [NSBezierPath bezierPathWithRoundedRect: box xRadius: 4 yRadius: 4];
    NSGradient* glow = [[[NSGradient alloc] initWithStartingColor: rgb (206, 255, 120)
                                                      endingColor: rgb (150, 232, 70)] autorelease];
    [glow drawInBezierPath: p relativeCenterPosition: NSMakePoint (0.0, 0.0)];
    [rgb (60, 62, 65) set];
    [p setLineWidth: 1.0];
    [p stroke];

    if (status[0])
        drawText (status, boldFont (fitFontSize (status, 16.0, NSWidth (kReadoutRect) - 16.0)), [NSColor blackColor], NSMidX (box), NSMidY (box));
    else if (readoutParam >= 0 && host)
    {
        char disp[16] = { 0 };
        host->getParamDisplay (readoutParam, disp);
        const char* name = 0;
        const char* chan = "";
        for (int i = 0; i < kNumCtls; ++i)
            if (kCtls[i].param == readoutParam) { name = kCtls[i].label; break; }
        if (readoutParam == kCh1Treble || readoutParam == kCh1Bass || readoutParam == kCh1Volume) chan = "CH 1 ";
        if (readoutParam == kCh2Treble || readoutParam == kCh2Bass || readoutParam == kCh2Volume) chan = "CH 2 ";
        if (readoutParam == kStage) name = "STUDIO / STAGE";
        if (readoutParam == kSustainOn) name = "SUSTAIN SWITCH";
        // Upper-case the value ("Bright" -> "BRIGHT")
        for (char* q = disp; *q; ++q) if (*q >= 'a' && *q <= 'z') *q = (char) (*q - 32);
        const int kind = kParamKind[readoutParam];
        const char* unit = (kind == kDb12 || kind == kDb24) ? " dB" : "";
        char text[96];
        std::snprintf (text, sizeof (text), "%s%s   %s%s", chan, name ? name : "", disp, unit);
        drawText (text, boldFont (fitFontSize (text, 16.0, NSWidth (kReadoutRect) - 16.0)), [NSColor blackColor], NSMidX (box), NSMidY (box));
    }
}

- (void) drawPresets
{
    NSColor* ink = [NSColor blackColor];
    drawText ("PRESETS", boldFont (10.0), ink, (NSMinX (kSaveRect) + NSMaxX (slotRect (kNumPresetSlots - 1))) * 0.5, kStripLabelY);

    NSBezierPath* sp = [NSBezierPath bezierPathWithRoundedRect: kSaveRect xRadius: 3 yRadius: 3];
    if (saveMode)
    {
        [rgb (200, 30, 30) set];
        [sp fill];
    }
    else
    {
        NSGradient* g = [[[NSGradient alloc] initWithStartingColor: rgb (232, 234, 236) endingColor: rgb (160, 163, 167)] autorelease];
        [g drawInBezierPath: sp angle: -90.0];
    }
    [rgb (40, 41, 43) set];
    [sp setLineWidth: 1.0];
    [sp stroke];
    drawText ("SAVE", boldFont (11.0), saveMode ? [NSColor whiteColor] : ink, NSMidX (kSaveRect), NSMidY (kSaveRect));

    for (int k = 0; k < kNumPresetSlots; ++k)
    {
        const NSRect r = slotRect (k);
        NSBezierPath* bp = [NSBezierPath bezierPathWithRoundedRect: r xRadius: 3 yRadius: 3];
        NSColor* fill = (k == activePreset) ? rgb (178, 245, 96) : (presetUsed[k] ? rgb (46, 47, 50) : rgb (214, 216, 219));
        NSColor* text = (k == activePreset) ? ink : (presetUsed[k] ? [NSColor whiteColor] : rgb (120, 122, 126));
        [fill set];
        [bp fill];
        [rgb (40, 41, 43) set];
        [bp setLineWidth: 1.0];
        [bp stroke];
        char num[4];
        std::snprintf (num, sizeof (num), "%d", k + 1);
        drawText (num, boldFont (12.0), text, NSMidX (r), NSMidY (r));
    }
}

- (void) drawRect: (NSRect) dirty
{
    (void) dirty;
    [self drawBackground];
    NSColor* ink = [NSColor blackColor];

    for (int i = 0; i < kNumCtls; ++i)
    {
        const Ctl& c = kCtls[i];
        switch (c.type)
        {
            case kCtlKnob:
                [self drawKnobAt: NSMakePoint (c.cx, c.cy) radius: c.r value: shown[c.param]];
                break;
            case kCtlRocker:
                [self drawRocker: c];
                if (c.cy > kStripTop)
                    drawText (c.label, boldFont (10.0), ink, c.cx, kStripLabelY);
                break;
            case kCtlSlide:
                [self drawSlide: c];
                break;
            case kCtlSmallKnob:
                [self drawKnobAt: NSMakePoint (c.cx, c.cy) radius: c.r value: shown[c.param]];
                drawText (c.label, boldFont (10.0), ink, c.cx, kStripLabelY);
                drawText (c.posA, plainFont (9.0), ink, c.cx - c.r - 2.0, c.cy + c.r + 9.0);
                drawText (c.posB, plainFont (9.0), ink, c.cx + c.r + 2.0, c.cy + c.r + 9.0);
                break;
            default: break;
        }
    }
    [self drawPresets];
    [self drawReadout];
}

//------------------------------------------------------------------------------
// Mouse

- (int) ctlAtPoint: (NSPoint) p
{
    for (int i = 0; i < kNumCtls; ++i)
    {
        const Ctl& c = kCtls[i];
        if (c.type == kCtlRocker)
        {
            if (NSPointInRect (p, NSMakeRect (c.cx - kRockerW * 0.5 - 5, c.cy - kRockerH * 0.5 - 5, kRockerW + 10, kRockerH + 10)))
                return i;
            continue;
        }
        if (c.type == kCtlSlide)
        {
            if (NSPointInRect (p, NSMakeRect (c.cx - kSlideHalfW - 4, c.cy - kSlideHalfH - 5, 2 * kSlideHalfW + 8, 2 * kSlideHalfH + 10)))
                return i;
            continue;
        }
        const CGFloat dx = p.x - c.cx, dy = p.y - c.cy, reach = c.r + 6.0;
        if (dx * dx + dy * dy < reach * reach) return i;
    }
    return -1;
}

- (void) setParam: (int) param value: (float) v
{
    if (! host) return;
    if (v < 0.0f) v = 0.0f;
    if (v > 1.0f) v = 1.0f;
    host->guiSetParam (param, v);
    shown[param] = host->getParam (param);
    readoutParam = param;
    [self setNeedsDisplay: YES];
}

// SAVE button and preset slots. Returns YES if the click was handled here.
//------------------------------------------------------------------------------
// Preset names: after a save, or a click on the readout while a preset is in
// use, the readout becomes a text box. Return keeps the name, Esc leaves the
// old one, a click elsewhere keeps what was typed. The text shrinks to fit.

- (void) showPreset: (int) slot verb: (const char*) verb
{
    char name[kMaxPresetName + 8] = { 0 };
    if (host) host->presetName (slot, name, (int) sizeof (name));
    if (name[0]) std::snprintf (status, sizeof (status), "%d   %s", slot + 1, name);
    else         std::snprintf (status, sizeof (status), "PRESET %d %s", slot + 1, verb);
    [self setNeedsDisplay: YES];
}

- (void) dropRetired
{
    if (! retiredField) return;
    [retiredField setDelegate: nil];
    [retiredField setTarget: nil];
    [retiredField removeFromSuperview];
    [retiredField release];
    retiredField = nil;
    if (! nameField && [self window]) [[self window] makeFirstResponder: self];
    [self setNeedsDisplay: YES];
}

// Size the name box's font so everything typed (or the hint) fits
- (void) fitNameField
{
    if (! nameField) return;
    const CGFloat maxW = NSWidth ([nameField frame]) - 10.0;
    NSString* s = [nameField stringValue];
    if ([s length] == 0) s = [[nameField cell] placeholderString];
    const char* u = s ? [s UTF8String] : "";
    [nameField setFont: boldFont (fitFontSize (u, 16.0, maxW))];
}

- (void) startNaming: (int) slot
{
    if (! host || slot < 0) return;
    if (nameField) [self finishNaming: YES];
    namingSlot = slot;
    char name[kMaxPresetName + 8] = { 0 };
    host->presetName (slot, name, (int) sizeof (name));

    const CGFloat fh = (CGFloat) (16.0 * 1.35 + 4.0);
    const NSRect r = kReadoutRect;
    nameField = [[NSTextField alloc] initWithFrame: NSMakeRect (NSMinX (r) + 6.0, NSMidY (r) - fh * 0.5,
                                                                 NSWidth (r) - 12.0, fh)];
    [nameField setBezeled: NO];
    [nameField setBordered: NO];
    [nameField setDrawsBackground: YES];
    [nameField setBackgroundColor: rgb (190, 248, 100)];
    [nameField setTextColor: [NSColor blackColor]];
    [nameField setAlignment: NSCenterTextAlignment];
    [nameField setFocusRingType: NSFocusRingTypeNone];
    [nameField setStringValue: [NSString stringWithUTF8String: name]];
    char hint[64];
    std::snprintf (hint, sizeof (hint), "NAME PRESET %d, THEN RETURN", slot + 1);
    [[nameField cell] setPlaceholderString: [NSString stringWithUTF8String: hint]];
    [[nameField cell] setScrollable: YES];
    [nameField setTarget: self];
    [nameField setAction: @selector (nameEntered:)];
    [nameField setDelegate: self];
    [self fitNameField];
    [self addSubview: nameField];
    status[0] = 0;
    readoutParam = -1;
    [[self window] makeFirstResponder: nameField];
    [self setNeedsDisplay: YES];
}

- (void) finishNaming: (BOOL) keep
{
    const int slot = namingSlot;
    if (slot < 0) return;
    namingSlot = -1;                       // only once (Return, then end of editing)
    if (keep && host && nameField)
    {
        const char* utf8 = [[nameField stringValue] UTF8String];
        host->renamePreset (slot, utf8 ? utf8 : "");
    }
    [self showPreset: slot verb: "SAVED"];
    // the field may be in the middle of sending its action: take it away afterwards
    [self dropRetired];
    retiredField = nameField;
    nameField = nil;
    [retiredField setDelegate: nil];
    [retiredField setTarget: nil];
    [self performSelector: @selector (dropRetired) withObject: nil afterDelay: 0.0];
}

- (void) nameEntered: (id) sender
{
    (void) sender;
    [self finishNaming: YES];
}

- (void) controlTextDidChange: (NSNotification*) n
{
    (void) n;
    [self fitNameField];
}

- (void) controlTextDidEndEditing: (NSNotification*) n
{
    (void) n;
    [self finishNaming: YES];
}

- (BOOL) control: (NSControl*) c textView: (NSTextView*) tv doCommandBySelector: (SEL) sel
{
    (void) c; (void) tv;
    if (sel == @selector (cancelOperation:)) { [self finishNaming: NO]; return YES; }
    return NO;
}

- (BOOL) presetClick: (NSPoint) p
{
    if (NSPointInRect (p, kSaveRect))
    {
        saveMode = ! saveMode;
        std::snprintf (status, sizeof (status), "%s", saveMode ? "SAVE: CLICK A SLOT 1-8" : "");
        [self setNeedsDisplay: YES];
        return YES;
    }
    for (int k = 0; k < kNumPresetSlots; ++k)
    {
        if (! NSPointInRect (p, slotRect (k))) continue;
        if (saveMode)
        {
            const bool ok = host->savePreset (k);
            saveMode = false;
            host->refreshPresets();
            for (int j = 0; j < kNumPresetSlots; ++j) presetUsed[j] = host->presetExists (j);
            if (ok)
            {
                activePreset = k;
                std::snprintf (status, sizeof (status), "PRESET %d SAVED", k + 1);
                [self setNeedsDisplay: YES];
                [self startNaming: k];
                return YES;
            }
            std::snprintf (status, sizeof (status), "COULD NOT SAVE PRESET %d", k + 1);
        }
        else
        {
            host->refreshPresets();
            for (int j = 0; j < kNumPresetSlots; ++j) presetUsed[j] = host->presetExists (j);
            if (presetUsed[k] && host->loadPreset (k))
            {
                for (int j = 0; j < kNumParams; ++j) shown[j] = host->getParam (j);
                activePreset = k;
                [self showPreset: k verb: "LOADED"];
            }
            else
                std::snprintf (status, sizeof (status), "PRESET %d IS EMPTY", k + 1);
        }
        [self setNeedsDisplay: YES];
        return YES;
    }
    return NO;
}

- (void) mouseDown: (NSEvent*) e
{
    const NSPoint p = [self convertPoint: [e locationInWindow] fromView: nil];
    if (! host) return;
    if (nameField) [self finishNaming: YES];
    if (NSPointInRect (p, kReadoutRect) && activePreset >= 0)
    {
        [self startNaming: activePreset];
        return;
    }
    if ([self presetClick: p]) return;
    const int i = [self ctlAtPoint: p];
    if (i < 0) return;
    const Ctl& c = kCtls[i];
    shown[c.param] = host->getParam (c.param);   // the live value, even between timer ticks
    readoutParam = c.param;
    status[0] = 0;
    activePreset = -1;     // settings are being changed away from the preset

    if (c.type == kCtlRocker || c.type == kCtlSlide)
    {
        // Rocker: press the top half for on, the bottom half for off.
        // Slide: click a side to move it there.
        const float v = c.type == kCtlRocker ? (p.y < c.cy ? 1.0f : 0.0f) : (p.x >= c.cx ? 1.0f : 0.0f);
        host->guiBeginEdit (c.param);
        [self setParam: c.param value: v];
        host->guiEndEdit (c.param);
        return;
    }

    host->guiBeginEdit (c.param);
    if ([e clickCount] == 2)
        [self setParam: c.param value: kDefaults[c.param]];   // double-click: back to default
    dragCtl = i;
    dragStartY = p.y;
    dragStartValue = shown[c.param];
    [self setNeedsDisplay: YES];
}

- (void) mouseDragged: (NSEvent*) e
{
    if (dragCtl < 0 || ! host) return;
    const Ctl& c = kCtls[dragCtl];
    const NSPoint p = [self convertPoint: [e locationInWindow] fromView: nil];
    const bool fine = ([e modifierFlags] & NSShiftKeyMask) != 0;
    const CGFloat perPixel = fine ? 1.0 / 1000.0 : 1.0 / 200.0;
    const CGFloat v = dragStartValue + (dragStartY - p.y) * perPixel;
    [self setParam: c.param value: (float) v];
}

- (void) mouseUp: (NSEvent*) e
{
    (void) e;
    if (dragCtl >= 0 && host) host->guiEndEdit (kCtls[dragCtl].param);
    dragCtl = -1;
}

- (void) scrollWheel: (NSEvent*) e
{
    const NSPoint p = [self convertPoint: [e locationInWindow] fromView: nil];
    const int i = [self ctlAtPoint: p];
    if (i < 0 || ! host) return;
    const Ctl& c = kCtls[i];
    if (c.type != kCtlKnob && c.type != kCtlSmallKnob) return;
    const CGFloat d = [e deltaY];
    if (d == 0.0) return;
    status[0] = 0;
    activePreset = -1;
    host->guiBeginEdit (c.param);
    [self setParam: c.param value: (float) (shown[c.param] + d * 0.01)];
    host->guiEndEdit (c.param);
}

@end

//==============================================================================
namespace tbic
{

// Both calls use their own autorelease pool, so nothing of this plugin's is
// left waiting in the host's pool when the editor closes or the plugin unloads.
void* editorOpen (void* parentView, EditorHost* host)
{
    NSView* parent = (NSView*) parentView;
    if (! parent || ! host) return 0;
    NSAutoreleasePool* pool = [[NSAutoreleasePool alloc] init];
    TBIC100SPanelView* v = [[TBIC100SPanelView alloc]
                              initWithFrame: NSMakeRect (0, 0, kEditorWidth, kEditorHeight) host: host];
    if (v) [parent addSubview: v];
    [pool release];
    return (void*) v;    // retained until editorClose
}

void editorClose (void* handle)
{
    TBIC100SPanelView* v = (TBIC100SPanelView*) handle;
    if (! v) return;
    NSAutoreleasePool* pool = [[NSAutoreleasePool alloc] init];
    [v shutdown];
    [v removeFromSuperview];
    [v release];
    [pool release];
}

} // namespace tbic
