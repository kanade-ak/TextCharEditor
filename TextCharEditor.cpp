#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <commdlg.h>
#include <imm.h>
#include <objidl.h>
#include <gdiplus.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <cwctype>
#include <memory>
#include <string>
#include <vector>

#include "plugin2.h"
#include "filter2.h"
#include "config2.h"

static constexpr LPCWSTR EFFECT_NAME   = L"文字位置調整";
static constexpr LPCWSTR ITEM_DATA     = L"文字データ";
static constexpr LPCWSTR WINDOW_NAME   = L"TextCharEditorWindow";
static constexpr LPCWSTR EXT_WINDOW_NAME = L"TextCharEditorExtendedWindow";
static constexpr LPCWSTR DRAW_WINDOW_NAME = L"TextCharEditorDrawWindow";
static constexpr LPCWSTR FONT_WINDOW_NAME = L"TextCharEditorFontWindow";
static constexpr UINT    WM_APP_REFRESH = WM_APP + 1;
static constexpr UINT    WM_APP_CAPTURE_READY = WM_APP + 2;
static constexpr UINT    WM_APP_SYNC_EXTENDED = WM_APP + 3;
static constexpr UINT_PTR HOST_SYNC_TIMER_ID = 1;
static constexpr UINT HOST_SYNC_INTERVAL_MS = 500;
static constexpr int HOST_CAPTURE_MAX_ATTEMPTS = 3;
static constexpr ULONGLONG HOST_CAPTURE_RETRY_INTERVAL_MS = 1000;
static constexpr int     IDC_RESET = 2001;
static constexpr int     IDC_CANVAS_LABEL = 2002;
static constexpr int     IDC_SOLID_BACKGROUND = 2003;
static constexpr int     IDC_BACKGROUND_COLOR = 2004;
static constexpr int     IDC_EXTENDED = 2005;
static constexpr int     IDC_SHORTCUT_LIST = 2101;
static constexpr int     IDC_HAND_DRAW = 2102;
static constexpr int     IDC_SHORTCUT_RESET = 2103;
static constexpr int     IDC_SHORTCUT_LABEL = 2104;
static constexpr int     IDC_SHORTCUT_DISABLE = 2105;
static constexpr int     IDC_BLOCK_NON_TEXT_UPDATES = 2106;
static constexpr int     IDC_PRESET_EXPORT = 2107;
static constexpr int     IDC_PRESET_IMPORT = 2108;
static constexpr int     IDC_DRAW_PEN = 2201;
static constexpr int     IDC_DRAW_ERASER = 2202;
static constexpr int     IDC_DRAW_COLOR = 2203;
static constexpr int     IDC_DRAW_UNDO = 2204;
static constexpr int     IDC_DRAW_CLEAR = 2205;
static constexpr int     IDC_DRAW_WIDTH = 2206;
static constexpr int     IDC_DRAW_REDO = 2207;
static constexpr int     IDC_DRAW_EXPORT = 2208;
static constexpr int     IDC_DRAW_IMPORT = 2209;
static constexpr int     IDC_DRAW_EYEDROPPER = 2210;
static constexpr int     IDC_DRAW_EXPORT_RANGE = 2211;
static constexpr int     IDM_ALIGN_VERTICAL = 2301;
static constexpr int     IDM_ALIGN_HORIZONTAL = 2302;
static constexpr int     IDM_ALIGN_SIZE = 2303;
static constexpr int     IDM_ALIGN_POSITION = 2304;
static constexpr int     IDM_ALIGN_ALL = 2305;
static constexpr int     IDM_CHARACTER_HAND_DRAW = 2306;
static constexpr int     IDM_CHARACTER_FONT_CHANGE = 2307;
static constexpr int     IDC_FONT_TARGET = 2501;
static constexpr int     IDC_FONT_SEARCH = 2502;
static constexpr int     IDC_FONT_TAB = 2503;
static constexpr int     IDC_FONT_SELECT_LIST = 2504;
static constexpr int     IDC_FONT_MANAGE_LIST = 2505;
static constexpr int     IDC_FONT_HISTORY_LABEL = 2506;
static constexpr int     IDC_FONT_HISTORY_LIST = 2507;
static constexpr int     IDC_FONT_SELECT_ALL = 2508;
static constexpr int     IDC_FONT_SELECT_WIN11 = 2509;
static constexpr int     IDC_FONT_APPLY = 2510;
static constexpr int     IDC_FONT_OBJECT_DEFAULT = 2511;
static constexpr int     IDC_FONT_CLOSE = 2512;
static constexpr int     IDC_FONT_PREVIEW = 2513;
static constexpr int     IDC_FONT_ZOOM_OUT = 2514;
static constexpr int     IDC_FONT_ZOOM_LABEL = 2515;
static constexpr int     IDC_FONT_ZOOM_IN = 2516;
static constexpr int     IDM_FONT_HISTORY_PIN = 2520;

static EDIT_HANDLE*   edit_handle = nullptr;
static HWND           g_host_hwnd = nullptr;
static HWND           g_hwnd      = nullptr;
static HWND           g_extended_hwnd = nullptr;
static HWND           g_draw_hwnd = nullptr;
static HWND           g_font_hwnd = nullptr;

struct CharTransform {
    float x  = 0.f;    // X移動量
    float y  = 0.f;    // Y移動量
    float sx = 1.f;    // X拡大率
    float sy = 1.f;    // Y拡大率
    float rz = 0.f;    // 回転角度
    bool  is_default() const { return x == 0 && y == 0 && sx == 1 && sy == 1 && rz == 0; }
};

struct CharBitmap {
    int width = 0;
    int height = 0;
    std::vector<std::uint32_t> pixels;
};

struct StrokePoint {
    float x = 0.f;
    float y = 0.f;
};

struct Stroke {
    bool erase = false;
    COLORREF color = RGB(255, 64, 64);
    float width = 0.04f;
    std::vector<StrokePoint> points;
};

// パース結果
struct CharState {
    CharTransform t;
    std::wstring glyph;
    std::vector<Stroke> strokes;
    std::wstring png_path;
    bool         hasBase = false;
    float        base_cx = 0, base_cy = 0;
    float        base_sx = 1, base_sy = 1;
    float        base_rz = 0;
    float        base_alpha = 1;
    int          image_w = 0, image_h = 0;
    std::shared_ptr<const CharBitmap> bitmap;
};

static std::vector<CharState> g_chars;
static bool g_has_focus    = false;   // テキストオブジェクト選択中
static bool g_individual   = false;   // 個別オブジェクトがON
static bool g_has_effect   = false;   // 「文字位置調整」が追加済み
static int  g_selected     = -1;

static POINT g_drag_start  {};
static CharTransform g_drag_orig;

static ULONGLONG g_last_write_ms  = 0;

struct CharBase {
    bool valid = false;
    std::uint64_t epoch = 0;
    float cx = 0, cy = 0;
    float sx = 1, sy = 1, rz = 0, alpha = 1;
    int width = 0, height = 0;
    std::shared_ptr<const CharBitmap> bitmap;
};

static std::vector<CharBase> g_base;
static SRWLOCK g_base_lock = SRWLOCK_INIT;
static std::atomic<std::uint64_t> g_capture_epoch { 1 };
static std::atomic<OBJECT_HANDLE> g_focus_object { nullptr };
static std::atomic<int> g_focus_layer { -1 };
static std::atomic<bool> g_capture_notify_pending { false };
static std::atomic<bool> g_render_request_pending { false };
static std::atomic<std::uint64_t> g_render_request_epoch { 0 };
static std::atomic<bool> g_refresh_notify_pending { false };
static int g_capture_attempt_count = 0;
static ULONGLONG g_capture_retry_after_ms = 0;
static OBJECT_HANDLE g_ui_object = nullptr;
static int g_ui_frame = -1;
static std::string g_ui_source_signature;
static std::string g_ui_data_signature;
static bool g_ui_individual = false;
static bool g_ui_has_effect = false;
static int g_ui_line_break_count = 0;
static float g_fit_s = 1.f, g_fit_cx = 0.f, g_fit_cy = 0.f;
static float g_win_cx = 0.f,  g_win_cy = 0.f;
static float g_fit_base = 1.f, g_user_zoom = 1.f;
enum class EditMode { NONE, MOVE, ROTATE, RESIZE, PAN };
static EditMode g_mode = EditMode::NONE;
static int g_resize_corner = -1;
static float g_pan_x = 0, g_pan_y = 0;
static ULONG_PTR g_gdiplus_token = 0;
static bool g_solid_background = false;
static COLORREF g_background_color = RGB(128, 128, 128);
static COLORREF g_custom_colors[16] = {};
static COLORREF g_pen_color = RGB(255, 64, 64);
static COLORREF g_pen_custom_colors[16] = {};
static float g_pen_width = 0.04f;
static bool g_pen_eraser = false;
static bool g_hand_drawing = false;
static int g_draw_char_index = -1;
static Gdiplus::RectF g_draw_image_rect = {};
static int g_shortcut_capture = -1;
static UINT g_shortcut_pending_modifier_key = 0;
static bool g_shortcut_transform_dirty = false;
static bool g_shortcuts_disabled = false;
static bool g_block_non_text_updates = true;
static bool g_eyedropper_active = false;
static std::wstring g_settings_path;
enum class HistoryKind { TRANSFORM, HAND };
struct HistoryStore {
    std::vector<std::string> undo;
    std::vector<std::string> redo;
    std::string before;
    bool active = false;
};
static HistoryStore g_transform_history;
static std::vector<HistoryStore> g_hand_histories;

struct PngCacheEntry {
    std::wstring path;
    int width = 0;
    int height = 0;
    std::shared_ptr<const CharBitmap> bitmap;
    ULONGLONG retry_after = 0;
};

static std::vector<PngCacheEntry> g_png_cache;
static SRWLOCK g_png_cache_lock = SRWLOCK_INIT;

enum ShortcutAction {
    SHORTCUT_SELECT_PREVIOUS,
    SHORTCUT_SELECT_NEXT,
    SHORTCUT_MOVE_LEFT,
    SHORTCUT_MOVE_RIGHT,
    SHORTCUT_MOVE_UP,
    SHORTCUT_MOVE_DOWN,
    SHORTCUT_LOCK_HORIZONTAL,
    SHORTCUT_LOCK_VERTICAL,
    SHORTCUT_SCALE_UP,
    SHORTCUT_SCALE_DOWN,
    SHORTCUT_ROTATE_LEFT,
    SHORTCUT_ROTATE_RIGHT,
    SHORTCUT_RESET_CHARACTER,
    SHORTCUT_UNDO,
    SHORTCUT_REDO,
    SHORTCUT_HAND_DRAW,
    SHORTCUT_COUNT
};

static constexpr UINT SHORTCUT_MOD_CTRL = 1;
static constexpr UINT SHORTCUT_MOD_SHIFT = 2;
static constexpr UINT SHORTCUT_MOD_ALT = 4;

struct ShortcutSetting {
    LPCWSTR label;
    LPCWSTR key;
    UINT vk;
    UINT modifiers;
    UINT default_vk;
    UINT default_modifiers;
};

static ShortcutSetting g_shortcuts[SHORTCUT_COUNT] = {
    { L"前の文字を選択", L"SelectPrevious", L'Q', 0, L'Q', 0 },
    { L"次の文字を選択", L"SelectNext", L'E', 0, L'E', 0 },
    { L"左へ移動", L"MoveLeft", VK_LEFT, 0, VK_LEFT, 0 },
    { L"右へ移動", L"MoveRight", VK_RIGHT, 0, VK_RIGHT, 0 },
    { L"上へ移動", L"MoveUp", VK_UP, 0, VK_UP, 0 },
    { L"下へ移動", L"MoveDown", VK_DOWN, 0, VK_DOWN, 0 },
    { L"横方向固定（ドラッグ）", L"LockHorizontal", VK_CONTROL, 0, VK_CONTROL, 0 },
    { L"縦方向固定（ドラッグ）", L"LockVertical", VK_SHIFT, 0, VK_SHIFT, 0 },
    { L"拡大", L"ScaleUp", L'W', 0, L'W', 0 },
    { L"縮小", L"ScaleDown", L'S', 0, L'S', 0 },
    { L"左へ回転", L"RotateLeft", L'A', 0, L'A', 0 },
    { L"右へ回転", L"RotateRight", L'D', 0, L'D', 0 },
    { L"選択文字をリセット", L"ResetCharacter", L'R', 0, L'R', 0 },
    { L"1つ戻す", L"Undo", L'Z', SHORTCUT_MOD_CTRL, L'Z', SHORTCUT_MOD_CTRL },
    { L"1つ進める", L"Redo", L'Y', SHORTCUT_MOD_CTRL, L'Y', SHORTCUT_MOD_CTRL },
    { L"文字装飾(手書き)", L"HandDraw", L'H', 0, L'H', 0 }
};

static void load_settings() {
    if (g_settings_path.empty()) return;
    g_solid_background = GetPrivateProfileIntW(
        L"Canvas", L"SolidBackground", 0, g_settings_path.c_str()) != 0;
    UINT color = GetPrivateProfileIntW(
        L"Canvas", L"BackgroundColor", g_background_color, g_settings_path.c_str());
    if (color <= 0xFFFFFFu) g_background_color = (COLORREF)color;
    g_shortcuts_disabled = GetPrivateProfileIntW(
        L"Shortcuts", L"Disabled", 0, g_settings_path.c_str()) != 0;
    g_block_non_text_updates = GetPrivateProfileIntW(
        L"HostSync", L"BlockNonTextUpdates", 1, g_settings_path.c_str()) != 0;
    for (auto& shortcut : g_shortcuts) {
        const UINT fallback = shortcut.default_vk | (shortcut.default_modifiers << 16);
        const UINT value = GetPrivateProfileIntW(
            L"Shortcuts", shortcut.key, fallback, g_settings_path.c_str());
        const UINT vk = value & 0xFFFFu;
        const UINT modifiers = (value >> 16) & 0x7u;
        if (vk <= 0xFFu) {
            shortcut.vk = vk;
            shortcut.modifiers = modifiers;
        }
    }
}

static void save_canvas_settings() {
    if (g_settings_path.empty()) return;
    WritePrivateProfileStringW(L"Canvas", L"SolidBackground",
                               g_solid_background ? L"1" : L"0",
                               g_settings_path.c_str());
    std::wstring color = std::to_wstring((unsigned)(g_background_color & 0xFFFFFFu));
    WritePrivateProfileStringW(L"Canvas", L"BackgroundColor", color.c_str(),
                               g_settings_path.c_str());
}

static void save_shortcuts() {
    if (g_settings_path.empty()) return;
    WritePrivateProfileStringW(L"Shortcuts", L"Disabled",
                               g_shortcuts_disabled ? L"1" : L"0",
                               g_settings_path.c_str());
    for (const auto& shortcut : g_shortcuts) {
        const UINT value = shortcut.vk | (shortcut.modifiers << 16);
        std::wstring encoded = std::to_wstring(value);
        WritePrivateProfileStringW(L"Shortcuts", shortcut.key, encoded.c_str(),
                                   g_settings_path.c_str());
    }
}

static void save_host_sync_settings() {
    if (g_settings_path.empty()) return;
    WritePrivateProfileStringW(L"HostSync", L"BlockNonTextUpdates",
                               g_block_non_text_updates ? L"1" : L"0",
                               g_settings_path.c_str());
}

static std::wstring utf8_to_wide(LPCSTR u) {
    if (!u || !*u) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, u, -1, nullptr, 0);
    if (n <= 0) return {};
    std::wstring w((size_t)n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, u, -1, w.data(), n);
    w.pop_back();
    return w;
}

static std::string wide_to_utf8(LPCWSTR w) {
    if (!w || !*w) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
    if (n <= 0) return {};
    std::string u((size_t)n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w, -1, u.data(), n, nullptr, nullptr);
    u.pop_back();
    return u;
}

static std::string hex_encode(const std::string& value) {
    static constexpr char digits[] = "0123456789ABCDEF";
    std::string encoded;
    encoded.reserve(value.size() * 2);
    for (unsigned char byte : value) {
        encoded.push_back(digits[byte >> 4]);
        encoded.push_back(digits[byte & 15]);
    }
    return encoded;
}

static int hex_value(wchar_t c) {
    if (c >= L'0' && c <= L'9') return c - L'0';
    if (c >= L'A' && c <= L'F') return c - L'A' + 10;
    if (c >= L'a' && c <= L'f') return c - L'a' + 10;
    return -1;
}

static std::string hex_decode(const wchar_t* begin, const wchar_t* end) {
    std::string decoded;
    if (!begin || !end || end < begin || ((end - begin) & 1)) return decoded;
    decoded.reserve((size_t)(end - begin) / 2);
    while (begin < end) {
        const int high = hex_value(*begin++);
        const int low = hex_value(*begin++);
        if (high < 0 || low < 0) return {};
        decoded.push_back((char)((high << 4) | low));
    }
    return decoded;
}

static std::string serialize_transform_state(const std::vector<CharState>& chars) {
    char buf[128];
    std::string out;
    for (size_t i = 0; i < chars.size(); i++) {
        const auto& c = chars[i];
        if (!c.t.is_default()) {
            snprintf(buf, sizeof(buf), "%d:%.2f,%.2f,%.4f,%.4f,%.2f;",
                     (int)i, c.t.x, c.t.y, c.t.sx, c.t.sy, c.t.rz);
            out += buf;
        }
    }
    return out;
}

static std::string serialize_data(const std::vector<CharState>& chars) {
    char buf[128];
    std::string out = serialize_transform_state(chars);
    bool section = false;
    for (size_t i = 0; i < chars.size(); i++) {
        for (const auto& stroke : chars[i].strokes) {
            if (stroke.points.empty()) continue;
            if (!section) {
                out += '#';
                section = true;
            }
            if (stroke.erase) {
                snprintf(buf, sizeof(buf), "%d:E,%.4f", (int)i, stroke.width);
            } else {
                snprintf(buf, sizeof(buf), "%d:P,%u,%.4f", (int)i,
                         (unsigned)(stroke.color & 0xFFFFFFu), stroke.width);
            }
            out += buf;
            for (const auto& point : stroke.points) {
                snprintf(buf, sizeof(buf), ",%.4f,%.4f", point.x, point.y);
                out += buf;
            }
            out += ';';
        }
    }
    bool png_section = false;
    for (size_t i = 0; i < chars.size(); i++) {
        if (chars[i].png_path.empty()) continue;
        if (!png_section) {
            out += '@';
            png_section = true;
        }
        snprintf(buf, sizeof(buf), "%d:", (int)i);
        out += buf;
        out += hex_encode(wide_to_utf8(chars[i].png_path.c_str()));
        out += ';';
    }
    return out;
}

static std::string serialize_hand_state(int char_index) {
    if (char_index < 0 || char_index >= (int)g_chars.size()) return {};
    std::vector<CharState> state(1);
    state[0].strokes = g_chars[char_index].strokes;
    state[0].png_path = g_chars[char_index].png_path;
    return serialize_data(state);
}
static bool parse_transform(LPCWSTR text, int want_index, CharTransform* out) {
    *out = CharTransform{};
    if (!text || !*text) return false;
    const wchar_t* p = text;
    while (*p && *p != L'#') {
        wchar_t* end = nullptr;
        long idx = wcstol(p, &end, 10);
        if (end == p || *end != L':') break;
        p = end + 1;
        float v[5] = {};
        int count = 0;
        while (count < 5) {
            double dv = wcstod(p, &end);
            if (end == p) break;
            v[count++] = (float)dv;
            p = end;
            if (*p != L',') break;
            p++;
        }
        if (count > 0 && idx == want_index) {
            out->x  = v[0];
            out->y  = count > 1 ? v[1] : 0.f;
            out->sx = (count > 2 && v[2] != 0.f) ? v[2] : 1.f;
            out->sy = (count > 3 && v[3] != 0.f) ? v[3] : 1.f;
            out->rz = count > 4 ? v[4] : 0.f;
            return true;
        }
        while (*p && *p != L';') p++;
        if (*p == L';') p++;
    }
    return false;
}

static void parse_strokes(LPCWSTR text, int want_index, std::vector<Stroke>* out) {
    out->clear();
    if (!text) return;
    const wchar_t* p = wcschr(text, L'#');
    if (!p) return;
    p++;
    while (*p) {
        while (*p == L';' || iswspace(*p)) p++;
        wchar_t* end = nullptr;
        long idx = wcstol(p, &end, 10);
        if (end == p || *end != L':') break;
        p = end + 1;
        const wchar_t mode = *p++;
        Stroke stroke;
        stroke.erase = mode == L'E';
        if ((mode != L'E' && mode != L'P') || *p != L',') {
            while (*p && *p != L';') p++;
            continue;
        }
        p++;
        if (!stroke.erase) {
            unsigned long color = wcstoul(p, &end, 10);
            if (end == p || *end != L',') {
                while (*p && *p != L';') p++;
                continue;
            }
            stroke.color = (COLORREF)(color & 0xFFFFFFu);
            p = end + 1;
        }
        double width = wcstod(p, &end);
        if (end == p) {
            while (*p && *p != L';') p++;
            continue;
        }
        stroke.width = (std::max)(0.002f, (std::min)(0.5f, (float)width));
        p = end;
        while (*p == L',') {
            p++;
            double x = wcstod(p, &end);
            if (end == p || *end != L',') break;
            p = end + 1;
            double y = wcstod(p, &end);
            if (end == p) break;
            stroke.points.push_back({
                (std::max)(0.f, (std::min)(1.f, (float)x)),
                (std::max)(0.f, (std::min)(1.f, (float)y))
            });
            p = end;
        }
        if (idx == want_index && !stroke.points.empty()) out->push_back(std::move(stroke));
        while (*p && *p != L';') p++;
        if (*p == L';') p++;
    }
}

static void parse_png_path(LPCWSTR text, int want_index, std::wstring* out) {
    out->clear();
    if (!text) return;
    const wchar_t* p = wcschr(text, L'@');
    if (!p) return;
    p++;
    while (*p) {
        while (*p == L';' || iswspace(*p)) p++;
        wchar_t* end = nullptr;
        const long index = wcstol(p, &end, 10);
        if (end == p || *end != L':') break;
        p = end + 1;
        const wchar_t* encoded_end = wcschr(p, L';');
        if (!encoded_end) encoded_end = p + wcslen(p);
        if (index == want_index) {
            const std::string decoded = hex_decode(p, encoded_end);
            *out = utf8_to_wide(decoded.c_str());
            return;
        }
        p = *encoded_end ? encoded_end + 1 : encoded_end;
    }
}

static std::wstring strip_control_tags(const std::wstring& src) {
    std::wstring out;
    bool in_tag = false;
    for (size_t i = 0; i < src.size(); i++) {
        const wchar_t c = src[i];
        if (in_tag) {
            if (c == L'>') in_tag = false;
            continue;
        }
        if (c == L'<') { in_tag = true; continue; }
        if (c == L'\\' && i + 1 < src.size()) {
            const wchar_t next = src[i + 1];
            if (next == L'\\') {
                out += L'\\';
                i++;
                continue;
            }
            if (next == L'n') {
                out += L'\n';
                i++;
                continue;
            }
        }
        out += c;
    }
    return out;
}

static void layout_chars();
static void clear_edit_history();
static bool edit_history_active();

static std::string alias_without_item_line(const std::string& alias, const std::string& key) {
    std::string out;
    const std::string needle = key + "=";
    size_t p = 0;
    while (p < alias.size()) {
        size_t e = alias.find('\n', p);
        if (e == std::string::npos) e = alias.size();
        size_t len = e - p;
        if (len && alias[p + len - 1] == '\r') len--;
        if (alias.compare(p, needle.size(), needle) != 0) {
            out.append(alias, p, e - p);
            if (e < alias.size()) out.push_back('\n');
        }
        p = e < alias.size() ? e + 1 : e;
    }
    return out;
}

// オブジェクトエイリアスから指定エフェクトの設定本文だけを取り出す。
// セクション見出しはエフェクトの並び替えで変わるため署名には含めない。
static std::string effect_section_signature(const std::string& alias,
                                            const std::string& effect_name) {
    const std::string needle = "effect.name=" + effect_name;
    size_t section_body = 0;
    bool matched = false;
    size_t line_start = 0;
    while (line_start < alias.size()) {
        size_t line_end = alias.find('\n', line_start);
        if (line_end == std::string::npos) line_end = alias.size();
        size_t line_length = line_end - line_start;
        if (line_length && alias[line_start + line_length - 1] == '\r') line_length--;

        const bool section_header = line_length >= 2 && alias[line_start] == '[' &&
                                    alias[line_start + line_length - 1] == ']';
        if (section_header) {
            if (matched) return alias.substr(section_body, line_start - section_body);
            section_body = line_end < alias.size() ? line_end + 1 : line_end;
            matched = false;
        } else if (line_length == needle.size() &&
                   alias.compare(line_start, line_length, needle) == 0) {
            matched = true;
        }
        line_start = line_end < alias.size() ? line_end + 1 : line_end;
    }
    return matched ? alias.substr(section_body) : std::string{};
}

static bool background_host_work_allowed() {
    if (!g_hwnd || !IsWindow(g_hwnd) || !IsWindowVisible(g_hwnd) ||
        !g_host_hwnd || !IsWindow(g_host_hwnd) || !IsWindowVisible(g_host_hwnd) ||
        !IsWindowEnabled(g_host_hwnd)) {
        return false;
    }
    const HWND popup = GetLastActivePopup(g_host_hwnd);
    return !popup || popup == g_host_hwnd || popup == g_hwnd ||
           popup == g_extended_hwnd || popup == g_draw_hwnd || popup == g_font_hwnd ||
           !IsWindowVisible(popup);
}

static void reset_host_capture_retry() {
    g_capture_attempt_count = 0;
    g_capture_retry_after_ms = 0;
}

static bool host_capture_retry_exhausted() {
    return g_capture_attempt_count >= HOST_CAPTURE_MAX_ATTEMPTS &&
           !g_render_request_pending.load(std::memory_order_acquire);
}

static void reset_host_capture() {
    reset_host_capture_retry();
    g_capture_epoch.fetch_add(1, std::memory_order_acq_rel);
    AcquireSRWLockExclusive(&g_base_lock);
    g_base.clear();
    ReleaseSRWLockExclusive(&g_base_lock);
}

static void post_host_refresh() {
    bool expected = false;
    if (!g_refresh_notify_pending.compare_exchange_strong(
            expected, true, std::memory_order_acq_rel)) return;
    HWND hwnd = g_hwnd;
    if (!hwnd || !PostMessageW(hwnd, WM_APP_REFRESH, 0, 0)) {
        g_refresh_notify_pending.store(false, std::memory_order_release);
    }
}

static bool has_complete_host_capture(int char_count) {
    if (char_count <= 0) return true;
    const std::uint64_t epoch = g_capture_epoch.load(std::memory_order_acquire);
    bool complete = true;
    AcquireSRWLockShared(&g_base_lock);
    if ((int)g_base.size() < char_count) {
        complete = false;
    } else {
        for (int i = 0; i < char_count; i++) {
            const CharBase& b = g_base[i];
            if (!b.valid || b.epoch != epoch || !b.bitmap) {
                complete = false;
                break;
            }
        }
    }
    ReleaseSRWLockShared(&g_base_lock);
    return complete;
}

static void on_capture_rendered(void*, int, const void*, int, int, int) {
    g_render_request_pending.store(false, std::memory_order_release);
    HWND hwnd = g_hwnd;
    if (hwnd) PostMessageW(hwnd, WM_APP_CAPTURE_READY, 0, 0);
}

static void request_host_capture(int frame) {
    if (!background_host_work_allowed() || !edit_handle ||
        edit_handle->get_edit_state() != EDIT_HANDLE::EDIT_STATE_EDIT ||
        frame < 0 || !g_hwnd || !IsWindowVisible(g_hwnd)) return;
    const ULONGLONG now = GetTickCount64();
    if (g_capture_attempt_count >= HOST_CAPTURE_MAX_ATTEMPTS ||
        now < g_capture_retry_after_ms) return;
    bool expected = false;
    if (!g_render_request_pending.compare_exchange_strong(expected, true,
                                                           std::memory_order_acq_rel)) return;
    g_capture_attempt_count++;
    g_capture_retry_after_ms = now + HOST_CAPTURE_RETRY_INTERVAL_MS;
    g_render_request_epoch.store(g_capture_epoch.load(std::memory_order_acquire),
                                 std::memory_order_release);
    if (!edit_handle->rendering_scene_video(frame, nullptr, on_capture_rendered)) {
        g_render_request_epoch.store(0, std::memory_order_release);
        g_render_request_pending.store(false, std::memory_order_release);
    }
}

static void refresh_from_host() {
    if (g_mode != EditMode::NONE || g_hand_drawing ||
        g_shortcut_transform_dirty || edit_history_active()) return;
    // レンダリング要求を再生フレームごとに発生させないことが最優先
    if (!background_host_work_allowed() || !edit_handle ||
        edit_handle->get_edit_state() != EDIT_HANDLE::EDIT_STATE_EDIT) return;

    struct Ctx {
        std::string text_utf8;
        std::string data_utf8;
        std::string individual_raw;
        std::string alias_utf8;
        OBJECT_HANDLE object = nullptr;
        int layer = -1;
        int frame = -1;
        bool has_effect = false;
    } ctx;

    const bool read = edit_handle->call_read_section_param(&ctx, [](void* p, EDIT_SECTION* edit) {
        auto* c = (Ctx*)p;
        OBJECT_HANDLE obj = edit->get_focus_object();
        if (!obj) return;
        c->object = obj;
        OBJECT_LAYER_FRAME lf = edit->get_object_layer_frame(obj);
        c->layer = lf.layer;
        c->frame = edit->info ? edit->info->frame : lf.start;
        if (auto a = edit->get_object_alias(obj)) {
            c->alias_utf8 = a;
        }

        if (auto v = edit->get_object_item_value(obj, L"テキスト", L"テキスト")) {
            c->text_utf8 = v;
            if (auto ind = edit->get_object_item_value(obj, L"テキスト", L"文字毎に個別オブジェクト")) {
                c->individual_raw = ind;
            }
            if (EFFECT_HANDLE effect = edit->find_effect(obj, EFFECT_NAME)) {
                c->has_effect = true;
                if (auto d = edit->get_effect_item_value(effect, ITEM_DATA)) {
                    c->data_utf8 = d;
                }
            }
        }
    });
    if (!read) return;

    const bool has_focus = ctx.object && !ctx.text_utf8.empty();
    const bool individual = ctx.object &&
                            !ctx.individual_raw.empty() && ctx.individual_raw != "0";
    const bool has_effect = ctx.has_effect;

    std::wstring body;
    if (has_focus) body = strip_control_tags(utf8_to_wide(ctx.text_utf8.c_str()));
    int line_break_count = 0;
    for (size_t i = 0; i < body.size(); i++) {
        if (body[i] == L'\n' ||
            (body[i] == L'\r' && (i + 1 >= body.size() || body[i + 1] != L'\n'))) {
            line_break_count++;
        }
    }

    std::string source_signature = g_block_non_text_updates ?
        effect_section_signature(ctx.alias_utf8, "テキスト") :
        alias_without_item_line(ctx.alias_utf8, "文字データ");
    // SDKやエイリアス形式の差でセクションを取得できない場合も、本文変更は検出する。
    if (g_block_non_text_updates && source_signature.empty() && !ctx.text_utf8.empty()) {
        source_signature = "text=" + ctx.text_utf8;
    }
    const bool object_changed = ctx.object != g_ui_object;
    const bool capture_source_changed =
        object_changed || (!g_block_non_text_updates && ctx.frame != g_ui_frame) ||
        source_signature != g_ui_source_signature;
    const bool editor_state_changed =
        object_changed || ctx.data_utf8 != g_ui_data_signature ||
        individual != g_ui_individual || has_effect != g_ui_has_effect ||
        has_focus != g_has_focus;
    const bool line_layout_changed =
        object_changed || line_break_count != g_ui_line_break_count;
    g_focus_object.store(ctx.object, std::memory_order_release);
    g_focus_layer.store(ctx.layer, std::memory_order_release);

    // UPDATE_OBJECTは変更項目を通知しないため、必要な状態に差分がなければ描画しない。
    // キャプチャが途中の場合だけ、次の取得要求を継続する。
    if (!capture_source_changed && !editor_state_changed) {
        if (has_focus && individual && has_effect &&
            !has_complete_host_capture((int)g_chars.size())) {
            request_host_capture(ctx.frame);
        }
        return;
    }

    g_has_focus = has_focus;
    g_individual = individual;
    g_has_effect = has_effect;
    if (capture_source_changed) {
        if (line_layout_changed) {
            g_pan_x = 0.f;
            g_pan_y = 0.f;
            g_user_zoom = 1.f;
        }
        g_ui_object = ctx.object;
        g_ui_frame = ctx.frame;
        g_ui_source_signature = source_signature;
        g_ui_line_break_count = line_break_count;
        clear_edit_history();
        reset_host_capture();
    }
    g_ui_data_signature = ctx.data_utf8;
    g_ui_individual = individual;
    g_ui_has_effect = has_effect;

    std::vector<CharState> next;
    if (g_has_focus) {
        size_t i = 0;
        while (i < body.size()) {
            const size_t start = i;
            wchar_t c = body[i];
            if (c >= 0xD800 && c <= 0xDBFF && i + 1 < body.size() &&
                body[i + 1] >= 0xDC00 && body[i + 1] <= 0xDFFF) {
                i += 2;
            } else {
                i += 1;
                if (c == L'\r' || c == L'\n' || c == L'\t') continue;
            }
            CharState state;
            state.glyph = body.substr(start, i - start);
            next.push_back(std::move(state));
        }
        std::wstring data_w = utf8_to_wide(ctx.data_utf8.c_str());
        for (size_t index = 0; index < next.size(); index++) {
            parse_transform(data_w.c_str(), (int)index, &next[index].t);
            parse_strokes(data_w.c_str(), (int)index, &next[index].strokes);
            parse_png_path(data_w.c_str(), (int)index, &next[index].png_path);
        }
    }
    g_chars = std::move(next);
    if (g_selected >= (int)g_chars.size()) g_selected = -1;
    layout_chars();
    if (g_extended_hwnd) PostMessageW(g_extended_hwnd, WM_APP_SYNC_EXTENDED, 0, 0);
    if (g_draw_hwnd) InvalidateRect(g_draw_hwnd, nullptr, FALSE);

    if (g_has_focus && g_individual && g_has_effect &&
        !has_complete_host_capture((int)g_chars.size())) {
        request_host_capture(ctx.frame);
    }

    InvalidateRect(g_hwnd, nullptr, FALSE);
}

static void write_data_to_object(bool force = false) {
    if (g_draw_hwnd) InvalidateRect(g_draw_hwnd, nullptr, FALSE);
    if (!g_has_focus || !g_has_effect || !edit_handle ||
        edit_handle->get_edit_state() != EDIT_HANDLE::EDIT_STATE_EDIT) return;
    ULONGLONG now = GetTickCount64();
    if (!force && now - g_last_write_ms < 40) return;
    g_last_write_ms = now;

    std::string utf8 = serialize_data(g_chars);
    edit_handle->call_edit_section_param(&utf8, [](void* p, EDIT_SECTION* edit) {
        auto* s = (std::string*)p;
        OBJECT_HANDLE obj = edit->get_focus_object();
        if (!obj) return;
        if (EFFECT_HANDLE effect = edit->find_effect(obj, EFFECT_NAME)) {
            edit->set_effect_item_value(effect, ITEM_DATA, s->c_str());
        }
    });
}

static void push_history(std::vector<std::string>* history, std::string state) {
    if (!history->empty() && history->back() == state) return;
    history->push_back(std::move(state));
    if (history->size() > 64) history->erase(history->begin());
}

static HistoryStore* get_history_store(HistoryKind kind, int char_index, bool create) {
    if (kind == HistoryKind::TRANSFORM) return &g_transform_history;
    if (char_index < 0) return nullptr;
    const size_t index = (size_t)char_index;
    if (index >= g_hand_histories.size()) {
        if (!create) return nullptr;
        g_hand_histories.resize(index + 1);
    }
    return &g_hand_histories[index];
}

static std::string capture_history_state(HistoryKind kind, int char_index) {
    return kind == HistoryKind::TRANSFORM ?
           serialize_transform_state(g_chars) : serialize_hand_state(char_index);
}

static void clear_edit_history() {
    g_transform_history = HistoryStore{};
    g_hand_histories.clear();
}

static bool edit_history_active() {
    if (g_transform_history.active) return true;
    for (const auto& history : g_hand_histories) {
        if (history.active) return true;
    }
    return false;
}

static void begin_edit_history(HistoryKind kind = HistoryKind::TRANSFORM,
                               int char_index = -1) {
    HistoryStore* history = get_history_store(kind, char_index, true);
    if (!history || history->active) return;
    history->before = capture_history_state(kind, char_index);
    history->active = true;
}

static void commit_edit_history(HistoryKind kind = HistoryKind::TRANSFORM,
                                int char_index = -1) {
    HistoryStore* history = get_history_store(kind, char_index, false);
    if (!history || !history->active) return;
    const std::string current = capture_history_state(kind, char_index);
    if (current != history->before) {
        push_history(&history->undo, std::move(history->before));
        history->redo.clear();
    }
    history->before.clear();
    history->active = false;
}

static void apply_history_state(const std::string& data, HistoryKind kind,
                                int char_index) {
    const std::wstring wide = utf8_to_wide(data.c_str());
    if (kind == HistoryKind::TRANSFORM) {
        for (size_t i = 0; i < g_chars.size(); i++) {
            parse_transform(wide.c_str(), (int)i, &g_chars[i].t);
        }
    } else if (char_index >= 0 && char_index < (int)g_chars.size()) {
        parse_strokes(wide.c_str(), 0, &g_chars[char_index].strokes);
        parse_png_path(wide.c_str(), 0, &g_chars[char_index].png_path);
    }
    write_data_to_object(true);
    if (g_hwnd) InvalidateRect(g_hwnd, nullptr, FALSE);
    if (g_draw_hwnd) InvalidateRect(g_draw_hwnd, nullptr, FALSE);
}

static bool undo_edit(HistoryKind kind = HistoryKind::TRANSFORM,
                      int char_index = -1) {
    commit_edit_history(kind, char_index);
    HistoryStore* history = get_history_store(kind, char_index, false);
    if (!history || history->undo.empty()) return false;
    std::string state = std::move(history->undo.back());
    history->undo.pop_back();
    push_history(&history->redo, capture_history_state(kind, char_index));
    apply_history_state(state, kind, char_index);
    return true;
}

static bool redo_edit(HistoryKind kind = HistoryKind::TRANSFORM,
                      int char_index = -1) {
    commit_edit_history(kind, char_index);
    HistoryStore* history = get_history_store(kind, char_index, false);
    if (!history || history->redo.empty()) return false;
    std::string state = std::move(history->redo.back());
    history->redo.pop_back();
    push_history(&history->undo, capture_history_state(kind, char_index));
    apply_history_state(state, kind, char_index);
    return true;
}

static bool undo_hand_edit() {
    return undo_edit(HistoryKind::HAND, g_draw_char_index);
}

static bool redo_hand_edit() {
    return redo_edit(HistoryKind::HAND, g_draw_char_index);
}

//=====================================================================
// フィルタ効果「文字位置調整」
//=====================================================================
static FILTER_ITEM_TEXT item_data = { ITEM_DATA, L"" };
static FILTER_ITEM_BUTTON item_reset = { L"すべてリセット", nullptr };

static void notify_capture_ready() {
    bool expected = false;
    if (!g_capture_notify_pending.compare_exchange_strong(expected, true,
                                                           std::memory_order_acq_rel)) return;
    HWND hwnd = g_hwnd;
    if (!hwnd || !PostMessageW(hwnd, WM_APP_CAPTURE_READY, 0, 0)) {
        g_capture_notify_pending.store(false, std::memory_order_release);
    }
}

static std::shared_ptr<const CharBitmap> capture_host_bitmap(FILTER_PROC_VIDEO* video) {
    auto bitmap = std::make_shared<CharBitmap>();
    bitmap->width = video->object->width;
    bitmap->height = video->object->height;
    if (bitmap->width <= 0 || bitmap->height <= 0) return bitmap;

    const size_t width = (size_t)bitmap->width;
    const size_t height = (size_t)bitmap->height;
    if (width > 16384 || height > 16384 || width > SIZE_MAX / height ||
        width * height > 64u * 1024u * 1024u) return bitmap;

    const size_t count = width * height;
    std::vector<PIXEL_RGBA> rgba(count);
    video->get_image_data(rgba.data());
    bitmap->pixels.resize(count);
    for (size_t i = 0; i < count; i++) {
        const unsigned a = rgba[i].a;
        const unsigned r = (rgba[i].r * a + 127) / 255;
        const unsigned g = (rgba[i].g * a + 127) / 255;
        const unsigned b = (rgba[i].b * a + 127) / 255;
        bitmap->pixels[i] = (a << 24) | (r << 16) | (g << 8) | b;
    }
    return bitmap;
}

static std::shared_ptr<const CharBitmap> load_png_bitmap(const std::wstring& path,
                                                         bool reload = false) {
    if (path.empty()) return {};
    if (!reload) {
        const ULONGLONG now = GetTickCount64();
        AcquireSRWLockShared(&g_png_cache_lock);
        for (const auto& entry : g_png_cache) {
            if (_wcsicmp(entry.path.c_str(), path.c_str()) == 0) {
                if (entry.bitmap || now < entry.retry_after) {
                    auto bitmap = entry.bitmap;
                    ReleaseSRWLockShared(&g_png_cache_lock);
                    return bitmap;
                }
                break;
            }
        }
        ReleaseSRWLockShared(&g_png_cache_lock);
    }

    std::shared_ptr<CharBitmap> decoded;
    Gdiplus::Bitmap source(path.c_str());
    if (source.GetLastStatus() == Gdiplus::Ok && source.GetWidth() > 0 && source.GetHeight() > 0) {
        const int width = (int)source.GetWidth();
        const int height = (int)source.GetHeight();
        const bool valid_size = width <= 16384 && height <= 16384 &&
            (size_t)width <= SIZE_MAX / (size_t)height &&
            (size_t)width * (size_t)height <= 64u * 1024u * 1024u;
        if (!valid_size) return {};
        Gdiplus::BitmapData data = {};
        Gdiplus::Rect rect(0, 0, width, height);
        if (source.LockBits(&rect, Gdiplus::ImageLockModeRead,
                            PixelFormat32bppPARGB, &data) == Gdiplus::Ok) {
            decoded = std::make_shared<CharBitmap>();
            decoded->width = width;
            decoded->height = height;
            decoded->pixels.resize((size_t)width * (size_t)height);
            for (int y = 0; y < height; y++) {
                const BYTE* row = (const BYTE*)data.Scan0 + (ptrdiff_t)y * data.Stride;
                memcpy(decoded->pixels.data() + (size_t)y * (size_t)width,
                        row, (size_t)width * sizeof(std::uint32_t));
            }
            source.UnlockBits(&data);
        }
    }

    AcquireSRWLockExclusive(&g_png_cache_lock);
    auto existing = std::find_if(g_png_cache.begin(), g_png_cache.end(),
        [&](const PngCacheEntry& entry) {
            return _wcsicmp(entry.path.c_str(), path.c_str()) == 0;
        });
    if (decoded || existing == g_png_cache.end() || !existing->bitmap) {
        if (existing != g_png_cache.end()) g_png_cache.erase(existing);
        if (g_png_cache.size() >= 32) g_png_cache.erase(g_png_cache.begin());
        g_png_cache.push_back({ path, decoded ? decoded->width : 0,
                                decoded ? decoded->height : 0, decoded,
                                decoded ? 0ull : GetTickCount64() + 1000ull });
    }
    ReleaseSRWLockExclusive(&g_png_cache_lock);
    return decoded;
}

template<class PixelWriter>
static void rasterize_stroke(int width, int height, const Stroke& stroke, PixelWriter write_pixel) {
    if (width <= 0 || height <= 0 || stroke.points.empty()) return;
    const float radius = (std::max)(0.75f, stroke.width * (float)(std::min)(width, height) * 0.5f);
    const float radius2 = radius * radius;
    auto stamp = [&](float x, float y) {
        const int left = (std::max)(0, (int)floorf(x - radius));
        const int top = (std::max)(0, (int)floorf(y - radius));
        const int right = (std::min)(width - 1, (int)ceilf(x + radius));
        const int bottom = (std::min)(height - 1, (int)ceilf(y + radius));
        for (int py = top; py <= bottom; py++) {
            for (int px = left; px <= right; px++) {
                const float dx = (float)px - x;
                const float dy = (float)py - y;
                if (dx * dx + dy * dy <= radius2) write_pixel(px, py);
            }
        }
    };
    float x0 = stroke.points[0].x * (float)(width - 1);
    float y0 = stroke.points[0].y * (float)(height - 1);
    stamp(x0, y0);
    for (size_t i = 1; i < stroke.points.size(); i++) {
        const float x1 = stroke.points[i].x * (float)(width - 1);
        const float y1 = stroke.points[i].y * (float)(height - 1);
        const int steps = (std::max)(1, (int)ceilf((std::max)(fabsf(x1 - x0), fabsf(y1 - y0))));
        for (int step = 1; step <= steps; step++) {
            const float f = (float)step / (float)steps;
            stamp(x0 + (x1 - x0) * f, y0 + (y1 - y0) * f);
        }
        x0 = x1;
        y0 = y1;
    }
}

static void apply_strokes_rgba(std::vector<PIXEL_RGBA>* pixels, int width, int height,
                               const std::vector<Stroke>& strokes) {
    for (const auto& stroke : strokes) {
        const PIXEL_RGBA color = {
            GetRValue(stroke.color), GetGValue(stroke.color), GetBValue(stroke.color), 255
        };
        rasterize_stroke(width, height, stroke, [&](int x, int y) {
            PIXEL_RGBA& pixel = (*pixels)[(size_t)y * (size_t)width + (size_t)x];
            pixel = stroke.erase ? PIXEL_RGBA{} : color;
        });
    }
}

static std::shared_ptr<const CharBitmap> character_source_bitmap(const CharState& state) {
    if (!state.png_path.empty()) {
        if (std::shared_ptr<const CharBitmap> imported = load_png_bitmap(state.png_path)) {
            return imported;
        }
    }
    return state.bitmap;
}

static CharBitmap decorated_character_bitmap(const CharState& state) {
    std::shared_ptr<const CharBitmap> source = character_source_bitmap(state);
    if (!source || source->width <= 0 || source->height <= 0 || source->pixels.empty()) return {};
    CharBitmap result = *source;
    for (const auto& stroke : state.strokes) {
        const std::uint32_t color = 0xFF000000u |
            ((std::uint32_t)GetRValue(stroke.color) << 16) |
            ((std::uint32_t)GetGValue(stroke.color) << 8) |
            (std::uint32_t)GetBValue(stroke.color);
        rasterize_stroke(result.width, result.height, stroke, [&](int x, int y) {
            result.pixels[(size_t)y * (size_t)result.width + (size_t)x] =
                stroke.erase ? 0u : color;
        });
    }
    return result;
}

static bool character_visual_size(const CharState& state, int* width, int* height) {
    std::shared_ptr<const CharBitmap> source = character_source_bitmap(state);
    if (!source || source->width <= 0 || source->height <= 0) return false;
    if (width) *width = source->width;
    if (height) *height = source->height;
    return true;
}

// 設定テキストから現在の文字(index)の変形を取得して適用
static bool proc_video(FILTER_PROC_VIDEO* video) {
    bool capture_target = false;
    if (g_render_request_pending.load(std::memory_order_acquire) &&
        video->object && video->param && g_hwnd && IsWindowVisible(g_hwnd)) {
        OBJECT_HANDLE focus = g_focus_object.load(std::memory_order_acquire);
        capture_target = focus && video->edit && video->edit->get_focus_object() == focus &&
                         video->object->layer == g_focus_layer.load(std::memory_order_acquire);
    }
    const std::uint64_t capture_epoch = g_capture_epoch.load(std::memory_order_acquire);
    if (capture_target && video->object->index >= 0 &&
        g_render_request_epoch.load(std::memory_order_acquire) == capture_epoch) {
        const int idx = video->object->index;
        const std::uint64_t epoch = capture_epoch;
        bool need_bitmap = false;
        bool layout_changed = false;

        AcquireSRWLockExclusive(&g_base_lock);
        if (idx >= (int)g_base.size()) g_base.resize(idx + 1);
        CharBase& base = g_base[idx];
        if (base.epoch != epoch) {
            base = CharBase{};
            base.epoch = epoch;
            layout_changed = true;
        }
        const float cx = video->param->x + video->param->cx;
        const float cy = video->param->y + video->param->cy;
        layout_changed = layout_changed || !base.valid || base.cx != cx || base.cy != cy ||
                         base.sx != video->param->sx || base.sy != video->param->sy ||
                         base.rz != video->param->rz || base.alpha != video->param->alpha ||
                         base.width != video->object->width || base.height != video->object->height;
        base.valid = true;
        base.cx = cx;
        base.cy = cy;
        base.sx = video->param->sx;
        base.sy = video->param->sy;
        base.rz = video->param->rz;
        base.alpha = video->param->alpha;
        base.width = video->object->width;
        base.height = video->object->height;
        need_bitmap = !base.bitmap || base.bitmap->width != base.width ||
                      base.bitmap->height != base.height;
        ReleaseSRWLockExclusive(&g_base_lock);

        if (need_bitmap) {
            std::shared_ptr<const CharBitmap> bitmap = capture_host_bitmap(video);
            AcquireSRWLockExclusive(&g_base_lock);
            if (idx < (int)g_base.size()) {
                CharBase& dst = g_base[idx];
                if (dst.valid && dst.epoch == epoch &&
                    epoch == g_capture_epoch.load(std::memory_order_acquire)) {
                    dst.bitmap = std::move(bitmap);
                    layout_changed = true;
                }
            }
            ReleaseSRWLockExclusive(&g_base_lock);
        }
        if (layout_changed) notify_capture_ready();
    }
    if (!video->object || !video->param || !item_data.value || !*item_data.value) return true;

    CharTransform t;
    parse_transform(item_data.value, video->object->index, &t);
    std::vector<Stroke> strokes;
    parse_strokes(item_data.value, video->object->index, &strokes);
    std::wstring png_path;
    parse_png_path(item_data.value, video->object->index, &png_path);
    std::shared_ptr<const CharBitmap> imported = load_png_bitmap(png_path);
    if ((imported || !strokes.empty()) &&
        video->object->width > 0 && video->object->height > 0) {
        const size_t width = imported ? (size_t)imported->width :
                                        (size_t)video->object->width;
        const size_t height = imported ? (size_t)imported->height :
                                         (size_t)video->object->height;
        if (width <= 16384 && height <= 16384 && width <= SIZE_MAX / height &&
            width * height <= 64u * 1024u * 1024u) {
            std::vector<PIXEL_RGBA> pixels(width * height);
            if (imported && imported->pixels.size() == pixels.size()) {
                for (size_t i = 0; i < pixels.size(); i++) {
                    const std::uint32_t value = imported->pixels[i];
                    const unsigned alpha = value >> 24;
                    if (alpha) {
                        const unsigned red = (std::min)(255u,
                            (((value >> 16) & 255u) * 255u + alpha / 2u) / alpha);
                        const unsigned green = (std::min)(255u,
                            (((value >> 8) & 255u) * 255u + alpha / 2u) / alpha);
                        const unsigned blue = (std::min)(255u,
                            ((value & 255u) * 255u + alpha / 2u) / alpha);
                        pixels[i] = { (BYTE)red, (BYTE)green, (BYTE)blue, (BYTE)alpha };
                    } else {
                        pixels[i] = {};
                    }
                }
            } else {
                video->get_image_data(pixels.data());
            }
            apply_strokes_rgba(&pixels, (int)width, (int)height, strokes);
            video->set_image_data(pixels.data(), (int)width, (int)height);
        }
    }

    OBJECT_IMAGE_PARAM* p = video->param;
    p->x  += t.x;
    p->y  += t.y;
    p->rz += t.rz;
    p->sx *= t.sx;
    p->sy *= t.sy;
    return true;
}

static void on_reset_button(EDIT_SECTION* edit) {
    OBJECT_HANDLE obj = edit->get_focus_object();
    if (!obj) return;
    if (EFFECT_HANDLE effect = edit->find_effect(obj, EFFECT_NAME)) {
        edit->set_effect_item_value(effect, ITEM_DATA, "");
    }
}

static void* filter_items[] = { &item_data, &item_reset, nullptr };

static FILTER_PLUGIN_TABLE filter_table = {
    FILTER_PLUGIN_TABLE::FLAG_VIDEO,     // flag
    EFFECT_NAME,                         // name
    L"文字位置調整", // label
    L"文字位置調整 ver3.0 (TextCharEditor)", // information
    filter_items,                        // items
    proc_video,                          // func_proc_video
    nullptr,                             // func_proc_audio
    nullptr,                             // func_create
    nullptr,                             // func_destroy
};


// UI実装は共有状態を非公開のまま保つため、同一翻訳単位へ分割して読み込む。

#include "TextCharEditorWindow.inl"
#include "TextCharEditorFont.inl"
#include "TextCharEditorDraw.inl"
#include "TextCharEditorPreset.inl"
#include "TextCharEditorExtended.inl"

//---------------------------------------------------------------------
// イベント (イベント通知スレッドから呼ばれる為、UIへ通知するだけ)
//---------------------------------------------------------------------
static void on_host_event(void*) {
    post_host_refresh();
}

//---------------------------------------------------------------------
// プラグイン登録
//---------------------------------------------------------------------
EXTERN_C __declspec(dllexport) void RegisterPlugin(HOST_APP_TABLE* host) {
    item_reset.callback = on_reset_button;
    host->register_filter_plugin(&filter_table);
    INITCOMMONCONTROLSEX controls = {
        sizeof(controls),
        ICC_STANDARD_CLASSES | ICC_BAR_CLASSES | ICC_LISTVIEW_CLASSES | ICC_TAB_CLASSES
    };
    InitCommonControlsEx(&controls);
    WNDCLASSEXW wcex = {};
    wcex.cbSize = sizeof(WNDCLASSEX);
    wcex.style = CS_DBLCLKS;
    wcex.lpszClassName = WINDOW_NAME;
    wcex.lpfnWndProc = wnd_proc;
    wcex.hInstance = GetModuleHandleW(nullptr);
    wcex.hbrBackground = nullptr;
    wcex.hCursor = LoadCursor(nullptr, IDC_ARROW);
    if (!RegisterClassExW(&wcex)) return;
    wcex.style = 0;
    wcex.lpszClassName = EXT_WINDOW_NAME;
    wcex.lpfnWndProc = extended_wnd_proc;
    wcex.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    if (!RegisterClassExW(&wcex)) return;
    wcex.lpszClassName = DRAW_WINDOW_NAME;
    wcex.lpfnWndProc = draw_wnd_proc;
    wcex.hbrBackground = nullptr;
    if (!RegisterClassExW(&wcex)) return;
    wcex.lpszClassName = FONT_WINDOW_NAME;
    wcex.lpfnWndProc = font_wnd_proc;
    wcex.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    if (!RegisterClassExW(&wcex)) return;

    g_hwnd = CreateWindowExW(0, WINDOW_NAME, L"文字エディタ",
                             WS_POPUP | WS_CLIPCHILDREN,
                             CW_USEDEFAULT, CW_USEDEFAULT, 640, 360,
                             nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (!g_hwnd) return;

    host->register_window_client(WINDOW_NAME, g_hwnd);
    edit_handle = host->create_edit_handle();
    if (edit_handle) g_host_hwnd = edit_handle->get_host_app_window();
    update_host_sync_timer(g_hwnd, IsWindowVisible(g_hwnd) != FALSE);
    host->register_event_listener(EVENT_TYPE::CHANGE_FOCUS_OBJECT, nullptr, on_host_event);
    host->register_event_listener(EVENT_TYPE::UPDATE_OBJECT, nullptr, on_host_event);
}

EXTERN_C __declspec(dllexport) void InitializeConfig(CONFIG_HANDLE* config) {
    if (!config || !config->app_data_path || !*config->app_data_path) return;
    g_settings_path = config->app_data_path;
    wchar_t tail = g_settings_path.back();
    if (tail != L'\\' && tail != L'/') g_settings_path += L'\\';
    g_settings_path += L"TextCharEditor.ini";
    load_settings();
}
