#include "layout.h"
#include <objbase.h>
#include <UIAutomation.h>
#include <winrt/base.h>

namespace {
struct Observer : winrt::implements<Observer, IUIAutomationStructureChangedEventHandler, IUIAutomationPropertyChangedEventHandler> {
    HWND owner;
    explicit Observer(HWND h) : owner(h) {}
    HRESULT __stdcall HandleStructureChangedEvent(IUIAutomationElement*, StructureChangeType, SAFEARRAY*) noexcept override {
        PostMessage(owner, layoutDirty, 0, 0); return S_OK;
    }
    HRESULT __stdcall HandlePropertyChangedEvent(IUIAutomationElement*, PROPERTYID, VARIANT) noexcept override {
        PostMessage(owner, layoutDirty, 0, 0); return S_OK;
    }
};
RECT findGap(IUIAutomation* api, IUIAutomationElement* root, HWND bar) {
    RECT r{};
    MONITORINFO monitor{sizeof monitor};
    if (!IsWindowVisible(bar) || !GetWindowRect(bar, &r) ||
        !GetMonitorInfo(MonitorFromWindow(bar, MONITOR_DEFAULTTOPRIMARY), &monitor) ||
        r.bottom > monitor.rcMonitor.bottom + 2 || r.top < monitor.rcMonitor.top - 2 ||
        r.right - r.left <= r.bottom - r.top) return {};
    winrt::com_ptr<IUIAutomationCondition> condition;
    winrt::check_hresult(api->get_ControlViewCondition(condition.put()));
    winrt::com_ptr<IUIAutomationCacheRequest> cache;
    winrt::check_hresult(api->CreateCacheRequest(cache.put()));
    for (auto id : {UIA_ControlTypePropertyId, UIA_BoundingRectanglePropertyId, UIA_IsOffscreenPropertyId})
        winrt::check_hresult(cache->AddProperty(id));
    winrt::com_ptr<IUIAutomationElementArray> elements;
    winrt::check_hresult(root->FindAllBuildCache(TreeScope_Descendants, condition.get(), cache.get(), elements.put()));
    std::vector<std::pair<LONG,LONG>> occupied;
    int length{};
    winrt::check_hresult(elements->get_Length(&length));
    for (int i = 0; i < length; ++i) {
        winrt::com_ptr<IUIAutomationElement> el;
        CONTROLTYPEID type{}; BOOL hidden{}; RECT item{}, intersection{};
        if (FAILED(elements->GetElement(i, el.put())) || FAILED(el->get_CachedControlType(&type)) ||
            FAILED(el->get_CachedIsOffscreen(&hidden)) || hidden ||
            FAILED(el->get_CachedBoundingRectangle(&item)) || !IntersectRect(&intersection, &item, &r)) continue;
        if (type == UIA_PaneControlTypeId || type == UIA_WindowControlTypeId || type == UIA_ToolBarControlTypeId ||
            type == UIA_GroupControlTypeId || type == UIA_ListControlTypeId || item.right-item.left >= (r.right-r.left)*0.85) continue;
        occupied.emplace_back(item.left, item.right);
    }
    if (occupied.empty()) return {};
    std::sort(occupied.begin(), occupied.end());
    int dpi = GetDpiForWindow(bar);
    LONG margin = MulDiv(8,dpi,96), minimum = MulDiv(88,dpi,96), maximum = MulDiv(236,dpi,96);
    LONG x = r.left + margin, end = r.left + (r.right-r.left)/2 - margin;
    for (const auto& [left,right] : occupied) {
        if (right <= x) continue;
        if (std::min(left-margin,end)-x >= minimum) { end = std::min(left-margin,end); break; }
        x = std::max(x,right+margin);
    }
    return end-x < minimum ? RECT{} : RECT{x,r.top,std::min(x+maximum,end),r.bottom};
}
}
void Layout::request(bool includeSecondary) {
    std::lock_guard guard(lock);
    if (!worker.joinable()) worker = std::thread([this] { run(); });
    allScreens = includeSecondary;
    requested = true; wake.notify_one();
}
std::vector<RECT> Layout::latest() { std::lock_guard guard(lock); return result; }
Layout::~Layout() {
    { std::lock_guard guard(lock); stopping = true; }
    wake.notify_one();
    if (worker.joinable()) worker.join();
}
void Layout::run() {
    winrt::init_apartment();
    {
        std::vector<HWND> watched;
        winrt::com_ptr<IUIAutomation> api;
        std::vector<winrt::com_ptr<IUIAutomationElement>> roots;
        auto observer = winrt::make_self<Observer>(owner);
        for (;;) {
            bool includeSecondary;
            { std::unique_lock guard(lock); wake.wait(guard,[&]{return requested || stopping;});
              if (stopping) break;
              includeSecondary = allScreens;
              requested = false; }
            // Index zero is reserved for the primary taskbar even while Explorer restarts.
            std::vector<HWND> bars{FindWindow(L"Shell_TrayWnd",nullptr)};
            if (includeSecondary) {
                HWND bar=nullptr;
                while ((bar=FindWindowEx(nullptr,bar,L"Shell_SecondaryTrayWnd",nullptr)))
                    bars.push_back(bar);
            }
            std::vector<RECT> slots(bars.size());
            try {
                if (!api) api = winrt::create_instance<IUIAutomation>(CLSID_CUIAutomation);
                if (bars!=watched) {
                    api->RemoveAllEventHandlers();
                    roots.clear(); roots.resize(bars.size());
                    watched=bars;
                }
                for (size_t i=0;i<bars.size();++i) {
                    if (!bars[i]) continue;
                    auto& root=roots[i];
                    try {
                        if (!root) {
                            winrt::check_hresult(api->ElementFromHandle(bars[i],root.put()));
                            PROPERTYID ids[]{UIA_BoundingRectanglePropertyId,UIA_IsOffscreenPropertyId};
                            api->AddStructureChangedEventHandler(root.get(),TreeScope_Subtree,nullptr,observer.get());
                            api->AddPropertyChangedEventHandlerNativeArray(root.get(),TreeScope_Subtree,nullptr,observer.get(),ids,2);
                        }
                        slots[i]=findGap(api.get(),root.get(),bars[i]);
                    } catch (winrt::hresult_error const&) { watched.clear(); }
                }
            } catch (winrt::hresult_error const&) { watched.clear(); }
            { std::lock_guard guard(lock); result=std::move(slots); }
            PostMessage(owner,layoutReady,0,0);
        }
        if (api) api->RemoveAllEventHandlers();
    }
    winrt::uninit_apartment();
}
