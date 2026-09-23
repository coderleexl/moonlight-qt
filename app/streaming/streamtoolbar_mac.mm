#include "SDL_compat.h"
#include <SDL_syswm.h>
#import <Cocoa/Cocoa.h>

void configureStreamToolbarMac(SDL_Window* toolbar, SDL_Window* stream)
{
    SDL_SysWMinfo toolbarInfo, streamInfo;
    SDL_VERSION(&toolbarInfo.version);
    SDL_VERSION(&streamInfo.version);
    if (SDL_GetWindowWMInfo(toolbar, &toolbarInfo) && SDL_GetWindowWMInfo(stream, &streamInfo)) {
        NSWindow* panel = toolbarInfo.info.cocoa.window;
        panel.collectionBehavior = NSWindowCollectionBehaviorFullScreenAuxiliary;
        panel.level = NSFloatingWindowLevel;
        panel.hidesOnDeactivate = YES;
        [streamInfo.info.cocoa.window addChildWindow:panel ordered:NSWindowAbove];
    }
}
