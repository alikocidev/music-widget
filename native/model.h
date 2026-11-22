#pragma once
#include <windows.h>
#include <algorithm>
#include <cwctype>
#include <optional>
#include <string>
#include <vector>

struct Song {
    std::wstring source, title, artist, album;
    int track{};
};
struct Controls { bool playing{}, previous{}, toggle{}, next{}; };
enum class Command : unsigned { previous = 101, toggle, next };
inline bool different(const std::wstring& a, const std::wstring& b) {
    return !a.empty() && !b.empty() && a != b;
}
inline bool changed(const std::optional<Song>& old, const Song& now) {
    return !old || old->source != now.source || old->title != now.title ||
        different(old->artist, now.artist) || different(old->album, now.album) ||
        (old->track > 0 && now.track > 0 && old->track != now.track);
}
enum class MusicSource { other, spotify, youtubeMusic, edge };
inline MusicSource classifySource(std::wstring id) {
    std::transform(id.begin(), id.end(), id.begin(), [](wchar_t c) { return wchar_t(towlower(c)); });
    // Installed web apps can have a domain-based package ID or a profile-specific Chromium ID.
    if ((id.starts_with(L"music.youtube.com-") && id.ends_with(L"!app")) ||
        id.find(L"._crx_cinhiknlkffjgod.") != id.npos || id.ends_with(L"._crx_cinhiknlkffjgod"))
        return MusicSource::youtubeMusic;
    std::erase_if(id, [](wchar_t c) { return c == L' ' || c == L'-'; });
    if (id.find(L"spotify") != id.npos) return MusicSource::spotify;
    if (id.find(L"youtubemusic") != id.npos || id.find(L"cinhimbnkkaeohfgghhklpknlkffjgod") != id.npos)
        return MusicSource::youtubeMusic;
    if (id == L"msedge" || id == L"msedge.exe") return MusicSource::edge;
    return MusicSource::other;
}
constexpr UINT layoutDirty = WM_APP + 1, layoutReady = WM_APP + 2, foregroundChanged = WM_APP + 3;
constexpr UINT selectSource = WM_APP + 4, metadataChanged = WM_APP + 5, playbackChanged = WM_APP + 6;
constexpr UINT trayEvent = WM_APP + 7, closeCard = WM_APP + 8;
constexpr UINT transportCommand = WM_APP + 9;
