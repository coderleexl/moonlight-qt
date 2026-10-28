#include "fakestate.h"
#include "backend/nativefiles/nativeplatform.h"
#include <QJsonArray>

namespace {
class FakeClipboard final : public NativeFilePlatform {
    QString path = qEnvironmentVariable("DESK_TEST_CLIPBOARD_STATE");

public:
    bool supported() const override { return !path.isEmpty(); }
    qint64 clipboardRevision() const override
    {
        const auto state = changeFakeClipboard(path, [](QJsonObject& state) {
            const auto revision = state.value("revision");
            state["observedRevision"] = revision;
        });
        return state["revision"].toVariant().toLongLong();
    }
    QStringList copiedFiles() const override
    {
        const auto state = changeFakeClipboard(path, [](QJsonObject& state) {
            const auto revision = state.value("revision");
            state["inspectedRevision"] = revision;
        });
        // Images can advertise cached paths, but remain image clipboard content.
        return state["kind"] == "file" ? state["files"].toVariant().toStringList() : QStringList();
    }
    bool ownsClipboard() const override { return readFakeClipboard(path)["owned"].toBool(); }
    bool publish(const QJsonObject& offer, NativeRead) override
    {
        changeFakeClipboard(path, [&](QJsonObject& state) {
            auto publications = state["publications"].toArray();
            publications.append(offer["id"]);
            state["publications"] = publications;
            state["kind"] = "remote";
            state["owned"] = true;
            state["revision"] = state["revision"].toVariant().toLongLong() + 1;
        });
        return true;
    }
    NativeClipboardPublication publishIfUnchanged(
        const QJsonObject& offer, NativeRead, qint64 expectedRevision) override
    {
        NativeClipboardPublication result { NativeClipboardPublication::Superseded, expectedRevision };
        changeFakeClipboard(path, [&](QJsonObject& state) {
            if (state["revision"].toVariant().toLongLong() != expectedRevision)
                return;
            auto publications = state["publications"].toArray();
            publications.append(offer["id"]);
            state["publications"] = publications;
            state["kind"] = "remote";
            state["owned"] = true;
            const auto writtenRevision = expectedRevision + 1;
            state["revision"] = writtenRevision;
            result = { NativeClipboardPublication::Published, writtenRevision };
            if (state.take("injectImageAfterPublication").toBool()) {
                // Another application copies immediately after our OS write,
                // before the native publication call returns to the agent.
                state["kind"] = "image";
                state["owned"] = false;
                state["revision"] = writtenRevision + 1;
                state["injectedRevision"] = writtenRevision + 1;
            }
        });
        return result;
    }
    void revoke() override { revokeClipboard(clipboardRevision()); }
    qint64 revokeClipboard(qint64 expectedRevision) override
    {
        auto result = expectedRevision;
        changeFakeClipboard(path, [&](QJsonObject& state) {
            if (!state["owned"].toBool())
                return;
            state["owned"] = false;
            state["kind"] = "empty";
            state["revision"] = state["revision"].toVariant().toLongLong() + 1;
            result = state["revision"].toVariant().toLongLong();
        });
        return result;
    }
    bool drag(const QJsonObject&, NativeRead, QWindow*) override { return false; }
};
}

std::unique_ptr<NativeFilePlatform> NativeFilePlatform::create()
{
    return std::make_unique<FakeClipboard>();
}
