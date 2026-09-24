#include "nativeplatform.h"
#include "offerstore.h"
#include <QClipboard>
#include <QFile>
#include <QGuiApplication>
#include <QJsonArray>
#include <QMimeData>
#include <QUrl>
#include <atomic>
#include <climits>
#include <cstring>
#include <ole2.h>
#include <shellapi.h>
#include <shlobj.h>
#include <windows.h>
namespace {
struct Selection {
    QJsonObject offer;
    NativeRead read;
    std::atomic<bool> active { true };
};
class FileStream final : public IStream {
    std::atomic<ULONG> refs { 1 };
    std::shared_ptr<Selection> data;
    QJsonObject file;
    ULONGLONG offset = 0;

public:
    FileStream(std::shared_ptr<Selection> d, QJsonObject f)
        : data(std::move(d))
        , file(std::move(f))
    {
    }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id, void** out) override
    {
        if (!out)
            return E_POINTER;
        *out = nullptr;
        if (id == IID_IUnknown || id == IID_ISequentialStream || id == IID_IStream) {
            *out = static_cast<IStream*>(this);
            AddRef();
            return S_OK;
        }
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs; }
    ULONG STDMETHODCALLTYPE Release() override
    {
        auto n = --refs;
        if (!n)
            delete this;
        return n;
    }
    HRESULT STDMETHODCALLTYPE Read(void* out, ULONG size, ULONG* done) override
    {
        if (done)
            *done = 0;
        if (!out)
            return STG_E_INVALIDPOINTER;
        if (!data->active)
            return STG_E_REVERTED;
        auto total = ULONGLONG(file["size"].toDouble());
        ULONG count = 0;
        while (count < size && offset < total) {
            int amount = int(qMin<ULONGLONG>(qMin<ULONGLONG>(size - count, 256 * 1024), total - offset));
            if (!data->active)
                return STG_E_REVERTED;
            auto r = data->read(file["id"].toString(), qint64(offset), amount);
            auto encoded = r["data"].toString().toLatin1();
            auto bytes = QByteArray::fromBase64(encoded);
            if (!r["ok"].toBool() || bytes.size() != amount || bytes.toBase64() != encoded)
                return STG_E_READFAULT;
            memcpy(static_cast<char*>(out) + count, bytes.constData(), amount);
            count += amount;
            offset += amount;
            if (done)
                *done = count;
        }
        return count == size ? S_OK : S_FALSE;
    }
    HRESULT STDMETHODCALLTYPE Write(const void*, ULONG, ULONG*) override { return STG_E_ACCESSDENIED; }
    HRESULT STDMETHODCALLTYPE Seek(LARGE_INTEGER move, DWORD origin, ULARGE_INTEGER* position) override
    {
        LONGLONG base = origin == STREAM_SEEK_SET ? 0
            : origin == STREAM_SEEK_CUR           ? LONGLONG(offset)
            : origin == STREAM_SEEK_END           ? LONGLONG(file["size"].toDouble())
                                                  : -1;
        if (base < 0 || move.QuadPart < -base || (move.QuadPart > 0 && move.QuadPart > LLONG_MAX - base))
            return STG_E_INVALIDFUNCTION;
        offset = ULONGLONG(base + move.QuadPart);
        if (position)
            position->QuadPart = offset;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE SetSize(ULARGE_INTEGER) override { return STG_E_ACCESSDENIED; }
    HRESULT STDMETHODCALLTYPE CopyTo(
        IStream* target, ULARGE_INTEGER size, ULARGE_INTEGER* rd, ULARGE_INTEGER* wr) override
    {
        if (rd)
            rd->QuadPart = 0;
        if (wr)
            wr->QuadPart = 0;
        char buf[65536];
        ULONGLONG copied = 0;
        while (copied < size.QuadPart) {
            ULONG got = 0, wrote = 0;
            auto hr = Read(buf, ULONG(qMin<ULONGLONG>(sizeof(buf), size.QuadPart - copied)), &got);
            if (FAILED(hr))
                return hr;
            if (!got)
                break;
            hr = target->Write(buf, got, &wrote);
            if (rd)
                rd->QuadPart += got;
            if (wr)
                wr->QuadPart += wrote;
            if (FAILED(hr) || wrote != got)
                return STG_E_WRITEFAULT;
            copied += got;
        }
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Commit(DWORD) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE Revert() override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE LockRegion(ULARGE_INTEGER, ULARGE_INTEGER, DWORD) override
    {
        return STG_E_INVALIDFUNCTION;
    }
    HRESULT STDMETHODCALLTYPE UnlockRegion(ULARGE_INTEGER, ULARGE_INTEGER, DWORD) override
    {
        return STG_E_INVALIDFUNCTION;
    }
    HRESULT STDMETHODCALLTYPE Stat(STATSTG* stat, DWORD) override
    {
        if (!stat)
            return E_POINTER;
        memset(stat, 0, sizeof(*stat));
        stat->type = STGTY_STREAM;
        stat->cbSize.QuadPart = ULONGLONG(file["size"].toDouble());
        stat->grfMode = STGM_READ;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Clone(IStream** result) override
    {
        if (!result)
            return E_POINTER;
        auto copy = new FileStream(data, file);
        copy->offset = offset;
        *result = copy;
        return S_OK;
    }
};
class FileData final : public IDataObject {
    std::atomic<ULONG> refs { 1 };

public:
    std::shared_ptr<Selection> data;
    const CLIPFORMAT descriptors = CLIPFORMAT(RegisterClipboardFormatW(CFSTR_FILEDESCRIPTORW));
    const CLIPFORMAT contents = CLIPFORMAT(RegisterClipboardFormatW(CFSTR_FILECONTENTS));
    explicit FileData(std::shared_ptr<Selection> value)
        : data(std::move(value))
    {
    }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id, void** out) override
    {
        if (!out)
            return E_POINTER;
        *out = nullptr;
        if (id == IID_IUnknown || id == IID_IDataObject) {
            *out = static_cast<IDataObject*>(this);
            AddRef();
            return S_OK;
        }
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs; }
    ULONG STDMETHODCALLTYPE Release() override
    {
        auto n = --refs;
        if (!n)
            delete this;
        return n;
    }
    HRESULT STDMETHODCALLTYPE QueryGetData(FORMATETC* f) override
    {
        if (!f || f->dwAspect != DVASPECT_CONTENT)
            return DV_E_FORMATETC;
        if (f->cfFormat == descriptors && (f->tymed & TYMED_HGLOBAL))
            return S_OK;
        if (f->cfFormat == contents && (f->tymed & TYMED_ISTREAM) && f->lindex >= 0
            && f->lindex < data->offer["entries"].toArray().size())
            return S_OK;
        return DV_E_FORMATETC;
    }
    HRESULT STDMETHODCALLTYPE GetData(FORMATETC* f, STGMEDIUM* out) override
    {
        if (!out)
            return E_POINTER;
        memset(out, 0, sizeof(*out));
        auto hr = QueryGetData(f);
        if (FAILED(hr))
            return hr;
        if (!data->active)
            return STG_E_REVERTED;
        auto entries = data->offer["entries"].toArray();
        if (f->cfFormat == contents) {
            auto entry = entries[f->lindex].toObject();
            if (entry["directory"].toBool())
                return DV_E_LINDEX;
            if (!data->read(entry["id"].toString(), 0, 0)["ok"].toBool())
                return STG_E_READFAULT;
            out->tymed = TYMED_ISTREAM;
            out->pstm = new FileStream(data, entry);
            return S_OK;
        }
        SIZE_T size = sizeof(FILEGROUPDESCRIPTORW) + (entries.size() - 1) * sizeof(FILEDESCRIPTORW);
        auto memory = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, size);
        if (!memory)
            return E_OUTOFMEMORY;
        auto group = static_cast<FILEGROUPDESCRIPTORW*>(GlobalLock(memory));
        if (!group) {
            GlobalFree(memory);
            return E_OUTOFMEMORY;
        }
        group->cItems = UINT(entries.size());
        for (int i = 0; i < entries.size(); ++i) {
            auto e = entries[i].toObject();
            auto& d = group->fgd[i];
            auto name = e["path"].toString().replace('/', '\\');
            if (name.size() >= MAX_PATH) {
                GlobalUnlock(memory);
                GlobalFree(memory);
                return DV_E_FORMATETC;
            }
            d.dwFlags = FD_ATTRIBUTES | FD_FILESIZE | FD_PROGRESSUI | FD_UNICODE;
            d.dwFileAttributes = e["directory"].toBool() ? FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_NORMAL;
            auto bytes = ULONGLONG(e["size"].toDouble());
            d.nFileSizeHigh = DWORD(bytes >> 32);
            d.nFileSizeLow = DWORD(bytes);
            memcpy(d.cFileName, name.utf16(), name.size() * sizeof(wchar_t));
        }
        GlobalUnlock(memory);
        out->tymed = TYMED_HGLOBAL;
        out->hGlobal = memory;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetDataHere(FORMATETC*, STGMEDIUM*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE GetCanonicalFormatEtc(FORMATETC*, FORMATETC* out) override
    {
        if (out)
            out->ptd = nullptr;
        return E_NOTIMPL;
    }
    HRESULT STDMETHODCALLTYPE SetData(FORMATETC*, STGMEDIUM* medium, BOOL release) override
    {
        if (release && medium)
            ReleaseStgMedium(medium);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE EnumFormatEtc(DWORD direction, IEnumFORMATETC** result) override
    {
        if (direction != DATADIR_GET)
            return E_NOTIMPL;
        FORMATETC formats[] = { { descriptors, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL },
            { contents, nullptr, DVASPECT_CONTENT, -1, TYMED_ISTREAM } };
        return SHCreateStdEnumFmtEtc(2, formats, result);
    }
    HRESULT STDMETHODCALLTYPE DAdvise(FORMATETC*, DWORD, IAdviseSink*, DWORD*) override
    {
        return OLE_E_ADVISENOTSUPPORTED;
    }
    HRESULT STDMETHODCALLTYPE DUnadvise(DWORD) override { return OLE_E_ADVISENOTSUPPORTED; }
    HRESULT STDMETHODCALLTYPE EnumDAdvise(IEnumSTATDATA**) override { return OLE_E_ADVISENOTSUPPORTED; }
};
class DropSource final : public IDropSource {
    std::atomic<ULONG> refs { 1 };

public:
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id, void** out) override
    {
        if (!out)
            return E_POINTER;
        *out = nullptr;
        if (id == IID_IUnknown || id == IID_IDropSource) {
            *out = static_cast<IDropSource*>(this);
            AddRef();
            return S_OK;
        }
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs; }
    ULONG STDMETHODCALLTYPE Release() override
    {
        auto n = --refs;
        if (!n)
            delete this;
        return n;
    }
    HRESULT STDMETHODCALLTYPE QueryContinueDrag(BOOL escape, DWORD keys) override
    {
        return escape ? DRAGDROP_S_CANCEL : !(keys & MK_LBUTTON) ? DRAGDROP_S_DROP : S_OK;
    }
    HRESULT STDMETHODCALLTYPE GiveFeedback(DWORD) override { return DRAGDROP_S_USEDEFAULTCURSORS; }
};
class WindowsFiles final : public NativeFilePlatform {
    HRESULT initialized = OleInitialize(nullptr);
    FileData* clipboardData = nullptr;

public:
    ~WindowsFiles() override
    {
        revoke();
        if (SUCCEEDED(initialized))
            OleUninitialize();
    }
    bool supported() const override { return SUCCEEDED(initialized); }
    bool ownsClipboard() const override { return clipboardData && OleIsCurrentClipboard(clipboardData) == S_OK; }
    QStringList copiedFiles() const override
    {
        QStringList files;
        if (ownsClipboard())
            return files;
        for (const auto& url : QGuiApplication::clipboard()->mimeData()->urls())
            if (url.isLocalFile())
                files.append(url.toLocalFile());
        return files;
    }
    bool validate(const QJsonObject& offer)
    {
        if (!NativeOfferStore::validManifest(offer))
            return false;
        for (auto v : offer["entries"].toArray())
            if (v.toObject()["path"].toString().size() >= MAX_PATH) {
                emit error(tr("A selected path is too long for Windows file dragging."));
                return false;
            }
        return true;
    }
    bool publish(const QJsonObject& offer, NativeRead read) override
    {
        if (!validate(offer))
            return false;
        revoke();
        auto selection = std::make_shared<Selection>();
        selection->offer = offer;
        selection->read = read;
        clipboardData = new FileData(selection);
        if (FAILED(OleSetClipboard(clipboardData))) {
            revoke();
            return false;
        }
        return true;
    }
    void revoke() override
    {
        if (!clipboardData)
            return;
        clipboardData->data->active = false;
        if (ownsClipboard())
            OleSetClipboard(nullptr);
        clipboardData->Release();
        clipboardData = nullptr;
    }
    bool drag(const QJsonObject& offer, NativeRead read, QWindow*) override
    {
        if (!validate(offer))
            return false;
        auto selection = std::make_shared<Selection>();
        selection->offer = offer;
        selection->read = read;
        auto data = new FileData(selection);
        auto source = new DropSource();
        DWORD effect = 0;
        auto result = DoDragDrop(data, source, DROPEFFECT_COPY, &effect);
        data->Release();
        source->Release();
        return result == DRAGDROP_S_DROP;
    }
};
}
std::unique_ptr<NativeFilePlatform> NativeFilePlatform::create() { return std::make_unique<WindowsFiles>(); }

#ifdef DESK_NATIVE_TESTS
QJsonObject nativeAdapterProbe(const QJsonObject& offer, NativeRead read, const QString& destination)
{
    auto selection = std::make_shared<Selection>();
    selection->offer = offer;
    selection->read = read;
    auto data = new FileData(selection);
    FORMATETC format { data->contents, nullptr, DVASPECT_CONTENT, 0, TYMED_ISTREAM };
    STGMEDIUM medium {};
    auto result = data->GetData(&format, &medium);
    QFile output(destination);
    bool ok = SUCCEEDED(result) && output.open(QIODevice::WriteOnly);
    if (ok) {
        char bytes[4096];
        ULONG count = 0;
        do {
            result = medium.pstm->Read(bytes, sizeof(bytes), &count);
            if (FAILED(result) || output.write(bytes, count) != count) {
                ok = false;
                break;
            }
        } while (count);
    }
    if (medium.tymed)
        ReleaseStgMedium(&medium);
    data->Release();
    return { { "ok", ok } };
}
#endif
