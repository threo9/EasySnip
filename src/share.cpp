#include "native.h"
#include <QWidget>
#include <QDir>
#include <QDebug>
#include <atomic>
#include <windows.h>
#include <inspectable.h>
#include <roapi.h>
#include <winstring.h>
#include <shobjidl.h>

namespace EasySnipWinRT {
// ABI prefixes mirror Microsoft's Windows SDK DataTransfer and Storage.Streams
// declarations. Keeping these small declarations here supports the user's
// MinGW 7.3 SDK, which predates C++/WinRT headers.
struct ShareToken {
    qint64 value;
};
struct ShareProperties : IInspectable {
    virtual HRESULT STDMETHODCALLTYPE get_Title(HSTRING *) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_Title(HSTRING) = 0;
};
struct ShareData : IInspectable {
    virtual HRESULT STDMETHODCALLTYPE GetView(IInspectable **) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_Properties(ShareProperties **) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_RequestedOperation(int *) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_RequestedOperation(int) = 0;
    virtual HRESULT STDMETHODCALLTYPE add_OperationCompleted(IUnknown *, ShareToken *) = 0;
    virtual HRESULT STDMETHODCALLTYPE remove_OperationCompleted(ShareToken) = 0;
    virtual HRESULT STDMETHODCALLTYPE add_Destroyed(IUnknown *, ShareToken *) = 0;
    virtual HRESULT STDMETHODCALLTYPE remove_Destroyed(ShareToken) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetData(HSTRING, IInspectable *) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetDataProvider(HSTRING, IUnknown *) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetText(HSTRING) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetUri(IInspectable *) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetHtmlFormat(HSTRING) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_ResourceMap(IInspectable **) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetRtf(HSTRING) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetBitmap(IInspectable *) = 0;
};
struct ShareRequest : IInspectable {
    virtual HRESULT STDMETHODCALLTYPE get_Data(ShareData **) = 0;
};
struct ShareArguments : IInspectable {
    virtual HRESULT STDMETHODCALLTYPE get_Request(ShareRequest **) = 0;
};
struct ShareEvent : IUnknown {
    virtual HRESULT STDMETHODCALLTYPE Invoke(IInspectable *, ShareArguments *) = 0;
};
struct ShareManager : IInspectable {
    virtual HRESULT STDMETHODCALLTYPE add_DataRequested(ShareEvent *, ShareToken *) = 0;
    virtual HRESULT STDMETHODCALLTYPE remove_DataRequested(ShareToken) = 0;
};
struct StreamReferenceFactory : IInspectable {
    virtual HRESULT STDMETHODCALLTYPE CreateFromFile(IInspectable *, IInspectable **) = 0;
    virtual HRESULT STDMETHODCALLTYPE CreateFromUri(IInspectable *, IInspectable **) = 0;
    virtual HRESULT STDMETHODCALLTYPE CreateFromStream(IInspectable *, IInspectable **) = 0;
};

IID interfaceId(const wchar_t *text) {
    IID result{};
    IIDFromString(text, &result);
    return result;
}
const IID managerId = interfaceId(L"{a5caee9b-8708-49d1-8d36-67d25a8da00c}");
const IID eventId = interfaceId(L"{ec6f9cc8-46d0-5e0e-b4d2-7d7773ae37a0}");
const IID interopId = interfaceId(L"{3a3dcd6c-3eab-43dc-bcde-45671ce800c8}");
const IID streamId = interfaceId(L"{905a0fe1-bc53-11df-8c49-001e4fc686da}");
const IID referenceFactoryId = interfaceId(L"{857309dc-3fbf-4e7d-986f-ef3b1a07a964}");
const IID dataId = interfaceId(L"{61ebf5c7-efea-4346-9554-981d7e198ffe}");

struct Runtime {
    HMODULE combase = LoadLibraryW(L"combase.dll");
    HMODULE shcore = LoadLibraryW(L"shcore.dll");
    using CreateStream = HRESULT(WINAPI *)(PCWSTR, DWORD, REFIID, void **);
    decltype(&RoInitialize) initialize =
        reinterpret_cast<decltype(&RoInitialize)>(GetProcAddress(combase, "RoInitialize"));
    decltype(&RoUninitialize) uninitialize =
        reinterpret_cast<decltype(&RoUninitialize)>(GetProcAddress(combase, "RoUninitialize"));
    decltype(&RoGetActivationFactory) factory = reinterpret_cast<decltype(&RoGetActivationFactory)>(
        GetProcAddress(combase, "RoGetActivationFactory"));
    decltype(&RoActivateInstance) activate =
        reinterpret_cast<decltype(&RoActivateInstance)>(GetProcAddress(combase, "RoActivateInstance"));
    decltype(&WindowsCreateString) createString =
        reinterpret_cast<decltype(&WindowsCreateString)>(GetProcAddress(combase, "WindowsCreateString"));
    decltype(&WindowsDeleteString) deleteString =
        reinterpret_cast<decltype(&WindowsDeleteString)>(GetProcAddress(combase, "WindowsDeleteString"));
    CreateStream stream =
        reinterpret_cast<CreateStream>(GetProcAddress(shcore, "CreateRandomAccessStreamOnFile"));
    bool available() const {
        return initialize && uninitialize && factory && activate && createString && deleteString && stream;
    }
    HRESULT getFactory(const wchar_t *name, REFIID id, void **result) const {
        HSTRING text = nullptr;
        HRESULT status = createString(name, UINT32(wcslen(name)), &text);
        if (SUCCEEDED(status))
            status = factory(text, id, result);
        if (text)
            deleteString(text);
        return status;
    }
};
Runtime &runtime() {
    static Runtime functions;
    return functions;
}

class ShareHandler final : public ShareEvent {
  public:
    explicit ShareHandler(IInspectable *bitmap) : bitmap_(bitmap) {
        bitmap_->AddRef();
    }
    ~ShareHandler() {
        bitmap_->Release();
    }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id, void **object) override {
        if (!object)
            return E_POINTER;
        *object = nullptr;
        if (id != IID_IUnknown && id != eventId)
            return E_NOINTERFACE;
        *object = static_cast<ShareEvent *>(this);
        AddRef();
        return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override {
        return ++references_;
    }
    ULONG STDMETHODCALLTYPE Release() override {
        const ULONG remaining = --references_;
        if (remaining == 0)
            delete this;
        return remaining;
    }
    HRESULT populate(ShareData *data) {
        ShareProperties *properties = nullptr;
        HRESULT status = data->get_Properties(&properties);
        HSTRING title = nullptr;
        if (SUCCEEDED(status))
            status = runtime().createString(L"EasySnip screenshot", 19, &title);
        if (SUCCEEDED(status))
            status = properties->put_Title(title);
        if (title)
            runtime().deleteString(title);
        if (properties)
            properties->Release();
        if (SUCCEEDED(status))
            status = data->SetBitmap(bitmap_);
        return status;
    }
    HRESULT STDMETHODCALLTYPE Invoke(IInspectable *, ShareArguments *arguments) override {
        ShareRequest *request = nullptr;
        ShareData *data = nullptr;
        HRESULT status = arguments->get_Request(&request);
        if (SUCCEEDED(status))
            status = request->get_Data(&data);
        if (SUCCEEDED(status))
            status = populate(data);
        if (data)
            data->Release();
        if (request)
            request->Release();
        if (FAILED(status))
            qWarning() << "Share request failed" << QString::number(quint32(status), 16);
        return status;
    }

  private:
    std::atomic<ULONG> references_{1};
    IInspectable *bitmap_ = nullptr;
};

class ShareSession final : public QObject {
  public:
    explicit ShareSession(QWidget *owner) : QObject(owner) {
        setObjectName("EasySnipNativeShare");
    }
    ~ShareSession() override {
        if (subscribed)
            manager->remove_DataRequested(token);
        if (manager)
            manager->Release();
        if (handler)
            handler->Release();
        if (initialized)
            runtime().uninitialize();
    }
    ShareManager *manager = nullptr;
    ShareHandler *handler = nullptr;
    ShareToken token{};
    bool initialized = false;
    bool subscribed = false;
};
} // namespace EasySnipWinRT

bool Native::shareImage(QWidget *owner, const QString &file, QString *error, bool showPanel) {
    using namespace EasySnipWinRT;
    if (!owner || !runtime().available()) {
        *error = QStringLiteral("当前系统缺少 Windows Runtime 分享接口。");
        return false;
    }
    if (auto *previous = owner->findChild<QObject *>("EasySnipNativeShare", Qt::FindDirectChildrenOnly))
        delete previous;
    auto *session = new ShareSession(owner);
    HRESULT status = runtime().initialize(RO_INIT_SINGLETHREADED);
    session->initialized = SUCCEEDED(status);
    IDataTransferManagerInterop *interop = nullptr;
    StreamReferenceFactory *factory = nullptr;
    IInspectable *stream = nullptr;
    IInspectable *bitmap = nullptr;
    if (SUCCEEDED(status))
        status = runtime().getFactory(L"Windows.ApplicationModel.DataTransfer.DataTransferManager", interopId,
                                      reinterpret_cast<void **>(&interop));
    if (SUCCEEDED(status))
        status = interop->GetForWindow(reinterpret_cast<HWND>(owner->winId()), managerId,
                                       reinterpret_cast<void **>(&session->manager));
    if (SUCCEEDED(status))
        status = runtime().getFactory(L"Windows.Storage.Streams.RandomAccessStreamReference",
                                      referenceFactoryId, reinterpret_cast<void **>(&factory));
    const std::wstring path = QDir::toNativeSeparators(file).toStdWString();
    constexpr DWORD fileAccessRead = 0;
    if (SUCCEEDED(status))
        status = runtime().stream(path.c_str(), fileAccessRead, streamId, reinterpret_cast<void **>(&stream));
    if (SUCCEEDED(status))
        status = factory->CreateFromStream(stream, &bitmap);
    if (SUCCEEDED(status)) {
        session->handler = new ShareHandler(bitmap);
        status = session->manager->add_DataRequested(session->handler, &session->token);
        session->subscribed = SUCCEEDED(status);
    }
    // Exercise data-package creation up front so failures are visible before the UI opens.
    if (SUCCEEDED(status)) {
        HSTRING name = nullptr;
        IInspectable *instance = nullptr;
        ShareData *data = nullptr;
        const wchar_t *className = L"Windows.ApplicationModel.DataTransfer.DataPackage";
        status = runtime().createString(className, UINT32(wcslen(className)), &name);
        if (SUCCEEDED(status))
            status = runtime().activate(name, &instance);
        if (SUCCEEDED(status))
            status = instance->QueryInterface(dataId, reinterpret_cast<void **>(&data));
        if (SUCCEEDED(status))
            status = session->handler->populate(data);
        if (data)
            data->Release();
        if (instance)
            instance->Release();
        if (name)
            runtime().deleteString(name);
    }
    if (SUCCEEDED(status) && showPanel)
        status = interop->ShowShareUIForWindow(reinterpret_cast<HWND>(owner->winId()));
    if (bitmap)
        bitmap->Release();
    if (stream)
        stream->Release();
    if (factory)
        factory->Release();
    if (interop)
        interop->Release();
    if (FAILED(status)) {
        *error =
            QStringLiteral("系统分享初始化失败，HRESULT 0x%1").arg(quint32(status), 8, 16, QLatin1Char('0'));
        delete session;
        return false;
    }
    return true;
}
