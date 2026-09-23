#include "streamtoolbar_mac.h"
#include <SDL_syswm.h>
#import <Cocoa/Cocoa.h>
#import <QuartzCore/QuartzCore.h>

namespace {
constexpr NSInteger GlassTag = 71031;
constexpr NSInteger ArtworkTag = 71032;
}

// Keep pointer and keyboard delivery in SDL's existing content view.
@interface DeskGlassView : NSVisualEffectView
@end
@implementation DeskGlassView
- (NSInteger)tag { return GlassTag; }
- (NSView*)hitTest:(NSPoint)point { Q_UNUSED(point); return nil; }
@end

@interface DeskToolbarImageView : NSImageView
@end
@implementation DeskToolbarImageView
- (NSView*)hitTest:(NSPoint)point { Q_UNUSED(point); return nil; }
@end

namespace {
NSWindow* nativeWindow(SDL_Window* window)
{
    SDL_SysWMinfo info;
    SDL_VERSION(&info.version);
    return SDL_GetWindowWMInfo(window, &info) ? info.info.cocoa.window : nil;
}
}

bool streamToolbarMacShouldShow(SDL_Window* stream)
{
    NSWindow* window = nativeWindow(stream);
    // A child belongs to the visible stream, even while another application
    // has keyboard focus. Its parent's ordering keeps it below other apps.
    return window.visible && !window.miniaturized;
}

bool syncStreamToolbarMac(SDL_Window* toolbar, SDL_Window* stream, bool dark)
{
    NSWindow* panel = nativeWindow(toolbar);
    NSWindow* parent = nativeWindow(stream);
    if (!panel || !parent) return false;
    bool repaint = false;
    if (panel.parentWindow != parent) {
        [panel.parentWindow removeChildWindow:panel];
        [parent addChildWindow:panel ordered:NSWindowAbove];
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "Reattached session toolbar to native stream window");
        repaint = true;
    }
    panel.collectionBehavior = NSWindowCollectionBehaviorFullScreenAuxiliary;
    panel.level = parent.level;
    // Visibility follows the stream window, not application activation.
    panel.hidesOnDeactivate = NO;
    panel.opaque = NO;
    panel.backgroundColor = NSColor.clearColor;
    panel.hasShadow = YES;
    NSView* content = panel.contentView;
    content.wantsLayer = YES;
    content.layer.backgroundColor = NSColor.clearColor.CGColor;
    content.layer.cornerRadius = MIN(12, content.bounds.size.height / 2);
    content.layer.masksToBounds = YES;

    DeskGlassView* glass = (DeskGlassView*)[content viewWithTag:GlassTag];
    if (!glass) {
        glass = [[DeskGlassView alloc] initWithFrame:content.bounds];
        glass.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
        glass.material = NSVisualEffectMaterialHUDWindow;
        glass.blendingMode = NSVisualEffectBlendingModeBehindWindow;
        glass.state = NSVisualEffectStateActive;
        [content addSubview:glass];
        [glass release];
        DeskToolbarImageView* artwork = [[DeskToolbarImageView alloc] initWithFrame:content.bounds];
        artwork.tag = ArtworkTag;
        artwork.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
        artwork.imageScaling = NSImageScaleAxesIndependently;
        // Keep text/icons outside the translucent material so they stay crisp.
        [content addSubview:artwork];
        [artwork release];
        repaint = true;
    }
    glass.alphaValue = dark ? 0.68 : 0.56;
    glass.appearance = [NSAppearance appearanceNamed:dark ? NSAppearanceNameDarkAqua : NSAppearanceNameAqua];
    // Cocoa native visibility and SDL renderer window recreation can leave
    // SDL_WINDOW_HIDDEN stale. Restore native stacking without stealing focus.
    if (!panel.visible) repaint = true;
    [panel orderWindow:NSWindowAbove relativeTo:parent.windowNumber];
    return repaint;
}

QSize streamToolbarMacPixelSize(SDL_Window* toolbar)
{
    NSWindow* panel = nativeWindow(toolbar);
    NSSize size = panel.contentView.bounds.size;
    return QSize(qRound(size.width * panel.backingScaleFactor), qRound(size.height * panel.backingScaleFactor));
}

void presentStreamToolbarMac(SDL_Window* toolbar, const QImage& image)
{
    NSWindow* panel = nativeWindow(toolbar);
    NSImageView* artwork = (NSImageView*)[panel.contentView viewWithTag:ArtworkTag];
    if (!artwork) return;
    // Copy pixels: the caller's QImage is released when paint() returns.
    CFDataRef data = CFDataCreate(kCFAllocatorDefault, image.constBits(), image.sizeInBytes());
    CGDataProviderRef provider = CGDataProviderCreateWithCFData(data);
    CGColorSpaceRef colors = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
    CGImageRef cgImage = CGImageCreate(image.width(), image.height(), 8, 32, image.bytesPerLine(),
                                      colors, kCGImageAlphaLast | kCGBitmapByteOrder32Big,
                                      provider, nullptr, false, kCGRenderingIntentDefault);
    if (cgImage) {
        NSImage* nativeImage = [[NSImage alloc] initWithCGImage:cgImage size:artwork.bounds.size];
        artwork.image = nativeImage;
        [nativeImage release];
        CGImageRelease(cgImage);
        [artwork displayIfNeeded];
        [panel invalidateShadow];
    }
    CGColorSpaceRelease(colors);
    CGDataProviderRelease(provider);
    CFRelease(data);
}
