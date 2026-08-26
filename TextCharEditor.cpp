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
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

#include "plugin2.h"
#include "filter2.h"
#include "config2.h"

static constexpr LPCWSTR EFFECT_NAME   = L"文字位置調整";
static constexpr LPCWSTR ITEM_DATA     = L"文字データ";
static constexpr LPCWSTR WINDOW_NAME   = L"TextCharEditorWindow";
static constexpr UINT    WM_APP_REFRESH = WM_APP + 1;
static constexpr UINT    WM_APP_CAPTURE_READY = WM_APP + 2;
static constexpr int     IDC_RESET = 2001;
static constexpr int     IDC_CANVAS_LABEL = 2002;
static constexpr int     IDC_SOLID_BACKGROUND = 2003;
static constexpr int     IDC_BACKGROUND_COLOR = 2004;

static EDIT_HANDLE*   edit_handle = nullptr;
static HWND           g_hwnd      = nullptr;

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

// パース結果
struct CharState {
    CharTransform t;
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
static std::wstring g_settings_path;

static void load_canvas_settings() {
    if (g_settings_path.empty()) return;
    g_solid_background = GetPrivateProfileIntW(
        L"Canvas", L"SolidBackground", 0, g_settings_path.c_str()) != 0;
    UINT color = GetPrivateProfileIntW(
        L"Canvas", L"BackgroundColor", g_background_color, g_settings_path.c_str());
    if (color <= 0xFFFFFFu) g_background_color = (COLORREF)color;
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

static std::wstring utf8_to_wide(LPCSTR u) {
    if (!u || !*u) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, u, -1, nullptr, 0);
    std::wstring w(n > 0 ? n - 1 : 0, L'\0');
    if (n > 0) MultiByteToWideChar(CP_UTF8, 0, u, -1, w.data(), n);
    return w;
}
static std::string serialize_transforms(const std::vector<CharState>& chars) {
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
static bool parse_transforms(LPCWSTR text, int want_index, CharTransform* out) {
    *out = CharTransform{};
    if (!text || !*text) return false;
    const wchar_t* p = text;
    while (*p) {
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
    if (g_mode != EditMode::NONE) return;
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
        reset_host_capture();
    }

    std::vector<CharState> next;
    if (g_has_focus) {
        std::wstring body = strip_control_tags(utf8_to_wide(ctx.text_utf8.c_str()));
        size_t i = 0;
        while (i < body.size()) {
            wchar_t c = body[i];
            if (c >= 0xD800 && c <= 0xDBFF && i + 1 < body.size() &&
                body[i + 1] >= 0xDC00 && body[i + 1] <= 0xDFFF) {
                i += 2;
            } else {
                i += 1;
                if (c == L'\r' || c == L'\n') continue;
            }
            next.emplace_back();
        }
        std::wstring data_w = utf8_to_wide(ctx.data_utf8.c_str());
        for (size_t index = 0; index < next.size(); index++) {
            parse_transforms(data_w.c_str(), (int)index, &next[index].t);
        }
    }
    g_chars = std::move(next);
    if (g_selected >= (int)g_chars.size()) g_selected = -1;
    layout_chars();

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

    std::string utf8 = serialize_transforms(g_chars);
    edit_handle->call_edit_section_param(&utf8, [](void* p, EDIT_SECTION* edit) {
        auto* s = (std::string*)p;
        OBJECT_HANDLE obj = edit->get_focus_object();
        if (!obj) return;
        if (EFFECT_HANDLE effect = edit->find_effect(obj, EFFECT_NAME)) {
            edit->set_effect_item_value(effect, ITEM_DATA, s->c_str());
        }
    });
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
    if (!video->object || !item_data.value || !*item_data.value) return true;

    CharTransform t;
    if (!parse_transforms(item_data.value, video->object->index, &t)) return true;

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

    Gdiplus::Bitmap image(cs.bitmap->width, cs.bitmap->height, cs.bitmap->width * 4,
                          PixelFormat32bppPARGB,
                          reinterpret_cast<BYTE*>(const_cast<std::uint32_t*>(cs.bitmap->pixels.data())));
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
    int h = 28, m = 8;
    int y = rc.bottom - h - m;
    if (hReset) MoveWindow(hReset, m, y, 110, h, TRUE);
    if (hCanvasLabel) MoveWindow(hCanvasLabel, m + 118, y + 5, 92, 18, TRUE);
    if (hSolid) MoveWindow(hSolid, m + 210, y, 130, h, TRUE);
    if (hColor) MoveWindow(hColor, m + 348, y, 80, h, TRUE);
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
            swprintf(status, 256, L"角ハンドル=拡大縮小 / 上の丸印ドラッグ=回転 / 本体ドラッグ=移動 / 空白ドラッグ=キャンバス移動 / Ctrl+ホイール=キャンバスズーム / ダブルクリック=リセット");
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
            for (const auto& cs : g_chars) draw_host_char(graphics, cs);
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
    g_chars[index].t = CharTransform{};
    write_data_to_object(true);
    InvalidateRect(g_hwnd, nullptr, FALSE);
}

static bool choose_background_color(HWND hwnd) {
    CHOOSECOLORW chooser = {};
    chooser.lStructSize = sizeof(chooser);
    chooser.hwndOwner = hwnd;
    chooser.rgbResult = g_background_color;
    chooser.lpCustColors = g_custom_colors;
    chooser.Flags = CC_FULLOPEN | CC_RGBINIT;
    if (!ChooseColorW(&chooser)) return false;
    g_background_color = chooser.rgbResult;
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
                            8, 8, 110, 28, hwnd, (HMENU)(INT_PTR)IDC_RESET,
                            GetModuleHandleW(nullptr), nullptr);
            CreateWindowExW(0, WC_STATICW, L"キャンバス設定",
                            WS_VISIBLE | WS_CHILD | SS_LEFT,
                            126, 13, 92, 18, hwnd, (HMENU)(INT_PTR)IDC_CANVAS_LABEL,
                            GetModuleHandleW(nullptr), nullptr);
            CreateWindowExW(0, WC_BUTTONW, L"背景を単色化",
                            WS_VISIBLE | WS_CHILD | BS_AUTOCHECKBOX,
                            218, 8, 130, 28, hwnd, (HMENU)(INT_PTR)IDC_SOLID_BACKGROUND,
                            GetModuleHandleW(nullptr), nullptr);
            CreateWindowExW(0, WC_BUTTONW, L"色設定",
                            WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
                            356, 8, 80, 28, hwnd, (HMENU)(INT_PTR)IDC_BACKGROUND_COLOR,
                            GetModuleHandleW(nullptr), nullptr);
            SendMessageW(GetDlgItem(hwnd, IDC_RESET), WM_SETFONT, (WPARAM)g_font_ui, TRUE);
            SendMessageW(GetDlgItem(hwnd, IDC_CANVAS_LABEL), WM_SETFONT, (WPARAM)g_font_ui, TRUE);
            SendMessageW(GetDlgItem(hwnd, IDC_SOLID_BACKGROUND), WM_SETFONT, (WPARAM)g_font_ui, TRUE);
            SendMessageW(GetDlgItem(hwnd, IDC_BACKGROUND_COLOR), WM_SETFONT, (WPARAM)g_font_ui, TRUE);
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

        case WM_COMMAND:
            switch (LOWORD(wp)) {
                case IDC_RESET:
                    for (auto& cs : g_chars) cs.t = CharTransform{};
                    write_data_to_object(true);
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
            }
            return 0;

        case WM_LBUTTONDOWN: {
            int mx = GET_X_LPARAM(lp), my = GET_Y_LPARAM(lp);
            int which = 0, bidx = -1;
            bool onHandle = hit_handle(mx, my, &which);
            bool onBody   = hit_body(mx, my, &bidx);
            if (onHandle && g_selected >= 0) {
                g_mode = (which == 1) ? EditMode::ROTATE : EditMode::RESIZE;
                g_resize_corner = which - 2;
            } else if (onBody) {
                g_selected = bidx;
                g_mode = EditMode::MOVE;
            } else {
                g_mode = EditMode::PAN;   // 空き領域ドラッグ = キャンバス移動
            }
            if (g_mode != EditMode::NONE) {
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
            float k = delta > 0 ? 1.05f : 1.f / 1.05f;
            cs.t.sx = max(0.05f, min(10.f, cs.t.sx * k));
            cs.t.sy = cs.t.sx;
            write_data_to_object();
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        case WM_LBUTTONDBLCLK: {
            int bidx;
            if (hit_body(GET_X_LPARAM(lp), GET_Y_LPARAM(lp), &bidx)) {
                g_selected = bidx;
                reset_char(bidx);
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
    WNDCLASSEXW wcex = {};
    wcex.cbSize = sizeof(WNDCLASSEX);
    wcex.style = CS_DBLCLKS;
    wcex.lpszClassName = WINDOW_NAME;
    wcex.lpfnWndProc = wnd_proc;
    wcex.hInstance = GetModuleHandleW(nullptr);
    wcex.hbrBackground = nullptr;
    wcex.hCursor = LoadCursor(nullptr, IDC_ARROW);
    if (!RegisterClassExW(&wcex)) return;

    g_hwnd = CreateWindowExW(0, WINDOW_NAME, L"文字エディタ",
                             WS_POPUP | WS_CLIPCHILDREN,
                             CW_USEDEFAULT, CW_USEDEFAULT, 520, 360,
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
    load_canvas_settings();
}
