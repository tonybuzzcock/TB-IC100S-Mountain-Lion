// editor_test.mm  (macOS only)
// Loads the built plugin, opens its panel in an offscreen window the way a host
// does, renders screenshots, drives the controls with synthetic mouse events,
// checks host automation messages, and closes it again.
//
//   editor_test <plugin binary> <output dir for PNGs>

#import <Cocoa/Cocoa.h>

#include "../Source/VST2/vst2_abi.h"
#include "../Source/VST2/Params.h"
#include "../Source/VST2/Editor.h"

#include <dlfcn.h>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <algorithm>
#include <vector>

static int failures = 0;
static void check (bool ok, const char* what)
{
    std::printf ("  %s  %s\n", ok ? "ok  " : "FAIL", what);
    if (! ok) ++failures;
}

static int automateCalls = 0, beginCalls = 0, endCalls = 0;
static VstIntPtr VST_CALL hostCallback (AEffect*, VstInt32 opcode, VstInt32, VstIntPtr, void*, float)
{
    switch (opcode)
    {
        case audioMasterVersion:   return 2400;
        case audioMasterAutomate:  ++automateCalls; return 0;
        case audioMasterBeginEdit: ++beginCalls; return 1;
        case audioMasterEndEdit:   ++endCalls; return 1;
        default:                   return 0;
    }
}

typedef AEffect* (*EntryProc) (audioMasterCallback);

static VstIntPtr call (AEffect* e, VstInt32 op, VstInt32 idx = 0, VstIntPtr val = 0, void* ptr = 0, float opt = 0.0f)
{
    return e->dispatcher (e, op, idx, val, ptr, opt);
}

static NSData* renderPNG (NSView* v, const std::string& path)
{
    const NSRect b = [v bounds];
    NSBitmapImageRep* rep = [v bitmapImageRepForCachingDisplayInRect: b];
    [v cacheDisplayInRect: b toBitmapImageRep: rep];
    NSData* png = [rep representationUsingType: NSPNGFileType properties: [NSDictionary dictionary]];
    [png writeToFile: [NSString stringWithUTF8String: path.c_str()] atomically: YES];
    std::printf ("  wrote %s (%lu bytes)\n", path.c_str(), (unsigned long) [png length]);
    return png;
}

// Send a mouse event at a point in the panel's (flipped) coordinates.
static void mouse (NSWindow* w, NSView* v, NSEventType type, NSPoint panelPt, int clicks = 1)
{
    const NSPoint wp = [v convertPoint: panelPt toView: nil];
    NSEvent* e = [NSEvent mouseEventWithType: type location: wp modifierFlags: 0 timestamp: 0
                                windowNumber: [w windowNumber] context: nil eventNumber: 0
                                  clickCount: clicks pressure: 1.0f];
    if (type == NSLeftMouseDown) [v mouseDown: e];
    else if (type == NSLeftMouseDragged) [v mouseDragged: e];
    else [v mouseUp: e];
}

static void pump (double secs)
{
    [[NSRunLoop currentRunLoop] runUntilDate: [NSDate dateWithTimeIntervalSinceNow: secs]];
}

int main (int argc, char** argv)
{
    if (argc < 3) { std::printf ("usage: %s <plugin binary> <png dir>\n", argv[0]); return 2; }
    setvbuf (stdout, 0, _IONBF, 0);   // unbuffered, so a crash still shows how far it got
    NSAutoreleasePool* pool = [[NSAutoreleasePool alloc] init];
    [NSApplication sharedApplication];
    const std::string dir = argv[2];

    // Keep the test's presets away from any real ones
    const std::string presetFile = dir + "/test_presets/Presets.txt";
    setenv ("TBIC_PRESET_FILE", presetFile.c_str(), 1);
    std::remove (presetFile.c_str());

    void* lib = dlopen (argv[1], RTLD_NOW | RTLD_LOCAL);
    if (! lib) { std::printf ("dlopen failed: %s\n", dlerror()); return 1; }
    EntryProc entry = (EntryProc) dlsym (lib, "VSTPluginMain");
    if (! entry) { std::printf ("no entry point\n"); return 1; }
    AEffect* e = entry (hostCallback);
    call (e, effOpen);
    call (e, effSetSampleRate, 0, 0, 0, 44100.0f);
    call (e, effMainsChanged, 0, 1);

    using namespace tbic;
    std::printf ("Editor\n");
    check ((e->flags & effFlagsHasEditor) != 0, "plugin reports a custom editor");
    ERect* r = 0;
    call (e, effEditGetRect, 0, 0, &r);
    check (r && r->right - r->left == kEditorWidth && r->bottom - r->top == kEditorHeight,
           "editor size as declared");

    NSWindow* win = [[NSWindow alloc] initWithContentRect: NSMakeRect (0, 0, kEditorWidth, kEditorHeight)
                                                styleMask: NSBorderlessWindowMask
                                                  backing: NSBackingStoreBuffered defer: NO];
    NSView* parent = [win contentView];
    check (call (e, effEditOpen, 0, 0, parent) == 1, "effEditOpen succeeds");
    check ([[parent subviews] count] == 1, "panel view added to the host's view");
    NSView* panel = [[parent subviews] count] ? [[parent subviews] objectAtIndex: 0] : nil;
    if (! panel) return 1;
    check (NSEqualSizes ([panel frame].size, NSMakeSize (kEditorWidth, kEditorHeight)), "panel has the declared size");

    NSData* pngDefault = renderPNG (panel, dir + "/panel_default.png");
    check ([pngDefault length] > 10000, "panel renders");

    std::printf ("\nControls\n");
    // Drag the Ch 1 VOLUME knob down 50 px: -0.25
    e->setParameter (e, kCh1Volume, 0.5f);
    const float g0 = e->getParameter (e, kCh1Volume);
    automateCalls = beginCalls = endCalls = 0;
    mouse (win, panel, NSLeftMouseDown,    NSMakePoint (522, 92));
    mouse (win, panel, NSLeftMouseDragged, NSMakePoint (522, 117));
    mouse (win, panel, NSLeftMouseDragged, NSMakePoint (522, 142));
    mouse (win, panel, NSLeftMouseUp,      NSMakePoint (522, 142));
    const float g1 = e->getParameter (e, kCh1Volume);
    std::printf ("  Ch 1 Volume %.2f -> %.2f\n", g0, g1);
    check (std::fabs (g1 - (g0 - 0.25f)) < 0.02f, "dragging a knob down turns it down");
    check (automateCalls > 0 && beginCalls == 1 && endCalls == 1, "host gets begin / automate / end");

    mouse (win, panel, NSLeftMouseDown, NSMakePoint (522, 92), 2);
    mouse (win, panel, NSLeftMouseUp,   NSMakePoint (522, 92), 2);
    check (std::fabs (e->getParameter (e, kCh1Volume) - kDefaults[kCh1Volume]) < 1e-6f, "double-click resets a knob");

    // Each knob answers where it is drawn (dragged up from mid-travel)
    {
        const int knobs[11] = { kSustain, kPresence, kCh1Treble, kCh1Bass, kCh1Volume,
                                kBrilliance, kCh2Treble, kCh2Bass, kCh2Volume, kInput, kOutput };
        const CGFloat kx[11] = { 170, 258, 346, 434, 522, 690, 778, 866, 954, 170, 250 };
        const CGFloat ky[11] = { 92, 92, 92, 92, 92, 92, 92, 92, 92, 276, 276 };
        bool all = true;
        for (int k = 0; k < 11; ++k)
        {
            e->setParameter (e, knobs[k], 0.5f);
            mouse (win, panel, NSLeftMouseDown,    NSMakePoint (kx[k], ky[k]));
            mouse (win, panel, NSLeftMouseDragged, NSMakePoint (kx[k], ky[k] - 20));
            mouse (win, panel, NSLeftMouseUp,      NSMakePoint (kx[k], ky[k] - 20));
            if (! (e->getParameter (e, knobs[k]) > 0.55f)) { all = false; std::printf ("  knob %d did not turn\n", knobs[k]); }
            e->setParameter (e, knobs[k], kDefaults[knobs[k]]);
        }
        check (all, "all eleven knobs turn");
    }

    // Rockers: press the top half for on, the bottom half for off
    {
        const int rockers[6] = { kCh1Bright, kCh1On, kCh2Bright, kCh2On, kMains, kSpeaker };
        const CGFloat rx[6] = { 420, 520, 800, 900, 1110, 60 };
        const CGFloat ry[6] = { 194, 194, 194, 194, 194, 278 };
        bool all = true;
        for (int k = 0; k < 6; ++k)
        {
            mouse (win, panel, NSLeftMouseDown, NSMakePoint (rx[k], ry[k] + 8));
            mouse (win, panel, NSLeftMouseUp,   NSMakePoint (rx[k], ry[k] + 8));
            const bool off = e->getParameter (e, rockers[k]) < 0.5f;
            mouse (win, panel, NSLeftMouseDown, NSMakePoint (rx[k], ry[k] - 8));
            mouse (win, panel, NSLeftMouseUp,   NSMakePoint (rx[k], ry[k] - 8));
            const bool on = e->getParameter (e, rockers[k]) >= 0.5f;
            if (! (off && on)) { all = false; std::printf ("  rocker %d did not switch\n", rockers[k]); }
        }
        check (all, "all six rockers switch off (bottom) and on (top)");
        // Showcase settings: Ch 1 bright, both channels on, mains on
        e->setParameter (e, kCh1Bright, 1.0f);
        e->setParameter (e, kCh2Bright, 0.0f);
        e->setParameter (e, kCh2On, 1.0f);
    }

    // Slide switches: click a side
    {
        mouse (win, panel, NSLeftMouseDown, NSMakePoint (312, 194));
        mouse (win, panel, NSLeftMouseUp,   NSMakePoint (312, 194));
        mouse (win, panel, NSLeftMouseDown, NSMakePoint (648, 194));
        mouse (win, panel, NSLeftMouseUp,   NSMakePoint (648, 194));
        const bool susOn = e->getParameter (e, kSustainOn) >= 0.5f;
        const bool studio = e->getParameter (e, kStage) < 0.5f;
        check (susOn && studio, "slide switches: Sustain on, Studio");
        pump (0.1);
        renderPNG (panel, dir + "/panel_sustain_studio.png");
        mouse (win, panel, NSLeftMouseDown, NSMakePoint (288, 194));
        mouse (win, panel, NSLeftMouseUp,   NSMakePoint (288, 194));
        mouse (win, panel, NSLeftMouseDown, NSMakePoint (672, 194));
        mouse (win, panel, NSLeftMouseUp,   NSMakePoint (672, 194));
        check (e->getParameter (e, kSustainOn) < 0.5f && e->getParameter (e, kStage) >= 0.5f, "and back to Sustain off, Stage");
    }

    // Mains off look
    e->setParameter (e, kMains, 0.0f);
    pump (0.1);
    renderPNG (panel, dir + "/panel_mains_off.png");
    e->setParameter (e, kMains, 1.0f);

    // A knob turned shows in the readout
    mouse (win, panel, NSLeftMouseDown,    NSMakePoint (258, 92));
    mouse (win, panel, NSLeftMouseDragged, NSMakePoint (258, 72));
    mouse (win, panel, NSLeftMouseUp,      NSMakePoint (258, 72));
    renderPNG (panel, dir + "/panel_readout.png");
    e->setParameter (e, kPresence, kDefaults[kPresence]);

    std::printf ("\nPresets\n");
    #define SLOT(k) NSMakePoint (372.0 + (k) * 31.0 + 13.5, 277.0)
    e->setParameter (e, kCh1Bass, 0.3f);
    e->setParameter (e, kSustainOn, 1.0f);
    pump (0.1);
    mouse (win, panel, NSLeftMouseDown, NSMakePoint (337, 277));    // SAVE
    mouse (win, panel, NSLeftMouseUp,   NSMakePoint (337, 277));
    mouse (win, panel, NSLeftMouseDown, SLOT (2));                   // slot 3
    mouse (win, panel, NSLeftMouseUp,   SLOT (2));
    FILE* pf = std::fopen (presetFile.c_str(), "r");
    check (pf != 0, "SAVE then a slot writes the preset file");
    if (pf) std::fclose (pf);
    renderPNG (panel, dir + "/panel_preset_saved.png");

    e->setParameter (e, kCh1Bass, 0.9f);
    e->setParameter (e, kSustainOn, 0.0f);
    pump (0.1);
    automateCalls = 0;
    mouse (win, panel, NSLeftMouseDown, SLOT (2));
    mouse (win, panel, NSLeftMouseUp,   SLOT (2));
    check (std::fabs (e->getParameter (e, kCh1Bass) - 0.3f) < 1e-5f
           && e->getParameter (e, kSustainOn) >= 0.5f, "clicking the slot recalls the saved settings");
    check (automateCalls >= kNumParams, "recall tells the host about every parameter");

    const float before = e->getParameter (e, kCh1Bass);
    mouse (win, panel, NSLeftMouseDown, SLOT (5));                   // empty slot 6
    mouse (win, panel, NSLeftMouseUp,   SLOT (5));
    check (e->getParameter (e, kCh1Bass) == before, "an empty slot changes nothing");

    std::printf ("\nHost automation shows on the panel\n");
    NSData* pngBefore = renderPNG (panel, dir + "/panel_before_host.png");
    e->setParameter (e, kCh2Volume, 0.9f);
    e->setParameter (e, kCh2Bright, 1.0f);
    pump (0.3);
    NSData* pngAfter = renderPNG (panel, dir + "/panel_changed.png");
    check (! [pngAfter isEqualToData: pngBefore], "panel redraws after host changes");

    // Showcase: a typical setting, Ch 1 Volume shown in the readout
    {
        e->setParameter (e, kSustainOn, 0.0f);
        e->setParameter (e, kCh1Bass, 0.6f);
        e->setParameter (e, kCh1Treble, 0.55f);
        e->setParameter (e, kPresence, 0.45f);
        e->setParameter (e, kCh2Volume, 0.4f);
        e->setParameter (e, kCh2Bright, 0.0f);
        e->setParameter (e, kCh1Bright, 1.0f);
        mouse (win, panel, NSLeftMouseDown,    NSMakePoint (522, 92));
        mouse (win, panel, NSLeftMouseDragged, NSMakePoint (522, 72));
        mouse (win, panel, NSLeftMouseUp,      NSMakePoint (522, 72));
        pump (0.2);
        renderPNG (panel, dir + "/panel_showcase.png");
    }
    std::printf ("\nClose\n");
    call (e, effEditClose);
    check ([[parent subviews] count] == 0, "effEditClose removes the panel");
    check (call (e, effEditOpen, 0, 0, parent) == 1 && [[parent subviews] count] == 1, "reopens");
    call (e, effEditClose);
    call (e, effMainsChanged, 0, 0);
    call (e, effClose);
    [win release];
    [pool release];      // drain before unloading the plugin's code
    dlclose (lib);

    std::printf ("\n%s (%d failure%s)\n", failures ? "FAILED" : "ALL EDITOR CHECKS PASSED", failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
