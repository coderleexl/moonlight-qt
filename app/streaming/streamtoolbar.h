#pragma once

#include "SDL_compat.h"
#include <QRect>

// A separate software-rendered SDL window: no video frames or Qt event loop
// are needed to repaint or operate the session controls.
class StreamToolbar
{
public:
    enum class Action { None, Consumed, ReleaseInput, ResumeInput, ToggleMouseMode, ToggleFullscreen, Disconnect };

    explicit StreamToolbar(SDL_Window* streamWindow, bool dark);
    ~StreamToolbar();
    void close();
    bool available() const { return m_Window != nullptr; }
    bool expanded() const { return m_Expanded; }
    void sync(bool absoluteMouse, bool fullscreen);
    Action handleEvent(const SDL_Event& event);

private:
    void setExpanded(bool expanded);
    void paint();
    QRect buttonRect(int index) const;
    int hitTest(int x, int y) const;

    SDL_Window* m_StreamWindow;
    SDL_Window* m_Window = nullptr;
    bool m_Dark;
    bool m_Expanded = false;
    bool m_AbsoluteMouse = true;
    bool m_Fullscreen = false;
    int m_Hover = -1;
    int m_Pressed = -1;
    int m_KeyboardButton = -1;
    int m_Width = 76;
    float m_Scale = 1;
};
