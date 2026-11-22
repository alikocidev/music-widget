#include "layout.h"
#include "view.h"
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Media.Control.h>
#include <winrt/Windows.Storage.Streams.h>
#include <winrt/Windows.Graphics.Imaging.h>
#include <tuple>
#include <memory>

using namespace winrt;
using namespace Windows::Media::Control;
using Session=GlobalSystemMediaTransportControlsSession;
using Manager=GlobalSystemMediaTransportControlsSessionManager;
using Thumbnail=Windows::Storage::Streams::IRandomAccessStreamReference;
namespace imaging=Windows::Graphics::Imaging;
using PlaybackStatus=GlobalSystemMediaTransportControlsSessionPlaybackStatus;

namespace {
constexpr wchar_t settingsKey[]=L"Software\\MusicWidget";
constexpr wchar_t trackOnlyValue[]=L"ShowCardOnTrackChangeOnly";
constexpr wchar_t allScreensValue[]=L"TaskbarOnAllScreens";
enum MenuItem : UINT { exitWidget=1, toggleTrackOnly, primaryScreen, everyScreen };
bool readFlag(const wchar_t* name) {
    DWORD value{},size=sizeof value;
    return RegGetValueW(HKEY_CURRENT_USER,settingsKey,name,RRF_RT_REG_DWORD,nullptr,&value,&size)==ERROR_SUCCESS && value!=0;
}
std::wstring trim(hstring const& str) {
    std::wstring value(str);
    auto a=value.find_first_not_of(L" \n\t\r"), b=value.find_last_not_of(L" \n\t\r");
    return a==value.npos?L"":value.substr(a,b-a+1);
}
HWND messageWindow{};
void CALLBACK foreground(HWINEVENTHOOK,DWORD,HWND,LONG,LONG,DWORD,DWORD) {
    PostMessage(messageWindow,foregroundChanged,0,0);
}

class App : public std::enable_shared_from_this<App> {
    HWND window{};
    std::unique_ptr<View> view;
    std::unique_ptr<Layout> layout;
    UINT shellRestart=RegisterWindowMessage(L"TaskbarCreated");
    HWINEVENTHOOK hook{};
    Manager manager{nullptr};
    Session session{nullptr};
    Manager::SessionsChanged_revoker sessionList;
    Manager::CurrentSessionChanged_revoker active;
    Session::MediaPropertiesChanged_revoker media;
    struct Candidate {
        Session source;
        MusicSource kind;
        Session::PlaybackInfoChanged_revoker listener;
    };
    std::vector<Candidate> candidates;
    bool selecting{}, rebuildPending{}, selectionPending{};
    std::optional<Song> previous, current;
    std::optional<bool> lastPlaying;
    UINT_PTR generation{};
    uint64_t metadataVersion{}, artVersion{};
    bool trackOnly{}, allScreens{};
    bool baseline=true, closed=false, measuring=false, pending=false, timer=false;

    void invalidateLayout() {
        if (!current || closed) return;
        if (measuring) pending=true;
        else if (!timer) timer=SetTimer(window,1,180,nullptr)!=0;
    }
    void clearCurrent() {
        current.reset(); lastPlaying.reset(); ++artVersion; view->clear();
    }
    void syncCandidates() {
        auto available=manager.GetSessions();
        std::vector<Candidate> found;
        for (const auto& source : available) {
            if (!source) continue;
            auto known=std::find_if(candidates.begin(),candidates.end(),
                [&](const Candidate& candidate){ return candidate.source==source; });
            if (known!=candidates.end()) {
                found.push_back(std::move(*known));
                continue;
            }
            try {
                auto kind=classifySource(std::wstring(source.SourceAppUserModelId()));
                if (kind==MusicSource::other) continue;
                auto listener=source.PlaybackInfoChanged(auto_revoke,[h=window](auto&&,auto&&){
                    PostMessage(h,playbackChanged,0,0);
                });
                found.push_back({source,kind,std::move(listener)});
            } catch (hresult_error const&) { /* The session may disappear during enumeration. */ }
        }
        candidates=std::move(found);
    }
    void select(bool rebuild=false) {
        if (!manager || closed) return;
        if (selecting) {
            selectionPending=true;
            rebuildPending|=rebuild;
            return;
        }
        selecting=true;
        try {
            if (rebuild) syncCandidates();
            auto system=manager.GetCurrentSession();
            Session next{nullptr};
            std::tuple<int,bool,bool,bool> best{-1,false,false,false};
            for (const auto& candidate : candidates) {
                try {
                    if (!candidate.source) continue;
                    auto info=candidate.source.GetPlaybackInfo();
                    if (!info) continue;
                    auto status=info.PlaybackStatus();
                    if (status==PlaybackStatus::Closed) continue;
                    bool playing=status==PlaybackStatus::Playing;
                    // Prefer Spotify only during simultaneous playback. Paused sources keep their place.
                    auto rank=std::make_tuple(playing?2:status==PlaybackStatus::Paused?1:0,
                        playing && candidate.kind==MusicSource::spotify, candidate.source==session, candidate.source==system);
                    if (rank>best) { best=rank; next=candidate.source; }
                } catch (hresult_error const&) { /* Other sessions can still be selected. */ }
            }
            if (!closed && next!=session) {
                media.revoke();
                ++generation; ++metadataVersion;
                session=next;
                previous.reset();
                clearCurrent();
                if (next) {
                    media=next.MediaPropertiesChanged(auto_revoke,[h=window,g=generation](auto&&,auto&&){
                        PostMessage(h,metadataChanged,WPARAM(g),0);
                    });
                    refresh();
                }
            } else if (!closed && session) {
                if (current) {
                    auto state=readPlayback(session);
                    if (state && !closed && current) {
                        view->setSong(*current,state->buttons,generation);
                        if (playbackTransition(state->status) && !trackOnly) {
                            view->announce();
                            refresh(); // Fetch the current artwork when a closed card reopens.
                        }
                    }
                } else refresh();
            }
            if (!session) baseline=false;
        } catch (hresult_error const&) { /* A later session event retries selection. */ }
        selecting=false;
        if (selectionPending && !closed) {
            PostMessage(window,rebuildPending?selectSource:playbackChanged,0,0);
            rebuildPending=false;
            selectionPending=false;
        }
    }
    struct Playback {
        PlaybackStatus status;
        Controls buttons;
        bool useToggleCommand{};
    };
    bool playbackTransition(PlaybackStatus status) {
        if (status!=PlaybackStatus::Playing && status!=PlaybackStatus::Paused &&
            status!=PlaybackStatus::Stopped) return false;
        bool playing=status==PlaybackStatus::Playing;
        bool transition=lastPlaying && *lastPlaying!=playing;
        lastPlaying=playing;
        return transition;
    }
    static std::optional<Playback> readPlayback(Session source) {
        auto info=source.GetPlaybackInfo();
        if (!info) return std::nullopt;
        auto status=info.PlaybackStatus();
        bool playing=status==PlaybackStatus::Playing;
        auto buttons=info.Controls();
        if (!buttons) return Playback{status,{playing}};
        bool direct=playing?buttons.IsPauseEnabled():buttons.IsPlayEnabled();
        bool toggleOnly=!direct && buttons.IsPlayPauseToggleEnabled();
        return Playback{status,{playing,buttons.IsPreviousEnabled(),direct || toggleOnly,buttons.IsNextEnabled()},toggleOnly};
    }
    fire_and_forget loadArt(Thumbnail ref,uint64_t version) {
        auto lifetime=shared_from_this();
        try {
            auto stream=co_await ref.OpenReadAsync();
            if (closed || version!=artVersion || stream.Size()>8*1024*1024) co_return;
            auto decoder=co_await imaging::BitmapDecoder::CreateAsync(stream);
            auto w=decoder.PixelWidth(),h=decoder.PixelHeight();
            if (closed || version!=artVersion || !w || !h || uint64_t(w)*h>20000000) co_return;
            imaging::BitmapTransform transform;
            unsigned longest=std::max(w,h);
            auto scaled=[longest](unsigned size){return std::max(1u,size*std::min(128u,longest)/longest);};
            w=scaled(w); h=scaled(h); transform.ScaledWidth(w); transform.ScaledHeight(h);
            auto pixels=co_await decoder.GetPixelDataAsync(imaging::BitmapPixelFormat::Bgra8,
                imaging::BitmapAlphaMode::Ignore,transform,imaging::ExifOrientationMode::IgnoreExifOrientation,
                imaging::ColorManagementMode::DoNotColorManage);
            if (closed || version!=artVersion || !view->cardVisible()) co_return;
            auto bytes=pixels.DetachPixelData();
            if (bytes.size()==size_t(w)*h*4) view->setCover({int(w),int(h),{bytes.begin(),bytes.end()}});
        } catch (hresult_error const&) {}
    }
    fire_and_forget refresh() {
        auto lifetime=shared_from_this(); auto source=session;
        auto version=++metadataVersion;
        try {
            auto data=co_await source.TryGetMediaPropertiesAsync();
            if (closed || version!=metadataVersion) co_return;
            if (!data || trim(data.Title()).empty()) { clearCurrent(); baseline=false; co_return; }
            Song song{std::wstring(source.SourceAppUserModelId()),trim(data.Title()),trim(data.Artist()),trim(data.AlbumTitle()),data.TrackNumber()};
            auto ref=data.Thumbnail();
            auto state=readPlayback(source);
            // Calls into the player can dispatch other STA messages before they return.
            if (closed || version!=metadataVersion) co_return;
            bool stateChanged=state && playbackTransition(state->status);
            bool newSong=changed(previous,song), announce=!baseline && (newSong || (stateChanged && !trackOnly)), first=!current;
            current=song; previous=song; baseline=false;
            // Retire pending artwork before the new title can be drawn.
            auto coverVersion=++artVersion;
            if (newSong || !ref) view->setCover({});
            view->setSong(song,state?state->buttons:Controls{},generation);
            if (first) invalidateLayout();
            if (announce) view->announce();
            if (ref && coverVersion==artVersion && view->cardVisible()) loadArt(ref,coverVersion);
        } catch (hresult_error const&) {
            // Keep the last valid frame during a failed read. Session removal clears it in select().
        }
    }
    fire_and_forget command(Command action,UINT_PTR clickedGeneration) {
        auto lifetime=shared_from_this(); auto source=session;
        if (!source || !current || clickedGeneration!=generation) co_return;
        try {
            auto playback=readPlayback(source);
            if (!playback || closed || clickedGeneration!=generation || source!=session) co_return;
            auto s=playback->buttons;
            if (action==Command::previous && s.previous) co_await source.TrySkipPreviousAsync();
            if (action==Command::next && s.next) co_await source.TrySkipNextAsync();
            if (action==Command::toggle && s.toggle) {
                if (playback->useToggleCommand) co_await source.TryTogglePlayPauseAsync();
                else if (s.playing) co_await source.TryPauseAsync();
                else co_await source.TryPlayAsync();
            }
        } catch (hresult_error const&) { if (!closed) select(); }
    }
    void menu() {
        POINT p{}; GetCursorPos(&p);
        HMENU popup=CreatePopupMenu();
        HMENU screens=CreatePopupMenu();
        AppendMenu(screens,MF_STRING,primaryScreen,L"Ana ekran");
        AppendMenu(screens,MF_STRING,everyScreen,L"Tüm ekranlar");
        CheckMenuRadioItem(screens,primaryScreen,everyScreen,allScreens?everyScreen:primaryScreen,MF_BYCOMMAND);
        AppendMenu(popup,MF_POPUP,reinterpret_cast<UINT_PTR>(screens),L"Görev çubuğunda göster");
        AppendMenu(popup,MF_STRING|(trackOnly?MF_CHECKED:MF_UNCHECKED),toggleTrackOnly,L"Kartı yalnızca şarkı değiştiğinde göster");
        AppendMenu(popup,MF_SEPARATOR,0,nullptr);
        AppendMenu(popup,MF_STRING,exitWidget,L"Çıkış");
        SetForegroundWindow(window);
        TrackPopupMenu(popup,TPM_RIGHTBUTTON,p.x,p.y,0,window,nullptr);
        DestroyMenu(popup); PostMessage(window,WM_NULL,0,0);
    }
    fire_and_forget startFeed() {
        auto lifetime=shared_from_this();
        try {
            auto result=co_await Manager::RequestAsync();
            if (closed) co_return;
            manager=result;
            auto h=window;
            sessionList=manager.SessionsChanged(auto_revoke,[h](auto&&,auto&&){PostMessage(h,selectSource,0,0);});
            active=manager.CurrentSessionChanged(auto_revoke,[h](auto&&,auto&&){PostMessage(h,playbackChanged,0,0);});
            select(true);
        } catch (hresult_error const&) { if (!closed) view->warn(L"Windows medya oturumları açılamadı. Medya erişimini denetleyip uygulamayı yeniden başlatın."); }
    }
    LRESULT handle(UINT msg, WPARAM wp, LPARAM lp) {
        if (!view || closed) return DefWindowProc(window,msg,wp,lp);
        if (msg==shellRestart) { view->restoreTray(); invalidateLayout(); return 0; }
        switch (msg) {
        case selectSource: select(true); return 0;
        case metadataChanged: if (session && wp==generation) refresh(); return 0;
        case playbackChanged: select(); return 0;
        case transportCommand: command(Command(lp),wp); return 0;
        case WM_COMMAND:
            if (LOWORD(wp)==toggleTrackOnly) {
                DWORD value=trackOnly?0:1;
                if (RegSetKeyValueW(HKEY_CURRENT_USER,settingsKey,trackOnlyValue,REG_DWORD,&value,sizeof value)==ERROR_SUCCESS)
                    trackOnly=value!=0;
                else view->warn(L"Kart gösterim tercihi kaydedilemedi.");
            } else if (LOWORD(wp)==primaryScreen || LOWORD(wp)==everyScreen) {
                DWORD value=LOWORD(wp)==everyScreen;
                if (RegSetKeyValueW(HKEY_CURRENT_USER,settingsKey,allScreensValue,REG_DWORD,&value,sizeof value)==ERROR_SUCCESS) {
                    allScreens=value!=0;
                    view->setSlots({});
                    invalidateLayout();
                } else view->warn(L"Görev çubuğu ekran tercihi kaydedilemedi.");
            } else if (LOWORD(wp)==exitWidget) PostMessage(window,WM_CLOSE,0,0);
            return 0;
        case closeCard: ++artVersion; view->hideCard(); return 0;
        case trayEvent: if (lp==WM_RBUTTONUP || lp==WM_CONTEXTMENU) menu(); return 0;
        case WM_SETTINGCHANGE: case WM_THEMECHANGED: view->theme(); [[fallthrough]];
        case WM_DISPLAYCHANGE: case WM_DPICHANGED: case layoutDirty: invalidateLayout(); return 0;
        case WM_TIMER:
            if (wp==1) {
                KillTimer(window,1); timer=false;
                if (current) { measuring=true; layout->request(allScreens); }
            }
            return 0;
        case layoutReady: {
            auto slots=layout->latest();
            if (!allScreens && slots.size()>1) slots.resize(1);
            measuring=false; view->setSlots(slots);
            if (pending) { pending=false; invalidateLayout(); }
            return 0;
        }
        case foregroundChanged: view->placeText(); return 0;
        case WM_CLOSE:
            closed=true; sessionList.revoke(); active.revoke(); media.revoke();
            if (hook) UnhookWinEvent(hook);
            view.reset(); PostQuitMessage(0); return 0;
        }
        return DefWindowProc(window,msg,wp,lp);
    }
    static LRESULT CALLBACK proc(HWND h,UINT msg,WPARAM wp,LPARAM lp) {
        if (msg==WM_NCCREATE) {
            auto app=static_cast<App*>(reinterpret_cast<CREATESTRUCT*>(lp)->lpCreateParams);
            app->window=h; SetWindowLongPtr(h,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(app));
        }
        auto app=reinterpret_cast<App*>(GetWindowLongPtr(h,GWLP_USERDATA));
        return app?app->handle(msg,wp,lp):DefWindowProc(h,msg,wp,lp);
    }
public:
    int run() {
        trackOnly=readFlag(trackOnlyValue);
        allScreens=readFlag(allScreensValue);
        WNDCLASS wc{}; wc.hInstance=GetModuleHandle(nullptr); wc.lpfnWndProc=proc; wc.lpszClassName=L"MusicWidget.Host";
        check_bool(RegisterClass(&wc));
        check_pointer(CreateWindowEx(WS_EX_TOOLWINDOW,wc.lpszClassName,L"Music Widget",WS_POPUP,0,0,0,0,nullptr,nullptr,wc.hInstance,this));
        messageWindow=window;
        hook=SetWinEventHook(EVENT_SYSTEM_FOREGROUND,EVENT_SYSTEM_FOREGROUND,nullptr,foreground,0,0,WINEVENT_OUTOFCONTEXT);
        view=std::make_unique<View>(window); layout=std::make_unique<Layout>(window);
        startFeed();
        MSG msg{};
        while (GetMessage(&msg,nullptr,0,0)>0) { TranslateMessage(&msg); DispatchMessage(&msg); }
        layout.reset(); DestroyWindow(window);
        return int(msg.wParam);
    }
};
}
int WINAPI wWinMain(HINSTANCE,HINSTANCE,PWSTR,int) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    handle mutex{CreateMutex(nullptr,FALSE,L"Local\\MusicWidget.74643FA4")};
    if (!mutex || GetLastError()==ERROR_ALREADY_EXISTS) return 0;
    try {
        init_apartment(apartment_type::single_threaded);
        return std::make_shared<App>()->run();
    } catch (hresult_error const& e) { return e.code(); }
}
