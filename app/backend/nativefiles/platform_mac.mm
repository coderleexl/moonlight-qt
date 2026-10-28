#include "materialize.h"
#include "nativeplatform.h"
#include "offerstore.h"
#import <AppKit/AppKit.h>
#import <CoreServices/CoreServices.h>
#include <QClipboard>
#include <QDateTime>
#include <QDebug>
#include <QFile>
#include <QGuiApplication>
#include <QMimeData>
#include <QPointer>
#include <QRegularExpression>
#include <QSet>
#include <QTemporaryDir>
#include <QUuid>
#include <atomic>
#include <sys/xattr.h>
#ifdef DESK_NATIVE_TESTS
#include <QEventLoop>
#include <QTimer>
#include <copyfile.h>
#endif
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
    std::atomic<bool> pasteRequested { false };
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
    selection->pasteRequested = true;
}
@end
namespace {
class MacFiles final : public NativeFilePlatform {
    NSPasteboard* pasteboard;
    std::shared_ptr<MacOffer> clipboardOffer, dragOffer;
    qint64 ownedRevision = -1;
    NSMutableArray* pasteProviders = [NSMutableArray new];
    NSMutableArray* dragProviders = [NSMutableArray new];
    FSEventStreamRef events = nullptr;
    QSet<QString> processing;
    bool hasImageOrRichText() const
    {
        for (NSString* type in pasteboard.types) {
            const auto name = qt(type).toLower();
            if ([NSImage.imageTypes containsObject:type]
                || [type isEqualToString:NSPasteboardTypeHTML]
                || [type isEqualToString:NSPasteboardTypeRTF]
                || [type isEqualToString:NSPasteboardTypeRTFD]
                || name.contains("wechat") || name.contains("richedit"))
                return true;
        }
        return false;
    }
#ifdef DESK_NATIVE_TESTS
    int inspectedPaths = 0;
#endif
    // Filter names before any filesystem access. Probing arbitrary FSEvents
    // paths with getxattr can prompt for Photos, Documents, and other app data.
    static bool pasteCandidate(const QString& path, const QJsonObject& manifest)
    {
        const auto components = QDir::cleanPath(path).split('/');
        for (const auto& component : components) {
            const auto lower = component.toLower();
            if (lower == "library" || lower.endsWith(".photoslibrary")
                || lower.endsWith(".photolibrary") || lower.endsWith(".migratedphotolibrary")
                || lower.endsWith(".app"))
                return false;
        }
        const auto name = components.last();
        for (auto value : manifest["entries"].toArray()) {
            const auto entry = value.toObject();
            const auto root = entry["path"].toString();
            if (root.contains('/'))
                continue;
            if (name == root)
                return true;
            const auto dot = entry["directory"].toBool() ? -1 : root.lastIndexOf('.');
            const auto stem = dot > 0 ? root.left(dot) : root;
            const auto suffix = dot > 0 ? root.mid(dot) : QString();
            const QRegularExpression duplicate("^" + QRegularExpression::escape(stem)
                + "(?: copy)?(?: [0-9]+)?" + QRegularExpression::escape(suffix) + "$");
            if (duplicate.match(name).hasMatch())
                return true;
        }
        return false;
    }
    static void observed(ConstFSEventStreamRef, void* context, size_t count, void* paths,
        const FSEventStreamEventFlags[], const FSEventStreamEventId[])
    {
        auto self = static_cast<MacFiles*>(context);
        auto s = self->clipboardOffer;
        if (!s || !s->active || !s->pasteRequested || !self->ownsClipboard())
            return;
        auto array = static_cast<char**>(paths);
        for (size_t i = 0; i < count; ++i) {
            auto path = QString::fromUtf8(array[i]);
            if (path.endsWith("/.desk-promise"))
                path = QFileInfo(path).path();
            if (!pasteCandidate(path, s->manifest))
                continue;
            if (path.startsWith(QDir(s->temporary.path()).canonicalPath() + '/') || self->processing.contains(path))
                continue;
            auto encodedPath = QFile::encodeName(path);
#ifdef DESK_NATIVE_TESTS
            ++self->inspectedPaths;
#endif
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
#ifdef DESK_NATIVE_TESTS
    int observeForTest(const QString& path)
    {
        auto encoded = QFile::encodeName(path);
        char* paths[] = { encoded.data() };
        const auto before = inspectedPaths;
        observed(nullptr, this, 1, paths, nullptr, nullptr);
        return inspectedPaths - before;
    }
    bool processingForTest() const { return !processing.isEmpty(); }
#endif
    explicit MacFiles(NSPasteboard* board = NSPasteboard.generalPasteboard)
        : pasteboard(board)
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
    qint64 clipboardRevision() const override { return qint64(pasteboard.changeCount); }
    bool pendingDrag() const override { return dragOffer && dragOffer->busy(); }
    bool supported() const override { return events != nullptr; }
    bool ownsClipboard() const override
    {
        return clipboardOffer && clipboardRevision() == ownedRevision &&
            [[pasteboard stringForType:marker]
                isEqualToString:ns(clipboardOffer->manifest["id"].toString())];
    }
    QStringList copiedFiles() const override
    {
        QStringList paths;
        const auto revision = clipboardRevision();
        if (ownsClipboard())
            return paths;
        // A chat image may also expose a URL to its cached copy. Only export
        // explicit file selections: converting pixels to URLs changes paste
        // semantics in chat applications. Inspect types without reading data.
        if (hasImageOrRichText())
            return paths;
        // Read only a file representation explicitly declared by each item.
        // NSURL's pasteboard reader may negotiate other representations; that
        // must not cause background synchronization to request application data.
        for (NSPasteboardItem* item in pasteboard.pasteboardItems) {
            if (![item.types containsObject:NSPasteboardTypeFileURL])
                continue;
            NSString* value = [item stringForType:NSPasteboardTypeFileURL];
            NSURL* url = value ? [NSURL URLWithString:value] : nil;
            if (url.isFileURL && url.path.length > 0)
                paths.append(qt(url.path));
        }
        // A promised representation can replace the selection or append image
        // formats without another changeCount increment while being read.
        return clipboardRevision() == revision && !hasImageOrRichText() ? paths : QStringList();
    }
    void revoke() override
    {
        revokeClipboard(clipboardRevision());
    }
    qint64 revokeClipboard(qint64 expectedRevision) override
    {
        auto writtenRevision = expectedRevision;
        if (clipboardOffer) {
            clipboardOffer->active = false;
            if (clipboardRevision() == expectedRevision && ownsClipboard())
                writtenRevision = qint64([pasteboard clearContents]);
            clipboardOffer.reset();
        }
        ownedRevision = -1;
        [pasteProviders removeAllObjects];
        processing.clear();
        return writtenRevision;
    }
    bool publish(const QJsonObject& offer, NativeRead read) override
    {
        return publishIfUnchanged(offer, read, clipboardRevision()).status == NativeClipboardPublication::Published;
    }
    NativeClipboardPublication publishIfUnchanged(
        const QJsonObject& offer, NativeRead read, qint64 expectedRevision) override
    {
        if (!NativeOfferStore::validManifest(offer))
            return { NativeClipboardPublication::Failed, expectedRevision };
        auto selection = make(offer, read);
        NSMutableArray* items = [NSMutableArray new];
        NSMutableArray* providers = [NSMutableArray new];
        for (auto v : offer["entries"].toArray()) {
            auto e = v.toObject();
            auto root = e["path"].toString();
            if (root.contains('/'))
                continue;
            DeskPasteProvider* provider = [DeskPasteProvider new];
            provider->selection = selection;
            provider->root = root;
            provider->fileId = e["id"].toString();
            provider->directory = e["directory"].toBool();
            NSPasteboardItem* item = [NSPasteboardItem new];
            [item setDataProvider:provider forTypes:@[ NSPasteboardTypeFileURL ]];
            [item setString:ns(offer["id"].toString()) forType:marker];
            [items addObject:item];
            [providers addObject:provider];
        }
        // Build all providers before checking the clipboard for a newer copy.
        // NSPasteboard offers no atomic compare-and-write, so keep this final
        // check adjacent to the first operation that changes the pasteboard.
        const auto currentRevision = clipboardRevision();
        if (currentRevision != expectedRevision)
            return { NativeClipboardPublication::Superseded, expectedRevision };
        const auto writtenRevision = qint64([pasteboard clearContents]);
        if (clipboardOffer)
            clipboardOffer->active = false;
        clipboardOffer = selection;
        pasteProviders = providers;
        processing.clear();
        ownedRevision = writtenRevision;
        const bool written = [pasteboard writeObjects:items];
        if (!written)
            qWarning() << "macOS pasteboard writeObjects failed; items:" << int(items.count)
                       << "changeCount:" << qint64(pasteboard.changeCount);
        // writeObjects fills the generation returned by clearContents. Sampling
        // changeCount here could instead consume a concurrent local copy.
        return { written ? NativeClipboardPublication::Published : NativeClipboardPublication::Failed,
            writtenRevision };
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
@interface DeskClipboardReadProbe : NSObject <NSPasteboardItemDataProvider> {
@public
    NSMutableArray<NSString*>* requests;
    NSString* fileUrl;
    NSPasteboard* replacementBoard;
    bool appendImage;
}
@end
@implementation DeskClipboardReadProbe
- (void)pasteboard:(NSPasteboard*)pasteboard provideDataForType:(NSPasteboardType)type
{
    [requests addObject:type];
    [pasteboard setPropertyList:@[ [NSURL URLWithString:fileUrl].path ] forType:type];
}
- (void)pasteboard:(NSPasteboard*)pasteboard item:(NSPasteboardItem*)item provideDataForType:(NSPasteboardType)type
{
    Q_UNUSED(pasteboard);
    [requests addObject:type];
    [item setString:fileUrl forType:type];
    if (replacementBoard) {
        // Model a source application replacing its selection while a promised
        // representation is being read. This is always a private test board.
        if (!appendImage)
            [replacementBoard clearContents];
        [replacementBoard setData:[@"new picture" dataUsingEncoding:NSUTF8StringEncoding]
                          forType:NSPasteboardTypePNG];
    }
}
@end
QJsonObject nativePasteWatcherProbe(const QJsonObject& offer, NativeRead read, const QString& destination)
{
    NSPasteboard* board = [NSPasteboard pasteboardWithUniqueName];
    QJsonObject result;
    {
        MacFiles platform(board);
        result["published"] = platform.publish(offer, read);
        result["beforePasteReads"] = platform.observeForTest(destination);
        NSArray* urls = [board readObjectsForClasses:@[ [NSURL class] ]
                                            options:@{ NSPasteboardURLReadingFileURLsOnlyKey : @YES }];
        if (urls.count != 1)
            return { { "error", "Expected one file URL" } };
        const auto name = destination.mid(destination.lastIndexOf('/') + 1);
        result["unrelatedReads"] = platform.observeForTest("/unrelated/changed.log");
        result["photoReads"] = platform.observeForTest("/Pictures/Photos Library.photoslibrary/" + name);
        result["libraryReads"] = platform.observeForTest("/Users/example/Library/Caches/" + name);
        // Finder copies both the placeholder and its capability xattr.
        const auto source = QFile::encodeName(qt(((NSURL*)urls.firstObject).path));
        const auto target = QFile::encodeName(destination);
        result["copied"] = copyfile(source.constData(), target.constData(), nullptr, COPYFILE_ALL | COPYFILE_EXCL) == 0;
        result["pasteReads"] = platform.observeForTest(destination);
        QEventLoop loop;
        QTimer check;
        QObject::connect(&check, &QTimer::timeout, &loop, [&] {
            if (!platform.processingForTest())
                loop.quit();
        });
        check.start(10);
        QTimer::singleShot(5000, &loop, &QEventLoop::quit);
        loop.exec();
        result["finished"] = !platform.processingForTest();
        [board clearContents];
        [board setString:@"new local copy" forType:NSPasteboardTypeString];
        result["afterLocalCopyReads"] = platform.observeForTest(destination);
        const auto revision = board.changeCount;
        platform.revoke();
        result["localCopyPreserved"] = board.changeCount == revision;
    }
    [board releaseGlobally];
    return result;
}
QJsonObject nativeImageClipboardProbe(const QString& file)
{
    NSPasteboard* board = [NSPasteboard pasteboardWithUniqueName];
    QJsonObject result;
    {
        MacFiles platform(board);
        const auto url = [NSURL fileURLWithPath:ns(file)];
        [board writeObjects:@[ url ]];
        result["fileCount"] = platform.copiedFiles().size();
        NSPasteboardItem* picture = [NSPasteboardItem new];
        // Chat applications can provide both pixels and a cached file URL.
        NSData* pixels = [@"test image representation" dataUsingEncoding:NSUTF8StringEncoding];
        [picture setData:pixels forType:NSPasteboardTypePNG];
        [picture setString:url.absoluteString forType:NSPasteboardTypeFileURL];
        [board clearContents];
        [board writeObjects:@[ picture ]];
        const auto revision = board.changeCount;
        result["imageFileCount"] = platform.copiedFiles().size();
        platform.revoke();
        result["unchanged"] = board.changeCount == revision
            && [[board dataForType:NSPasteboardTypePNG] isEqualToData:pixels];
        QJsonArray richCopies;
        for (NSString* type in @[ @"com.trolltech.anymime.WeChat_RichEdit_Format",
                 @"com.trolltech.anymime.QQ_Unicode_RichEdit_Format",
                 @"com.trolltech.anymime.x-application-mimetype-xwechat",
                 NSPasteboardTypeHTML, NSPasteboardTypeRTF ]) {
            // Qt applications also publish legacy flavor names containing
            // underscores, which NSPasteboardItem refuses to create.
            PasteboardRef legacy = nullptr;
            PasteboardCreate((__bridge CFStringRef)board.name, &legacy);
            PasteboardClear(legacy);
            NSData* richData = [@"opaque application representation" dataUsingEncoding:NSUTF8StringEncoding];
            NSData* urlData = [url.absoluteString dataUsingEncoding:NSUTF8StringEncoding];
            PasteboardPutItemFlavor(legacy, (PasteboardItemID)1, (__bridge CFStringRef)type,
                (__bridge CFDataRef)richData, 0);
            PasteboardPutItemFlavor(legacy, (PasteboardItemID)1, (__bridge CFStringRef)NSPasteboardTypeFileURL,
                (__bridge CFDataRef)urlData, 0);
            CFRelease(legacy);
            const auto before = board.changeCount;
            const auto count = platform.copiedFiles().size();
            platform.revoke();
            richCopies.append(QJsonObject { { "files", count },
                { "unchanged", board.changeCount == before }, { "declared", [board.types containsObject:type] == YES },
                { "type", qt(type) } });
        }
        result["richCopies"] = richCopies;
        QJsonArray deferredCopies;
        NSArray* cases = @[
            @{ @"name" : @"text", @"items" : @[ @[ NSPasteboardTypeString ] ] },
            @{ @"name" : @"generic-url", @"items" : @[ @[ NSPasteboardTypeURL ] ] },
            @{ @"name" : @"private-format", @"items" : @[ @[ @"org.example.screenshot" ] ] },
            @{ @"name" : @"image-and-file", @"items" : @[ @[ NSPasteboardTypePNG, NSPasteboardTypeFileURL ] ] },
            @{ @"name" : @"rich-and-file", @"items" : @[ @[ @"org.example.wechat-rich-copy", NSPasteboardTypeFileURL ] ] },
            @{ @"name" : @"explicit-file", @"items" : @[ @[ NSPasteboardTypeFileURL, NSPasteboardTypeString ] ] },
            @{ @"name" : @"mixed-items", @"items" : @[ @[ NSPasteboardTypeURL ], @[ NSPasteboardTypeFileURL ] ] },
            @{ @"name" : @"replaced-during-read", @"items" : @[ @[ NSPasteboardTypeFileURL ] ] },
            @{ @"name" : @"image-added-during-read", @"items" : @[ @[ NSPasteboardTypeFileURL ] ] }
        ];
        for (NSDictionary* test : cases) {
            DeskClipboardReadProbe* provider = [DeskClipboardReadProbe new];
            provider->requests = [NSMutableArray new];
            provider->fileUrl = url.absoluteString;
            if ([test[@"name"] isEqualToString:@"replaced-during-read"])
                provider->replacementBoard = board;
            if ([test[@"name"] isEqualToString:@"image-added-during-read"]) {
                provider->replacementBoard = board;
                provider->appendImage = true;
            }
            NSMutableArray* items = [NSMutableArray new];
            for (NSArray* types in test[@"items"]) {
                NSPasteboardItem* item = [NSPasteboardItem new];
                [item setDataProvider:provider forTypes:types];
                [items addObject:item];
            }
            [board clearContents];
            const bool written = [board writeObjects:items];
            const auto before = board.changeCount;
            const auto files = platform.copiedFiles();
            int fileReads = 0;
            for (NSString* requested in provider->requests)
                if ([requested isEqualToString:NSPasteboardTypeFileURL])
                    ++fileReads;
            deferredCopies.append(QJsonObject { { "name", qt(test[@"name"]) }, { "written", written },
                { "files", files.size() }, { "reads", int(provider->requests.count) }, { "fileReads", fileReads },
                { "unchanged", board.changeCount == before },
                { "hasImage", [board.types containsObject:NSPasteboardTypePNG] == YES } });
            // Release promised items while their provider is still alive.
            [board clearContents];
        }
        result["deferredCopies"] = deferredCopies;
        DeskClipboardReadProbe* legacyProvider = [DeskClipboardReadProbe new];
        legacyProvider->requests = [NSMutableArray new];
        legacyProvider->fileUrl = url.absoluteString;
        [board declareTypes:@[ @"NSFilenamesPboardType" ] owner:legacyProvider];
        const auto legacyRevision = board.changeCount;
        result["legacyFiles"] = platform.copiedFiles().size();
        result["legacyReads"] = int(legacyProvider->requests.count);
        result["legacyUnchanged"] = board.changeCount == legacyRevision;
        [board clearContents];
    }
    [board releaseGlobally];
    return result;
}
QJsonObject nativeClipboardProbe(const QJsonObject& offer, NativeRead read)
{
    NSPasteboard* board = [NSPasteboard pasteboardWithUniqueName];
    QJsonObject result;
    {
        MacFiles platform(board);
        const bool published = platform.publish(offer, read);
        NSArray* urls = [board readObjectsForClasses:@[ [NSURL class] ]
                                            options:@{ NSPasteboardURLReadingFileURLsOnlyKey : @YES }];
        result = { { "ok", published }, { "owned", platform.ownsClipboard() }, { "urls", int(urls.count) } };
        // Replacing an offer must also work without retaining expired providers.
        result["replaced"] = platform.publish(offer, read);
        result["ownedAfterReplace"] = platform.ownsClipboard();
        QJsonArray protectedCopies;
        for (NSString* type in @[ NSPasteboardTypePNG, NSPasteboardTypeHTML, @"org.example.wechat-rich-copy" ]) {
            const auto staleRevision = platform.clipboardRevision();
            NSPasteboardItem* item = [NSPasteboardItem new];
            NSData* content = [@"new local picture or rich copy" dataUsingEncoding:NSUTF8StringEncoding];
            [item setData:content forType:type];
            // Clipboard managers may preserve unknown metadata. An old marker
            // alone must not grant ownership over a new clipboard generation.
            [item setString:ns(offer["id"].toString()) forType:marker];
            [board clearContents];
            [board writeObjects:@[ item ]];
            const auto localRevision = platform.clipboardRevision();
            const bool owned = platform.ownsClipboard();
            const auto stale = platform.publishIfUnchanged(offer, read, staleRevision);
            platform.revoke();
            const bool preserved = platform.clipboardRevision() == localRevision
                && [[board dataForType:type] isEqualToData:content];
            const auto current = platform.publishIfUnchanged(offer, read, localRevision);
            protectedCopies.append(QJsonObject { { "type", qt(type) }, { "ownedLocalCopy", owned },
                { "superseded", stale.status == NativeClipboardPublication::Superseded },
                { "preserved", preserved }, { "published", current.status == NativeClipboardPublication::Published },
                { "revisionMatches", current.revision == platform.clipboardRevision() },
                { "ownedAfterPublish", platform.ownsClipboard() } });
        }
        result["protectedCopies"] = protectedCopies;
        const auto beforeClear = platform.clipboardRevision();
        const auto clearedRevision = platform.revokeClipboard(beforeClear);
        result["ownedClearRevision"] = clearedRevision == platform.clipboardRevision()
            && clearedRevision > beforeClear;
        NSData* newImage = [@"another local picture" dataUsingEncoding:NSUTF8StringEncoding];
        [board clearContents];
        [board setData:newImage forType:NSPasteboardTypePNG];
        const auto localRevision = platform.clipboardRevision();
        result["unownedClearRevision"] = platform.revokeClipboard(clearedRevision) == clearedRevision;
        result["unownedClearPreserved"] = platform.clipboardRevision() == localRevision
            && [[board dataForType:NSPasteboardTypePNG] isEqualToData:newImage];
    }
    [board releaseGlobally];
    return result;
}
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
