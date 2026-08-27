#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <commdlg.h>
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
static constexpr UINT    WM_APP_REFRESH = WM_APP + 1;
static constexpr UINT    WM_APP_CAPTURE_READY = WM_APP + 2;
static constexpr UINT    WM_APP_SYNC_EXTENDED = WM_APP + 3;
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
static constexpr int     IDM_ALIGN_VERTICAL = 2301;
static constexpr int     IDM_ALIGN_HORIZONTAL = 2302;
static constexpr int     IDM_ALIGN_SIZE = 2303;
static constexpr int     IDM_ALIGN_POSITION = 2304;
static constexpr int     IDM_ALIGN_ALL = 2305;

static EDIT_HANDLE*   edit_handle = nullptr;
static HWND           g_hwnd      = nullptr;
static HWND           g_extended_hwnd = nullptr;
static HWND           g_draw_hwnd = nullptr;

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
static OBJECT_HANDLE g_ui_object = nullptr;
static int g_ui_frame = -1;
static std::string g_ui_source_signature;
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
    for (wchar_t c : src) {
        if (in_tag) {
            if (c == L'>') in_tag = false;
            continue;
        }
        if (c == L'<') { in_tag = true; continue; }
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

static void reset_host_capture() {
    g_capture_epoch.fetch_add(1, std::memory_order_acq_rel);
    AcquireSRWLockExclusive(&g_base_lock);
    g_base.clear();
    ReleaseSRWLockExclusive(&g_base_lock);
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
    if (!edit_handle || edit_handle->get_edit_state() != EDIT_HANDLE::EDIT_STATE_EDIT ||
        frame < 0 || !g_hwnd || !IsWindowVisible(g_hwnd)) return;
    bool expected = false;
    if (!g_render_request_pending.compare_exchange_strong(expected, true,
                                                           std::memory_order_acq_rel)) return;
    if (!edit_handle->rendering_scene_video(frame, nullptr, on_capture_rendered)) {
        g_render_request_pending.store(false, std::memory_order_release);
    }
}

static void refresh_from_host() {
    if (g_mode != EditMode::NONE || g_hand_drawing ||
        g_shortcut_transform_dirty || edit_history_active()) return;
    // レンダリング要求を再生フレームごとに発生させないことが最優先
    if (!edit_handle || edit_handle->get_edit_state() != EDIT_HANDLE::EDIT_STATE_EDIT) return;

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

    edit_handle->call_read_section_param(&ctx, [](void* p, EDIT_SECTION* edit) {
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

    g_has_focus  = ctx.object && !ctx.text_utf8.empty();
    g_individual = ctx.object &&
                   !ctx.individual_raw.empty() && ctx.individual_raw != "0";
    g_has_effect = ctx.has_effect;

    const std::string source_signature = alias_without_item_line(ctx.alias_utf8, "文字データ");
    const bool capture_source_changed =
        ctx.object != g_ui_object || ctx.frame != g_ui_frame ||
        source_signature != g_ui_source_signature;
    g_focus_object.store(ctx.object, std::memory_order_release);
    g_focus_layer.store(ctx.layer, std::memory_order_release);
    if (capture_source_changed) {
        g_ui_object = ctx.object;
        g_ui_frame = ctx.frame;
        g_ui_source_signature = source_signature;
        clear_edit_history();
        reset_host_capture();
    }

    std::vector<CharState> next;
    if (g_has_focus) {
        std::wstring body = strip_control_tags(utf8_to_wide(ctx.text_utf8.c_str()));
        size_t i = 0;
        while (i < body.size()) {
            const size_t start = i;
            wchar_t c = body[i];
            if (c >= 0xD800 && c <= 0xDBFF && i + 1 < body.size() &&
                body[i + 1] >= 0xDC00 && body[i + 1] <= 0xDFFF) {
                i += 2;
            } else {
                i += 1;
                if (c == L'\r' || c == L'\n') continue;
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
                                                         int width, int height,
                                                         bool reload = false) {
    if (path.empty() || width <= 0 || height <= 0 || width > 16384 || height > 16384 ||
        (size_t)width > SIZE_MAX / (size_t)height ||
        (size_t)width * (size_t)height > 64u * 1024u * 1024u) return {};
    if (!reload) {
        const ULONGLONG now = GetTickCount64();
        AcquireSRWLockShared(&g_png_cache_lock);
        for (const auto& entry : g_png_cache) {
            if (entry.width == width && entry.height == height &&
                _wcsicmp(entry.path.c_str(), path.c_str()) == 0) {
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
        Gdiplus::Bitmap target(width, height, PixelFormat32bppPARGB);
        if (target.GetLastStatus() == Gdiplus::Ok) {
            Gdiplus::Graphics graphics(&target);
            graphics.SetCompositingMode(Gdiplus::CompositingModeSourceCopy);
            graphics.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
            graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
            if (graphics.Clear(Gdiplus::Color(0, 0, 0, 0)) == Gdiplus::Ok &&
                graphics.DrawImage(&source, Gdiplus::Rect(0, 0, width, height)) == Gdiplus::Ok) {
                Gdiplus::BitmapData data = {};
                Gdiplus::Rect rect(0, 0, width, height);
                if (target.LockBits(&rect, Gdiplus::ImageLockModeRead,
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
                    target.UnlockBits(&data);
                }
            }
        }
    }

    AcquireSRWLockExclusive(&g_png_cache_lock);
    auto existing = std::find_if(g_png_cache.begin(), g_png_cache.end(),
        [&](const PngCacheEntry& entry) {
            return entry.width == width && entry.height == height &&
                   _wcsicmp(entry.path.c_str(), path.c_str()) == 0;
        });
    if (decoded || existing == g_png_cache.end() || !existing->bitmap) {
        if (existing != g_png_cache.end()) g_png_cache.erase(existing);
        if (g_png_cache.size() >= 32) g_png_cache.erase(g_png_cache.begin());
        g_png_cache.push_back({ path, width, height, decoded,
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

static std::vector<std::uint32_t> decorated_pixels(const CharState& state) {
    if (!state.bitmap) return {};
    std::shared_ptr<const CharBitmap> imported = load_png_bitmap(
        state.png_path, state.bitmap->width, state.bitmap->height);
    std::vector<std::uint32_t> pixels = imported ? imported->pixels : state.bitmap->pixels;
    for (const auto& stroke : state.strokes) {
        const std::uint32_t color = 0xFF000000u |
            ((std::uint32_t)GetRValue(stroke.color) << 16) |
            ((std::uint32_t)GetGValue(stroke.color) << 8) |
            (std::uint32_t)GetBValue(stroke.color);
        rasterize_stroke(state.bitmap->width, state.bitmap->height, stroke, [&](int x, int y) {
            pixels[(size_t)y * (size_t)state.bitmap->width + (size_t)x] =
                stroke.erase ? 0u : color;
        });
    }
    return pixels;
}

// 設定テキストから現在の文字(index)の変形を取得して適用
static bool proc_video(FILTER_PROC_VIDEO* video) {
    bool capture_target = false;
    if (g_render_request_pending.load(std::memory_order_acquire) &&
        edit_handle && edit_handle->get_edit_state() == EDIT_HANDLE::EDIT_STATE_EDIT &&
        video->object && video->param && g_hwnd && IsWindowVisible(g_hwnd)) {
        OBJECT_HANDLE focus = g_focus_object.load(std::memory_order_acquire);
        capture_target = focus && video->edit && video->edit->get_focus_object() == focus &&
                         video->object->layer == g_focus_layer.load(std::memory_order_acquire);
    }
    if (capture_target && video->object->index >= 0) {
        const int idx = video->object->index;
        const std::uint64_t epoch = g_capture_epoch.load(std::memory_order_acquire);
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
    std::shared_ptr<const CharBitmap> imported = load_png_bitmap(
        png_path, video->object->width, video->object->height);
    if ((imported || !strokes.empty()) &&
        video->object->width > 0 && video->object->height > 0) {
        const size_t width = (size_t)video->object->width;
        const size_t height = (size_t)video->object->height;
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
    L"文字位置調整 ver1.0 (TextCharEditor)", // information
    filter_items,                        // items
    proc_video,                          // func_proc_video
    nullptr,                             // func_proc_audio
    nullptr,                             // func_create
    nullptr,                             // func_destroy
};

//=====================================================================
// 独自ウィンドウ UI
//=====================================================================
static HFONT g_font_ui    = nullptr;

struct Pt { float x, y; };
static float dist2(Pt a, Pt b) { float dx = a.x - b.x, dy = a.y - b.y; return dx*dx + dy*dy; }
static Pt obj_to_screen(float ox, float oy) {
    return { (ox - g_fit_cx) * g_fit_s + g_win_cx + g_pan_x, (oy - g_fit_cy) * g_fit_s + g_win_cy + g_pan_y };
}
static void screen_to_obj(float sx, float sy, float* ox, float* oy) {
    *ox = (sx - g_win_cx - g_pan_x) / g_fit_s + g_fit_cx;
    *oy = (sy - g_win_cy - g_pan_y) / g_fit_s + g_fit_cy;
}
static void rotate_pt(Pt& p, Pt c, float rz) {
    float rad = rz * 3.14159265358979f / 180.f, co = cosf(rad), si = sinf(rad);
    float dx = p.x - c.x, dy = p.y - c.y;
    p.x = c.x + dx*co - dy*si;
    p.y = c.y + dx*si + dy*co;
}
static float char_rotation(const CharState& cs) {
    return cs.base_rz + cs.t.rz;
}
// 選択文字の画面情報(中心/半幅)を取得
static void char_screen(const CharState& cs, Pt* center, float* hw, float* hh) {
    Pt c = obj_to_screen(cs.base_cx + cs.t.x, cs.base_cy + cs.t.y);
    *center = c;
    *hw = fabsf((float)cs.image_w * cs.base_sx * cs.t.sx * g_fit_s) / 2.f;
    *hh = fabsf((float)cs.image_h * cs.base_sy * cs.t.sy * g_fit_s) / 2.f;
}
// 4隅 + 回転ノブの画面位置
static void get_handles(const CharState& cs, Pt corners[4], Pt* knob) {
    Pt c; float hw, hh; char_screen(cs, &c, &hw, &hh);
    Pt local[4] = { {-hw,-hh},{hw,-hh},{hw,hh},{-hw,hh} };
    const float rz = char_rotation(cs);
    for (int k = 0; k < 4; k++) {
        corners[k] = { local[k].x + c.x, local[k].y + c.y };
        rotate_pt(corners[k], c, rz);
    }
    *knob = { c.x, c.y - hh - 30.f };
    rotate_pt(*knob, c, rz);
}

static void draw_host_char(Gdiplus::Graphics& graphics, const CharState& cs) {
    if (!cs.hasBase || !cs.bitmap || cs.bitmap->width <= 0 || cs.bitmap->height <= 0 ||
        cs.bitmap->pixels.empty()) return;

    Pt center; float hw, hh; char_screen(cs, &center, &hw, &hh);
    Pt tl = { center.x - hw, center.y - hh };
    Pt tr = { center.x + hw, center.y - hh };
    Pt bl = { center.x - hw, center.y + hh };
    const float rz = char_rotation(cs);
    rotate_pt(tl, center, rz);
    rotate_pt(tr, center, rz);
    rotate_pt(bl, center, rz);

    std::vector<std::uint32_t> decorated;
    const std::uint32_t* pixels = cs.bitmap->pixels.data();
    if (!cs.strokes.empty()) {
        decorated = decorated_pixels(cs);
        pixels = decorated.data();
    }
    Gdiplus::Bitmap image(cs.bitmap->width, cs.bitmap->height, cs.bitmap->width * 4,
                          PixelFormat32bppPARGB,
                          reinterpret_cast<BYTE*>(const_cast<std::uint32_t*>(pixels)));
    if (image.GetLastStatus() != Gdiplus::Ok) return;

    Gdiplus::PointF dst[3] = {
        { tl.x, tl.y }, { tr.x, tr.y }, { bl.x, bl.y }
    };
    Gdiplus::ImageAttributes attributes;
    const float alpha = (std::max)(0.f, (std::min)(1.f, cs.base_alpha));
    Gdiplus::ColorMatrix color_matrix = {
        1.f, 0.f, 0.f, 0.f, 0.f,
        0.f, 1.f, 0.f, 0.f, 0.f,
        0.f, 0.f, 1.f, 0.f, 0.f,
        0.f, 0.f, 0.f, alpha, 0.f,
        0.f, 0.f, 0.f, 0.f, 1.f
    };
    attributes.SetColorMatrix(&color_matrix, Gdiplus::ColorMatrixFlagsDefault,
                              Gdiplus::ColorAdjustTypeBitmap);
    graphics.DrawImage(&image, dst, 3, 0.f, 0.f,
                       (float)cs.bitmap->width, (float)cs.bitmap->height,
                       Gdiplus::UnitPixel, &attributes);
}

static void draw_selection_handles(HDC dc, const CharState& cs) {
    Pt corners[4], knob; get_handles(cs, corners, &knob);
    HPEN pen = CreatePen(PS_DASH, 1, RGB(0, 120, 215));
    HGDIOBJ old = SelectObject(dc, pen);
    MoveToEx(dc, (int)corners[0].x, (int)corners[0].y, nullptr);
    for (int k = 1; k < 4; k++) LineTo(dc, (int)corners[k].x, (int)corners[k].y);
    LineTo(dc, (int)corners[0].x, (int)corners[0].y);
    Pt tm = { (corners[0].x + corners[1].x) / 2.f, (corners[0].y + corners[1].y) / 2.f };
    MoveToEx(dc, (int)tm.x, (int)tm.y, nullptr);
    LineTo(dc, (int)knob.x, (int)knob.y);
    SelectObject(dc, old); DeleteObject(pen);

    HBRUSH hb = CreateSolidBrush(RGB(0, 120, 215));
    old = SelectObject(dc, hb);
    for (int k = 0; k < 4; k++)
        Rectangle(dc, (int)corners[k].x - 4, (int)corners[k].y - 4, (int)corners[k].x + 4, (int)corners[k].y + 4);
    Ellipse(dc, (int)knob.x - 5, (int)knob.y - 5, (int)knob.x + 5, (int)knob.y + 5);
    SelectObject(dc, old); DeleteObject(hb);
}

// 選択文字のハンドル(回転ノブ=1 / 4隅=2..5)に当たっているか
static bool hit_handle(int mx, int my, int* which) {
    if (g_selected < 0) return false;
    const CharState& cs = g_chars[g_selected];
    Pt corners[4], knob; get_handles(cs, corners, &knob);
    if (dist2({ (float)mx, (float)my }, knob) <= 8 * 8) { *which = 1; return true; }
    for (int k = 0; k < 4; k++)
        if (dist2({ (float)mx, (float)my }, corners[k]) <= 8 * 8) { *which = 2 + k; return true; }
    return false;
}
// いずれかの文字本体(回転ボックス内)に当たっているか
static bool hit_body(int mx, int my, int* idx) {
    for (int i = (int)g_chars.size() - 1; i >= 0; i--) {
        const CharState& cs = g_chars[i];
        if (!cs.hasBase) continue;
        Pt c; float hw, hh; char_screen(cs, &c, &hw, &hh);
        float rad = -char_rotation(cs) * 3.14159265358979f / 180.f, co = cosf(rad), si = sinf(rad);
        float dx = (float)mx - c.x, dy = (float)my - c.y;
        float lx = dx*co - dy*si, ly = dx*si + dy*co;
        if (fabsf(lx) <= hw + 2 && fabsf(ly) <= hh + 2) { *idx = i; return true; }
    }
    return false;
}

static void layout_chars() {
    if (!g_hwnd) return;
    RECT rc; GetClientRect(g_hwnd, &rc);
    int top = 26, bottom = rc.bottom - 40;
    const std::uint64_t epoch = g_capture_epoch.load(std::memory_order_acquire);
    AcquireSRWLockShared(&g_base_lock);
    for (size_t i = 0; i < g_chars.size(); i++) {
        auto& cs = g_chars[i];
        if (i < g_base.size()) {
            const CharBase& b = g_base[i];
            if (b.valid && b.epoch == epoch) {
                cs.hasBase = true;
                cs.base_cx = b.cx; cs.base_cy = b.cy;
                cs.base_sx = b.sx; cs.base_sy = b.sy;
                cs.base_rz = b.rz; cs.base_alpha = b.alpha;
                cs.image_w = b.width; cs.image_h = b.height;
                cs.bitmap = b.bitmap;
                continue;
            }
        }
        cs.hasBase = false;
        cs.bitmap.reset();
    }
    ReleaseSRWLockShared(&g_base_lock);

    float minx = 1e9, miny = 1e9, maxx = -1e9, maxy = -1e9; bool any = false;
    for (size_t i = 0; i < g_chars.size(); i++) {
        auto& cs = g_chars[i];
        float bx = cs.hasBase ? cs.base_cx : (float)(200 + i * 120);
        float by = cs.hasBase ? cs.base_cy : 400.f;
        if (!any) { minx = maxx = bx; miny = maxy = by; any = true; }
        else { if (bx < minx) minx = bx; if (bx > maxx) maxx = bx; if (by < miny) miny = by; if (by > maxy) maxy = by; }
    }
    if (!any) {
        g_fit_s = 1.f;
        g_fit_cx = 0.f;
        g_fit_cy = 0.f;
        g_win_cx = static_cast<float>(rc.right) / 2.f;
        g_win_cy = static_cast<float>(top + bottom) / 2.f;
        return;
    }

    float avg = 0; int pairs = 0;
    for (size_t i = 1; i < g_chars.size(); i++) {
        if (g_chars[i].hasBase && g_chars[i-1].hasBase) {
            float d = fabsf(g_chars[i].base_cx - g_chars[i-1].base_cx);
            if (d > 0.5f) { avg += d; pairs++; }
        }
    }
    if (pairs > 0) avg /= pairs;
    float single_extent = 0.f;
    for (const auto& cs : g_chars) {
        if (cs.hasBase) {
            single_extent = (std::max)(single_extent,
                (std::max)(fabsf(cs.image_w * cs.base_sx), fabsf(cs.image_h * cs.base_sy)));
        }
    }
    float sc = (avg > 1.f) ? (56.0f / avg) :
               (single_extent > 1.f ? 80.0f / single_extent : 0.3f);
    if (!(sc > 0)) sc = 0.3f;
    if (sc > 3.f)  sc = 3.f;
    g_fit_base = sc;
    g_fit_s    = sc * g_user_zoom;
    g_fit_cx = (minx + maxx) / 2; g_fit_cy = (miny + maxy) / 2;
    g_win_cx = static_cast<float>(rc.right) / 2.f;
    g_win_cy = static_cast<float>(top + bottom) / 2.f;
}

static void relayout(HWND hwnd) {
    RECT rc;
    GetClientRect(hwnd, &rc);

    HWND hReset = GetDlgItem(hwnd, IDC_RESET);
    HWND hCanvasLabel = GetDlgItem(hwnd, IDC_CANVAS_LABEL);
    HWND hSolid = GetDlgItem(hwnd, IDC_SOLID_BACKGROUND);
    HWND hColor = GetDlgItem(hwnd, IDC_BACKGROUND_COLOR);
    HWND hExtended = GetDlgItem(hwnd, IDC_EXTENDED);
    int h = 28, m = 8;
    int y = rc.bottom - h - m;
    if (hReset) MoveWindow(hReset, m, y, 100, h, TRUE);
    if (hCanvasLabel) MoveWindow(hCanvasLabel, m + 108, y + 5, 84, 18, TRUE);
    if (hSolid) MoveWindow(hSolid, m + 192, y, 120, h, TRUE);
    if (hColor) MoveWindow(hColor, m + 320, y, 72, h, TRUE);
    if (hExtended) MoveWindow(hExtended, m + 400, y, 88, h, TRUE);
    layout_chars();
}

static void paint_window(HWND hwnd) {
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(hwnd, &ps);
    RECT rc;
    GetClientRect(hwnd, &rc);

    // 背景バッファ
    HDC mem = CreateCompatibleDC(dc);
    HBITMAP bmp = CreateCompatibleBitmap(dc, rc.right, rc.bottom);
    HGDIOBJ oldbmp = SelectObject(mem, bmp);

    FillRect(mem, &rc, (HBRUSH)(COLOR_BTNFACE + 1));
    RECT canvas = { 0, 26, rc.right, (std::max)(26L, rc.bottom - 40L) };
    if (g_solid_background) {
        HBRUSH background = CreateSolidBrush(g_background_color);
        FillRect(mem, &canvas, background);
        DeleteObject(background);
    } else {
        HBRUSH checker[2] = {
            CreateSolidBrush(RGB(238, 238, 238)), CreateSolidBrush(RGB(210, 210, 210))
        };
        constexpr LONG checker_size = 16;
        for (LONG y = canvas.top; y < canvas.bottom; y += checker_size) {
            for (LONG x = canvas.left; x < canvas.right; x += checker_size) {
                RECT tile = { x, y, (std::min)(x + checker_size, canvas.right),
                              (std::min)(y + checker_size, canvas.bottom) };
                FillRect(mem, &tile, checker[((x / checker_size) + (y / checker_size)) & 1]);
            }
        }
        DeleteObject(checker[0]);
        DeleteObject(checker[1]);
    }

    wchar_t status[256] = L"テキストオブジェクトを選択してください";
    COLORREF band = RGB(240, 240, 240);
    if (g_has_focus) {
        if (!g_individual) {
            swprintf(status, 256, L"テキストの設定で「個別オブジェクト」をONにしてください");
            band = RGB(255, 238, 190);
        } else if (!g_has_effect) {
            swprintf(status, 256, L"このオブジェクトにフィルタ効果「%s」を追加してください", EFFECT_NAME);
            band = RGB(255, 238, 190);
        } else if (!has_complete_host_capture((int)g_chars.size())) {
            swprintf(status, 256, L"AviUtl2から文字の実描画を取得しています...");
            band = RGB(235, 235, 235);
        } else {
            swprintf(status, 256, L"ドラッグ=移動 / 横・縦固定キーは「キー設定」で変更 / 角=拡大縮小 / 丸印=回転 / 右クリック=整列 / ダブルクリック=変形リセット");
            band = RGB(222, 240, 255);
        }
    }
    RECT band_rc = { 0, 0, rc.right, 26 };
    HBRUSH br = CreateSolidBrush(band);
    FillRect(mem, &band_rc, br);
    DeleteObject(br);
    HGDIOBJ oldfont = SelectObject(mem, g_font_ui);
    SetBkMode(mem, TRANSPARENT);
    SetTextColor(mem, RGB(60, 60, 60));
    TextOutW(mem, 6, 6, status, (int)wcslen(status));
    SelectObject(mem, oldfont);

    // AviUtl2自身が生成した文字画像を実寸で配置する
    if (g_has_focus && g_individual && g_has_effect && !g_chars.empty()) {
        {
            Gdiplus::Graphics graphics(mem);
            graphics.SetCompositingMode(Gdiplus::CompositingModeSourceOver);
            graphics.SetCompositingQuality(Gdiplus::CompositingQualityHighQuality);
            graphics.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBilinear);
            graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
            for (const auto& state : g_chars) draw_host_char(graphics, state);
        }
        if (g_selected >= 0 && g_selected < (int)g_chars.size() &&
            g_chars[g_selected].hasBase) {
            draw_selection_handles(mem, g_chars[g_selected]);
        }
    }

    BitBlt(dc, 0, 0, rc.right, rc.bottom, mem, 0, 0, SRCCOPY);
    SelectObject(mem, oldbmp);
    DeleteObject(bmp);
    DeleteDC(mem);
    EndPaint(hwnd, &ps);
}

static void reset_char(int index) {
    if (index < 0 || index >= (int)g_chars.size()) return;
    begin_edit_history();
    g_chars[index].t = CharTransform{};
    write_data_to_object(true);
    commit_edit_history();
    InvalidateRect(g_hwnd, nullptr, FALSE);
}

static float limited_scale(float value) {
    if (!std::isfinite(value)) return 1.f;
    const float sign = value < 0.f ? -1.f : 1.f;
    return sign * (std::max)(0.05f, (std::min)(10.f, fabsf(value)));
}

static void align_character(int command, int target_index, int reference_index) {
    if (target_index < 0 || reference_index < 0 ||
        target_index >= (int)g_chars.size() || reference_index >= (int)g_chars.size() ||
        target_index == reference_index) return;
    CharState& target = g_chars[target_index];
    const CharState& reference = g_chars[reference_index];
    if (!target.hasBase || !reference.hasBase) return;
    begin_edit_history();
    const bool position = command == IDM_ALIGN_POSITION || command == IDM_ALIGN_ALL;
    const bool shape = command == IDM_ALIGN_SIZE || command == IDM_ALIGN_ALL;
    if (command == IDM_ALIGN_VERTICAL || position) {
        target.t.y = reference.base_cy + reference.t.y - target.base_cy;
    }
    if (command == IDM_ALIGN_HORIZONTAL || position) {
        target.t.x = reference.base_cx + reference.t.x - target.base_cx;
    }
    if (shape) {
        const float target_w = (float)target.image_w * target.base_sx;
        const float target_h = (float)target.image_h * target.base_sy;
        const float reference_w = (float)reference.image_w * reference.base_sx * reference.t.sx;
        const float reference_h = (float)reference.image_h * reference.base_sy * reference.t.sy;
        if (fabsf(target_w) > 0.0001f) target.t.sx = limited_scale(reference_w / target_w);
        if (fabsf(target_h) > 0.0001f) target.t.sy = limited_scale(reference_h / target_h);
        target.t.rz = reference.base_rz + reference.t.rz - target.base_rz;
    }
    write_data_to_object(true);
    commit_edit_history();
    InvalidateRect(g_hwnd, nullptr, FALSE);
}

static void show_alignment_menu(HWND hwnd, int reference_index, POINT screen) {
    if (g_selected < 0 || reference_index < 0 || g_selected == reference_index) return;
    HMENU menu = CreatePopupMenu();
    if (!menu) return;
    AppendMenuW(menu, MF_STRING, IDM_ALIGN_VERTICAL, L"縦を合わせる");
    AppendMenuW(menu, MF_STRING, IDM_ALIGN_HORIZONTAL, L"横を合わせる");
    AppendMenuW(menu, MF_STRING, IDM_ALIGN_SIZE, L"大きさ(形)を合わせる");
    AppendMenuW(menu, MF_STRING, IDM_ALIGN_POSITION, L"位置だけ合わせる");
    AppendMenuW(menu, MF_STRING, IDM_ALIGN_ALL, L"全て合わせる");
    const int command = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON,
                                       screen.x, screen.y, 0, hwnd, nullptr);
    DestroyMenu(menu);
    if (command) align_character(command, g_selected, reference_index);
}

static bool choose_color(HWND hwnd, COLORREF* color, COLORREF* custom_colors) {
    CHOOSECOLORW chooser = {};
    chooser.lStructSize = sizeof(chooser);
    chooser.hwndOwner = hwnd;
    chooser.rgbResult = *color;
    chooser.lpCustColors = custom_colors;
    chooser.Flags = CC_FULLOPEN | CC_RGBINIT;
    if (!ChooseColorW(&chooser)) return false;
    *color = chooser.rgbResult;
    return true;
}

static bool choose_background_color(HWND hwnd) {
    return choose_color(hwnd, &g_background_color, g_custom_colors);
}

static void show_hand_editor(HWND owner);
static void show_extended_editor(HWND owner);

static UINT current_shortcut_modifiers() {
    UINT modifiers = 0;
    if (GetKeyState(VK_CONTROL) & 0x8000) modifiers |= SHORTCUT_MOD_CTRL;
    if (GetKeyState(VK_SHIFT) & 0x8000) modifiers |= SHORTCUT_MOD_SHIFT;
    if (GetKeyState(VK_MENU) & 0x8000) modifiers |= SHORTCUT_MOD_ALT;
    return modifiers;
}

static bool is_modifier_key(UINT vk) {
    return vk == VK_CONTROL || vk == VK_LCONTROL || vk == VK_RCONTROL ||
           vk == VK_SHIFT || vk == VK_LSHIFT || vk == VK_RSHIFT ||
           vk == VK_MENU || vk == VK_LMENU || vk == VK_RMENU ||
           vk == VK_LWIN || vk == VK_RWIN;
}

static bool is_drag_lock_action(int action) {
    return action == SHORTCUT_LOCK_HORIZONTAL || action == SHORTCUT_LOCK_VERTICAL;
}

static bool shortcut_held(int action) {
    if (action < 0 || action >= SHORTCUT_COUNT) return false;
    const auto& shortcut = g_shortcuts[action];
    if (!shortcut.vk || !(GetKeyState((int)shortcut.vk) & 0x8000)) return false;
    return (current_shortcut_modifiers() & shortcut.modifiers) == shortcut.modifiers;
}

static std::wstring shortcut_key_name(UINT vk, UINT modifiers) {
    if (!vk) return L"未設定";
    std::wstring name;
    if (modifiers & SHORTCUT_MOD_CTRL) name += L"Ctrl+";
    if (modifiers & SHORTCUT_MOD_SHIFT) name += L"Shift+";
    if (modifiers & SHORTCUT_MOD_ALT) name += L"Alt+";
    wchar_t key[64] = {};
    if ((vk >= L'A' && vk <= L'Z') || (vk >= L'0' && vk <= L'9')) {
        key[0] = (wchar_t)vk;
    } else {
        UINT scan = MapVirtualKeyW(vk, MAPVK_VK_TO_VSC);
        LONG key_data = (LONG)(scan << 16);
        if (vk == VK_LEFT || vk == VK_RIGHT || vk == VK_UP || vk == VK_DOWN ||
            vk == VK_INSERT || vk == VK_DELETE || vk == VK_HOME || vk == VK_END ||
            vk == VK_PRIOR || vk == VK_NEXT || vk == VK_DIVIDE || vk == VK_NUMLOCK) {
            key_data |= 1 << 24;
        }
        if (!GetKeyNameTextW(key_data, key, (int)std::size(key))) {
            swprintf_s(key, L"VK_%02X", vk);
        }
    }
    name += key;
    return name;
}

static void sync_shortcut_window(HWND hwnd) {
    HWND button = GetDlgItem(hwnd, IDC_HAND_DRAW);
    const BOOL enabled = g_selected >= 0 && g_selected < (int)g_chars.size();
    if (button && IsWindowEnabled(button) != enabled) EnableWindow(button, enabled);
}

static void refresh_shortcut_list(HWND hwnd) {
    HWND list = GetDlgItem(hwnd, IDC_SHORTCUT_LIST);
    if (!list) return;
    int selected = g_shortcut_capture >= 0 ? g_shortcut_capture :
                   (int)SendMessageW(list, LB_GETCURSEL, 0, 0);
    SendMessageW(list, WM_SETREDRAW, FALSE, 0);
    SendMessageW(list, LB_RESETCONTENT, 0, 0);
    for (int i = 0; i < SHORTCUT_COUNT; i++) {
        std::wstring row = g_shortcuts[i].label;
        row += L" : ";
        row += shortcut_key_name(g_shortcuts[i].vk, g_shortcuts[i].modifiers);
        SendMessageW(list, LB_ADDSTRING, 0, (LPARAM)row.c_str());
    }
    if (selected < 0 || selected >= SHORTCUT_COUNT) selected = 0;
    SendMessageW(list, LB_SETCURSEL, selected, 0);
    SendMessageW(list, WM_SETREDRAW, TRUE, 0);
    RedrawWindow(list, nullptr, nullptr, RDW_INVALIDATE);
    SetWindowTextW(GetDlgItem(hwnd, IDC_SHORTCUT_LABEL),
                   g_shortcut_capture >= 0 ?
                   L"割り当てるキーを押してください（Escで中止・Backspaceで解除）" :
                   L"項目をダブルクリックしてキーを入力\r\n方向固定・Ctrl+ホイール・戻す/進めるは常に有効です");
    sync_shortcut_window(hwnd);
}

static void set_shortcut(int action, UINT vk, UINT modifiers) {
    if (action < 0 || action >= SHORTCUT_COUNT) return;
    if (vk) {
        for (int i = 0; i < SHORTCUT_COUNT; i++) {
            if (i != action && g_shortcuts[i].vk == vk &&
                g_shortcuts[i].modifiers == modifiers) {
                g_shortcuts[i].vk = 0;
                g_shortcuts[i].modifiers = 0;
            }
        }
    }
    g_shortcuts[action].vk = vk;
    g_shortcuts[action].modifiers = modifiers;
    save_shortcuts();
}

static void reset_shortcuts() {
    for (auto& shortcut : g_shortcuts) {
        shortcut.vk = shortcut.default_vk;
        shortcut.modifiers = shortcut.default_modifiers;
    }
    save_shortcuts();
}

static bool execute_shortcut(HWND owner, UINT vk) {
    const UINT modifiers = current_shortcut_modifiers();
    int action = -1;
    for (int i = 0; i < SHORTCUT_COUNT; i++) {
        if (!is_drag_lock_action(i) && g_shortcuts[i].vk == vk &&
            g_shortcuts[i].modifiers == modifiers) {
            action = i;
            break;
        }
    }
    if (action < 0) return false;
    if (g_shortcuts_disabled && action != SHORTCUT_UNDO && action != SHORTCUT_REDO) {
        return false;
    }
    if (g_mode != EditMode::NONE || g_hand_drawing) return true;
    if (action == SHORTCUT_SELECT_PREVIOUS || action == SHORTCUT_SELECT_NEXT) {
        if (g_chars.empty()) {
            g_selected = -1;
        } else if (action == SHORTCUT_SELECT_PREVIOUS) {
            g_selected = g_selected <= 0 ? (int)g_chars.size() - 1 : g_selected - 1;
        } else {
            g_selected = g_selected < 0 || g_selected + 1 >= (int)g_chars.size() ?
                         0 : g_selected + 1;
        }
        if (g_extended_hwnd) PostMessageW(g_extended_hwnd, WM_APP_SYNC_EXTENDED, 0, 0);
        InvalidateRect(owner, nullptr, FALSE);
        return true;
    }
    if (action == SHORTCUT_HAND_DRAW) {
        show_hand_editor(owner);
        return true;
    }
    if (action == SHORTCUT_UNDO) {
        undo_edit();
        return true;
    }
    if (action == SHORTCUT_REDO) {
        redo_edit();
        return true;
    }
    if (g_selected < 0 || g_selected >= (int)g_chars.size()) return true;
    CharTransform& transform = g_chars[g_selected].t;
    if (!g_shortcut_transform_dirty) begin_edit_history();
    switch (action) {
        case SHORTCUT_MOVE_LEFT: transform.x -= 1.f; break;
        case SHORTCUT_MOVE_RIGHT: transform.x += 1.f; break;
        case SHORTCUT_MOVE_UP: transform.y -= 1.f; break;
        case SHORTCUT_MOVE_DOWN: transform.y += 1.f; break;
        case SHORTCUT_SCALE_UP:
            transform.sx = limited_scale(transform.sx * 1.05f);
            transform.sy = limited_scale(transform.sy * 1.05f);
            break;
        case SHORTCUT_SCALE_DOWN:
            transform.sx = limited_scale(transform.sx / 1.05f);
            transform.sy = limited_scale(transform.sy / 1.05f);
            break;
        case SHORTCUT_ROTATE_LEFT: transform.rz -= 1.f; break;
        case SHORTCUT_ROTATE_RIGHT: transform.rz += 1.f; break;
        case SHORTCUT_RESET_CHARACTER:
            transform = CharTransform{};
            write_data_to_object(true);
            g_shortcut_transform_dirty = false;
            commit_edit_history();
            InvalidateRect(owner, nullptr, FALSE);
            return true;
    }
    g_shortcut_transform_dirty = true;
    write_data_to_object();
    InvalidateRect(owner, nullptr, FALSE);
    return true;
}

static LRESULT CALLBACK wnd_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_CREATE: {
            Gdiplus::GdiplusStartupInput startup_input;
            Gdiplus::GdiplusStartup(&g_gdiplus_token, &startup_input, nullptr);
            g_font_ui = CreateFontW(13, 0, 0, 0, FW_NORMAL, 0, 0, 0,
                                    DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                                    CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                    DEFAULT_PITCH, L"Meiryo UI");
            CreateWindowExW(0, WC_BUTTONW, L"全リセット",
                            WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
                            8, 8, 100, 28, hwnd, (HMENU)(INT_PTR)IDC_RESET,
                            GetModuleHandleW(nullptr), nullptr);
            CreateWindowExW(0, WC_STATICW, L"キャンバス設定",
                            WS_VISIBLE | WS_CHILD | SS_LEFT,
                            116, 13, 84, 18, hwnd, (HMENU)(INT_PTR)IDC_CANVAS_LABEL,
                            GetModuleHandleW(nullptr), nullptr);
            CreateWindowExW(0, WC_BUTTONW, L"背景を単色化",
                            WS_VISIBLE | WS_CHILD | BS_AUTOCHECKBOX,
                            200, 8, 120, 28, hwnd, (HMENU)(INT_PTR)IDC_SOLID_BACKGROUND,
                            GetModuleHandleW(nullptr), nullptr);
            CreateWindowExW(0, WC_BUTTONW, L"色設定",
                            WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
                            328, 8, 72, 28, hwnd, (HMENU)(INT_PTR)IDC_BACKGROUND_COLOR,
                            GetModuleHandleW(nullptr), nullptr);
            CreateWindowExW(0, WC_BUTTONW, L"キー設定",
                            WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
                            408, 8, 88, 28, hwnd, (HMENU)(INT_PTR)IDC_EXTENDED,
                            GetModuleHandleW(nullptr), nullptr);
            SendMessageW(GetDlgItem(hwnd, IDC_RESET), WM_SETFONT, (WPARAM)g_font_ui, TRUE);
            SendMessageW(GetDlgItem(hwnd, IDC_CANVAS_LABEL), WM_SETFONT, (WPARAM)g_font_ui, TRUE);
            SendMessageW(GetDlgItem(hwnd, IDC_SOLID_BACKGROUND), WM_SETFONT, (WPARAM)g_font_ui, TRUE);
            SendMessageW(GetDlgItem(hwnd, IDC_BACKGROUND_COLOR), WM_SETFONT, (WPARAM)g_font_ui, TRUE);
            SendMessageW(GetDlgItem(hwnd, IDC_EXTENDED), WM_SETFONT, (WPARAM)g_font_ui, TRUE);
            CheckDlgButton(hwnd, IDC_SOLID_BACKGROUND,
                           g_solid_background ? BST_CHECKED : BST_UNCHECKED);
            SetTimer(hwnd, 1, 500, nullptr); // 状態ポーリング (イベントが飛ない操作へのフォールバック)
            return 0;
        }
        case WM_SIZE:
            relayout(hwnd);
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;

        case WM_SHOWWINDOW:
            if (wp) {
                layout_chars();
                PostMessageW(hwnd, WM_APP_REFRESH, 0, 0);
            }
            return 0;

        case WM_APP_REFRESH:
            refresh_from_host();
            return 0;

        case WM_APP_CAPTURE_READY:
            g_capture_notify_pending.store(false, std::memory_order_release);
            layout_chars();
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;

        case WM_TIMER:
            // ドラッグ中でなければ定期再読み込み
            if (g_mode == EditMode::NONE && edit_handle &&
                edit_handle->get_edit_state() == EDIT_HANDLE::EDIT_STATE_EDIT) {
                PostMessageW(hwnd, WM_APP_REFRESH, 0, 0);
            }
            return 0;

        case WM_PAINT:
            paint_window(hwnd);
            return 0;

        case WM_ERASEBKGND:
            return 1;

        case WM_GETDLGCODE:
            return DLGC_WANTARROWS | DLGC_WANTCHARS;

        case WM_KEYDOWN:
        case WM_SYSKEYDOWN:
            if (execute_shortcut(hwnd, (UINT)wp)) return 0;
            break;

        case WM_KEYUP:
        case WM_SYSKEYUP:
            if (g_shortcut_transform_dirty) {
                g_shortcut_transform_dirty = false;
                write_data_to_object(true);
                commit_edit_history();
            }
            break;

        case WM_KILLFOCUS:
            if (g_shortcut_transform_dirty) {
                g_shortcut_transform_dirty = false;
                write_data_to_object(true);
                commit_edit_history();
            }
            break;

        case WM_COMMAND:
            switch (LOWORD(wp)) {
                case IDC_RESET:
                    begin_edit_history();
                    for (size_t i = 0; i < g_chars.size(); i++) {
                        begin_edit_history(HistoryKind::HAND, (int)i);
                    }
                    for (size_t i = 0; i < g_chars.size(); i++) {
                        g_chars[i].t = CharTransform{};
                        g_chars[i].strokes.clear();
                        g_chars[i].png_path.clear();
                    }
                    write_data_to_object(true);
                    commit_edit_history();
                    for (size_t i = 0; i < g_chars.size(); i++) {
                        commit_edit_history(HistoryKind::HAND, (int)i);
                    }
                    if (g_extended_hwnd) PostMessageW(g_extended_hwnd, WM_APP_SYNC_EXTENDED, 0, 0);
                    if (g_draw_hwnd) InvalidateRect(g_draw_hwnd, nullptr, FALSE);
                    InvalidateRect(hwnd, nullptr, FALSE);
                    break;
                case IDC_SOLID_BACKGROUND:
                    if (HIWORD(wp) == BN_CLICKED) {
                        g_solid_background =
                            IsDlgButtonChecked(hwnd, IDC_SOLID_BACKGROUND) == BST_CHECKED;
                        save_canvas_settings();
                        InvalidateRect(hwnd, nullptr, FALSE);
                    }
                    break;
                case IDC_BACKGROUND_COLOR:
                    if (HIWORD(wp) == BN_CLICKED && choose_background_color(hwnd)) {
                        save_canvas_settings();
                        InvalidateRect(hwnd, nullptr, FALSE);
                    }
                    break;
                case IDC_EXTENDED:
                    if (HIWORD(wp) == BN_CLICKED) show_extended_editor(hwnd);
                    break;
            }
            return 0;

        case WM_LBUTTONDOWN: {
            SetFocus(hwnd);
            int mx = GET_X_LPARAM(lp), my = GET_Y_LPARAM(lp);
            int which = 0, bidx = -1;
            bool onHandle = hit_handle(mx, my, &which);
            bool onBody   = hit_body(mx, my, &bidx);
            if (onHandle && g_selected >= 0) {
                g_mode = (which == 1) ? EditMode::ROTATE : EditMode::RESIZE;
                g_resize_corner = which - 2;
            } else if (onBody) {
                g_selected = bidx;
                if (g_extended_hwnd) PostMessageW(g_extended_hwnd, WM_APP_SYNC_EXTENDED, 0, 0);
                g_mode = EditMode::MOVE;
            } else {
                g_mode = EditMode::PAN;   // 空き領域ドラッグ = キャンバス移動
            }
            if (g_mode != EditMode::NONE) {
                if (g_mode != EditMode::PAN) begin_edit_history();
                SetCapture(hwnd);
                g_drag_start = { mx, my };
                if (g_selected >= 0) g_drag_orig = g_chars[g_selected].t;
            }
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        case WM_MOUSEMOVE: {
            int mx = GET_X_LPARAM(lp), my = GET_Y_LPARAM(lp);
            if (g_mode == EditMode::PAN) {
                g_pan_x += (float)(mx - g_drag_start.x);
                g_pan_y += (float)(my - g_drag_start.y);
                g_drag_start = { mx, my };
                SetCursor(LoadCursor(nullptr, IDC_SIZEALL));
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }
            if (g_mode == EditMode::NONE) {
                int h = 0, b = 0;
                bool ho = hit_handle(mx, my, &h), bo = hit_body(mx, my, &b);
                SetCursor((ho || bo) ? LoadCursor(nullptr, IDC_SIZEALL) : LoadCursor(nullptr, IDC_ARROW));
                return 0;
            }
            CharState& cs = g_chars[g_selected];
            if (g_mode == EditMode::MOVE) {
                float ox0, oy0, ox1, oy1;
                screen_to_obj((float)g_drag_start.x, (float)g_drag_start.y, &ox0, &oy0);
                screen_to_obj((float)mx, (float)my, &ox1, &oy1);
                if (shortcut_held(SHORTCUT_LOCK_HORIZONTAL)) oy1 = oy0;
                if (shortcut_held(SHORTCUT_LOCK_VERTICAL)) ox1 = ox0;
                cs.t.x = g_drag_orig.x + (ox1 - ox0);
                cs.t.y = g_drag_orig.y + (oy1 - oy0);
            } else if (g_mode == EditMode::ROTATE) {
                Pt c; float hw, hh; char_screen(cs, &c, &hw, &hh);
                const float final_rz = atan2f((float)my - c.y, (float)mx - c.x) *
                                       180.f / 3.14159265358979f + 90.f;
                cs.t.rz = final_rz - cs.base_rz;
            } else if (g_mode == EditMode::RESIZE) {
                float cx = cs.base_cx + cs.t.x, cy = cs.base_cy + cs.t.y;
                float ox, oy; screen_to_obj((float)mx, (float)my, &ox, &oy);
                float rad = -char_rotation(cs) * 3.14159265358979f / 180.f;
                float co = cosf(rad), si = sinf(rad);
                float dx = ox - cx, dy = oy - cy;
                float lx = dx * co - dy * si;
                float ly = dx * si + dy * co;
                float cellW = (std::max)(1.f, fabsf(cs.image_w * cs.base_sx));
                float cellH = (std::max)(1.f, fabsf(cs.image_h * cs.base_sy));
                float sxk = (g_resize_corner == 1 || g_resize_corner == 2) ? 1.f : -1.f;
                float syk = (g_resize_corner == 0 || g_resize_corner == 1) ? -1.f : 1.f;
                cs.t.sx = (std::max)(0.05f, (std::min)(10.f, lx / (sxk * cellW / 2.f)));
                cs.t.sy = (std::max)(0.05f, (std::min)(10.f, ly / (syk * cellH / 2.f)));
            }
            write_data_to_object();
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        case WM_CAPTURECHANGED:
        case WM_LBUTTONUP:
            if (g_mode != EditMode::NONE) {
                g_mode = EditMode::NONE;
                g_resize_corner = -1;
                if (GetCapture() == hwnd) ReleaseCapture();
                write_data_to_object(true); // 最終確定値を確実に書き込む
                commit_edit_history();
            }
            return 0;

        case WM_MOUSEWHEEL: {
            POINT pt = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
            ScreenToClient(hwnd, &pt);
            int mx = pt.x, my = pt.y;
            short delta = GET_WHEEL_DELTA_WPARAM(wp);
            if (GetKeyState(VK_CONTROL) & 0x8000) {
                // キャンバス拡大縮小(カーソル位置を基準にズーム)
                float factor = delta > 0 ? 1.1f : 1.f / 1.1f;
                float s0 = g_fit_s;
                float s1 = max(0.02f, min(8.f, s0 * factor));
                if (s1 != s0) {
                    float k = s1 / s0;
                    g_pan_x = (mx - g_win_cx) - k * (mx - g_win_cx - g_pan_x);
                    g_pan_y = (my - g_win_cy) - k * (my - g_win_cy - g_pan_y);
                    g_user_zoom = s1 / g_fit_base;
                    g_fit_s = s1;
                    InvalidateRect(hwnd, nullptr, FALSE);
                }
                return 0;
            }
            if (g_selected < 0) break;
            CharState& cs = g_chars[g_selected];
            begin_edit_history();
            float k = delta > 0 ? 1.05f : 1.f / 1.05f;
            cs.t.sx = max(0.05f, min(10.f, cs.t.sx * k));
            cs.t.sy = cs.t.sx;
            write_data_to_object(true);
            commit_edit_history();
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        case WM_LBUTTONDBLCLK: {
            int bidx;
            if (hit_body(GET_X_LPARAM(lp), GET_Y_LPARAM(lp), &bidx)) {
                g_selected = bidx;
                if (g_extended_hwnd) PostMessageW(g_extended_hwnd, WM_APP_SYNC_EXTENDED, 0, 0);
                reset_char(bidx);
            }
            return 0;
        }
        case WM_RBUTTONUP: {
            int reference = -1;
            if (hit_body(GET_X_LPARAM(lp), GET_Y_LPARAM(lp), &reference)) {
                POINT screen = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
                ClientToScreen(hwnd, &screen);
                show_alignment_menu(hwnd, reference, screen);
            }
            return 0;
        }
        case WM_DESTROY:
            KillTimer(hwnd, 1);
            g_focus_object.store(nullptr, std::memory_order_release);
            g_focus_layer.store(-1, std::memory_order_release);
            g_hwnd = nullptr;
            if (g_font_ui) { DeleteObject(g_font_ui); g_font_ui = nullptr; }
            if (g_gdiplus_token) {
                Gdiplus::GdiplusShutdown(g_gdiplus_token);
                g_gdiplus_token = 0;
            }
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

static bool get_png_encoder(CLSID* clsid) {
    UINT count = 0;
    UINT bytes = 0;
    if (Gdiplus::GetImageEncodersSize(&count, &bytes) != Gdiplus::Ok || !count || !bytes) {
        return false;
    }
    std::vector<BYTE> storage(bytes);
    auto* encoders = reinterpret_cast<Gdiplus::ImageCodecInfo*>(storage.data());
    if (Gdiplus::GetImageEncoders(count, bytes, encoders) != Gdiplus::Ok) return false;
    for (UINT i = 0; i < count; i++) {
        if (encoders[i].MimeType && wcscmp(encoders[i].MimeType, L"image/png") == 0) {
            *clsid = encoders[i].Clsid;
            return true;
        }
    }
    return false;
}

static bool choose_png_file(HWND owner, bool save, int char_index, std::wstring* path) {
    std::vector<wchar_t> buffer(32768, L'\0');
    if (save) swprintf_s(buffer.data(), buffer.size(), L"文字%d.png", char_index + 1);
    OPENFILENAMEW dialog = {};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = owner;
    dialog.lpstrFilter = L"PNG画像 (*.png)\0*.png\0すべてのファイル (*.*)\0*.*\0\0";
    dialog.lpstrFile = buffer.data();
    dialog.nMaxFile = (DWORD)buffer.size();
    dialog.lpstrDefExt = L"png";
    dialog.Flags = OFN_EXPLORER | OFN_NOCHANGEDIR | OFN_PATHMUSTEXIST |
                   (save ? OFN_OVERWRITEPROMPT : OFN_FILEMUSTEXIST);
    const BOOL selected = save ? GetSaveFileNameW(&dialog) : GetOpenFileNameW(&dialog);
    if (!selected) return false;
    *path = buffer.data();
    if (save) {
        const size_t slash = path->find_last_of(L"\\/");
        const size_t dot = path->find_last_of(L'.');
        if (dot == std::wstring::npos || (slash != std::wstring::npos && dot < slash)) {
            *path += L".png";
        }
    }
    return true;
}

static bool can_load_png_file(const std::wstring& path) {
    Gdiplus::Bitmap image(path.c_str());
    return image.GetLastStatus() == Gdiplus::Ok &&
           image.GetWidth() > 0 && image.GetHeight() > 0;
}

static bool export_character_png(HWND owner) {
    const int char_index = g_draw_char_index;
    const OBJECT_HANDLE object = g_ui_object;
    if (char_index < 0 || char_index >= (int)g_chars.size()) return false;
    if (!g_chars[char_index].bitmap || g_chars[char_index].bitmap->width <= 0 ||
        g_chars[char_index].bitmap->height <= 0) {
        MessageBoxW(owner, L"文字画像を取得中です。少し待ってから再実行してください。",
                    L"文字装飾(手書き)", MB_OK | MB_ICONINFORMATION);
        return false;
    }
    std::wstring path;
    if (!choose_png_file(owner, true, char_index, &path)) return false;
    if (object != g_ui_object || char_index >= (int)g_chars.size()) {
        MessageBoxW(owner, L"対象文字が変更されたため、PNGを書き出せませんでした。",
                    L"文字装飾(手書き)", MB_OK | MB_ICONERROR);
        return false;
    }
    const CharState& state = g_chars[char_index];
    if (!state.bitmap || state.bitmap->width <= 0 || state.bitmap->height <= 0) {
        MessageBoxW(owner, L"文字画像を取得できませんでした。再実行してください。",
                    L"文字装飾(手書き)", MB_OK | MB_ICONERROR);
        return false;
    }
    std::vector<std::uint32_t> pixels = decorated_pixels(state);
    if (pixels.empty()) return false;
    Gdiplus::Bitmap image(state.bitmap->width, state.bitmap->height,
                          state.bitmap->width * 4, PixelFormat32bppPARGB,
                          reinterpret_cast<BYTE*>(pixels.data()));
    CLSID encoder = {};
    if (image.GetLastStatus() != Gdiplus::Ok || !get_png_encoder(&encoder) ||
        image.Save(path.c_str(), &encoder, nullptr) != Gdiplus::Ok) {
        MessageBoxW(owner, L"PNGを書き出せませんでした。", L"文字装飾(手書き)",
                    MB_OK | MB_ICONERROR);
        return false;
    }
    return true;
}

static bool import_character_png(HWND owner) {
    const int char_index = g_draw_char_index;
    const OBJECT_HANDLE object = g_ui_object;
    if (char_index < 0 || char_index >= (int)g_chars.size()) return false;
    std::wstring path;
    if (!choose_png_file(owner, false, char_index, &path)) return false;
    if (object != g_ui_object || char_index >= (int)g_chars.size()) {
        MessageBoxW(owner, L"対象文字が変更されたため、PNGを読み込めませんでした。",
                    L"文字装飾(手書き)", MB_OK | MB_ICONERROR);
        return false;
    }
    CharState& state = g_chars[char_index];
    const bool loaded = state.bitmap && state.bitmap->width > 0 && state.bitmap->height > 0 ?
        load_png_bitmap(path, state.bitmap->width, state.bitmap->height, true) != nullptr :
        can_load_png_file(path);
    if (!loaded) {
        MessageBoxW(owner, L"PNGを読み込めませんでした。", L"文字装飾(手書き)",
                    MB_OK | MB_ICONERROR);
        return false;
    }
    begin_edit_history(HistoryKind::HAND, char_index);
    state.png_path = std::move(path);
    state.strokes.clear();
    write_data_to_object(true);
    commit_edit_history(HistoryKind::HAND, char_index);
    if (g_hwnd) InvalidateRect(g_hwnd, nullptr, FALSE);
    InvalidateRect(owner, nullptr, FALSE);
    return true;
}

static bool draw_mouse_point(int x, int y, bool clamp_point, StrokePoint* point) {
    if (g_draw_image_rect.Width <= 0.f || g_draw_image_rect.Height <= 0.f) return false;
    float px = (float)x;
    float py = (float)y;
    if (!clamp_point && (px < g_draw_image_rect.X || py < g_draw_image_rect.Y ||
        px > g_draw_image_rect.GetRight() || py > g_draw_image_rect.GetBottom())) return false;
    px = (std::max)(g_draw_image_rect.X, (std::min)(g_draw_image_rect.GetRight(), px));
    py = (std::max)(g_draw_image_rect.Y, (std::min)(g_draw_image_rect.GetBottom(), py));
    point->x = (px - g_draw_image_rect.X) / g_draw_image_rect.Width;
    point->y = (py - g_draw_image_rect.Y) / g_draw_image_rect.Height;
    return true;
}

static void commit_hand_change() {
    write_data_to_object(true);
    commit_edit_history(HistoryKind::HAND, g_draw_char_index);
    if (g_hwnd) InvalidateRect(g_hwnd, nullptr, FALSE);
    if (g_draw_hwnd) InvalidateRect(g_draw_hwnd, nullptr, FALSE);
}

static void stop_eyedropper(HWND hwnd) {
    g_eyedropper_active = false;
    if (GetCapture() == hwnd) ReleaseCapture();
    SetWindowTextW(GetDlgItem(hwnd, IDC_DRAW_EYEDROPPER), L"スポイト");
}

static void sample_screen_color(HWND hwnd) {
    POINT point = {};
    GetCursorPos(&point);
    HDC screen = GetDC(nullptr);
    const COLORREF color = screen ? GetPixel(screen, point.x, point.y) : CLR_INVALID;
    if (screen) ReleaseDC(nullptr, screen);
    stop_eyedropper(hwnd);
    if (color == CLR_INVALID) return;
    g_pen_color = color;
    g_pen_eraser = false;
    CheckRadioButton(hwnd, IDC_DRAW_PEN, IDC_DRAW_ERASER, IDC_DRAW_PEN);
}

static void paint_draw_window(HWND hwnd) {
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(hwnd, &ps);
    RECT rc;
    GetClientRect(hwnd, &rc);
    HDC mem = CreateCompatibleDC(dc);
    HBITMAP buffer = CreateCompatibleBitmap(dc, (std::max)(1L, rc.right), (std::max)(1L, rc.bottom));
    HGDIOBJ old_buffer = SelectObject(mem, buffer);
    FillRect(mem, &rc, (HBRUSH)(COLOR_BTNFACE + 1));
    g_draw_image_rect = Gdiplus::RectF();

    if (g_draw_char_index >= 0 && g_draw_char_index < (int)g_chars.size()) {
        const CharState& state = g_chars[g_draw_char_index];
        if (state.bitmap && state.bitmap->width > 0 && state.bitmap->height > 0 &&
            !state.bitmap->pixels.empty()) {
            const float available_w = (std::max)(1.f, (float)rc.right - 20.f);
            const float available_h = (std::max)(1.f, (float)rc.bottom - 94.f);
            const float scale = (std::min)(available_w / (float)state.bitmap->width,
                                           available_h / (float)state.bitmap->height);
            const float width = (float)state.bitmap->width * scale;
            const float height = (float)state.bitmap->height * scale;
            g_draw_image_rect = Gdiplus::RectF(((float)rc.right - width) * 0.5f,
                                               84.f + (available_h - height) * 0.5f,
                                               width, height);
            HBRUSH checker[2] = {
                CreateSolidBrush(RGB(245, 245, 245)), CreateSolidBrush(RGB(210, 210, 210))
            };
            const int left = (int)floorf(g_draw_image_rect.X);
            const int top = (int)floorf(g_draw_image_rect.Y);
            const int right = (int)ceilf(g_draw_image_rect.GetRight());
            const int bottom = (int)ceilf(g_draw_image_rect.GetBottom());
            for (int y = top; y < bottom; y += 12) {
                for (int x = left; x < right; x += 12) {
                    RECT tile = { x, y, (std::min)(x + 12, right), (std::min)(y + 12, bottom) };
                    FillRect(mem, &tile, checker[((x - left) / 12 + (y - top) / 12) & 1]);
                }
            }
            DeleteObject(checker[0]);
            DeleteObject(checker[1]);

            std::vector<std::uint32_t> pixels = decorated_pixels(state);
            Gdiplus::Bitmap image(state.bitmap->width, state.bitmap->height,
                                  state.bitmap->width * 4, PixelFormat32bppPARGB,
                                  reinterpret_cast<BYTE*>(pixels.data()));
            Gdiplus::Graphics graphics(mem);
            graphics.SetInterpolationMode(Gdiplus::InterpolationModeNearestNeighbor);
            graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
            graphics.DrawImage(&image, g_draw_image_rect);
            Gdiplus::Pen border(Gdiplus::Color(255, 80, 80, 80), 1.f);
            graphics.DrawRectangle(&border, g_draw_image_rect);
        } else {
            RECT text_rc = { 10, 92, rc.right - 10, rc.bottom - 10 };
            SetBkMode(mem, TRANSPARENT);
            SelectObject(mem, g_font_ui);
            DrawTextW(mem, L"文字画像を取得中です", -1, &text_rc,
                      DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }
    }

    BitBlt(dc, 0, 0, rc.right, rc.bottom, mem, 0, 0, SRCCOPY);
    SelectObject(mem, old_buffer);
    DeleteObject(buffer);
    DeleteDC(mem);
    EndPaint(hwnd, &ps);
}

static LRESULT CALLBACK draw_wnd_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_CREATE: {
            HWND pen = CreateWindowExW(0, WC_BUTTONW, L"ペン",
                                       WS_VISIBLE | WS_CHILD | WS_GROUP | BS_AUTORADIOBUTTON,
                                       8, 8, 48, 28, hwnd, (HMENU)(INT_PTR)IDC_DRAW_PEN,
                                       GetModuleHandleW(nullptr), nullptr);
            HWND eraser = CreateWindowExW(0, WC_BUTTONW, L"消しゴム",
                                          WS_VISIBLE | WS_CHILD | BS_AUTORADIOBUTTON,
                                          60, 8, 66, 28, hwnd, (HMENU)(INT_PTR)IDC_DRAW_ERASER,
                                          GetModuleHandleW(nullptr), nullptr);
            HWND color = CreateWindowExW(0, WC_BUTTONW, L"ペン色",
                                         WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
                                         130, 8, 58, 28, hwnd, (HMENU)(INT_PTR)IDC_DRAW_COLOR,
                                         GetModuleHandleW(nullptr), nullptr);
            HWND eyedropper = CreateWindowExW(0, WC_BUTTONW, L"スポイト",
                                              WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
                                              192, 8, 64, 28, hwnd,
                                              (HMENU)(INT_PTR)IDC_DRAW_EYEDROPPER,
                                              GetModuleHandleW(nullptr), nullptr);
            HWND width_label = CreateWindowExW(0, WC_STATICW, L"太さ",
                                               WS_VISIBLE | WS_CHILD | SS_LEFT,
                                               264, 14, 32, 18, hwnd, nullptr,
                                               GetModuleHandleW(nullptr), nullptr);
            HWND width = CreateWindowExW(0, TRACKBAR_CLASSW, L"",
                                         WS_VISIBLE | WS_CHILD | TBS_HORZ | TBS_NOTICKS,
                                         296, 6, 96, 32, hwnd, (HMENU)(INT_PTR)IDC_DRAW_WIDTH,
                                         GetModuleHandleW(nullptr), nullptr);
            HWND undo = CreateWindowExW(0, WC_BUTTONW, L"戻す",
                                        WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
                                        400, 8, 64, 28, hwnd, (HMENU)(INT_PTR)IDC_DRAW_UNDO,
                                        GetModuleHandleW(nullptr), nullptr);
            HWND redo = CreateWindowExW(0, WC_BUTTONW, L"進める",
                                        WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
                                        468, 8, 64, 28, hwnd, (HMENU)(INT_PTR)IDC_DRAW_REDO,
                                        GetModuleHandleW(nullptr), nullptr);
            HWND clear = CreateWindowExW(0, WC_BUTTONW, L"全消去",
                                         WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
                                         536, 8, 64, 28, hwnd, (HMENU)(INT_PTR)IDC_DRAW_CLEAR,
                                         GetModuleHandleW(nullptr), nullptr);
            HWND export_png = CreateWindowExW(0, WC_BUTTONW, L"PNG書き出し",
                                              WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
                                              8, 42, 96, 28, hwnd,
                                              (HMENU)(INT_PTR)IDC_DRAW_EXPORT,
                                              GetModuleHandleW(nullptr), nullptr);
            HWND import_png = CreateWindowExW(0, WC_BUTTONW, L"PNG読み込み",
                                              WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
                                              108, 42, 96, 28, hwnd,
                                              (HMENU)(INT_PTR)IDC_DRAW_IMPORT,
                                              GetModuleHandleW(nullptr), nullptr);
            for (HWND control : { pen, eraser, color, eyedropper, width_label, width,
                                  undo, redo, clear, export_png, import_png }) {
                SendMessageW(control, WM_SETFONT, (WPARAM)g_font_ui, TRUE);
            }
            SendMessageW(width, TBM_SETRANGE, TRUE, MAKELPARAM(5, 200));
            SendMessageW(width, TBM_SETPOS, TRUE, (LPARAM)lroundf(g_pen_width * 1000.f));
            CheckRadioButton(hwnd, IDC_DRAW_PEN, IDC_DRAW_ERASER,
                             g_pen_eraser ? IDC_DRAW_ERASER : IDC_DRAW_PEN);
            return 0;
        }
        case WM_SIZE: {
            RECT rc;
            GetClientRect(hwnd, &rc);
            HWND clear = GetDlgItem(hwnd, IDC_DRAW_CLEAR);
            HWND undo = GetDlgItem(hwnd, IDC_DRAW_UNDO);
            HWND redo = GetDlgItem(hwnd, IDC_DRAW_REDO);
            if (clear) MoveWindow(clear, (std::max)(536, (int)rc.right - 68), 8, 64, 28, TRUE);
            if (redo) MoveWindow(redo, (std::max)(468, (int)rc.right - 136), 8, 64, 28, TRUE);
            if (undo) MoveWindow(undo, (std::max)(400, (int)rc.right - 204), 8, 64, 28, TRUE);
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        case WM_GETMINMAXINFO: {
            auto* info = (MINMAXINFO*)lp;
            info->ptMinTrackSize = { 640, 360 };
            return 0;
        }
        case WM_PAINT:
            paint_draw_window(hwnd);
            return 0;
        case WM_ERASEBKGND:
            return 1;
        case WM_HSCROLL:
            if ((HWND)lp == GetDlgItem(hwnd, IDC_DRAW_WIDTH)) {
                const LRESULT value = SendMessageW((HWND)lp, TBM_GETPOS, 0, 0);
                g_pen_width = (std::max)(0.005f, (std::min)(0.2f, (float)value / 1000.f));
            }
            return 0;
        case WM_COMMAND:
            switch (LOWORD(wp)) {
                case IDC_DRAW_PEN:
                    if (HIWORD(wp) == BN_CLICKED) g_pen_eraser = false;
                    break;
                case IDC_DRAW_ERASER:
                    if (HIWORD(wp) == BN_CLICKED) g_pen_eraser = true;
                    break;
                case IDC_DRAW_COLOR:
                    if (HIWORD(wp) == BN_CLICKED &&
                        choose_color(hwnd, &g_pen_color, g_pen_custom_colors)) {
                        g_pen_eraser = false;
                        CheckRadioButton(hwnd, IDC_DRAW_PEN, IDC_DRAW_ERASER, IDC_DRAW_PEN);
                    }
                    break;
                case IDC_DRAW_EYEDROPPER:
                    if (HIWORD(wp) == BN_CLICKED) {
                        if (g_eyedropper_active) {
                            stop_eyedropper(hwnd);
                        } else {
                            g_eyedropper_active = true;
                            SetWindowTextW(GetDlgItem(hwnd, IDC_DRAW_EYEDROPPER), L"色を取得");
                            SetCapture(hwnd);
                            SetCursor(LoadCursor(nullptr, IDC_CROSS));
                        }
                    }
                    break;
                case IDC_DRAW_UNDO:
                    if (HIWORD(wp) == BN_CLICKED) undo_hand_edit();
                    break;
                case IDC_DRAW_REDO:
                    if (HIWORD(wp) == BN_CLICKED) redo_hand_edit();
                    break;
                case IDC_DRAW_CLEAR:
                    if (HIWORD(wp) == BN_CLICKED && g_draw_char_index >= 0 &&
                        g_draw_char_index < (int)g_chars.size()) {
                        begin_edit_history(HistoryKind::HAND, g_draw_char_index);
                        g_chars[g_draw_char_index].strokes.clear();
                        g_chars[g_draw_char_index].png_path.clear();
                        commit_hand_change();
                    }
                    break;
                case IDC_DRAW_EXPORT:
                    if (HIWORD(wp) == BN_CLICKED) export_character_png(hwnd);
                    break;
                case IDC_DRAW_IMPORT:
                    if (HIWORD(wp) == BN_CLICKED) import_character_png(hwnd);
                    break;
            }
            return 0;
        case WM_LBUTTONDOWN: {
            if (g_eyedropper_active) {
                sample_screen_color(hwnd);
                return 0;
            }
            StrokePoint point;
            if (g_draw_char_index >= 0 && g_draw_char_index < (int)g_chars.size() &&
                g_chars[g_draw_char_index].bitmap &&
                draw_mouse_point(GET_X_LPARAM(lp), GET_Y_LPARAM(lp), false, &point)) {
                begin_edit_history(HistoryKind::HAND, g_draw_char_index);
                Stroke stroke;
                stroke.erase = g_pen_eraser;
                stroke.color = g_pen_color;
                stroke.width = g_pen_width;
                stroke.points.push_back(point);
                g_chars[g_draw_char_index].strokes.push_back(std::move(stroke));
                g_hand_drawing = true;
                SetCapture(hwnd);
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            return 0;
        }
        case WM_MOUSEMOVE:
            if (g_eyedropper_active) {
                SetCursor(LoadCursor(nullptr, IDC_CROSS));
                return 0;
            }
            if (g_hand_drawing && GetCapture() == hwnd && g_draw_char_index >= 0 &&
                g_draw_char_index < (int)g_chars.size() &&
                !g_chars[g_draw_char_index].strokes.empty()) {
                StrokePoint point;
                if (draw_mouse_point(GET_X_LPARAM(lp), GET_Y_LPARAM(lp), true, &point)) {
                    auto& points = g_chars[g_draw_char_index].strokes.back().points;
                    const StrokePoint& last = points.back();
                    const float dx = point.x - last.x;
                    const float dy = point.y - last.y;
                    if (dx * dx + dy * dy >= 0.000004f && points.size() < 8192) {
                        points.push_back(point);
                        InvalidateRect(hwnd, nullptr, FALSE);
                    }
                }
                return 0;
            }
            if (g_draw_image_rect.Contains((Gdiplus::REAL)GET_X_LPARAM(lp),
                                           (Gdiplus::REAL)GET_Y_LPARAM(lp))) {
                SetCursor(LoadCursor(nullptr, IDC_CROSS));
                return 0;
            }
            break;
        case WM_LBUTTONUP:
            if (g_hand_drawing) {
                g_hand_drawing = false;
                if (GetCapture() == hwnd) ReleaseCapture();
                commit_hand_change();
                return 0;
            }
            break;
        case WM_CAPTURECHANGED:
            if (g_eyedropper_active) {
                g_eyedropper_active = false;
                SetWindowTextW(GetDlgItem(hwnd, IDC_DRAW_EYEDROPPER), L"スポイト");
            }
            if (g_hand_drawing) {
                g_hand_drawing = false;
                commit_hand_change();
            }
            break;
        case WM_SETCURSOR:
            if (g_eyedropper_active) {
                SetCursor(LoadCursor(nullptr, IDC_CROSS));
                return TRUE;
            }
            break;
        case WM_KEYDOWN:
            if (!g_hand_drawing && wp == L'Z' && (GetKeyState(VK_CONTROL) & 0x8000)) {
                undo_hand_edit();
                return 0;
            }
            if (!g_hand_drawing && wp == L'Y' && (GetKeyState(VK_CONTROL) & 0x8000)) {
                redo_hand_edit();
                return 0;
            }
            if (wp == VK_ESCAPE) {
                if (g_eyedropper_active) {
                    stop_eyedropper(hwnd);
                    return 0;
                }
                if (g_hand_drawing) {
                    g_hand_drawing = false;
                    if (GetCapture() == hwnd) ReleaseCapture();
                    commit_hand_change();
                }
                ShowWindow(hwnd, SW_HIDE);
                return 0;
            }
            break;
        case WM_CLOSE:
            if (g_eyedropper_active) stop_eyedropper(hwnd);
            if (g_hand_drawing) {
                g_hand_drawing = false;
                if (GetCapture() == hwnd) ReleaseCapture();
                commit_hand_change();
            }
            ShowWindow(hwnd, SW_HIDE);
            return 0;
        case WM_DESTROY:
            g_eyedropper_active = false;
            if (g_hand_drawing) {
                g_hand_drawing = false;
                commit_hand_change();
            }
            g_draw_hwnd = nullptr;
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

static void show_hand_editor(HWND owner) {
    if (g_selected < 0 || g_selected >= (int)g_chars.size()) return;
    g_draw_char_index = g_selected;
    if (!g_draw_hwnd) {
        g_draw_hwnd = CreateWindowExW(WS_EX_TOOLWINDOW, DRAW_WINDOW_NAME, L"文字装飾(手書き)",
                                      WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU |
                                      WS_THICKFRAME | WS_CLIPCHILDREN,
                                      CW_USEDEFAULT, CW_USEDEFAULT, 640, 520,
                                      owner, nullptr, GetModuleHandleW(nullptr), nullptr);
        if (!g_draw_hwnd) return;
    }
    std::wstring title = L"文字装飾(手書き) - ";
    title += g_chars[g_draw_char_index].glyph;
    SetWindowTextW(g_draw_hwnd, title.c_str());
    ShowWindow(g_draw_hwnd, SW_SHOWNORMAL);
    SetForegroundWindow(g_draw_hwnd);
    InvalidateRect(g_draw_hwnd, nullptr, FALSE);
}

static LRESULT CALLBACK extended_wnd_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_CREATE: {
            HWND label = CreateWindowExW(0, WC_STATICW,
                                         L"項目をダブルクリックしてキーを入力\r\n方向固定・Ctrl+ホイール・戻す/進めるは常に有効です",
                                         WS_VISIBLE | WS_CHILD | SS_LEFT,
                                         8, 8, 360, 38, hwnd,
                                         (HMENU)(INT_PTR)IDC_SHORTCUT_LABEL,
                                         GetModuleHandleW(nullptr), nullptr);
            HWND disable = CreateWindowExW(0, WC_BUTTONW,
                                           L"ショートカットキーを無効化",
                                           WS_VISIBLE | WS_CHILD | BS_AUTOCHECKBOX,
                                           8, 48, 260, 24, hwnd,
                                           (HMENU)(INT_PTR)IDC_SHORTCUT_DISABLE,
                                           GetModuleHandleW(nullptr), nullptr);
            HWND list = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTBOXW, L"",
                                        WS_VISIBLE | WS_CHILD | WS_VSCROLL | LBS_NOTIFY |
                                        LBS_NOINTEGRALHEIGHT,
                                        8, 76, 360, 274, hwnd,
                                        (HMENU)(INT_PTR)IDC_SHORTCUT_LIST,
                                        GetModuleHandleW(nullptr), nullptr);
            HWND draw = CreateWindowExW(0, WC_BUTTONW, L"文字装飾(手書き)",
                                        WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
                                        8, 298, 148, 30, hwnd,
                                        (HMENU)(INT_PTR)IDC_HAND_DRAW,
                                        GetModuleHandleW(nullptr), nullptr);
            HWND reset = CreateWindowExW(0, WC_BUTTONW, L"キー設定を初期化",
                                         WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
                                         164, 298, 204, 30, hwnd,
                                         (HMENU)(INT_PTR)IDC_SHORTCUT_RESET,
                                         GetModuleHandleW(nullptr), nullptr);
            for (HWND control : { label, disable, list, draw, reset }) {
                SendMessageW(control, WM_SETFONT, (WPARAM)g_font_ui, TRUE);
            }
            CheckDlgButton(hwnd, IDC_SHORTCUT_DISABLE,
                           g_shortcuts_disabled ? BST_CHECKED : BST_UNCHECKED);
            refresh_shortcut_list(hwnd);
            return 0;
        }
        case WM_SIZE: {
            RECT rc;
            GetClientRect(hwnd, &rc);
            const int button_y = (std::max)(38, (int)rc.bottom - 38);
            MoveWindow(GetDlgItem(hwnd, IDC_SHORTCUT_LABEL), 8, 8,
                       (std::max)(1, (int)rc.right - 16), 38, TRUE);
            MoveWindow(GetDlgItem(hwnd, IDC_SHORTCUT_DISABLE), 8, 48,
                       (std::max)(1, (int)rc.right - 16), 24, TRUE);
            MoveWindow(GetDlgItem(hwnd, IDC_SHORTCUT_LIST), 8, 76,
                       (std::max)(1, (int)rc.right - 16), (std::max)(1, button_y - 84), TRUE);
            MoveWindow(GetDlgItem(hwnd, IDC_HAND_DRAW), 8, button_y, 148, 30, TRUE);
            MoveWindow(GetDlgItem(hwnd, IDC_SHORTCUT_RESET), 164, button_y,
                       (std::max)(1, (int)rc.right - 172), 30, TRUE);
            return 0;
        }
        case WM_GETMINMAXINFO: {
            auto* info = (MINMAXINFO*)lp;
            info->ptMinTrackSize = { 300, 300 };
            return 0;
        }
        case WM_APP_SYNC_EXTENDED:
            sync_shortcut_window(hwnd);
            return 0;
        case WM_COMMAND:
            switch (LOWORD(wp)) {
                case IDC_SHORTCUT_LIST:
                    if (HIWORD(wp) == LBN_DBLCLK) {
                        const int row = (int)SendDlgItemMessageW(
                            hwnd, IDC_SHORTCUT_LIST, LB_GETCURSEL, 0, 0);
                        if (row >= 0 && row < SHORTCUT_COUNT) {
                            g_shortcut_capture = row;
                            g_shortcut_pending_modifier_key = 0;
                            refresh_shortcut_list(hwnd);
                            SetFocus(hwnd);
                        }
                    }
                    break;
                case IDC_HAND_DRAW:
                    if (HIWORD(wp) == BN_CLICKED) show_hand_editor(hwnd);
                    break;
                case IDC_SHORTCUT_DISABLE:
                    if (HIWORD(wp) == BN_CLICKED) {
                        g_shortcuts_disabled = IsDlgButtonChecked(
                            hwnd, IDC_SHORTCUT_DISABLE) == BST_CHECKED;
                        save_shortcuts();
                    }
                    break;
                case IDC_SHORTCUT_RESET:
                    if (HIWORD(wp) == BN_CLICKED) {
                        g_shortcut_capture = -1;
                        reset_shortcuts();
                        refresh_shortcut_list(hwnd);
                    }
                    break;
            }
            return 0;
        case WM_GETDLGCODE:
            return DLGC_WANTALLKEYS;
        case WM_KEYDOWN:
        case WM_SYSKEYDOWN:
            if (g_shortcut_capture >= 0) {
                if (wp == VK_ESCAPE) {
                    g_shortcut_capture = -1;
                    g_shortcut_pending_modifier_key = 0;
                } else if (wp == VK_BACK) {
                    set_shortcut(g_shortcut_capture, 0, 0);
                    g_shortcut_capture = -1;
                    g_shortcut_pending_modifier_key = 0;
                } else if (is_modifier_key((UINT)wp) &&
                           is_drag_lock_action(g_shortcut_capture)) {
                    if (!g_shortcut_pending_modifier_key) {
                        g_shortcut_pending_modifier_key = (UINT)wp;
                    }
                } else if (!is_modifier_key((UINT)wp)) {
                    set_shortcut(g_shortcut_capture, (UINT)wp,
                                 current_shortcut_modifiers());
                    g_shortcut_capture = -1;
                    g_shortcut_pending_modifier_key = 0;
                }
                refresh_shortcut_list(hwnd);
                if (g_shortcut_capture < 0) {
                    SetFocus(GetDlgItem(hwnd, IDC_SHORTCUT_LIST));
                }
                return 0;
            }
            if (wp == VK_ESCAPE) {
                ShowWindow(hwnd, SW_HIDE);
                return 0;
            }
            break;
        case WM_KEYUP:
        case WM_SYSKEYUP:
            if (g_shortcut_capture >= 0 && is_drag_lock_action(g_shortcut_capture) &&
                g_shortcut_pending_modifier_key == (UINT)wp) {
                set_shortcut(g_shortcut_capture, (UINT)wp, 0);
                g_shortcut_capture = -1;
                g_shortcut_pending_modifier_key = 0;
                refresh_shortcut_list(hwnd);
                SetFocus(GetDlgItem(hwnd, IDC_SHORTCUT_LIST));
                return 0;
            }
            break;
        case WM_CHAR:
        case WM_SYSCHAR:
            return 0;
        case WM_CLOSE:
            g_shortcut_capture = -1;
            g_shortcut_pending_modifier_key = 0;
            ShowWindow(hwnd, SW_HIDE);
            return 0;
        case WM_DESTROY:
            g_shortcut_capture = -1;
            g_shortcut_pending_modifier_key = 0;
            g_extended_hwnd = nullptr;
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

static void show_extended_editor(HWND owner) {
    if (!g_extended_hwnd) {
        RECT owner_rect;
        GetWindowRect(owner, &owner_rect);
        g_extended_hwnd = CreateWindowExW(WS_EX_TOOLWINDOW, EXT_WINDOW_NAME,
                                          L"ショートカットキー編集",
                                          WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU |
                                          WS_THICKFRAME | WS_CLIPCHILDREN,
                                          owner_rect.right + 8, owner_rect.top, 400, 460,
                                          owner, nullptr, GetModuleHandleW(nullptr), nullptr);
        if (!g_extended_hwnd) return;
    }
    refresh_shortcut_list(g_extended_hwnd);
    ShowWindow(g_extended_hwnd, SW_SHOWNORMAL);
    SetForegroundWindow(g_extended_hwnd);
}

//---------------------------------------------------------------------
// イベント (イベント通知スレッドから呼ばれる為、UIへ通知するだけ)
//---------------------------------------------------------------------
static void on_host_event(void*) {
    if (g_hwnd) PostMessageW(g_hwnd, WM_APP_REFRESH, 0, 0);
}

//---------------------------------------------------------------------
// プラグイン登録
//---------------------------------------------------------------------
EXTERN_C __declspec(dllexport) void RegisterPlugin(HOST_APP_TABLE* host) {
    item_reset.callback = on_reset_button;
    host->register_filter_plugin(&filter_table);
    INITCOMMONCONTROLSEX controls = { sizeof(controls), ICC_STANDARD_CLASSES | ICC_BAR_CLASSES };
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

    g_hwnd = CreateWindowExW(0, WINDOW_NAME, L"文字エディタ",
                             WS_POPUP | WS_CLIPCHILDREN,
                             CW_USEDEFAULT, CW_USEDEFAULT, 640, 360,
                             nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (!g_hwnd) return;

    host->register_window_client(WINDOW_NAME, g_hwnd);
    edit_handle = host->create_edit_handle();
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
