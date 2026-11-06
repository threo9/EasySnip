#include "native.h"
#include "uiautomation_compat.h"
#include <QElapsedTimer>
#include <algorithm>
#include <memory>

namespace {
using namespace EasySnipUIA;

constexpr int BoundingRectangle = 30001;
constexpr int ControlType = 30003;
constexpr int IsControlElement = 30016;
constexpr int NativeWindowHandle = 30020;
constexpr int IsOffscreen = 30022;
constexpr int ImageControl = 50006;
constexpr int TextControl = 50020;
constexpr int ToolBarControl = 50021;
constexpr int GroupControl = 50026;
constexpr int WindowControl = 50032;
constexpr int PaneControl = 50033;

struct ReleaseInterface {
    template <typename T> void operator()(T *value) const {
        if (value)
            value->Release();
    }
};
template <typename T> using ComPtr = std::unique_ptr<T, ReleaseInterface>;

struct Apartment {
    HRESULT status = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    ~Apartment() {
        if (SUCCEEDED(status))
            CoUninitialize();
    }
};

ComPtr<IUIAutomation> createAutomation() {
    // CUIAutomation8 enables the modern provider bridge used by Windows 11 XAML islands.
    const CLSID modern = {0xe22ad333, 0xb25f, 0x460c, {0x83, 0xd0, 0x05, 0x81, 0x10, 0x73, 0x95, 0xc9}};
    const IID version2 = {0x34723aff, 0x0c9d, 0x49d0, {0x98, 0x96, 0x7a, 0xb5, 0x2d, 0xf8, 0xcd, 0x8a}};
    IUIAutomation2 *extended = nullptr;
    if (SUCCEEDED(CoCreateInstance(modern, nullptr, CLSCTX_INPROC_SERVER, version2,
                                   reinterpret_cast<void **>(&extended)))) {
        extended->put_AutoSetFocus(FALSE);
        extended->put_ConnectionTimeout(300);
        extended->put_TransactionTimeout(500);
        return ComPtr<IUIAutomation>(extended);
    }
    const CLSID legacy = {0xff48dba4, 0x60ef, 0x4201, {0xaa, 0x87, 0x54, 0x10, 0x3e, 0xef, 0x59, 0x4e}};
    const IID version1 = {0x30cbe57d, 0xd9d0, 0x452a, {0xab, 0x13, 0x7a, 0xc5, 0xac, 0x48, 0x25, 0xee}};
    IUIAutomation *automation = nullptr;
    CoCreateInstance(legacy, nullptr, CLSCTX_INPROC_SERVER, version1, reinterpret_cast<void **>(&automation));
    return ComPtr<IUIAutomation>(automation);
}

QRect cachedRectangle(IUIAutomation *automation, IUIAutomationElement *element) {
    VARIANT value;
    VariantInit(&value);
    RECT bounds{};
    if (SUCCEEDED(element->GetCachedPropertyValue(BoundingRectangle, &value)))
        automation->VariantToRect(value, &bounds);
    VariantClear(&value);
    if (SUCCEEDED(element->GetCachedPropertyValue(NativeWindowHandle, &value)) && value.vt == VT_I4) {
        const HWND window = reinterpret_cast<HWND>(quintptr(quint32(value.lVal)));
        if (window && IsWindow(window))
            GetWindowRect(window, &bounds);
    }
    VariantClear(&value);
    return QRect(bounds.left, bounds.top, bounds.right - bounds.left, bounds.bottom - bounds.top);
}

bool selectable(IUIAutomationElement *element, bool &hasControl) {
    VARIANT value;
    VariantInit(&value);
    element->GetCachedPropertyValue(IsOffscreen, &value);
    const bool hidden = value.vt == VT_BOOL && value.boolVal == VARIANT_TRUE;
    VariantClear(&value);
    if (hidden)
        return false;
    element->GetCachedPropertyValue(IsControlElement, &value);
    const bool control = value.vt != VT_BOOL || value.boolVal == VARIANT_TRUE;
    VariantClear(&value);
    element->GetCachedPropertyValue(ControlType, &value);
    const int type = value.vt == VT_I4 ? value.lVal : 0;
    VariantClear(&value);
    // The icon/text inside a button should select the button's clickable bounds.
    const bool selected = control && type != ImageControl && type != TextControl;
    // A container-only result is insufficient to freeze future point queries.
    if (selected && type && type != ToolBarControl && type != GroupControl &&
        type != WindowControl && type != PaneControl)
        hasControl = true;
    return selected;
}

bool collect(IUIAutomation *automation, IUIAutomationElement *element, IUIAutomationCacheRequest *cache,
             const QPoint *point, QVector<QRect> &rectangles, QElapsedTimer &elapsed, int &visited, int depth,
             bool &hasControl, const QRect *nativeBounds = nullptr) {
    if (++visited > 2048 || depth > 32 || elapsed.elapsed() > 800)
        return false;
    const QRect bounds = nativeBounds ? *nativeBounds : cachedRectangle(automation, element);
    // Some XAML host providers report logical root sizes even to a DPI-aware client.
    // The native HWND has already been hit-tested, so always inspect its children.
    if (point && depth > 0 && !bounds.isEmpty() && !bounds.contains(*point))
        return true;
    if (!bounds.isEmpty() && (!point || bounds.contains(*point)) &&
        selectable(element, hasControl) && !rectangles.contains(bounds))
        rectangles.append(bounds);
    IUIAutomationElementArray *rawChildren = nullptr;
    const HRESULT status = element->GetCachedChildren(&rawChildren);
    if (FAILED(status) || !rawChildren)
        return SUCCEEDED(status);
    ComPtr<IUIAutomationElementArray> children(rawChildren);
    int length = 0;
    if (FAILED(children->get_Length(&length)))
        return false;
    bool complete = true;
    for (int i = 0; i < length; ++i) {
        if (visited >= 2048 || elapsed.elapsed() > 800)
            return false;
        IUIAutomationElement *rawChild = nullptr;
        if (FAILED(children->GetElement(i, &rawChild)) || !rawChild) {
            complete = false;
            continue;
        }
        ComPtr<IUIAutomationElement> child(rawChild);
        if (point) {
            const QRect childBounds = cachedRectangle(automation, child.get());
            if (!childBounds.isEmpty() && !childBounds.contains(*point))
                continue;
            IUIAutomationElement *updated = nullptr;
            if (SUCCEEDED(child->BuildUpdatedCache(cache, &updated)) && updated)
                child.reset(updated);
            else
                complete = false;
        }
        if (!collect(automation, child.get(), cache, point, rectangles, elapsed, visited, depth + 1, hasControl))
            complete = false;
    }
    return complete;
}
} // namespace

QVector<QRect> Native::automationElements(quintptr window, const QPoint *point, bool *complete) {
    if (complete)
        *complete = false;
    QVector<QRect> rectangles;
    if (!window || !IsWindow(reinterpret_cast<HWND>(window)))
        return rectangles;
    Apartment apartment;
    if (FAILED(apartment.status))
        return rectangles;
    auto automation = createAutomation();
    if (!automation)
        return rectangles;
    IUIAutomationCacheRequest *rawCache = nullptr;
    if (FAILED(automation->CreateCacheRequest(&rawCache)) || !rawCache)
        return rectangles;
    ComPtr<IUIAutomationCacheRequest> cache(rawCache);
    for (int property : {BoundingRectangle, ControlType, IsControlElement, NativeWindowHandle, IsOffscreen})
        cache->AddProperty(property);
    cache->put_TreeScope(point ? TreeScope(TreeScope_Element | TreeScope_Children) : TreeScope_Subtree);
    cache->put_AutomationElementMode(point ? AutomationElementMode_Full : AutomationElementMode_None);
    IUIAutomationCondition *rawCondition = nullptr;
    if (SUCCEEDED(automation->get_RawViewCondition(&rawCondition)) && rawCondition) {
        ComPtr<IUIAutomationCondition> condition(rawCondition);
        cache->put_TreeFilter(condition.get());
    }
    QElapsedTimer elapsed;
    elapsed.start();
    // XAML islands can be missing from the parent's UIA tree. Query native child
    // HWND roots as well, while staying inside the captured window's ownership.
    struct Roots {
        QVector<HWND> handles;
        const QPoint *point;
        bool complete = true;
    } roots{{reinterpret_cast<HWND>(window)}, point};
    EnumChildWindows(reinterpret_cast<HWND>(window), [](HWND child, LPARAM data) -> BOOL {
        auto *roots = reinterpret_cast<Roots *>(data);
        RECT bounds{};
        if (!IsWindowVisible(child) || !GetWindowRect(child, &bounds))
            return TRUE;
        const QRect rectangle(bounds.left, bounds.top, bounds.right - bounds.left, bounds.bottom - bounds.top);
        if (rectangle.isEmpty() || (roots->point && !rectangle.contains(*roots->point)))
            return TRUE;
        if (roots->handles.size() >= 128) {
            roots->complete = false;
            return FALSE;
        }
        roots->handles.append(child);
        return TRUE;
    }, reinterpret_cast<LPARAM>(&roots));
    if (point)
        std::reverse(roots.handles.begin(), roots.handles.end());
    int visited = 0;
    bool hasControl = false;
    bool collected = roots.complete;
    for (HWND handle : roots.handles) {
        if (visited >= 2048 || elapsed.elapsed() > 800) {
            collected = false;
            break;
        }
        IUIAutomationElement *rawRoot = nullptr;
        if (FAILED(automation->ElementFromHandleBuildCache(handle, cache.get(), &rawRoot)) || !rawRoot) {
            collected = false;
            continue;
        }
        ComPtr<IUIAutomationElement> root(rawRoot);
        RECT nativeRect{};
        GetWindowRect(handle, &nativeRect);
        const QRect nativeBounds(nativeRect.left, nativeRect.top,
                                 nativeRect.right - nativeRect.left, nativeRect.bottom - nativeRect.top);
        if (!collect(automation.get(), root.get(), cache.get(), point, rectangles, elapsed, visited, 0, hasControl, &nativeBounds))
            collected = false;
    }
    RECT outer{};
    GetWindowRect(reinterpret_cast<HWND>(window), &outer);
    const QRect outerBounds(outer.left, outer.top, outer.right - outer.left, outer.bottom - outer.top);
    const bool hasChildren = std::any_of(rectangles.cbegin(), rectangles.cend(), [&](const QRect &bounds) {
        const QRect clipped = bounds.intersected(outerBounds);
        return !clipped.isEmpty() && clipped != outerBounds;
    });
    if (complete)
        *complete = collected && hasChildren && hasControl;
    return rectangles;
}
