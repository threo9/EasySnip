#include "native.h"
#include <QApplication>
#include <QHash>
#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSettings>
#include <QTimer>
#include <windows.h>
#include <dwmapi.h>
#include <oleacc.h>
#include <winhttp.h>
#include <QElapsedTimer>

namespace {
GlobalInput *inputInstance = nullptr;
HHOOK mouseHook = nullptr;
bool superDragging = false;
POINT dragStart{};

QString processPath(HWND window) {
    DWORD pid = 0;
    GetWindowThreadProcessId(window, &pid);
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!process)
        return {};
    wchar_t path[32768];
    DWORD size = 32768;
    const bool ok = QueryFullProcessImageNameW(process, 0, path, &size);
    CloseHandle(process);
    return ok ? QString::fromWCharArray(path, int(size)) : QString();
}

QRect windowRect(HWND window) {
    RECT rect{};
    if (FAILED(DwmGetWindowAttribute(window, DWMWA_EXTENDED_FRAME_BOUNDS, &rect, sizeof(rect))))
        GetWindowRect(window, &rect);
    return QRect(rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top);
}

struct WindowSearch {
    QPoint point;
    QStringList excluded;
    HWND found = nullptr;
};
BOOL CALLBACK findWindow(HWND window, LPARAM parameter) {
    auto *search = reinterpret_cast<WindowSearch *>(parameter);
    DWORD pid = 0;
    GetWindowThreadProcessId(window, &pid);
    if (pid == GetCurrentProcessId() || !IsWindowVisible(window) || IsIconic(window))
        return TRUE;
    DWORD cloaked = 0;
    DwmGetWindowAttribute(window, DWMWA_CLOAKED, &cloaked, sizeof(cloaked));
    if (cloaked || !windowRect(window).contains(search->point))
        return TRUE;
    if (!search->excluded.isEmpty() && Native::matchesProcess(processPath(window), search->excluded))
        return TRUE;
    search->found = window;
    return FALSE;
}

LRESULT CALLBACK mouseProcedure(int code, WPARAM message, LPARAM data) {
    if (code == HC_ACTION && inputInstance) {
        const auto *event = reinterpret_cast<MSLLHOOKSTRUCT *>(data);
        if (message == WM_LBUTTONDOWN && (GetAsyncKeyState(VK_MENU) & 0x8000) &&
            (GetAsyncKeyState(VK_SHIFT) & 0x8000)) {
            dragStart = event->pt;
            superDragging = true;
            return 1;
        }
        if (superDragging && message == WM_LBUTTONUP) {
            superDragging = false;
            const QRect rect =
                QRect(QPoint(dragStart.x, dragStart.y), QPoint(event->pt.x, event->pt.y)).normalized();
            QTimer::singleShot(0, inputInstance, [rect] {
                if (inputInstance && rect.width() > 2 && rect.height() > 2)
                    emit inputInstance->superDragged(rect);
            });
            return 1;
        }
    }
    return CallNextHookEx(mouseHook, code, message, data);
}
} // namespace

void Native::enableDpiAwareness() {
    using SetContext = BOOL(WINAPI *)(HANDLE);
    const auto setContext = reinterpret_cast<SetContext>(
        GetProcAddress(GetModuleHandleW(L"user32.dll"), "SetProcessDpiAwarenessContext"));
    if (setContext)
        setContext(reinterpret_cast<HANDLE>(-4));
}

Native::Desktop Native::captureDesktop() {
    Desktop desktop;
    EnumDisplayMonitors(
        nullptr, nullptr,
        [](HMONITOR, HDC, LPRECT rect, LPARAM data) -> BOOL {
            auto *result = reinterpret_cast<Desktop *>(data);
            const QRect screen(rect->left, rect->top, rect->right - rect->left, rect->bottom - rect->top);
            result->screens.append(screen);
            result->geometry = result->geometry.united(screen);
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&desktop));
    const QRect bounds = desktop.geometry;
    if (bounds.isEmpty())
        return desktop;
    HDC screen = GetDC(nullptr);
    HDC memory = CreateCompatibleDC(screen);
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = bounds.width();
    info.bmiHeader.biHeight = -bounds.height();
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    void *bits = nullptr;
    HBITMAP bitmap = CreateDIBSection(screen, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (screen && memory && bitmap && bits) {
        HGDIOBJ old = SelectObject(memory, bitmap);
        if (BitBlt(memory, 0, 0, bounds.width(), bounds.height(), screen, bounds.x(), bounds.y(),
                   SRCCOPY | CAPTUREBLT)) {
            desktop.image =
                QImage(static_cast<uchar *>(bits), bounds.width(), bounds.height(), QImage::Format_RGB32)
                    .copy();
        }
        SelectObject(memory, old);
    }
    if (bitmap)
        DeleteObject(bitmap);
    if (memory)
        DeleteDC(memory);
    if (screen)
        ReleaseDC(nullptr, screen);

    CURSORINFO cursor{sizeof(CURSORINFO), 0, nullptr, {0, 0}};
    if (GetCursorInfo(&cursor) && (cursor.flags & CURSOR_SHOWING)) {
        ICONINFO icon{};
        if (GetIconInfo(cursor.hCursor, &icon)) {
            desktop.cursorPosition = QPoint(cursor.ptScreenPos.x, cursor.ptScreenPos.y);
            desktop.cursorHotspot = QPoint(icon.xHotspot, icon.yHotspot);
            // Render on both black and white to recover alpha, including monochrome cursors.
            const int width = GetSystemMetrics(SM_CXCURSOR);
            const int height = GetSystemMetrics(SM_CYCURSOR);
            QImage black(width, height, QImage::Format_RGB32);
            QImage white(width, height, QImage::Format_RGB32);
            auto render = [&](QImage &image, DWORD background) {
                HDC dc = CreateCompatibleDC(nullptr);
                BITMAPINFO bi{};
                bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
                bi.bmiHeader.biWidth = width;
                bi.bmiHeader.biHeight = -height;
                bi.bmiHeader.biPlanes = 1;
                bi.bmiHeader.biBitCount = 32;
                void *pixels = nullptr;
                HBITMAP bm = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &pixels, nullptr, 0);
                if (bm && pixels) {
                    std::fill_n(static_cast<DWORD *>(pixels), width * height, background);
                    HGDIOBJ previous = SelectObject(dc, bm);
                    DrawIconEx(dc, 0, 0, cursor.hCursor, width, height, 0, nullptr, DI_NORMAL);
                    image = QImage(static_cast<uchar *>(pixels), width, height, QImage::Format_RGB32).copy();
                    SelectObject(dc, previous);
                    DeleteObject(bm);
                }
                DeleteDC(dc);
            };
            black.fill(Qt::black);
            white.fill(Qt::white);
            render(black, 0);
            render(white, 0x00ffffff);
            desktop.cursorImage = QImage(width, height, QImage::Format_ARGB32_Premultiplied);
            for (int y = 0; y < height; ++y)
                for (int x = 0; x < width; ++x) {
                    const QRgb b = black.pixel(x, y), w = white.pixel(x, y);
                    const int alpha = 255 - qBound(0, qRed(w) - qRed(b), 255);
                    desktop.cursorImage.setPixel(
                        x, y,
                        qRgba(qMin(qRed(b), alpha), qMin(qGreen(b), alpha), qMin(qBlue(b), alpha), alpha));
                }
            if (icon.hbmMask)
                DeleteObject(icon.hbmMask);
            if (icon.hbmColor)
                DeleteObject(icon.hbmColor);
        }
    }
    return desktop;
}

QRect Native::foregroundWindow() {
    static QRect lastExternalWindow;
    const HWND window = GetForegroundWindow();
    DWORD process = 0;
    GetWindowThreadProcessId(window, &process);
    if (window && process != GetCurrentProcessId() && !IsIconic(window))
        lastExternalWindow = windowRect(window);
    return lastExternalWindow;
}
QString Native::foregroundExecutable() {
    return processPath(GetForegroundWindow());
}

bool Native::matchesProcess(const QString &path, const QStringList &patterns) {
    for (const QString &pattern : patterns) {
        if (pattern.trimmed().isEmpty())
            continue;
        const QRegularExpression expression(
            QRegularExpression::wildcardToRegularExpression(pattern.trimmed()),
            QRegularExpression::CaseInsensitiveOption);
        if (expression.match(path).hasMatch() || expression.match(QFileInfo(path).fileName()).hasMatch())
            return true;
    }
    return false;
}

QRect Native::windowAt(const QPoint &point, const QStringList &excluded) {
    WindowSearch search{point, excluded, nullptr};
    EnumWindows(findWindow, reinterpret_cast<LPARAM>(&search));
    return search.found ? windowRect(search.found) : QRect();
}
QVector<QRect> Native::windowElements(const QRect &region) {
    struct Snapshot {
        QRect region;
        QVector<QRect> rectangles;
    } snapshot{region, {}};
    EnumWindows(
        [](HWND window, LPARAM data) -> BOOL {
            auto *snapshot = reinterpret_cast<Snapshot *>(data);
            DWORD process = 0;
            GetWindowThreadProcessId(window, &process);
            if (process == GetCurrentProcessId() || !IsWindowVisible(window) || IsIconic(window))
                return TRUE;
            const QRect outer = windowRect(window);
            if (!snapshot->region.intersects(outer))
                return TRUE;
            snapshot->rectangles.append(outer);
            EnumChildWindows(
                window,
                [](HWND child, LPARAM data) -> BOOL {
                    auto *snapshot = reinterpret_cast<Snapshot *>(data);
                    const QRect rect = windowRect(child);
                    if (IsWindowVisible(child) && !rect.isEmpty() && snapshot->region.intersects(rect) &&
                        !snapshot->rectangles.contains(rect))
                        snapshot->rectangles.append(rect);
                    return snapshot->rectangles.size() < 2000;
                },
                data);
            return snapshot->rectangles.size() < 2000;
        },
        reinterpret_cast<LPARAM>(&snapshot));
    return snapshot.rectangles;
}

QVector<QRect> Native::elementsAt(const QPoint &point, const QStringList &excluded,
                                  const QStringList &nativePrograms) {
    QVector<QRect> rectangles;
    WindowSearch search{point, excluded, nullptr};
    EnumWindows(findWindow, reinterpret_cast<LPARAM>(&search));
    if (!search.found)
        return rectangles;
    if (matchesProcess(processPath(search.found), nativePrograms)) {
        for (const auto &rect : windowElements(windowRect(search.found)))
            if (rect.contains(point))
                rectangles.append(rect);
        std::sort(rectangles.begin(), rectangles.end(), [](const QRect &a, const QRect &b) {
            return qint64(a.width()) * a.height() < qint64(b.width()) * b.height();
        });
        return rectangles;
    }
    const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    IAccessible *accessible = nullptr;
    if (SUCCEEDED(AccessibleObjectFromWindow(search.found, OBJID_CLIENT, IID_IAccessible,
                                             reinterpret_cast<void **>(&accessible)))) {
        for (int depth = 0; depth < 24 && accessible; ++depth) {
            VARIANT child;
            VariantInit(&child);
            child.vt = VT_I4;
            child.lVal = CHILDID_SELF;
            long x = 0, y = 0, width = 0, height = 0;
            if (SUCCEEDED(accessible->accLocation(&x, &y, &width, &height, child))) {
                const QRect rect(x, y, width, height);
                if (rect.contains(point) && !rectangles.contains(rect))
                    rectangles.prepend(rect);
            }
            VARIANT hit;
            VariantInit(&hit);
            IAccessible *next = nullptr;
            if (SUCCEEDED(accessible->accHitTest(point.x(), point.y(), &hit))) {
                if (hit.vt == VT_DISPATCH && hit.pdispVal) {
                    hit.pdispVal->QueryInterface(IID_IAccessible, reinterpret_cast<void **>(&next));
                } else if (hit.vt == VT_I4 && hit.lVal != CHILDID_SELF) {
                    if (SUCCEEDED(accessible->accLocation(&x, &y, &width, &height, hit))) {
                        const QRect rect(x, y, width, height);
                        if (rect.contains(point) && !rectangles.contains(rect))
                            rectangles.prepend(rect);
                    }
                    IDispatch *dispatch = nullptr;
                    if (SUCCEEDED(accessible->get_accChild(hit, &dispatch)) && dispatch) {
                        dispatch->QueryInterface(IID_IAccessible, reinterpret_cast<void **>(&next));
                        dispatch->Release();
                    }
                }
            }
            VariantClear(&hit);
            const bool same = next == accessible;
            accessible->Release();
            accessible = next;
            if (same)
                break;
        }
        if (accessible)
            accessible->Release();
    }
    const QRect outer = windowRect(search.found);
    if (!rectangles.contains(outer))
        rectangles.append(outer);
    if (SUCCEEDED(com))
        CoUninitialize();
    return rectangles;
}

bool Native::setAutostart(bool enabled) {
    QSettings run("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run",
                  QSettings::NativeFormat);
    if (enabled)
        run.setValue("EasySnip",
                     '"' + QDir::toNativeSeparators(QApplication::applicationFilePath()) + "\" --tray");
    else
        run.remove("EasySnip");
    run.sync();
    return run.status() == QSettings::NoError;
}
void Native::setPriority(const QString &priority) {
    SetPriorityClass(GetCurrentProcess(), priority == "low"    ? BELOW_NORMAL_PRIORITY_CLASS
                                          : priority == "high" ? ABOVE_NORMAL_PRIORITY_CLASS
                                                               : NORMAL_PRIORITY_CLASS);
}

QPair<QByteArray, QString> Native::downloadImage(const QUrl &url, qint64 maximumBytes) {
    struct InternetHandle {
        HINTERNET value = nullptr;
        ~InternetHandle() {
            if (value)
                WinHttpCloseHandle(value);
        }
    };
    // Windows 8.1+ constant, absent from the bundled MinGW 7.3 headers.
    constexpr DWORD accessAutomaticProxy = 4;
    InternetHandle session{WinHttpOpen(L"EasySnip/0.1", accessAutomaticProxy, WINHTTP_NO_PROXY_NAME,
                                       WINHTTP_NO_PROXY_BYPASS, 0)};
    if (!session.value)
        return {{}, QStringLiteral("无法初始化 Windows 网络服务。")};
    WinHttpSetTimeouts(session.value, 10000, 10000, 15000, 15000);
    const std::wstring host = QString::fromLatin1(QUrl::toAce(url.host())).toStdWString();
    const bool secure = url.scheme() == "https";
    InternetHandle connection{
        WinHttpConnect(session.value, host.c_str(), INTERNET_PORT(url.port(secure ? 443 : 80)), 0)};
    if (!connection.value)
        return {{}, QStringLiteral("无法连接图片服务器。")};
    QString resource = url.path(QUrl::FullyEncoded);
    if (resource.isEmpty())
        resource = "/";
    if (url.hasQuery())
        resource += '?' + url.query(QUrl::FullyEncoded);
    const std::wstring requestPath = resource.toStdWString();
    InternetHandle request{WinHttpOpenRequest(connection.value, L"GET", requestPath.c_str(), nullptr,
                                              WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                              secure ? WINHTTP_FLAG_SECURE : 0)};
    if (!request.value)
        return {{}, QStringLiteral("无法创建图片请求。")};
    DWORD redirects = WINHTTP_OPTION_REDIRECT_POLICY_DISALLOW_HTTPS_TO_HTTP;
    WinHttpSetOption(request.value, WINHTTP_OPTION_REDIRECT_POLICY, &redirects, sizeof(redirects));
    if (!WinHttpSendRequest(request.value, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0,
                            0) ||
        !WinHttpReceiveResponse(request.value, nullptr))
        return {{}, QStringLiteral("图片下载失败，Windows 错误 %1").arg(GetLastError())};
    DWORD status = 0, statusSize = sizeof(status);
    if (!WinHttpQueryHeaders(request.value, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                             WINHTTP_HEADER_NAME_BY_INDEX, &status, &statusSize, WINHTTP_NO_HEADER_INDEX) ||
        status < 200 || status >= 300)
        return {{}, QStringLiteral("图片服务器返回 HTTP %1").arg(status)};
    QByteArray data;
    QElapsedTimer elapsed;
    elapsed.start();
    char block[32768];
    for (;;) {
        DWORD received = 0;
        if (!WinHttpReadData(request.value, block, sizeof(block), &received))
            return {{}, QStringLiteral("读取图片失败，Windows 错误 %1").arg(GetLastError())};
        if (received == 0)
            break;
        if (data.size() + qint64(received) > maximumBytes)
            return {{}, QStringLiteral("网页图片超过配置的下载大小上限。")};
        data.append(block, int(received));
        if (elapsed.elapsed() > 30000)
            return {{}, QStringLiteral("图片下载超时。")};
    }
    return {data, {}};
}

GlobalInput::GlobalInput(QObject *parent) : QObject(parent) {
    inputInstance = this;
    qApp->installNativeEventFilter(this);
}
GlobalInput::~GlobalInput() {
    enableSuperDrag(false);
    clear();
    qApp->removeNativeEventFilter(this);
    inputInstance = nullptr;
}
void GlobalInput::clear() {
    for (int id : registered_)
        UnregisterHotKey(nullptr, id);
    registered_.clear();
}
bool GlobalInput::registerShortcut(int id, const QKeySequence &sequence, QString *error) {
    if (sequence.isEmpty())
        return true;
    if (sequence.count() != 1) {
        *error = QStringLiteral("全局快捷键仅支持单组按键");
        return false;
    }
    const int combined = sequence[0];
    const int key = combined & ~Qt::KeyboardModifierMask;
    UINT modifiers = MOD_NOREPEAT;
    if (combined & Qt::CTRL)
        modifiers |= MOD_CONTROL;
    if (combined & Qt::SHIFT)
        modifiers |= MOD_SHIFT;
    if (combined & Qt::ALT)
        modifiers |= MOD_ALT;
    if (combined & Qt::META)
        modifiers |= MOD_WIN;
    UINT vk = 0;
    if (key >= Qt::Key_F1 && key <= Qt::Key_F24)
        vk = VK_F1 + key - Qt::Key_F1;
    else if ((key >= Qt::Key_A && key <= Qt::Key_Z) || (key >= Qt::Key_0 && key <= Qt::Key_9))
        vk = UINT(key);
    else {
        const QHash<int, UINT> specialKeys{
            {Qt::Key_Print, VK_SNAPSHOT}, {Qt::Key_Space, VK_SPACE},   {Qt::Key_Insert, VK_INSERT},
            {Qt::Key_Pause, VK_PAUSE},    {Qt::Key_Delete, VK_DELETE}, {Qt::Key_Home, VK_HOME},
            {Qt::Key_End, VK_END},        {Qt::Key_PageUp, VK_PRIOR},  {Qt::Key_PageDown, VK_NEXT},
            {Qt::Key_Left, VK_LEFT},      {Qt::Key_Right, VK_RIGHT},   {Qt::Key_Up, VK_UP},
            {Qt::Key_Down, VK_DOWN},      {Qt::Key_Tab, VK_TAB},       {Qt::Key_Escape, VK_ESCAPE},
            {Qt::Key_Return, VK_RETURN},  {Qt::Key_Enter, VK_RETURN},  {Qt::Key_Backspace, VK_BACK}};
        vk = specialKeys.value(key, 0);
        if (!vk && key > 0 && key < 128) {
            const SHORT scan = VkKeyScanW(wchar_t(key));
            if (scan != -1) {
                vk = LOBYTE(scan);
                if (HIBYTE(scan) & 1)
                    modifiers |= MOD_SHIFT;
                if (HIBYTE(scan) & 2)
                    modifiers |= MOD_CONTROL;
                if (HIBYTE(scan) & 4)
                    modifiers |= MOD_ALT;
            }
        }
    }
    if (!vk || key == Qt::Key_F12 || !RegisterHotKey(nullptr, id, modifiers, vk)) {
        *error =
            QStringLiteral("快捷键 %1 注册失败，可能被其他程序占用或键位不受支持").arg(sequence.toString());
        return false;
    }
    registered_.append(id);
    return true;
}
void GlobalInput::enableSuperDrag(bool enabled) {
    if (enabled && !mouseHook)
        mouseHook = SetWindowsHookExW(WH_MOUSE_LL, mouseProcedure, GetModuleHandleW(nullptr), 0);
    if (!enabled && mouseHook) {
        UnhookWindowsHookEx(mouseHook);
        mouseHook = nullptr;
        superDragging = false;
    }
}
bool GlobalInput::nativeEventFilter(const QByteArray &, void *message, long *result) {
    auto *msg = static_cast<MSG *>(message);
    if (msg->message != WM_HOTKEY)
        return false;
    emit activated(int(msg->wParam));
    *result = 0;
    return true;
}
