#pragma once
#include <windows.h>
#include <oleacc.h>

// COM ABI declarations for the bundled MinGW 7.3 SDK, which has UIA constants only.
// Method order follows Microsoft's UIAutomationClient.h. Automation is complete so
// Automation2 timeout properties retain their SDK vtable positions.
// https://github.com/microsoft/win32metadata/blob/main/generation/WinSDK/RecompiledIdlHeaders/um/UIAutomationClient.h
namespace EasySnipUIA {
using PROPERTYID = int;
using PATTERNID = int;
using EVENTID = int;
using UIA_HWND = HWND;
enum TreeScope { TreeScope_Element = 1, TreeScope_Children = 2, TreeScope_Descendants = 4, TreeScope_Subtree = 7 };
enum AutomationElementMode { AutomationElementMode_None = 0, AutomationElementMode_Full = 1 };
enum PropertyConditionFlags { PropertyConditionFlags_None = 0, PropertyConditionFlags_IgnoreCase = 1 };
struct IUIAutomationElementArray;
struct IUIAutomationCacheRequest;
struct IUIAutomationElement;
struct IUIAutomation;
struct IUIAutomation2;
struct IUIAutomationCondition : IUnknown {};
struct IUIAutomationTreeWalker;
struct IUIAutomationEventHandler;
struct IUIAutomationPropertyChangedEventHandler;
struct IUIAutomationStructureChangedEventHandler;
struct IUIAutomationFocusChangedEventHandler;
struct IUIAutomationProxyFactory;
struct IUIAutomationProxyFactoryEntry;
struct IUIAutomationProxyFactoryMapping;

struct IUIAutomationElementArray : IUnknown {
    virtual HRESULT STDMETHODCALLTYPE get_Length(int *length) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetElement(int index, IUIAutomationElement **element) = 0;
};

struct IUIAutomationCacheRequest : IUnknown {
    virtual HRESULT STDMETHODCALLTYPE AddProperty(PROPERTYID propertyId) = 0;
    virtual HRESULT STDMETHODCALLTYPE AddPattern(PATTERNID patternId) = 0;
    virtual HRESULT STDMETHODCALLTYPE Clone(IUIAutomationCacheRequest **clonedRequest) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_TreeScope(enum TreeScope *scope) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_TreeScope(enum TreeScope scope) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_TreeFilter(IUIAutomationCondition **filter) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_TreeFilter(IUIAutomationCondition *filter) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_AutomationElementMode(enum AutomationElementMode *mode) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_AutomationElementMode(enum AutomationElementMode mode) = 0;
};

struct IUIAutomationElement : IUnknown {
    virtual HRESULT STDMETHODCALLTYPE SetFocus(void) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetRuntimeId(SAFEARRAY * *runtimeId) = 0;
    virtual HRESULT STDMETHODCALLTYPE FindFirst(enum TreeScope scope, IUIAutomationCondition *condition, IUIAutomationElement **found) = 0;
    virtual HRESULT STDMETHODCALLTYPE FindAll(enum TreeScope scope, IUIAutomationCondition *condition, IUIAutomationElementArray **found) = 0;
    virtual HRESULT STDMETHODCALLTYPE FindFirstBuildCache(enum TreeScope scope, IUIAutomationCondition *condition, IUIAutomationCacheRequest *cacheRequest, IUIAutomationElement **found) = 0;
    virtual HRESULT STDMETHODCALLTYPE FindAllBuildCache(enum TreeScope scope, IUIAutomationCondition *condition, IUIAutomationCacheRequest *cacheRequest, IUIAutomationElementArray **found) = 0;
    virtual HRESULT STDMETHODCALLTYPE BuildUpdatedCache(IUIAutomationCacheRequest *cacheRequest, IUIAutomationElement **updatedElement) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetCurrentPropertyValue(PROPERTYID propertyId, VARIANT *retVal) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetCurrentPropertyValueEx(PROPERTYID propertyId, BOOL ignoreDefaultValue, VARIANT *retVal) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetCachedPropertyValue(PROPERTYID propertyId, VARIANT *retVal) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetCachedPropertyValueEx(PROPERTYID propertyId, BOOL ignoreDefaultValue, VARIANT *retVal) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetCurrentPatternAs(PATTERNID patternId, REFIID riid, void **patternObject) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetCachedPatternAs(PATTERNID patternId, REFIID riid, void **patternObject) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetCurrentPattern(PATTERNID patternId, IUnknown **patternObject) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetCachedPattern(PATTERNID patternId, IUnknown **patternObject) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetCachedParent(IUIAutomationElement **parent) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetCachedChildren(IUIAutomationElementArray **children) = 0;
};

struct IUIAutomation : IUnknown {
    virtual HRESULT STDMETHODCALLTYPE CompareElements(IUIAutomationElement *el1, IUIAutomationElement *el2, BOOL *areSame) = 0;
    virtual HRESULT STDMETHODCALLTYPE CompareRuntimeIds(SAFEARRAY * runtimeId1, SAFEARRAY * runtimeId2, BOOL *areSame) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetRootElement(IUIAutomationElement **root) = 0;
    virtual HRESULT STDMETHODCALLTYPE ElementFromHandle(UIA_HWND hwnd, IUIAutomationElement **element) = 0;
    virtual HRESULT STDMETHODCALLTYPE ElementFromPoint(POINT pt, IUIAutomationElement **element) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetFocusedElement(IUIAutomationElement **element) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetRootElementBuildCache(IUIAutomationCacheRequest *cacheRequest, IUIAutomationElement **root) = 0;
    virtual HRESULT STDMETHODCALLTYPE ElementFromHandleBuildCache(UIA_HWND hwnd, IUIAutomationCacheRequest *cacheRequest, IUIAutomationElement **element) = 0;
    virtual HRESULT STDMETHODCALLTYPE ElementFromPointBuildCache(POINT pt, IUIAutomationCacheRequest *cacheRequest, IUIAutomationElement **element) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetFocusedElementBuildCache(IUIAutomationCacheRequest *cacheRequest, IUIAutomationElement **element) = 0;
    virtual HRESULT STDMETHODCALLTYPE CreateTreeWalker(IUIAutomationCondition *pCondition, IUIAutomationTreeWalker **walker) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_ControlViewWalker(IUIAutomationTreeWalker **walker) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_ContentViewWalker(IUIAutomationTreeWalker **walker) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_RawViewWalker(IUIAutomationTreeWalker **walker) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_RawViewCondition(IUIAutomationCondition **condition) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_ControlViewCondition(IUIAutomationCondition **condition) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_ContentViewCondition(IUIAutomationCondition **condition) = 0;
    virtual HRESULT STDMETHODCALLTYPE CreateCacheRequest(IUIAutomationCacheRequest **cacheRequest) = 0;
    virtual HRESULT STDMETHODCALLTYPE CreateTrueCondition(IUIAutomationCondition **newCondition) = 0;
    virtual HRESULT STDMETHODCALLTYPE CreateFalseCondition(IUIAutomationCondition **newCondition) = 0;
    virtual HRESULT STDMETHODCALLTYPE CreatePropertyCondition(PROPERTYID propertyId, VARIANT value, IUIAutomationCondition **newCondition) = 0;
    virtual HRESULT STDMETHODCALLTYPE CreatePropertyConditionEx(PROPERTYID propertyId, VARIANT value, enum PropertyConditionFlags flags, IUIAutomationCondition **newCondition) = 0;
    virtual HRESULT STDMETHODCALLTYPE CreateAndCondition(IUIAutomationCondition *condition1, IUIAutomationCondition *condition2, IUIAutomationCondition **newCondition) = 0;
    virtual HRESULT STDMETHODCALLTYPE CreateAndConditionFromArray(SAFEARRAY * conditions, IUIAutomationCondition **newCondition) = 0;
    virtual HRESULT STDMETHODCALLTYPE CreateAndConditionFromNativeArray(IUIAutomationCondition **conditions, int conditionCount, IUIAutomationCondition **newCondition) = 0;
    virtual HRESULT STDMETHODCALLTYPE CreateOrCondition(IUIAutomationCondition *condition1, IUIAutomationCondition *condition2, IUIAutomationCondition **newCondition) = 0;
    virtual HRESULT STDMETHODCALLTYPE CreateOrConditionFromArray(SAFEARRAY * conditions, IUIAutomationCondition **newCondition) = 0;
    virtual HRESULT STDMETHODCALLTYPE CreateOrConditionFromNativeArray(IUIAutomationCondition **conditions, int conditionCount, IUIAutomationCondition **newCondition) = 0;
    virtual HRESULT STDMETHODCALLTYPE CreateNotCondition(IUIAutomationCondition *condition, IUIAutomationCondition **newCondition) = 0;
    virtual HRESULT STDMETHODCALLTYPE AddAutomationEventHandler(EVENTID eventId, IUIAutomationElement *element, enum TreeScope scope, IUIAutomationCacheRequest *cacheRequest, IUIAutomationEventHandler *handler) = 0;
    virtual HRESULT STDMETHODCALLTYPE RemoveAutomationEventHandler(EVENTID eventId, IUIAutomationElement *element, IUIAutomationEventHandler *handler) = 0;
    virtual HRESULT STDMETHODCALLTYPE AddPropertyChangedEventHandlerNativeArray(IUIAutomationElement *element, enum TreeScope scope, IUIAutomationCacheRequest *cacheRequest, IUIAutomationPropertyChangedEventHandler *handler, PROPERTYID *propertyArray, int propertyCount) = 0;
    virtual HRESULT STDMETHODCALLTYPE AddPropertyChangedEventHandler(IUIAutomationElement *element, enum TreeScope scope, IUIAutomationCacheRequest *cacheRequest, IUIAutomationPropertyChangedEventHandler *handler, SAFEARRAY * propertyArray) = 0;
    virtual HRESULT STDMETHODCALLTYPE RemovePropertyChangedEventHandler(IUIAutomationElement *element, IUIAutomationPropertyChangedEventHandler *handler) = 0;
    virtual HRESULT STDMETHODCALLTYPE AddStructureChangedEventHandler(IUIAutomationElement *element, enum TreeScope scope, IUIAutomationCacheRequest *cacheRequest, IUIAutomationStructureChangedEventHandler *handler) = 0;
    virtual HRESULT STDMETHODCALLTYPE RemoveStructureChangedEventHandler(IUIAutomationElement *element, IUIAutomationStructureChangedEventHandler *handler) = 0;
    virtual HRESULT STDMETHODCALLTYPE AddFocusChangedEventHandler(IUIAutomationCacheRequest *cacheRequest, IUIAutomationFocusChangedEventHandler *handler) = 0;
    virtual HRESULT STDMETHODCALLTYPE RemoveFocusChangedEventHandler(IUIAutomationFocusChangedEventHandler *handler) = 0;
    virtual HRESULT STDMETHODCALLTYPE RemoveAllEventHandlers(void) = 0;
    virtual HRESULT STDMETHODCALLTYPE IntNativeArrayToSafeArray(int *array, int arrayCount, SAFEARRAY * *safeArray) = 0;
    virtual HRESULT STDMETHODCALLTYPE IntSafeArrayToNativeArray(SAFEARRAY * intArray, int **array, int *arrayCount) = 0;
    virtual HRESULT STDMETHODCALLTYPE RectToVariant(RECT rc, VARIANT *var) = 0;
    virtual HRESULT STDMETHODCALLTYPE VariantToRect(VARIANT var, RECT *rc) = 0;
    virtual HRESULT STDMETHODCALLTYPE SafeArrayToRectNativeArray(SAFEARRAY * rects, RECT **rectArray, int *rectArrayCount) = 0;
    virtual HRESULT STDMETHODCALLTYPE CreateProxyFactoryEntry(IUIAutomationProxyFactory *factory, IUIAutomationProxyFactoryEntry **factoryEntry) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_ProxyFactoryMapping(IUIAutomationProxyFactoryMapping **factoryMapping) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetPropertyProgrammaticName(PROPERTYID property, BSTR *name) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetPatternProgrammaticName(PATTERNID pattern, BSTR *name) = 0;
    virtual HRESULT STDMETHODCALLTYPE PollForPotentialSupportedPatterns(IUIAutomationElement *pElement, SAFEARRAY * *patternIds, SAFEARRAY * *patternNames) = 0;
    virtual HRESULT STDMETHODCALLTYPE PollForPotentialSupportedProperties(IUIAutomationElement *pElement, SAFEARRAY * *propertyIds, SAFEARRAY * *propertyNames) = 0;
    virtual HRESULT STDMETHODCALLTYPE CheckNotSupported(VARIANT value, BOOL *isNotSupported) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_ReservedNotSupportedValue(IUnknown **notSupportedValue) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_ReservedMixedAttributeValue(IUnknown **mixedAttributeValue) = 0;
    virtual HRESULT STDMETHODCALLTYPE ElementFromIAccessible(IAccessible *accessible, int childId, IUIAutomationElement **element) = 0;
    virtual HRESULT STDMETHODCALLTYPE ElementFromIAccessibleBuildCache(IAccessible *accessible, int childId, IUIAutomationCacheRequest *cacheRequest, IUIAutomationElement **element) = 0;
};

struct IUIAutomation2 : IUIAutomation {
    virtual HRESULT STDMETHODCALLTYPE get_AutoSetFocus(BOOL *autoSetFocus) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_AutoSetFocus(BOOL autoSetFocus) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_ConnectionTimeout(DWORD *timeout) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_ConnectionTimeout(DWORD timeout) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_TransactionTimeout(DWORD *timeout) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_TransactionTimeout(DWORD timeout) = 0;
};

} // namespace EasySnipUIA
