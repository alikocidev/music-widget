#pragma once
#include "model.h"
#include <mutex>
#include <thread>
#include <condition_variable>

// Explorer UI Automation lives on one MTA worker, separate from the window thread.
class Layout {
    HWND owner;
    std::mutex lock;
    std::condition_variable wake;
    bool requested{}, stopping{}, allScreens{};
    std::vector<RECT> result;
    std::thread worker;
    void run();
public:
    explicit Layout(HWND window) : owner(window) {}
    ~Layout();
    void request(bool includeSecondary);
    std::vector<RECT> latest();
};
