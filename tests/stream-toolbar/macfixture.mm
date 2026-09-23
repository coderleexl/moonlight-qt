#include "SDL_compat.h"
#include <SDL_syswm.h>
#include <QProcess>
#import <Cocoa/Cocoa.h>

static NSWindow* windowFor(SDL_Window* window)
{
    SDL_SysWMinfo info;
    SDL_VERSION(&info.version);
    return SDL_GetWindowWMInfo(window, &info) ? info.info.cocoa.window : nil;
}

void loseToolbarNativeState(SDL_Window* panel)
{
    NSWindow* window = windowFor(panel);
    // Fullscreen renderers can raise the video window above floating panels.
    window.parentWindow.level = NSMainMenuWindowLevel + 1;
    [window.parentWindow removeChildWindow:window];
    [window orderOut:nil];
    [NSApp deactivate];
}

bool checkToolbarNativeState(SDL_Window* panel, SDL_Window* stream)
{
    NSWindow* window = windowFor(panel);
    if (!window.visible || window.parentWindow != windowFor(stream) || window.opaque ||
        window.level < windowFor(stream).level) {
        return false;
    }
    bool hasGlass = false;
    bool hasArtwork = false;
    for (NSView* view in window.contentView.subviews) {
        if ([view isKindOfClass:NSVisualEffectView.class]) {
            NSVisualEffectView* glass = (NSVisualEffectView*)view;
            if (glass.blendingMode != NSVisualEffectBlendingModeBehindWindow) return false;
            hasGlass = glass.alphaValue < 1.0 && glass.alphaValue > 0.0;
        }
        if ([view isKindOfClass:NSImageView.class] && ((NSImageView*)view).image) {
            hasArtwork = view.alphaValue == 1.0;
        }
    }
    return hasGlass && hasArtwork;
}

void saveToolbarNativeScreenshot(SDL_Window* panel, const char* path)
{
    // Optional visual review on a developer Mac with screen recording access.
    if (qEnvironmentVariableIsSet("DESK_TOOLBAR_SCREENSHOTS")) {
        [windowFor(panel) displayIfNeeded];
        const int result = QProcess::execute("/usr/sbin/screencapture", {"-x", "-o", "-l",
                          QString::number(windowFor(panel).windowNumber), QString::fromUtf8(path)});
        if (result != 0) {
            // Render only our view when screen recording permission is absent.
            // This preview can't include the WindowServer's behind-window blur.
            NSView* view = windowFor(panel).contentView;
            NSBitmapImageRep* bitmap = [view bitmapImageRepForCachingDisplayInRect:view.bounds];
            [view cacheDisplayInRect:view.bounds toBitmapImageRep:bitmap];
            NSData* data = [bitmap representationUsingType:NSBitmapImageFileTypePNG properties:@{}];
            [data writeToFile:[NSString stringWithUTF8String:path] atomically:YES];
        }
    }
}
