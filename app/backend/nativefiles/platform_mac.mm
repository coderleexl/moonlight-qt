#include "materialize.h"
#include "nativeplatform.h"
#include "offerstore.h"
#import <AppKit/AppKit.h>
#import <CoreServices/CoreServices.h>
#include <QClipboard>
#include <QDateTime>
#include <QFile>
#include <QGuiApplication>
#include <QMimeData>
#include <QPointer>
#include <QSet>
#include <QTemporaryDir>
#include <QUuid>
#include <atomic>
#include <sys/xattr.h>
namespace {
NSString* const marker = @"io.github.coderleexl.Desk.file-offer";
const char* attribute = "io.github.coderleexl.Desk.promise";
QString qt(NSString* text) { return text ? QString::fromUtf8(text.UTF8String) : QString(); }
NSString* ns(const QString& text) { return [NSString stringWithUTF8String:text.toUtf8().constData()]; }
struct MacOffer {
    QJsonObject manifest;
    NativeRead read;
    std::atomic<bool> active { true };
    std::atomic<int> pending { 0 }, writing { 0 };
    std::atomic<bool> dragging { false };
    std::atomic<qint64> deadline { 0 };
    bool busy() const
    {
        return dragging || writing > 0 || (pending > 0 && QDateTime::currentMSecsSinceEpoch() < deadline);
    }
    QTemporaryDir temporary;
    QByteArray token = QUuid::createUuid().toString(QUuid::WithoutBraces).toUtf8();
    QPointer<NativeFilePlatform> platform;
    NativeRead reader()
    {
        return [this](const QString& id, qint64 offset, int length) {
            if (!active)
                return QJsonObject { { "ok", false }, { "error", "File selection expired" } };
            auto result = read(id, offset, length);
            if (platform && result["ok"].toBool())
                for (auto v : manifest["entries"].toArray()) {
                    auto e = v.toObject();
                    if (e["id"] == id)
                        emit platform->progress(e["path"].toString(), offset + length, qint64(e["size"].toDouble()));
                }
            return result;
        };
    }
};
}
@interface DeskFilePromise : NSObject <NSFilePromiseProviderDelegate, NSDraggingSource> {
@public
    std::shared_ptr<MacOffer> selection;
    QString root;
}
@end
@implementation DeskFilePromise
- (NSString*)filePromiseProvider:(NSFilePromiseProvider*)provider fileNameForType:(NSString*)type
{
    Q_UNUSED(provider);
    Q_UNUSED(type);
    return ns(root);
}
- (NSOperationQueue*)operationQueueForFilePromiseProvider:(NSFilePromiseProvider*)provider
{
    Q_UNUSED(provider);
    return [NSOperationQueue new];
}
- (void)filePromiseProvider:(NSFilePromiseProvider*)provider
          writePromiseToURL:(NSURL*)url
          completionHandler:(void (^)(NSError*))complete
{
    Q_UNUSED(provider);
    auto s = selection;
    ++s->writing;
    auto error = materializeNative(s->manifest, s->reader(), root, qt(url.path));
    if (!error.isEmpty() && s->platform)
        emit s->platform->error(error);
    --s->writing;
    --s->pending;
    complete(error.isEmpty() ? nil
                             : [NSError errorWithDomain:@"DeskFileTransfer"
                                                   code:1
                                               userInfo:@{ NSLocalizedDescriptionKey : ns(error) }]);
}
- (void)draggingSession:(NSDraggingSession*)session endedAtPoint:(NSPoint)point operation:(NSDragOperation)operation
{
    Q_UNUSED(session);
    Q_UNUSED(point);
    selection->dragging = false;
    if (operation == NSDragOperationNone)
        selection->pending = 0;
    selection->deadline = QDateTime::currentMSecsSinceEpoch() + 60000;
}
- (NSDragOperation)draggingSession:(NSDraggingSession*)session
    sourceOperationMaskForDraggingContext:(NSDraggingContext)context
{
    Q_UNUSED(session);
    Q_UNUSED(context);
    return NSDragOperationCopy;
}
@end
@interface DeskPasteProvider : NSObject <NSPasteboardItemDataProvider> {
@public
    std::shared_ptr<MacOffer> selection;
    QString root;
    bool directory;
    QString fileId;
}
@end
@implementation DeskPasteProvider
- (void)pasteboard:(NSPasteboard*)pasteboard item:(NSPasteboardItem*)item provideDataForType:(NSPasteboardType)type
{
    Q_UNUSED(pasteboard);
    if (!selection->active || ![type isEqualToString:NSPasteboardTypeFileURL])
        return;
    auto path = QDir(selection->temporary.path()).canonicalPath() + '/' + root;
    auto capability = selection->token + ':' + fileId.toUtf8();
    if (directory) {
        if (!QDir().mkpath(path))
            return;
        QFile f(path + "/.desk-promise");
        if (!f.open(QIODevice::WriteOnly))
            return;
        f.write(capability);
    } else {
        QFile f(path);
        if (!f.open(QIODevice::WriteOnly))
            return;
        f.write(capability);
    }
    if (setxattr(QFile::encodeName(path).constData(), attribute, capability.constData(), size_t(capability.size()), 0,
            XATTR_NOFOLLOW)
        != 0)
        return;
    [item setString:[NSURL fileURLWithPath:ns(path)].absoluteString forType:NSPasteboardTypeFileURL];
}
@end
namespace {
class MacFiles final : public NativeFilePlatform {
    std::shared_ptr<MacOffer> clipboardOffer, dragOffer;
    NSMutableArray* pasteProviders = [NSMutableArray new];
    NSMutableArray* dragProviders = [NSMutableArray new];
    FSEventStreamRef events = nullptr;
    QSet<QString> processing;
    static void observed(ConstFSEventStreamRef, void* context, size_t count, void* paths,
        const FSEventStreamEventFlags[], const FSEventStreamEventId[])
    {
        auto self = static_cast<MacFiles*>(context);
        auto s = self->clipboardOffer;
        if (!s || !s->active)
            return;
        auto array = static_cast<char**>(paths);
        for (size_t i = 0; i < count; ++i) {
            auto path = QString::fromUtf8(array[i]);
            if (path.endsWith("/.desk-promise"))
                path = QFileInfo(path).path();
            auto encodedPath = QFile::encodeName(path);
            if (path.startsWith(QDir(s->temporary.path()).canonicalPath() + '/') || self->processing.contains(path))
                continue;
            char token[256];
            auto size = getxattr(encodedPath.constData(), attribute, token, sizeof(token), 0, XATTR_NOFOLLOW);
            if (size <= 0 || size > 255 || QFileInfo(path).isSymLink())
                continue;
            auto capability = QByteArray(token, int(size));
            if (!capability.startsWith(s->token + ':'))
                continue;
            auto fileId = QString::fromUtf8(capability.mid(s->token.size() + 1));
            QJsonObject root;
            for (auto v : s->manifest["entries"].toArray()) {
                auto e = v.toObject();
                if (!e["path"].toString().contains('/') && e["id"] == fileId) {
                    root = e;
                    break;
                }
            }
            if (root.isEmpty())
                continue;
            const bool directory = root["directory"].toBool();
            auto sentinel = directory ? path + "/.desk-promise" : path;
            QFile f(sentinel);
            if (!f.open(QIODevice::ReadOnly) || f.read(257) != capability) {
                continue;
            }
            f.close();
            if (!QFile::remove(sentinel))
                continue;
            self->processing.insert(path);
            removexattr(encodedPath.constData(), attribute, XATTR_NOFOLLOW);
            auto name = root["path"].toString();
            QPointer<MacFiles> guard(self);
            [[NSOperationQueue new] addOperationWithBlock:^{
                auto error = materializeNative(s->manifest, s->reader(), name, path);
                if (guard)
                    QMetaObject::invokeMethod(
                        guard,
                        [guard, path] {
                            if (guard)
                                guard->processing.remove(path);
                        },
                        Qt::QueuedConnection);
                if (!error.isEmpty() && s->platform)
                    emit s->platform->error(error);
            }];
        }
    }
    std::shared_ptr<MacOffer> make(const QJsonObject& offer, NativeRead read)
    {
        auto s = std::make_shared<MacOffer>();
        s->manifest = offer;
        s->read = read;
        s->platform = this;
        return s;
    }

public:
    MacFiles()
    {
        FSEventStreamContext context { 0, this, nullptr, nullptr, nullptr };
        NSArray* roots = @[ @"/" ];
        events = FSEventStreamCreate(nullptr, observed, &context, (__bridge CFArrayRef)roots,
            kFSEventStreamEventIdSinceNow, 0.15, kFSEventStreamCreateFlagFileEvents | kFSEventStreamCreateFlagNoDefer);
        if (events) {
            FSEventStreamSetDispatchQueue(events, dispatch_get_main_queue());
            FSEventStreamStart(events);
        }
    }
    ~MacFiles() override
    {
        revoke();
        if (events) {
            FSEventStreamStop(events);
            FSEventStreamInvalidate(events);
            FSEventStreamRelease(events);
        }
    }
    void setAgentMode() override { [NSApp setActivationPolicy:NSApplicationActivationPolicyAccessory]; }
    bool pendingDrag() const override { return dragOffer && dragOffer->busy(); }
    bool supported() const override { return events != nullptr; }
    bool ownsClipboard() const override
    {
        return clipboardOffer &&
            [[NSPasteboard.generalPasteboard stringForType:marker]
                isEqualToString:ns(clipboardOffer->manifest["id"].toString())];
    }
    QStringList copiedFiles() const override
    {
        QStringList paths;
        if (ownsClipboard())
            return paths;
        NSArray* urls = [NSPasteboard.generalPasteboard readObjectsForClasses:@[ [NSURL class] ]
                                                                      options:@{
                                                                          NSPasteboardURLReadingFileURLsOnlyKey : @YES
                                                                      }];
        for (NSURL* url in urls)
            if (url.isFileURL)
                paths.append(qt(url.path));
        return paths;
    }
    void revoke() override
    {
        if (clipboardOffer) {
            clipboardOffer->active = false;
            if (ownsClipboard())
                [NSPasteboard.generalPasteboard clearContents];
            clipboardOffer.reset();
        }
        [pasteProviders removeAllObjects];
        processing.clear();
    }
    bool publish(const QJsonObject& offer, NativeRead read) override
    {
        if (!NativeOfferStore::validManifest(offer))
            return false;
        revoke();
        clipboardOffer = make(offer, read);
        NSMutableArray* items = [NSMutableArray new];
        for (auto v : offer["entries"].toArray()) {
            auto e = v.toObject();
            auto root = e["path"].toString();
            if (root.contains('/'))
                continue;
            DeskPasteProvider* provider = [DeskPasteProvider new];
            provider->selection = clipboardOffer;
            provider->root = root;
            provider->fileId = e["id"].toString();
            provider->directory = e["directory"].toBool();
            NSPasteboardItem* item = [NSPasteboardItem new];
            [item setDataProvider:provider forTypes:@[ NSPasteboardTypeFileURL ]];
            [item setString:ns(offer["id"].toString()) forType:marker];
            [items addObject:item];
            [pasteProviders addObject:provider];
        }
        [NSPasteboard.generalPasteboard clearContents];
        return [NSPasteboard.generalPasteboard writeObjects:items];
    }
    bool drag(const QJsonObject& offer, NativeRead read, QWindow* window) override
    {
        if (!window || pendingDrag() || !NativeOfferStore::validManifest(offer))
            return false;
        NSView* view = (__bridge NSView*)(void*)window->winId();
        if (!view || !view.window)
            return false;
        NSEvent* event = NSApp.currentEvent;
        if (!event || !(NSEvent.pressedMouseButtons & 1))
            return false;
        auto s = make(offer, read);
        dragOffer = s;
        [dragProviders removeAllObjects];
        NSMutableArray* items = [NSMutableArray new];
        for (auto v : offer["entries"].toArray()) {
            auto e = v.toObject();
            auto root = e["path"].toString();
            if (root.contains('/'))
                continue;
            DeskFilePromise* delegate = [DeskFilePromise new];
            delegate->selection = s;
            delegate->root = root;
            NSFilePromiseProvider* provider = [[NSFilePromiseProvider alloc]
                initWithFileType:e["directory"].toBool() ? @"public.folder" : @"public.data"
                        delegate:delegate];
            NSDraggingItem* item = [[NSDraggingItem alloc] initWithPasteboardWriter:provider];
            NSImage* icon = [NSWorkspace.sharedWorkspace
                iconForFileType:e["directory"].toBool() ? @"public.folder" : @"public.data"];
            [item setDraggingFrame:NSMakeRect(event.locationInWindow.x, event.locationInWindow.y, 48, 48)
                          contents:icon];
            [items addObject:item];
            [dragProviders addObject:delegate];
        }
        if (items.count == 0)
            return false;
        s->pending = int(items.count);
        s->dragging = true;
        [view beginDraggingSessionWithItems:items event:event source:dragProviders.firstObject];
        return true;
    }
};
}
std::unique_ptr<NativeFilePlatform> NativeFilePlatform::create() { return std::make_unique<MacFiles>(); }

#ifdef DESK_NATIVE_TESTS
QJsonObject nativeAdapterProbe(const QJsonObject& offer, NativeRead read, const QString& destination)
{
    auto selection = std::make_shared<MacOffer>();
    selection->manifest = offer;
    selection->read = read;
    auto root = offer["entries"].toArray().first().toObject()["path"].toString();
    DeskFilePromise* delegate = [DeskFilePromise new];
    delegate->selection = selection;
    delegate->root = root;
    __block bool called = false;
    __block bool ok = false;
    [delegate filePromiseProvider:nil
                writePromiseToURL:[NSURL fileURLWithPath:ns(destination)]
                completionHandler:^(NSError* error) {
                    called = true;
                    ok = error == nil;
                }];
    return { { "ok", called && ok } };
}
#endif
