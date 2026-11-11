#include "native.h"
#include <QApplication>
#include <QHash>
#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSettings>
#include <QTimer>
#include <QDebug>
#include <QOperatingSystemVersion>
#include <windows.h>
#include <shellapi.h>
#include <dwmapi.h>
#include <oleacc.h>

namespace {
GlobalInput *inputInstance = nullptr;
HHOOK mouseHook = nullptr;
bool superDragging = false;
QString superModifiers = "Alt+Shift";
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
    if (GetAncestor(window, GA_ROOT) != window ||
        FAILED(DwmGetWindowAttribute(window, DWMWA_EXTENDED_FRAME_BOUNDS, &rect, sizeof(rect))))
        GetWindowRect(window, &rect);
    return QRect(rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top);
}

QVector<QRect> childWindowElements(HWND window, const QRect &geometry) {
    struct Children {
        QRect geometry;
        QVector<QRect> rectangles;
    } children{geometry, {}};
    EnumChildWindows(window, [](HWND child, LPARAM data) -> BOOL {
        auto *children = reinterpret_cast<Children *>(data);
        const QRect bounds = windowRect(child).intersected(children->geometry);
        if (IsWindowVisible(child) && !bounds.isEmpty() && !children->rectangles.contains(bounds))
            children->rectangles.append(bounds);
        return children->rectangles.size() < 2048;
    }, reinterpret_cast<LPARAM>(&children));
    return children.rectangles;
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
    if (code == HC_ACTION && inputInstance && (message == WM_LBUTTONDOWN || message == WM_LBUTTONUP)) {
        const auto *event = reinterpret_cast<MSLLHOOKSTRUCT *>(data);
        const bool alt = GetAsyncKeyState(VK_MENU) & 0x8000;
        const bool shift = GetAsyncKeyState(VK_SHIFT) & 0x8000;
        const bool control = GetAsyncKeyState(VK_CONTROL) & 0x8000;
        const bool win = (GetAsyncKeyState(VK_LWIN) | GetAsyncKeyState(VK_RWIN)) & 0x8000;
        const bool trigger = superModifiers == "Win" ? win :
                             superModifiers == "Ctrl+Alt" ? control && alt : alt && shift;
        if (message == WM_LBUTTONDOWN && trigger) {
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

QImage Native::captureRegion(const QRect &bounds) {
    QImage image;
    if (bounds.isEmpty())
        return image;
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
            image =
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
    return image;
}

Native::ScrollTarget Native::scrollTarget(const QRect &region) {
    ScrollTarget target;
    target.point = region.center();
    const POINT point{target.point.x(), target.point.y()};
    const HWND window = WindowFromPoint(point);
    const HWND root = GetAncestor(window, GA_ROOT);
    if (!window || !root || !windowRect(root).contains(region))
        return {};
    target.window = reinterpret_cast<quintptr>(window);
    target.root = reinterpret_cast<quintptr>(root);
    DWORD processId = 0;
    GetWindowThreadProcessId(root, &processId);
    target.processId = processId;
    target.geometry = windowRect(root);
    return target;
}

bool Native::scrollTargetValid(const ScrollTarget &target, const QRect &region, quintptr ignoredWindow) {
    const HWND root = reinterpret_cast<HWND>(target.root);
    const HWND window = reinterpret_cast<HWND>(target.window);
    DWORD processId = 0;
    GetWindowThreadProcessId(root, &processId);
    if (!IsWindow(window) || !IsWindowVisible(root) || IsIconic(root) ||
        processId != target.processId ||
        GetAncestor(window, GA_ROOT) != root || windowRect(root) != target.geometry ||
        !target.geometry.contains(region))
        return false;
    const QRect inside = region.adjusted(2, 2, -2, -2);
    for (const auto &point : {inside.topLeft(), inside.topRight(), inside.center(),
                              inside.bottomLeft(), inside.bottomRight()}) {
        const POINT nativePoint{point.x(), point.y()};
        HWND visible = GetAncestor(WindowFromPoint(nativePoint), GA_ROOT);
        if (ignoredWindow && visible == reinterpret_cast<HWND>(ignoredWindow)) {
            visible = GetWindow(visible, GW_HWNDNEXT);
            while (visible) {
                DWORD cloaked = 0;
                DwmGetWindowAttribute(visible, DWMWA_CLOAKED, &cloaked, sizeof(cloaked));
                if (IsWindowVisible(visible) && !IsIconic(visible) && !cloaked && windowRect(visible).contains(point)) {
                    // Respect native holes such as the scroll-capture mask.
                    // Window regions use the full window origin, including
                    // its non-client frame, rather than the client origin.
                    RECT bounds{};
                    GetWindowRect(visible, &bounds);
                    HRGN shape = CreateRectRgn(0, 0, 0, 0);
                    const bool coversPoint = GetWindowRgn(visible, shape) == ERROR ||
                        PtInRegion(shape, point.x() - bounds.left, point.y() - bounds.top);
                    DeleteObject(shape);
                    if (coversPoint)
                        break;
                }
                visible = GetWindow(visible, GW_HWNDNEXT);
            }
        }
        if (visible != root)
            return false;
    }
    return true;
}

bool Native::excludeFromCapture(quintptr window) {
    const auto version = QOperatingSystemVersion::current();
    // Older systems interpret 0x11 as WDA_MONITOR, which produces a black box.
    if (version.majorVersion() < 10 || (version.majorVersion() == 10 && version.microVersion() < 19041))
        return false;
    return SetWindowDisplayAffinity(reinterpret_cast<HWND>(window), 0x00000011) != 0;
}

Native::Desktop Native::captureDesktop(bool snapshotElements) {
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
    if (desktop.geometry.isEmpty())
        return desktop;
    desktop.image = captureRegion(desktop.geometry);

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
    if (snapshotElements) {
        struct SnapshotContext {
            QVector<WindowSnapshot> windows;
            QHash<DWORD, QString> processes;
        } context;
        EnumWindows([](HWND window, LPARAM data) -> BOOL {
            auto *context = reinterpret_cast<SnapshotContext *>(data);
            DWORD pid = 0, cloaked = 0;
            GetWindowThreadProcessId(window, &pid);
            DwmGetWindowAttribute(window, DWMWA_CLOAKED, &cloaked, sizeof(cloaked));
            if (pid == GetCurrentProcessId() || !IsWindowVisible(window) || IsIconic(window) || cloaked)
                return TRUE;
            WindowSnapshot item;
            item.handle = reinterpret_cast<quintptr>(window);
            item.geometry = windowRect(window);
            if (item.geometry.isEmpty())
                return TRUE;
            if (!context->processes.contains(pid))
                context->processes.insert(pid, processPath(window));
            item.executable = context->processes.value(pid);
            item.nativeElements = childWindowElements(window, item.geometry);
            item.elements = item.nativeElements;
            wchar_t name[256]{};
            GetClassNameW(window, name, 256);
            const QString className = QString::fromWCharArray(name);
            if (className.contains("Tray") || className.contains("Overflow") ||
                className == "XamlExplorerHostIslandWindow" || className == "#32768") {
                const auto accessible = automationElements(item.handle, nullptr, &item.cached);
                for (const auto &bounds : accessible)
                    if (!item.elements.contains(bounds))
                        item.elements.append(bounds);
                qInfo() << "Captured accessibility snapshot" << className << accessible.size()
                        << "elements, complete" << item.cached;
            }
            context->windows.append(std::move(item));
            return TRUE;
        }, reinterpret_cast<LPARAM>(&context));
        desktop.windows = std::move(context.windows);
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

namespace {
Native::WindowSnapshot detectionWindow(const QPoint &point, const QStringList &excluded,
                                      const QVector<Native::WindowSnapshot> &windows) {
    for (const auto &window : windows)
        if (window.geometry.contains(point) && !Native::matchesProcess(window.executable, excluded))
            return window;
    if (!windows.isEmpty())
        return {};
    WindowSearch search{point, excluded, nullptr};
    EnumWindows(findWindow, reinterpret_cast<LPARAM>(&search));
    Native::WindowSnapshot result;
    if (search.found) {
        result.handle = reinterpret_cast<quintptr>(search.found);
        result.geometry = windowRect(search.found);
        result.executable = processPath(search.found);
    }
    return result;
}
QVector<QRect> elementHierarchy(const QPoint &point, const Native::WindowSnapshot &window,
                               const QVector<QRect> &elements) {
    QVector<QRect> candidates;
    for (const auto &element : elements) {
        const QRect bounds = element.intersected(window.geometry);
        if (!bounds.isEmpty() && bounds.contains(point) && !candidates.contains(bounds))
            candidates.append(bounds);
    }
    if (!window.geometry.isEmpty() && window.geometry.contains(point) && !candidates.contains(window.geometry))
        candidates.append(window.geometry);
    std::sort(candidates.begin(), candidates.end(), [](const QRect &a, const QRect &b) {
        return qint64(a.width()) * a.height() < qint64(b.width()) * b.height();
    });
    QVector<QRect> hierarchy;
    for (const auto &bounds : candidates)
        if (hierarchy.isEmpty() || bounds.contains(hierarchy.last()))
            hierarchy.append(bounds);
    return hierarchy;
}
} // namespace

QRect Native::windowAt(const QPoint &point, const QStringList &excluded, const QVector<WindowSnapshot> &windows) {
    return detectionWindow(point, excluded, windows).geometry;
}

QVector<QRect> Native::cachedElementsAt(const QPoint &point, const QVector<WindowSnapshot> &windows,
                                       const QStringList &excluded, const QStringList &nativePrograms) {
    if (windows.isEmpty())
        return {};
    const auto window = detectionWindow(point, excluded, windows);
    if (matchesProcess(window.executable, nativePrograms))
        return elementHierarchy(point, window, window.nativeElements);
    return window.cached ? elementHierarchy(point, window, window.elements) : QVector<QRect>();
}
QVector<QRect> Native::elementsAt(const QPoint &point, const QStringList &excluded,
                                  const QStringList &nativePrograms, const QVector<WindowSnapshot> &windows) {
    const auto target = detectionWindow(point, excluded, windows);
    QVector<QRect> rectangles = target.elements;
    const HWND window = reinterpret_cast<HWND>(target.handle);
    if (!window)
        return {};
    const bool unchanged = IsWindow(window) && IsWindowVisible(window) && windowRect(window) == target.geometry;
    if (matchesProcess(target.executable, nativePrograms)) {
        rectangles = target.nativeElements;
        if (unchanged)
            rectangles += childWindowElements(window, target.geometry);
        return elementHierarchy(point, target, rectangles);
    }
    if (target.cached || !unchanged)
        return elementHierarchy(point, target, rectangles);
    rectangles += automationElements(target.handle, &point);
    const HRESULT com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    IAccessible *accessible = nullptr;
    if (SUCCEEDED(AccessibleObjectFromWindow(window, OBJID_CLIENT, IID_IAccessible,
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
    if (SUCCEEDED(com))
        CoUninitialize();
    return elementHierarchy(point, target, rectangles);
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

bool Native::isElevated() {
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token))
        return false;
    TOKEN_ELEVATION elevation{};
    DWORD size = 0;
    const bool ok = GetTokenInformation(token, TokenElevation, &elevation, sizeof(elevation), &size);
    CloseHandle(token);
    return ok && elevation.TokenIsElevated;
}

QString Native::quoteArguments(const QStringList &arguments) {
    QStringList quoted;
    for (const QString &argument : arguments) {
        QString result = "\"";
        int slashes = 0;
        for (const QChar character : argument) {
            if (character == '\\') {
                ++slashes;
                continue;
            }
            result += QString(character == '"' ? slashes * 2 + 1 : slashes, '\\');
            result += character;
            slashes = 0;
        }
        result += QString(slashes * 2, '\\') + '"';
        quoted.append(result);
    }
    return quoted.join(' ');
}

bool Native::restartElevated(const QStringList &arguments, QString *error) {
    if (error)
        error->clear();
    const std::wstring executable = QDir::toNativeSeparators(QApplication::applicationFilePath()).toStdWString();
    const std::wstring parameters = quoteArguments(arguments.mid(1)).toStdWString();
    const std::wstring directory = QDir::currentPath().toStdWString();
    const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    SHELLEXECUTEINFOW info{};
    info.cbSize = sizeof(info);
    info.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_NOASYNC | SEE_MASK_FLAG_NO_UI;
    info.lpVerb = L"runas";
    info.lpFile = executable.c_str();
    info.lpParameters = parameters.c_str();
    info.lpDirectory = directory.c_str();
    info.nShow = SW_HIDE;
    const bool ok = ShellExecuteExW(&info);
    const DWORD code = ok ? ERROR_SUCCESS : GetLastError();
    if (info.hProcess)
        CloseHandle(info.hProcess);
    if (SUCCEEDED(com))
        CoUninitialize();
    if (!ok && code != ERROR_CANCELLED && error)
        *error = QStringLiteral("管理员启动失败，Windows 错误 %1").arg(code);
    return ok;
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
    for (int id : registered_.keys())
        UnregisterHotKey(nullptr, id);
    registered_.clear();
}
bool GlobalInput::registerShortcut(int id, const QKeySequence &sequence, QString *error, bool *alreadyInUse) {
    if (error)
        error->clear();
    if (alreadyInUse)
        *alreadyInUse = false;
    auto fail = [error](const QString &message) {
        if (error)
            *error = message;
        return false;
    };
    if (id < 1 || id > 0xbfff)
        return fail(QStringLiteral("全局快捷键标识超出 Windows 支持范围"));
    if (sequence.isEmpty()) {
        if (registered_.remove(id))
            UnregisterHotKey(nullptr, id);
        return true;
    }
    if (sequence.count() != 1)
        return fail(QStringLiteral("全局快捷键仅支持单组按键"));
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
    if (!vk || key == Qt::Key_F12)
        return fail(QStringLiteral("快捷键 %1 的键位不受支持，F12 为系统调试保留键").arg(sequence.toString()));
    const bool replacing = registered_.contains(id);
    const Shortcut previous = registered_.value(id);
    if (replacing && previous.modifiers == modifiers && previous.key == vk)
        return true;
    if (replacing)
        UnregisterHotKey(nullptr, id);
    if (!RegisterHotKey(nullptr, id, modifiers, vk)) {
        const DWORD code = GetLastError();
        if (alreadyInUse)
            *alreadyInUse = code == ERROR_HOTKEY_ALREADY_REGISTERED;
        if (replacing && !RegisterHotKey(nullptr, id, previous.modifiers, previous.key)) {
            registered_.remove(id);
            qWarning() << "Could not restore previous shortcut" << id << GetLastError();
        }
        const QString reason = code == ERROR_HOTKEY_ALREADY_REGISTERED
            ? QStringLiteral("按键已被其他热键占用") : QStringLiteral("系统拒绝注册");
        return fail(QStringLiteral("快捷键 %1 注册失败，%2，Windows 错误 %3")
                    .arg(sequence.toString(), reason).arg(code));
    }
    registered_.insert(id, {modifiers, vk});
    qInfo().noquote() << "Global shortcut registered" << sequence.toString() << "id" << id
                     << "elevated" << Native::isElevated();
    return true;
}
void GlobalInput::enableSuperDrag(bool enabled, const QString &modifiers) {
    superModifiers = modifiers;
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
    if (msg->message != WM_HOTKEY || !registered_.contains(int(msg->wParam)))
        return false;
    const Shortcut shortcut = registered_.value(int(msg->wParam));
    if (HIWORD(msg->lParam) != shortcut.key || LOWORD(msg->lParam) != (shortcut.modifiers & ~MOD_NOREPEAT))
        return false;
    qInfo() << "Global shortcut activated" << int(msg->wParam);
    emit activated(int(msg->wParam));
    if (result)
        *result = 0;
    return true;
}
