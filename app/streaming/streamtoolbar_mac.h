#pragma once

#include "SDL_compat.h"
#include <QImage>
#include <QSize>

bool streamToolbarMacShouldShow(SDL_Window* stream);
bool syncStreamToolbarMac(SDL_Window* toolbar, SDL_Window* stream, bool dark);
QSize streamToolbarMacPixelSize(SDL_Window* toolbar);
void presentStreamToolbarMac(SDL_Window* toolbar, const QImage& image);
