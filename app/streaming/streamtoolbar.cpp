#include "streamtoolbar.h"

#include <QCoreApplication>
#include <QGuiApplication>
#include <QImage>
#include <QPainter>
#include <QPainterPath>
#include <QScreen>
#include <QtMath>

#ifdef Q_OS_DARWIN
void configureStreamToolbarMac(SDL_Window* toolbar, SDL_Window* stream);
#endif

namespace {
QString tr(const char* text)
{
    return QCoreApplication::translate("StreamToolbar", text);
}
constexpr int ExpandedWidth = 600;
constexpr int ExpandedHeight = 104;
constexpr int CollapsedHeight = 26;

struct RestoreGlContext {
    SDL_Window* window = SDL_GL_GetCurrentWindow();
    SDL_GLContext context = SDL_GL_GetCurrentContext();
    ~RestoreGlContext()
    {
        if (window && context && (SDL_GL_GetCurrentWindow() != window || SDL_GL_GetCurrentContext() != context)) {
            SDL_GL_MakeCurrent(window, context);
        }
    }
};
}

StreamToolbar::StreamToolbar(SDL_Window* streamWindow, bool dark)
    : m_StreamWindow(streamWindow), m_Dark(dark)
{
    // Positioning a second top-level window is not supported by native Wayland
    // or direct-display backends. The existing session shortcuts remain usable.
    const char* driver = SDL_GetCurrentVideoDriver();
    if (!driver || (strcmp(driver, "cocoa") && strcmp(driver, "windows") && strcmp(driver, "x11"))) {
        return;
    }
    m_Window = SDL_CreateWindow("Desk — Session controls", SDL_WINDOWPOS_UNDEFINED,
                               SDL_WINDOWPOS_UNDEFINED, m_Width, CollapsedHeight,
                               SDL_WINDOW_HIDDEN | SDL_WINDOW_BORDERLESS | SDL_WINDOW_ALWAYS_ON_TOP |
                               SDL_WINDOW_SKIP_TASKBAR | SDL_WINDOW_UTILITY | SDL_WINDOW_ALLOW_HIGHDPI);
    if (!m_Window) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Session toolbar unavailable: %s", SDL_GetError());
        return;
    }
#ifdef Q_OS_DARWIN
    configureStreamToolbarMac(m_Window, m_StreamWindow);
#endif
}

StreamToolbar::~StreamToolbar()
{
    close();
}

void StreamToolbar::close()
{
    if (m_Window) {
        SDL_DestroyWindow(m_Window);
        m_Window = nullptr;
    }
}

void StreamToolbar::setExpanded(bool expanded)
{
    m_Expanded = expanded;
    m_Hover = m_Pressed = m_KeyboardButton = -1;
    sync(m_AbsoluteMouse, m_Fullscreen);
    paint();
}

void StreamToolbar::sync(bool absoluteMouse, bool fullscreen)
{
    if (!m_Window) {
        return;
    }
    bool repaint = m_AbsoluteMouse != absoluteMouse || m_Fullscreen != fullscreen;
    m_AbsoluteMouse = absoluteMouse;
    m_Fullscreen = fullscreen;
    SDL_Window* focus = SDL_GetKeyboardFocus();

    int x, y, w, h;
    SDL_GetWindowPosition(m_StreamWindow, &x, &y);
    SDL_GetWindowSize(m_StreamWindow, &w, &h);
    const int display = SDL_GetWindowDisplayIndex(m_StreamWindow);
    float scale = 1;
#ifndef Q_OS_DARWIN
    const auto screens = QGuiApplication::screens();
    // Cocoa already measures window coordinates in points. Windows/X11 use pixels.
    if (display >= 0 && display < screens.size()) {
        scale = screens[display]->devicePixelRatio();
    }
#endif
    m_Width = m_Expanded ? ExpandedWidth : 76;
    m_Scale = qMin(scale, float(w) / float(m_Width + 16));
    int desiredW = qMax(1, qRound(m_Width * m_Scale));
    int desiredH = qMax(1, qRound((m_Expanded ? ExpandedHeight : CollapsedHeight) * m_Scale));
    int oldW, oldH;
    SDL_GetWindowSize(m_Window, &oldW, &oldH);
    if (oldW != desiredW || oldH != desiredH) {
        SDL_SetWindowSize(m_Window, desiredW, desiredH);
        repaint = true;
    }
    int newX = x + (w - desiredW) / 2;
    int newY = y + qRound(8 * m_Scale);
    // Keep the tab reachable when the window is partly off-screen or below a notch.
    SDL_Rect bounds;
    if (SDL_GetDisplayUsableBounds(display, &bounds) == 0) {
        newX = qMax(bounds.x, qMin(newX, bounds.x + bounds.w - desiredW));
        newY = qMax(bounds.y, qMin(newY, bounds.y + bounds.h - desiredH));
    }
    int oldX, oldY;
    SDL_GetWindowPosition(m_Window, &oldX, &oldY);
    if (oldX != newX || oldY != newY) {
        SDL_SetWindowPosition(m_Window, newX, newY);
    }
    if ((focus != m_StreamWindow && focus != m_Window) ||
        (SDL_GetWindowFlags(m_StreamWindow) & (SDL_WINDOW_MINIMIZED | SDL_WINDOW_HIDDEN))) {
        SDL_HideWindow(m_Window);
        return;
    }
    if (SDL_GetWindowFlags(m_Window) & SDL_WINDOW_HIDDEN) {
        SDL_ShowWindow(m_Window);
        // Showing the tab must not steal keyboard focus from the remote session.
        SDL_RaiseWindow(focus);
        repaint = true;
    }
    if (repaint) {
        paint();
    }
}

QRect StreamToolbar::buttonRect(int index) const
{
    if (index == 4) {
        return QRect(ExpandedWidth - 34, 5, 26, 22);
    }
    return QRect(12 + index * 145, 34, 137, 42);
}

int StreamToolbar::hitTest(int x, int y) const
{
    if (!m_Expanded) {
        return 4;
    }
    for (int i = 0; i < 5; i++) {
        if (buttonRect(i).contains(qRound(x / m_Scale), qRound(y / m_Scale))) {
            return i;
        }
    }
    return -1;
}

StreamToolbar::Action StreamToolbar::handleEvent(const SDL_Event& event)
{
    if (!m_Window) {
        return Action::None;
    }
    // Always reachable in relative/game mouse mode, even with no incoming frames.
    if (event.type == SDL_KEYDOWN && !event.key.repeat &&
        (event.key.keysym.mod & KMOD_CTRL) && (event.key.keysym.mod & KMOD_ALT) &&
        (event.key.keysym.mod & KMOD_SHIFT) &&
        (event.key.keysym.sym == SDLK_t || event.key.keysym.scancode == SDL_SCANCODE_T)) {
        setExpanded(!m_Expanded);
        SDL_RaiseWindow(m_Expanded ? m_Window : m_StreamWindow);
        return m_Expanded ? Action::ReleaseInput : Action::ResumeInput;
    }
    Uint32 windowId = 0;
    switch (event.type) {
    case SDL_WINDOWEVENT: windowId = event.window.windowID; break;
    case SDL_KEYDOWN: case SDL_KEYUP: windowId = event.key.windowID; break;
    case SDL_TEXTINPUT: windowId = event.text.windowID; break;
    case SDL_TEXTEDITING: windowId = event.edit.windowID; break;
    case SDL_MOUSEMOTION: windowId = event.motion.windowID; break;
    case SDL_MOUSEBUTTONDOWN: case SDL_MOUSEBUTTONUP: windowId = event.button.windowID; break;
    case SDL_MOUSEWHEEL: windowId = event.wheel.windowID; break;
    default: return Action::None;
    }
    if (windowId != SDL_GetWindowID(m_Window)) {
        if (m_Expanded && event.type == SDL_MOUSEBUTTONUP &&
            windowId == SDL_GetWindowID(m_StreamWindow) && event.button.button == SDL_BUTTON_LEFT) {
            setExpanded(false);
            return Action::ResumeInput;
        }
        return Action::None;
    }

    int activated = -1;
    switch (event.type) {
    case SDL_WINDOWEVENT:
        if (event.window.event == SDL_WINDOWEVENT_CLOSE) {
            return Action::Disconnect;
        }
        if (event.window.event == SDL_WINDOWEVENT_LEAVE) {
            m_Hover = m_Pressed = -1;
        }
        paint();
        if (event.window.event == SDL_WINDOWEVENT_ENTER || event.window.event == SDL_WINDOWEVENT_FOCUS_GAINED) {
            return Action::ReleaseInput;
        }
        break;
    case SDL_MOUSEMOTION:
        m_Hover = hitTest(event.motion.x, event.motion.y);
        paint();
        break;
    case SDL_MOUSEBUTTONDOWN:
        if (event.button.button == SDL_BUTTON_LEFT) {
            m_Pressed = hitTest(event.button.x, event.button.y);
        }
        break;
    case SDL_MOUSEBUTTONUP:
        if (event.button.button == SDL_BUTTON_LEFT) {
            int hit = hitTest(event.button.x, event.button.y);
            if (hit == m_Pressed) {
                activated = hit;
            }
            m_Pressed = -1;
        }
        break;
    case SDL_KEYDOWN:
        if (event.key.repeat) break;
        // Existing emergency shortcuts must also work while the toolbar has focus.
        if ((event.key.keysym.mod & KMOD_CTRL) && (event.key.keysym.mod & KMOD_ALT) &&
            (event.key.keysym.mod & KMOD_SHIFT)) {
            if (event.key.keysym.scancode == SDL_SCANCODE_Q) return Action::Disconnect;
            if (event.key.keysym.scancode == SDL_SCANCODE_X) return Action::ToggleFullscreen;
            if (event.key.keysym.scancode == SDL_SCANCODE_Z) return Action::ReleaseInput;
        }
        if (event.key.keysym.sym == SDLK_ESCAPE) {
            if (m_Expanded) activated = 4;
        }
        else if (event.key.keysym.sym == SDLK_TAB) {
            const bool backwards = event.key.keysym.mod & KMOD_SHIFT;
            m_KeyboardButton = m_KeyboardButton < 0 ? (backwards ? 4 : 0) :
                               (m_KeyboardButton + (backwards ? 4 : 1)) % 5;
            paint();
        }
        else if (event.key.keysym.sym == SDLK_RETURN || event.key.keysym.sym == SDLK_SPACE) {
            activated = m_KeyboardButton;
        }
        break;
    }
    switch (activated) {
    case 0: return Action::ToggleMouseMode;
    case 1: return Action::ToggleFullscreen;
    case 2:
        setExpanded(false);
        SDL_RaiseWindow(m_StreamWindow);
        return Action::ReleaseInput;
    case 3: return Action::Disconnect;
    case 4:
        setExpanded(!m_Expanded);
        SDL_RaiseWindow(m_Expanded ? m_Window : m_StreamWindow);
        return m_Expanded ? Action::ReleaseInput : Action::ResumeInput;
    default: return Action::Consumed;
    }
}

void StreamToolbar::paint()
{
    if (!m_Window || (SDL_GetWindowFlags(m_Window) & SDL_WINDOW_HIDDEN)) return;
    // SDL may use a GPU to present a CPU-painted surface (required by Cocoa).
    // Restore the video renderer's GL context if presentation changes it.
    RestoreGlContext restoreContext;
    SDL_Surface* surface = SDL_GetWindowSurface(m_Window);
    if (!surface) return;
    QImage image(surface->w, surface->h, QImage::Format_RGBA8888);
    const QColor bg(m_Dark ? "#202020" : "#FFFFFF");
    const QColor fg(m_Dark ? "#DDDDDD" : "#17212B");
    const QColor accent(m_Dark ? "#4DAAFC" : "#1677FF");
    const QColor border(m_Dark ? "#414141" : "#DCE5F0");
    image.fill(bg);
    QPainter p(&image);
    p.setRenderHint(QPainter::Antialiasing);
    p.scale(double(surface->w) / m_Width, double(surface->h) / (m_Expanded ? ExpandedHeight : CollapsedHeight));
    p.setPen(border);
    p.drawRect(QRectF(0.5, 0.5, m_Width - 1, (m_Expanded ? ExpandedHeight : CollapsedHeight) - 1));
    QFont font = QGuiApplication::font();
    font.setPixelSize(12);
    p.setFont(font);
    auto chevron = [&](int x, int y, bool up) {
        p.setPen(QPen(accent, 1.8, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        QPainterPath path;
        path.moveTo(x - 4, y + (up ? 2 : -2));
        path.lineTo(x, y + (up ? -2 : 2));
        path.lineTo(x + 4, y + (up ? 2 : -2));
        p.drawPath(path);
    };
    if (!m_Expanded) {
        p.setPen(fg);
        p.drawText(QRect(9, 0, 38, CollapsedHeight), Qt::AlignCenter, "Desk");
        chevron(59, 13, false);
    }
    else {
        p.setPen(fg);
        p.drawText(QRect(14, 4, 540, 24), Qt::AlignVCenter, tr("Session controls"));
        const QString labels[] = {m_AbsoluteMouse ? tr("Mouse: Desktop") : tr("Mouse: Game"),
                                  m_Fullscreen ? tr("Windowed") : tr("Fullscreen"),
                                  tr("Release mouse"), tr("Disconnect")};
        for (int i = 0; i < 5; i++) {
            const QRect r = buttonRect(i);
            p.setPen(i == m_KeyboardButton ? QPen(accent, 2) : QPen(border));
            p.setBrush(i == m_Hover ? QColor(m_Dark ? "#363636" : "#E7F0FF") : bg);
            p.drawRoundedRect(r.adjusted(1, 1, -1, -1), 6, 6);
            if (i == 4) {
                chevron(r.center().x(), r.center().y(), true);
                continue;
            }
            const QColor ink = i == 3 ? QColor(m_Dark ? "#FF9999" : "#C13C3C") : accent;
            p.setPen(QPen(ink, 1.6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            p.setBrush(Qt::NoBrush);
            const int x = r.x() + 12, y = r.center().y();
            if (i == 0) {
                p.drawRoundedRect(QRect(x, y - 9, 12, 18), 5, 5);
                p.drawLine(x + 6, y - 5, x + 6, y - 1);
            }
            else if (i == 1) {
                p.drawRect(QRect(x, y - 6, 16, 12));
                p.drawLine(x + 4, y + 9, x + 12, y + 9);
            }
            else if (i == 2) {
                QPolygon pointer;
                pointer << QPoint(x, y - 8) << QPoint(x + 12, y + 1) << QPoint(x + 6, y + 3) << QPoint(x + 3, y + 9);
                p.drawPolygon(pointer);
            }
            else {
                p.drawArc(QRect(x, y - 7, 15, 15), 45 * 16, 270 * 16);
                p.drawLine(x + 7, y - 9, x + 7, y - 1);
            }
            p.setPen(i == 3 ? ink : fg);
            QRect textRect = r.adjusted(34, 0, -5, 0);
            p.drawText(textRect, Qt::AlignCenter, p.fontMetrics().elidedText(labels[i], Qt::ElideRight, textRect.width()));
        }
        p.setPen(QColor(m_Dark ? "#A0A0A0" : "#657184"));
        font.setPixelSize(11);
        p.setFont(font);
        p.drawText(QRect(12, 78, 576, 23), Qt::AlignCenter,
                   tr("Ctrl+Alt+Shift+T: toolbar · Q: disconnect · Z: release mouse"));
    }
    p.end();
    SDL_Surface* pixels = SDL_CreateRGBSurfaceWithFormatFrom(image.bits(), image.width(), image.height(),
                                                            32, image.bytesPerLine(), SDL_PIXELFORMAT_RGBA32);
    if (pixels) {
        SDL_BlitSurface(pixels, nullptr, surface, nullptr);
        SDL_FreeSurface(pixels);
        SDL_UpdateWindowSurface(m_Window);
    }
}
