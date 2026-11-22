#include "view.h"
#include <dwmapi.h>
#include <strsafe.h>

namespace {
constexpr COLORREF key = RGB(255,0,255);
constexpr int cardWidth=400, cardHeight=132, panelAlpha=224;
constexpr DWORD panelPixel(unsigned glyph,COLORREF ink) {
    const unsigned panel=(panelAlpha*(255-glyph)+127)/255;
    auto channel=[&](unsigned bg,unsigned fg){ return (bg*panel+fg*glyph+127)/255; };
    return ((panel+glyph)<<24) | (channel(32,ink&255)<<16) |
        (channel(32,(ink>>8)&255)<<8) | channel(36,(ink>>16)&255);
}
int px(HWND w, int value) { return MulDiv(value,GetDpiForWindow(w),96); }
RECT box(HWND w,int x,int y,int width,int height) {
    return {px(w,x),px(w,y),px(w,x+width),px(w,y+height)};
}
RECT buttonRect(HWND card,int i) { return box(card,132+i*56,78,48,38); }
void makeOpaque(DWORD* pixels,int stride,RECT rect) {
    for (int y=rect.top;y<rect.bottom;++y)
        for (int x=rect.left;x<rect.right;++x) pixels[y*stride+x]|=0xff000000u;
}
void fill(HDC dc,RECT r,COLORREF color) {
    SetDCBrushColor(dc,color); FillRect(dc,&r,static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
}
void text(HDC dc,HWND w,RECT rect,const std::wstring& value,int size,COLORREF color,
          UINT flags=DT_LEFT,int weight=FW_NORMAL,const wchar_t* face=L"Segoe UI") {
    auto font=CreateFont(-px(w,size),0,0,0,weight,FALSE,FALSE,FALSE,DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,ANTIALIASED_QUALITY,DEFAULT_PITCH,face);
    auto old=SelectObject(dc,font);
    SetBkMode(dc,TRANSPARENT); SetTextColor(dc,color);
    DrawText(dc,value.c_str(),-1,&rect,flags|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX|DT_END_ELLIPSIS);
    SelectObject(dc,old); DeleteObject(font);
}
HICON noteIcon() {
    // Monochrome mask with a mint, opaque musical note. Transparent pixels have zero alpha.
    DWORD pixels[32*32]{};
    for (int y=0;y<32;++y) for (int x=0;x<32;++x) {
        auto oval=[&](int cx,int cy){return (x-cx)*(x-cx)*9+(y-cy)*(y-cy)*25<=225;};
        if ((x>=11 && x<=14 && y>=9 && y<=24) || (x>=23 && x<=26 && y>=6 && y<=21) ||
            (x>=11 && x<=26 && y>=8-(x-11)/5 && y<=11-(x-11)/5) || oval(9,24) || oval(21,21))
            pixels[y*32+x]=0xffa1e9c7;
    }
    BYTE mask[128]{};
    ICONINFO info{TRUE,0,0,CreateBitmap(32,32,1,1,mask),CreateBitmap(32,32,1,32,pixels)};
    auto icon=CreateIconIndirect(&info);
    DeleteObject(info.hbmMask); DeleteObject(info.hbmColor); return icon;
}
bool fullscreen(const RECT& slot) {
    HWND top=GetForegroundWindow(); RECT r{};
    MONITORINFO m{sizeof m};
    if (!GetWindowRect(top,&r) || !GetMonitorInfo(MonitorFromRect(&slot,MONITOR_DEFAULTTONULL),&m) ||
        r.left>m.rcMonitor.left || r.top>m.rcMonitor.top || r.right<m.rcMonitor.right || r.bottom<m.rcMonitor.bottom) return false;
    wchar_t name[64]{}; GetClassName(top,name,64);
    return wcscmp(name,L"Progman") && wcscmp(name,L"WorkerW") && wcscmp(name,L"Shell_TrayWnd");
}
}
View::View(HWND w) : owner(w) {
    WNDCLASS wc{}; wc.lpfnWndProc=proc; wc.hInstance=GetModuleHandle(nullptr); wc.hCursor=LoadCursor(nullptr,IDC_ARROW);
    wc.lpszClassName=L"MusicWidget.Text"; RegisterClass(&wc);
    wc.lpszClassName=L"MusicWidget.Card"; RegisterClass(&wc);
    labels.push_back({createLabel(),{}});
    icon=noteIcon();
    tray.hWnd=owner; tray.uID=1; tray.uFlags=NIF_ICON|NIF_MESSAGE|NIF_TIP;
    tray.uCallbackMessage=trayEvent; tray.hIcon=icon;
    StringCchCopy(tray.szTip,ARRAYSIZE(tray.szTip),L"Music Widget");
    restoreTray(); theme();
}
View::~View() {
    Shell_NotifyIcon(NIM_DELETE,&tray);
    if (card) DestroyWindow(card);
    for (const auto& label : labels) DestroyWindow(label.window);
    DestroyIcon(icon);
}
HWND View::createLabel() {
    auto window=CreateWindowEx(WS_EX_TOPMOST|WS_EX_NOACTIVATE|WS_EX_TOOLWINDOW|WS_EX_LAYERED|WS_EX_TRANSPARENT,
        L"MusicWidget.Text",song.title.c_str(),WS_POPUP,0,0,0,0,nullptr,nullptr,GetModuleHandle(nullptr),this);
    if (window) SetLayeredWindowAttributes(window,key,0,LWA_COLORKEY);
    return window;
}
void View::restoreTray() { Shell_NotifyIcon(NIM_ADD,&tray); }
void View::theme() {
    DWORD light=0,size=sizeof light;
    RegGetValue(HKEY_CURRENT_USER,L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
        L"SystemUsesLightTheme",RRF_RT_REG_DWORD,nullptr,&light,&size);
    textColor=light?RGB(24,24,26):RGB(255,255,255);
    for (const auto& label : labels) if (label.window) InvalidateRect(label.window,nullptr,FALSE);
}
void View::warn(const wchar_t* message) {
    auto n=tray; n.uFlags=NIF_INFO; n.dwInfoFlags=NIIF_WARNING;
    StringCchCopy(n.szInfoTitle,ARRAYSIZE(n.szInfoTitle),L"Music Widget");
    StringCchCopy(n.szInfo,ARRAYSIZE(n.szInfo),message);
    Shell_NotifyIcon(NIM_MODIFY,&n);
}
void View::setSong(const Song& value, Controls state, UINT_PTR generation) {
    const bool updated=song.title!=value.title || song.artist!=value.artist;
    song=value; controls=state; songGeneration=generation;
    if (updated) {
        std::wstring tip=song.title;
        if (!song.artist.empty()) tip+=L" — "+song.artist;
        StringCchCopyN(tray.szTip,ARRAYSIZE(tray.szTip),tip.c_str(),63);
        Shell_NotifyIcon(NIM_MODIFY,&tray);
        for (const auto& label : labels) if (label.window) {
            SetWindowText(label.window,song.title.c_str()); InvalidateRect(label.window,nullptr,FALSE);
        }
    }
    if (card) {
        const bool enabled[]{state.previous,state.toggle,state.next};
        for (int i=0;i<3;++i) EnableWindow(buttons[i],enabled[i]);
        SetWindowText(buttons[1],state.playing?L"Duraklat":L"Oynat");
        InvalidateRect(card,nullptr,FALSE);
    }
    placeText();
}
void View::setSlots(const std::vector<RECT>& slots) {
    while (labels.size()>std::max(size_t{1},slots.size())) {
        DestroyWindow(labels.back().window); labels.pop_back();
    }
    labels.resize(std::max(size_t{1},slots.size()));
    for (size_t i=0;i<labels.size();++i) {
        if (!labels[i].window) labels[i].window=createLabel();
        labels[i].slot=i<slots.size()?slots[i]:RECT{};
    }
    placeText();
}
void View::placeText() {
    for (const auto& [label,slot] : labels) {
        if (!label) continue;
        if (song.title.empty() || IsRectEmpty(&slot) || fullscreen(slot)) ShowWindow(label,SW_HIDE);
        else SetWindowPos(label,HWND_TOPMOST,slot.left,slot.top,slot.right-slot.left,slot.bottom-slot.top,SWP_NOACTIVATE|SWP_SHOWWINDOW);
    }
}
void View::clear() {
    song={}; songGeneration=0; hideCard();
    for (const auto& label : labels) {
        ShowWindow(label.window,SW_HIDE); SetWindowText(label.window,L"");
    }
    StringCchCopy(tray.szTip,ARRAYSIZE(tray.szTip),L"Music Widget"); Shell_NotifyIcon(NIM_MODIFY,&tray);
}
void View::createCard() {
    card=CreateWindowEx(WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE|WS_EX_TOPMOST|WS_EX_LAYERED,L"MusicWidget.Card",L"Çalan şarkı",
        WS_POPUP|WS_CLIPCHILDREN,0,0,0,0,nullptr,nullptr,GetModuleHandle(nullptr),this);
    int round=2; DwmSetWindowAttribute(card,33,&round,sizeof round);
    const wchar_t* names[]{L"Önceki parça",L"Oynat",L"Sonraki parça"};
    for (int i=0;i<3;++i) {
        auto r=buttonRect(card,i);
        buttons[i]=CreateWindow(L"BUTTON",names[i],WS_CHILD|WS_VISIBLE|BS_OWNERDRAW,
            r.left,r.top,r.right-r.left,r.bottom-r.top,card,reinterpret_cast<HMENU>(INT_PTR(101+i)),GetModuleHandle(nullptr),nullptr);
    }
}
void View::announce() {
    if (!card) { createCard(); setSong(song,controls,songGeneration); }
    RECT work{}; SystemParametersInfo(SPI_GETWORKAREA,0,&work,0);
    int inset=px(card,12),width=px(card,cardWidth),height=px(card,cardHeight);
    SetWindowPos(card,HWND_TOPMOST,work.left+inset,work.top+inset,width,height,SWP_NOACTIVATE);
    for (int i=0;i<3;++i) {
        const auto r=buttonRect(card,i);
        SetWindowPos(buttons[i],nullptr,r.left,r.top,r.right-r.left,r.bottom-r.top,SWP_NOZORDER|SWP_NOACTIVATE);
    }
    renderCard();
    ShowWindow(card,SW_SHOWNOACTIVATE);
    SetTimer(card,1,4000,nullptr);
}
void View::hideCard() {
    if (card) { KillTimer(card,1); ShowWindow(card,SW_HIDE); }
    setCover({});
}
void View::setCover(Cover value) {
    art=std::move(value);
    if (card) InvalidateRect(card,nullptr,FALSE);
}
void View::paintLabel(HWND label,HDC dc) {
    RECT client{}; GetClientRect(label,&client);
    fill(dc,client,key);
    const int size=px(label,20);
    const int top=(client.bottom-size)/2;
    if (DrawIconEx(dc,client.left,top,icon,size,size,0,nullptr,DI_NORMAL))
        client.left+=size+px(label,4);
    text(dc,label,client,song.title,13,textColor);
}
void View::paintCover(HDC dc,RECT r) {
    fill(dc,r,RGB(50,50,56));
    if (art.pixels.empty()) text(dc,card,r,L"♪",36,RGB(192,192,192),DT_CENTER);
    else {
        BITMAPINFO bi{}; bi.bmiHeader.biSize=sizeof(BITMAPINFOHEADER); bi.bmiHeader.biWidth=art.width;
        bi.bmiHeader.biHeight=-art.height; bi.bmiHeader.biPlanes=1; bi.bmiHeader.biBitCount=32;
        int side=r.right-r.left, longest=std::max(art.width,art.height);
        int width=MulDiv(side,art.width,longest),height=MulDiv(side,art.height,longest);
        SetStretchBltMode(dc,HALFTONE); SetBrushOrgEx(dc,0,0,nullptr);
        StretchDIBits(dc,r.left+(side-width)/2,r.top+(side-height)/2,width,height,0,0,art.width,art.height,
            art.pixels.data(),&bi,DIB_RGB_COLORS,SRCCOPY);
    }
}
void View::renderCard() {
    const int width=px(card,cardWidth),height=px(card,cardHeight);
    BITMAPINFO bi{};
    bi.bmiHeader={sizeof(BITMAPINFOHEADER),width,-height,1,32,BI_RGB};
    void* bits=nullptr;
    auto dc=CreateCompatibleDC(nullptr);
    if (!dc) return;
    auto bitmap=CreateDIBSection(dc,&bi,DIB_RGB_COLORS,&bits,nullptr,0);
    if (!bitmap) { DeleteDC(dc); return; }
    auto old=SelectObject(dc,bitmap);
    auto pixels=static_cast<DWORD*>(bits);
    const auto titleRect=box(card,132,14,252,31), artistRect=box(card,132,46,252,25);
    // Draw a grayscale text mask, then blend opaque glyphs over the translucent panel.
    std::fill_n(pixels,width*height,0u);
    text(dc,card,titleRect,song.title,19,RGB(255,255,255),DT_LEFT,FW_SEMIBOLD);
    text(dc,card,artistRect,song.artist,16,RGB(255,255,255));
    GdiFlush();
    for (int y=0;y<height;++y) {
        const auto ink=y<artistRect.top?RGB(246,246,249):RGB(196,196,204);
        for (int x=0;x<width;++x) {
            auto& pixel=pixels[y*width+x];
            const unsigned glyph=pixel&255;
            pixel=glyph?panelPixel(glyph,ink):panelPixel(0,0);
        }
    }
    const auto cover=box(card,16,16,100,100);
    paintCover(dc,cover);
    for (int i=0;i<3;++i) {
        const auto r=buttonRect(card,i);
        const bool pressed=(SendMessage(buttons[i],BM_GETSTATE,0,0)&BST_PUSHED)!=0;
        fill(dc,r,pressed?RGB(75,75,82):RGB(54,54,60));
        const wchar_t* glyph=i==0?L"\uE892":i==2?L"\uE893":controls.playing?L"\uE769":L"\uE768";
        text(dc,card,r,glyph,18,IsWindowEnabled(buttons[i])?RGB(246,246,249):RGB(122,122,128),
            DT_CENTER,FW_NORMAL,L"Segoe MDL2 Assets");
    }
    GdiFlush();
    makeOpaque(pixels,width,cover);
    for (int i=0;i<3;++i) makeOpaque(pixels,width,buttonRect(card,i));
    POINT origin{}; SIZE size{width,height};
    BLENDFUNCTION blend{AC_SRC_OVER,0,255,AC_SRC_ALPHA};
    UpdateLayeredWindow(card,nullptr,nullptr,&size,dc,&origin,0,&blend,ULW_ALPHA);
    SelectObject(dc,old); DeleteObject(bitmap); DeleteDC(dc);
}
LRESULT CALLBACK View::proc(HWND w,UINT msg,WPARAM wp,LPARAM lp) {
    if (msg==WM_NCCREATE) SetWindowLongPtr(w,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(reinterpret_cast<CREATESTRUCT*>(lp)->lpCreateParams));
    auto self=reinterpret_cast<View*>(GetWindowLongPtr(w,GWLP_USERDATA));
    if (self) switch(msg) {
    case WM_MOUSEACTIVATE: return MA_NOACTIVATE;
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps; auto dc=BeginPaint(w,&ps);
        if (w!=self->card) self->paintLabel(w,dc);
        else if (self->cardVisible()) self->renderCard();
        EndPaint(w,&ps); return 0;
    }
    case WM_DRAWITEM: InvalidateRect(w,nullptr,FALSE); return TRUE;
    case WM_COMMAND:
        if (HIWORD(wp)==BN_CLICKED && self->cardVisible() && self->songGeneration)
            PostMessage(self->owner,transportCommand,self->songGeneration,LOWORD(wp));
        return 0;
    case WM_TIMER: if (wp==1) SendMessage(self->owner,closeCard,0,0); return 0;
    case WM_CLOSE: SendMessage(self->owner,closeCard,0,0); return 0;
    }
    return DefWindowProc(w,msg,wp,lp);
}
