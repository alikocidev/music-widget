#pragma once
#include "model.h"
#include <shellapi.h>

struct Cover { int width{}, height{}; std::vector<uint8_t> pixels; };

class View {
    HWND owner{}, card{}, buttons[3]{};
    struct Label { HWND window{}; RECT slot{}; };
    std::vector<Label> labels;
    NOTIFYICONDATAW tray{sizeof tray};
    HICON icon{};
    Song song;
    Controls controls;
    UINT_PTR songGeneration{};
    Cover art;
    COLORREF textColor{};
    HWND createLabel();
    void createCard();
    void paintLabel(HWND,HDC);
    void renderCard();
    void paintCover(HDC, RECT);
    static LRESULT CALLBACK proc(HWND, UINT, WPARAM, LPARAM);
public:
    explicit View(HWND);
    ~View();
    void restoreTray();
    void theme();
    void setSong(const Song&, Controls, UINT_PTR generation);
    void setSlots(const std::vector<RECT>& slots);
    void placeText();
    void announce();
    void hideCard();
    bool cardVisible() const { return card && IsWindowVisible(card); }
    void setCover(Cover value);
    void clear();
    void warn(const wchar_t* message);
};
