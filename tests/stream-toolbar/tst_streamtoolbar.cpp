#include "streaming/streamtoolbar.h"
#include <QGuiApplication>
#include <QImage>
#include <QTest>
#include <QTranslator>

class ToolbarTest : public QObject
{
    Q_OBJECT
private slots:
    void controlsWithoutVideo()
    {
        QVERIFY(SDL_Init(SDL_INIT_VIDEO) == 0);
        SDL_Window* stream = SDL_CreateWindow("Toolbar test — no video", 160, 160, 1000, 600, SDL_WINDOW_SHOWN);
        QVERIFY(stream);
        {
            StreamToolbar toolbar(stream, false);
            QVERIFY(toolbar.available());
            SDL_RaiseWindow(stream);
            QTRY_VERIFY_WITH_TIMEOUT((SDL_PumpEvents(), SDL_GetKeyboardFocus() == stream), 2000);
            toolbar.sync(true, false);
            SDL_Window* panel = nullptr;
            for (Uint32 id = 1; id < 10; ++id) {
                SDL_Window* candidate = SDL_GetWindowFromID(id);
                if (candidate && candidate != stream) panel = candidate;
            }
            QVERIFY(panel);
            const Uint32 panelId = SDL_GetWindowID(panel);
            auto key = [&](SDL_Keycode code, Uint16 mod = 0) {
                SDL_Event e = {};
                e.type = SDL_KEYDOWN;
                e.key.windowID = panelId;
                e.key.keysym.sym = code;
                e.key.keysym.scancode = SDL_GetScancodeFromKey(code);
                e.key.keysym.mod = mod;
                return toolbar.handleEvent(e);
            };
            auto click = [&](int index) {
                int width, height;
                SDL_GetWindowSize(panel, &width, &height);
                SDL_Event e = {};
                e.type = SDL_MOUSEBUTTONDOWN;
                e.button.windowID = panelId;
                e.button.button = SDL_BUTTON_LEFT;
                e.button.x = (12 + index * 145 + 68) * width / 600;
                e.button.y = 55 * height / 104;
                toolbar.handleEvent(e);
                e.type = SDL_MOUSEBUTTONUP;
                return toolbar.handleEvent(e);
            };
            const Uint16 combo = KMOD_CTRL | KMOD_ALT | KMOD_SHIFT;
            QCOMPARE(key(SDLK_t, combo), StreamToolbar::Action::ReleaseInput);
            QVERIFY(toolbar.expanded());
            QCOMPARE(click(0), StreamToolbar::Action::ToggleMouseMode);
            QCOMPARE(click(1), StreamToolbar::Action::ToggleFullscreen);
            QCOMPARE(click(3), StreamToolbar::Action::Disconnect);
            QCOMPARE(key(SDLK_q, combo), StreamToolbar::Action::Disconnect);
            QCOMPARE(key(SDLK_x, combo), StreamToolbar::Action::ToggleFullscreen);
            QCOMPARE(key(SDLK_z, combo), StreamToolbar::Action::ReleaseInput);
            QCOMPARE(key(SDLK_a), StreamToolbar::Action::Consumed);
            QCOMPARE(key(SDLK_TAB), StreamToolbar::Action::Consumed);
            QCOMPARE(key(SDLK_RETURN), StreamToolbar::Action::ToggleMouseMode);
            QCOMPARE(key(SDLK_ESCAPE), StreamToolbar::Action::ResumeInput);
            QVERIFY(!toolbar.expanded());
            QCOMPARE(key(SDLK_t, combo), StreamToolbar::Action::ReleaseInput);
            QCOMPARE(click(2), StreamToolbar::Action::ReleaseInput);
            QVERIFY(!toolbar.expanded());
            key(SDLK_t, combo);
            SDL_Event remoteClick = {};
            remoteClick.type = SDL_MOUSEBUTTONUP;
            remoteClick.button.windowID = SDL_GetWindowID(stream);
            remoteClick.button.button = SDL_BUTTON_LEFT;
            QCOMPARE(toolbar.handleEvent(remoteClick), StreamToolbar::Action::ResumeInput);
            QVERIFY(!toolbar.expanded());

            // Repaint and operate the controls after an idle period with no
            // incoming video, network events, Qt event processing, or renderer.
            key(SDLK_t, combo);
            SDL_Delay(150);
            SDL_PumpEvents();
            toolbar.sync(false, true);
            QCOMPARE(click(3), StreamToolbar::Action::Disconnect);
            SDL_Surface* surface = SDL_GetWindowSurface(panel);
            QVERIFY2(surface, SDL_GetError());
            SDL_Surface* rgba = SDL_ConvertSurfaceFormat(surface, SDL_PIXELFORMAT_RGBA32, 0);
            QVERIFY(rgba);
            QImage image(static_cast<uchar*>(rgba->pixels), rgba->w, rgba->h, rgba->pitch, QImage::Format_RGBA8888);
            QVERIFY(image.save("toolbar-light.png"));
            SDL_FreeSurface(rgba);
            toolbar.close();
            QVERIFY(!toolbar.available());
        }
        {
            StreamToolbar dark(stream, true);
            SDL_RaiseWindow(stream);
            QTRY_VERIFY_WITH_TIMEOUT((SDL_PumpEvents(), SDL_GetKeyboardFocus() == stream), 2000);
            dark.sync(true, false);
            SDL_Event e = {};
            e.type = SDL_KEYDOWN;
            e.key.keysym.sym = SDLK_t;
            e.key.keysym.mod = KMOD_CTRL | KMOD_ALT | KMOD_SHIFT;
            QCOMPARE(dark.handleEvent(e), StreamToolbar::Action::ReleaseInput);
            QTRY_VERIFY_WITH_TIMEOUT((SDL_PumpEvents(), SDL_GetKeyboardFocus() != stream && SDL_GetKeyboardFocus()), 2000);
            dark.sync(true, false);
            SDL_Window* panel = SDL_GetKeyboardFocus();
            QVERIFY(panel && panel != stream);
            SDL_Surface* surface = SDL_GetWindowSurface(panel);
            QVERIFY2(surface, SDL_GetError());
            SDL_Surface* rgba = SDL_ConvertSurfaceFormat(surface, SDL_PIXELFORMAT_RGBA32, 0);
            QVERIFY(rgba);
            QImage image(static_cast<uchar*>(rgba->pixels), rgba->w, rgba->h, rgba->pitch, QImage::Format_RGBA8888);
            QVERIFY(image.save("toolbar-dark.png"));
            SDL_FreeSurface(rgba);
        }
        SDL_DestroyWindow(stream);
        SDL_Quit();
    }
};

int main(int argc, char** argv)
{
    QGuiApplication app(argc, argv);
    SDL_SetMainReady();
    QTranslator translator;
    if (translator.load(qEnvironmentVariable("DESK_TEST_TRANSLATION"))) app.installTranslator(&translator);
    ToolbarTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_streamtoolbar.moc"
