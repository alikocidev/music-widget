// Silent, out-of-process Windows media session. Never sends commands to another player.
#include <windows.h>
#include <shobjidl.h>
#include <SystemMediaTransportControlsInterop.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Media.h>
#include <winrt/Windows.Storage.h>
#include <winrt/Windows.Storage.Streams.h>
#include <cstdio>
#include <filesystem>
#include <string>
using namespace winrt;
using namespace Windows::Media;
using namespace Windows::Storage;
using namespace Windows::Storage::Streams;

namespace {
SystemMediaTransportControls source{nullptr};
RandomAccessStreamReference covers[2]{nullptr,nullptr};
void publish(unsigned index) {
    auto update=source.DisplayUpdater();
    update.Type(MediaPlaybackType::Music);
    auto properties=update.MusicProperties();
    properties.Title(L"Widget fixture "+std::to_wstring(index));
    properties.Artist(L"Music Widget benchmark");
    properties.AlbumTitle(L"Local fixture"); properties.TrackNumber(index+1);
    update.Thumbnail(covers[index%2]); update.Update();
}
LRESULT CALLBACK proc(HWND w,UINT msg,WPARAM wp,LPARAM lp) {
    if (msg==WM_APP+1) { publish(unsigned(wp)); return 0; }
    if (msg==WM_APP+2) {
        source.PlaybackStatus(wp?MediaPlaybackStatus::Playing:MediaPlaybackStatus::Paused); return 0;
    }
    if (msg==WM_APP+3) { source.IsNextEnabled(wp!=0); return 0; }
    if (msg==WM_CLOSE) { PostQuitMessage(0); return 0; }
    return DefWindowProc(w,msg,wp,lp);
}
}
int wmain(int argc,wchar_t** argv) {
    if (argc!=3) return 2;
    try {
        init_apartment();
        SetCurrentProcessExplicitAppUserModelID(L"MusicWidget.Spotify.TestFixture");
        WNDCLASS wc{}; wc.hInstance=GetModuleHandle(nullptr); wc.lpfnWndProc=proc; wc.lpszClassName=L"MusicWidget.SilentFixture";
        check_bool(RegisterClass(&wc));
        auto w=CreateWindow(wc.lpszClassName,L"MusicWidget test fixture",WS_POPUP,0,0,0,0,nullptr,nullptr,wc.hInstance,nullptr);
        check_pointer(w);
        auto factory=get_activation_factory<SystemMediaTransportControls,ISystemMediaTransportControlsInterop>();
        check_hresult(factory->GetForWindow(w,guid_of<SystemMediaTransportControls>(),put_abi(source)));
        for (int i=0;i<2;++i) {
            auto path=std::filesystem::absolute(argv[i+1]).make_preferred();
            covers[i]=RandomAccessStreamReference::CreateFromFile(StorageFile::GetFileFromPathAsync(path.native()).get());
        }
        source.IsEnabled(true); source.IsPlayEnabled(true); source.IsPauseEnabled(true);
        source.IsNextEnabled(true); source.IsPreviousEnabled(true);
        auto listen=source.ButtonPressed(auto_revoke,[](auto&&,auto const& e){
            std::printf("button %d\n",int(e.Button())); std::fflush(stdout);
        });
        publish(0); source.PlaybackStatus(MediaPlaybackStatus::Playing);
        std::puts("ready"); std::fflush(stdout);
        MSG m{}; while (GetMessage(&m,nullptr,0,0)>0) {TranslateMessage(&m); DispatchMessage(&m);}
        source.IsEnabled(false); listen.revoke(); source=nullptr;
        DestroyWindow(w); return 0;
    } catch (hresult_error const& e) {
        std::fprintf(stderr,"fixture error 0x%08x %ls\n",unsigned(e.code()),e.message().c_str()); return 1;
    }
}
