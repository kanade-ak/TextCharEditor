// メイン文字エディタとショートカット操作
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
    int visual_width = cs.image_w;
    int visual_height = cs.image_h;
    character_visual_size(cs, &visual_width, &visual_height);
    *center = c;
    *hw = fabsf((float)visual_width * cs.base_sx * cs.t.sx * g_fit_s) / 2.f;
    *hh = fabsf((float)visual_height * cs.base_sy * cs.t.sy * g_fit_s) / 2.f;
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

    std::shared_ptr<const CharBitmap> source_owner = character_source_bitmap(cs);
    const CharBitmap* source = source_owner.get();
    CharBitmap decorated;
    if (!cs.strokes.empty()) {
        decorated = decorated_character_bitmap(cs);
        if (decorated.width > 0 && decorated.height > 0 && !decorated.pixels.empty()) {
            source = &decorated;
        }
    }
    if (!source || source->width <= 0 || source->height <= 0 || source->pixels.empty()) return;

    Pt center; float hw, hh; char_screen(cs, &center, &hw, &hh);
    Pt tl = { center.x - hw, center.y - hh };
    Pt tr = { center.x + hw, center.y - hh };
    Pt bl = { center.x - hw, center.y + hh };
    const float rz = char_rotation(cs);
    rotate_pt(tl, center, rz);
    rotate_pt(tr, center, rz);
    rotate_pt(bl, center, rz);

    Gdiplus::Bitmap image(source->width, source->height, source->width * 4,
                          PixelFormat32bppPARGB,
                          reinterpret_cast<BYTE*>(
                              const_cast<std::uint32_t*>(source->pixels.data())));
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
                       (float)source->width, (float)source->height,
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

static HCURSOR diagonal_cursor_for_corner(int corner) {
    // 角番号: 0=左上, 1=右上, 2=右下, 3=左下
    const auto cursor = (corner == 0 || corner == 2) ? IDC_SIZENWSE : IDC_SIZENESW;
    return LoadCursor(nullptr, cursor);
}

static HCURSOR editor_cursor_at(int mx, int my) {
    int handle = 0;
    if (hit_handle(mx, my, &handle)) {
        if (handle >= 2 && handle <= 5) {
            return diagonal_cursor_for_corner(handle - 2);
        }
        return LoadCursor(nullptr, IDC_SIZEWE);
    }
    int body = -1;
    if (hit_body(mx, my, &body)) return LoadCursor(nullptr, IDC_SIZEALL);
    return LoadCursor(nullptr, IDC_ARROW);
}

static void layout_chars() {
    if (!g_hwnd) return;
    RECT rc; GetClientRect(g_hwnd, &rc);
    int top = 26, bottom = rc.bottom - 40;
    const std::uint64_t epoch = g_capture_epoch.load(std::memory_order_acquire);
    bool capture_complete = true;
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
        capture_complete = false;
    }
    ReleaseSRWLockShared(&g_base_lock);
    if (!capture_complete) {
        for (auto& cs : g_chars) {
            cs.hasBase = false;
            cs.bitmap.reset();
        }
    }

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
            const CharState& previous = g_chars[i - 1];
            const CharState& current = g_chars[i];
            const float previous_height = fabsf(previous.image_h * previous.base_sy);
            const float current_height = fabsf(current.image_h * current.base_sy);
            const float same_line_limit =
                (std::max)(2.f, (std::max)(previous_height, current_height) * 0.75f);
            if (fabsf(current.base_cy - previous.base_cy) <= same_line_limit) {
                const float d = fabsf(current.base_cx - previous.base_cx);
                if (d > 0.5f) { avg += d; pairs++; }
            }
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
            if (host_capture_retry_exhausted()) {
                swprintf(status, 256, L"一部文字の実描画を取得できませんでした（選択し直すと再取得します）");
                band = RGB(255, 238, 190);
            } else {
                swprintf(status, 256, L"AviUtl2から文字の実描画を取得しています...");
                band = RGB(235, 235, 235);
            }
        } else {
            swprintf(status, 256, L"ドラッグ=移動 / 横・縦固定キーは「拡張機能」で変更 / 角=拡大縮小 / 丸印=回転 / 右クリック=整列 / ダブルクリック=変形リセット");
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
static void show_font_editor(HWND owner, int character_index);

static void disable_plugin_ime(HWND hwnd) {
    if (!hwnd) return;
    ImmAssociateContextEx(hwnd, nullptr, IACE_CHILDREN | IACE_IGNORENOCONTEXT);
}

static UINT normalize_shortcut_virtual_key(HWND hwnd, UINT vk) {
    if (vk == VK_PROCESSKEY) {
        const UINT original = ImmGetVirtualKey(hwnd);
        if (original && original != VK_PROCESSKEY) return original;
    }
    return vk;
}

static UINT shortcut_virtual_key_from_fullwidth(WPARAM value) {
    wchar_t character = (wchar_t)value;
    if (character >= 0xff01 && character <= 0xff5e) {
        character = (wchar_t)(character - 0xfee0);
    } else if (character == 0x3000) {
        character = L' ';
    } else {
        return 0;
    }
    const SHORT mapped = VkKeyScanExW(character, GetKeyboardLayout(0));
    return mapped == -1 ? 0 : (UINT)(mapped & 0xff);
}

static constexpr wchar_t CHARACTER_FONT_BEGIN[] = L"<//TCE_FONT//>";
static constexpr wchar_t CHARACTER_FONT_END[] = L"<//TCE_FONT_END//>";

static bool same_font_name(const std::wstring& left, const std::wstring& right) {
    return CompareStringOrdinal(left.c_str(), -1, right.c_str(), -1, TRUE) == CSTR_EQUAL;
}

static bool parse_font_control_tag(const std::wstring& tag,
                                   const std::wstring& base_font,
                                   std::wstring* font) {
    if (!font || tag.size() < 3 || tag[0] != L'<' || tag[1] != L'@' ||
        tag.back() != L'>') return false;
    std::wstring value = tag.substr(2, tag.size() - 3);
    if (!value.empty() && (value[0] == L'+' || value[0] == L'-')) return false;
    const size_t comma = value.find(L',');
    if (comma != std::wstring::npos) value.resize(comma);
    *font = value.empty() ? base_font : value;
    return true;
}

static void update_active_font(const std::wstring& tag,
                               const std::wstring& base_font,
                               std::wstring* active_font) {
    std::wstring font;
    if (parse_font_control_tag(tag, base_font, &font)) {
        *active_font = std::move(font);
        return;
    }
    if (tag == L"<s>" || tag == L"<$>") {
        *active_font = base_font;
        return;
    }
    if (tag.size() >= 4 && tag[0] == L'<' && tag[1] == L's' && tag.back() == L'>') {
        const size_t first_comma = tag.find(L',', 2);
        if (first_comma == std::wstring::npos) return;
        size_t end = tag.find(L',', first_comma + 1);
        if (end == std::wstring::npos) end = tag.size() - 1;
        const std::wstring specified = tag.substr(first_comma + 1,
                                                   end - first_comma - 1);
        *active_font = specified.empty() ? base_font : specified;
    }
}

struct CharacterGlyphLocation {
    size_t glyph_begin = 0;
    size_t glyph_end = 0;
    std::wstring current_font;
    bool managed_wrapper = false;
    size_t wrapper_begin = 0;
    size_t wrapper_end = 0;
    std::wstring restore_font;
};

static void detect_managed_font_wrapper(const std::wstring& text,
                                        const std::wstring& base_font,
                                        CharacterGlyphLocation* location) {
    if (!location || location->glyph_begin == 0 || location->glyph_end >= text.size()) return;
    const size_t open_begin = text.rfind(L'<', location->glyph_begin - 1);
    if (open_begin == std::wstring::npos) return;
    const std::wstring open_tag = text.substr(open_begin,
                                              location->glyph_begin - open_begin);
    std::wstring selected_font;
    if (!parse_font_control_tag(open_tag, base_font, &selected_font)) return;

    constexpr size_t begin_length = _countof(CHARACTER_FONT_BEGIN) - 1;
    if (open_begin < begin_length ||
        text.compare(open_begin - begin_length, begin_length,
                     CHARACTER_FONT_BEGIN) != 0) return;

    const size_t close_begin = location->glyph_end;
    if (text[close_begin] != L'<') return;
    const size_t close_end = text.find(L'>', close_begin + 1);
    if (close_end == std::wstring::npos) return;
    const std::wstring close_tag = text.substr(close_begin,
                                               close_end - close_begin + 1);
    std::wstring restore_font;
    if (!parse_font_control_tag(close_tag, base_font, &restore_font)) return;

    constexpr size_t end_length = _countof(CHARACTER_FONT_END) - 1;
    const size_t end_marker = close_end + 1;
    if (end_marker + end_length > text.size() ||
        text.compare(end_marker, end_length, CHARACTER_FONT_END) != 0) return;

    location->managed_wrapper = true;
    location->wrapper_begin = open_begin - begin_length;
    location->wrapper_end = end_marker + end_length;
    location->restore_font = std::move(restore_font);
    location->current_font = std::move(selected_font);
}

static bool locate_character_glyph(const std::wstring& text, int wanted_index,
                                   const std::wstring& base_font,
                                   CharacterGlyphLocation* location) {
    if (!location || wanted_index < 0) return false;
    std::wstring active_font = base_font;
    int visible_index = 0;
    size_t position = 0;
    while (position < text.size()) {
        const wchar_t character = text[position];
        if (character == L'<') {
            const size_t end = text.find(L'>', position + 1);
            if (end == std::wstring::npos) return false;
            update_active_font(text.substr(position, end - position + 1),
                               base_font, &active_font);
            position = end + 1;
            continue;
        }

        size_t glyph_end = position + 1;
        if (character == L'\\' && position + 1 < text.size()) {
            const wchar_t escaped = text[position + 1];
            if (escaped == L'\\') {
                glyph_end = position + 2;
            } else if (escaped == L'n') {
                position += 2;
                continue;
            }
        } else if (character == L'\r' || character == L'\n' || character == L'\t') {
            position++;
            continue;
        } else if (character >= 0xd800 && character <= 0xdbff &&
                   position + 1 < text.size() && text[position + 1] >= 0xdc00 &&
                   text[position + 1] <= 0xdfff) {
            glyph_end = position + 2;
        }

        if (visible_index == wanted_index) {
            *location = {};
            location->glyph_begin = position;
            location->glyph_end = glyph_end;
            location->current_font = active_font;
            detect_managed_font_wrapper(text, base_font, location);
            return true;
        }
        visible_index++;
        position = glyph_end;
    }
    return false;
}

static std::wstring make_font_control_tag(const std::wstring& font) {
    if (font.empty()) return L"<@>";
    return L"<@" + font + L">";
}

static bool make_character_font_text(const std::wstring& source, int character_index,
                                     const std::wstring& base_font,
                                     const std::wstring& selected_font,
                                     std::wstring* result) {
    CharacterGlyphLocation location;
    if (!result || !locate_character_glyph(source, character_index, base_font, &location)) {
        return false;
    }
    if (same_font_name(location.current_font, selected_font)) return false;

    const std::wstring surrounding_font = location.managed_wrapper ?
        location.restore_font : location.current_font;
    const std::wstring glyph = source.substr(location.glyph_begin,
                                              location.glyph_end - location.glyph_begin);
    if (same_font_name(selected_font, surrounding_font)) {
        if (!location.managed_wrapper) return false;
        *result = source.substr(0, location.wrapper_begin) + glyph +
                  source.substr(location.wrapper_end);
        return true;
    }

    std::wstring replacement = CHARACTER_FONT_BEGIN;
    replacement += make_font_control_tag(selected_font);
    replacement += glyph;
    replacement += make_font_control_tag(surrounding_font);
    replacement += CHARACTER_FONT_END;
    const size_t replace_begin = location.managed_wrapper ?
        location.wrapper_begin : location.glyph_begin;
    const size_t replace_end = location.managed_wrapper ?
        location.wrapper_end : location.glyph_end;
    *result = source.substr(0, replace_begin) + replacement + source.substr(replace_end);
    return true;
}

struct CharacterFontSource {
    OBJECT_HANDLE object = nullptr;
    std::string text_utf8;
    std::wstring text;
    std::wstring base_font;
};

static bool read_character_font_source(CharacterFontSource* source) {
    if (!source || !edit_handle) return false;
    *source = {};
    struct Context {
        CharacterFontSource* source;
        bool has_text = false;
    } context { source };
    const bool read = edit_handle->call_read_section_param(
        &context, [](void* param, EDIT_SECTION* edit) {
            auto* context = static_cast<Context*>(param);
            OBJECT_HANDLE object = edit->get_focus_object();
            if (!object) return;
            if (LPCSTR text = edit->get_object_item_value(object, L"テキスト", L"テキスト")) {
                context->source->object = object;
                context->source->text_utf8 = text;
                context->has_text = true;
            }
            if (!context->has_text) return;
            if (LPCSTR font = edit->get_object_item_value(object, L"テキスト", L"フォント")) {
                context->source->base_font = utf8_to_wide(font);
            }
        });
    if (!read || !context.has_text || !source->object) return false;
    source->text = utf8_to_wide(source->text_utf8.c_str());
    return true;
}

struct CharacterFontWriteContext {
    OBJECT_HANDLE expected_object = nullptr;
    std::string expected_text;
    std::string replacement_text;
    bool stale = false;
    bool written = false;
};

static bool change_character_font(HWND owner, int character_index,
                                  const CharacterFontSource& source,
                                  const std::wstring& selected_font) {
    std::wstring replacement;
    if (!make_character_font_text(source.text, character_index, source.base_font,
                                  selected_font, &replacement)) return false;

    CharacterFontWriteContext context;
    context.expected_object = source.object;
    context.expected_text = source.text_utf8;
    context.replacement_text = wide_to_utf8(replacement.c_str());
    const bool edited = edit_handle->call_edit_section_param(
        &context, [](void* param, EDIT_SECTION* edit) {
            auto* context = static_cast<CharacterFontWriteContext*>(param);
            OBJECT_HANDLE object = edit->get_focus_object();
            if (!object || object != context->expected_object) {
                context->stale = true;
                return;
            }
            LPCSTR current = edit->get_object_item_value(object, L"テキスト", L"テキスト");
            if (!current || context->expected_text != current) {
                context->stale = true;
                return;
            }
            context->written = edit->set_object_item_value(
                object, L"テキスト", L"テキスト", context->replacement_text.c_str());
        });
    if (!edited) {
        MessageBoxW(owner, L"現在はテキストを編集できません。", L"フォント変更",
                    MB_OK | MB_ICONERROR);
        return false;
    }
    if (context.stale) {
        MessageBoxW(owner, L"対象テキストが変更されたため、フォント変更を中止しました。",
                    L"フォント変更", MB_OK | MB_ICONINFORMATION);
        return false;
    }
    if (!context.written) {
        MessageBoxW(owner, L"テキストへフォント設定を反映できませんでした。",
                    L"フォント変更", MB_OK | MB_ICONERROR);
        return false;
    }
    g_ui_source_signature.clear();
    g_ui_frame = -1;
    post_host_refresh();
    return true;
}

static std::vector<std::wstring> enumerate_text_fonts() {
    std::vector<std::wstring> fonts;
    if (!edit_handle) return fonts;
    edit_handle->enum_font_name(
        &fonts, [](void* param, LPCWSTR name) {
            if (name && *name) static_cast<std::vector<std::wstring>*>(param)->emplace_back(name);
        });
    std::sort(fonts.begin(), fonts.end(), [](const std::wstring& left,
                                             const std::wstring& right) {
        const int compared = CompareStringOrdinal(left.c_str(), -1, right.c_str(), -1, TRUE);
        if (compared == CSTR_EQUAL) return left < right;
        return compared == CSTR_LESS_THAN;
    });
    fonts.erase(std::unique(fonts.begin(), fonts.end(), [](const std::wstring& left,
                                                           const std::wstring& right) {
        return same_font_name(left, right);
    }), fonts.end());
    return fonts;
}

static void show_selected_character_menu(HWND hwnd, int character_index, POINT screen) {
    if (character_index < 0 || character_index >= (int)g_chars.size() ||
        character_index != g_selected) return;

    CharacterFontSource source;
    CharacterGlyphLocation location;
    const bool can_change_font = read_character_font_source(&source) &&
        locate_character_glyph(source.text, character_index, source.base_font, &location);

    HMENU menu = CreatePopupMenu();
    if (!menu) return;
    AppendMenuW(menu, MF_STRING | (can_change_font ? MF_ENABLED : MF_GRAYED),
                IDM_CHARACTER_FONT_CHANGE, L"フォント変更");
    AppendMenuW(menu, MF_STRING, IDM_CHARACTER_HAND_DRAW, L"文字装飾(手書き)");

    SetForegroundWindow(hwnd);
    const UINT command = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON,
                                        screen.x, screen.y, 0, hwnd, nullptr);
    DestroyMenu(menu);
    if (command == IDM_CHARACTER_HAND_DRAW) {
        show_hand_editor(hwnd);
    } else if (can_change_font && command == IDM_CHARACTER_FONT_CHANGE) {
        show_font_editor(hwnd, character_index);
    }
}

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

static UINT g_last_shortcut_keydown = 0;
static ULONGLONG g_last_shortcut_keydown_time = 0;

static void update_host_sync_timer(HWND hwnd, bool visible) {
    if (!hwnd) return;
    if (visible && edit_handle && g_host_hwnd) {
        SetTimer(hwnd, HOST_SYNC_TIMER_ID, HOST_SYNC_INTERVAL_MS, nullptr);
    } else {
        KillTimer(hwnd, HOST_SYNC_TIMER_ID);
    }
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
            CreateWindowExW(0, WC_BUTTONW, L"拡張機能",
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
            disable_plugin_ime(hwnd);
            return 0;
        }
        case WM_SIZE:
            relayout(hwnd);
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;

        case WM_SHOWWINDOW:
            if (wp) {
                reset_host_capture_retry();
                update_host_sync_timer(hwnd, true);
                layout_chars();
                post_host_refresh();
            } else {
                update_host_sync_timer(hwnd, false);
            }
            return 0;

        case WM_APP_REFRESH:
            g_refresh_notify_pending.store(false, std::memory_order_release);
            refresh_from_host();
            return 0;

        case WM_APP_CAPTURE_READY:
            g_capture_notify_pending.store(false, std::memory_order_release);
            layout_chars();
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;

        case WM_TIMER:
            if (wp != HOST_SYNC_TIMER_ID) break;
            // ドラッグ中でなければ定期で再読み込み
            if (g_mode == EditMode::NONE && background_host_work_allowed() &&
                edit_handle &&
                edit_handle->get_edit_state() == EDIT_HANDLE::EDIT_STATE_EDIT) {
                post_host_refresh();
            }
            return 0;

        case WM_PAINT:
            paint_window(hwnd);
            return 0;

        case WM_ERASEBKGND:
            return 1;

        case WM_SETCURSOR:
            if (LOWORD(lp) == HTCLIENT) {
                POINT point = {};
                GetCursorPos(&point);
                ScreenToClient(hwnd, &point);
                HCURSOR cursor = nullptr;
                if (g_mode == EditMode::RESIZE && g_resize_corner >= 0) {
                    cursor = diagonal_cursor_for_corner(g_resize_corner);
                } else if (g_mode == EditMode::ROTATE) {
                    cursor = LoadCursor(nullptr, IDC_SIZEWE);
                } else if (g_mode != EditMode::NONE) {
                    cursor = LoadCursor(nullptr, IDC_SIZEALL);
                } else {
                    cursor = editor_cursor_at(point.x, point.y);
                }
                SetCursor(cursor);
                return TRUE;
            }
            break;

        case WM_GETDLGCODE:
            return DLGC_WANTARROWS | DLGC_WANTCHARS;

        case WM_KEYDOWN:
        case WM_SYSKEYDOWN: {
            const UINT vk = normalize_shortcut_virtual_key(hwnd, (UINT)wp);
            if (execute_shortcut(hwnd, vk)) {
                g_last_shortcut_keydown = vk;
                g_last_shortcut_keydown_time = GetTickCount64();
                return 0;
            }
            break;
        }

        case WM_CHAR:
        case WM_SYSCHAR:
        case WM_IME_CHAR: {
            const UINT vk = shortcut_virtual_key_from_fullwidth(wp);
            const ULONGLONG now = GetTickCount64();
            if (vk && !(vk == g_last_shortcut_keydown &&
                        now - g_last_shortcut_keydown_time <= 250) &&
                execute_shortcut(hwnd, vk)) {
                return 0;
            }
            return 0; // 編集文字の入力先ではないので文字入力は常に破棄
        }

        case WM_SETFOCUS:
            disable_plugin_ime(hwnd);
            return 0;

        case WM_IME_SETCONTEXT:
        case WM_IME_STARTCOMPOSITION:
        case WM_IME_COMPOSITION:
        case WM_IME_ENDCOMPOSITION:
        case WM_IME_NOTIFY:
        case WM_IME_REQUEST:
            return 0;

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
                SetCursor(editor_cursor_at(mx, my));
                return 0;
            }
            if (g_mode == EditMode::RESIZE && g_resize_corner >= 0) {
                SetCursor(diagonal_cursor_for_corner(g_resize_corner));
            } else if (g_mode == EditMode::ROTATE) {
                SetCursor(LoadCursor(nullptr, IDC_SIZEWE));
            } else {
                SetCursor(LoadCursor(nullptr, IDC_SIZEALL));
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
                const float free_sx =
                    (std::max)(0.05f, (std::min)(10.f, lx / (sxk * cellW / 2.f)));
                const float free_sy =
                    (std::max)(0.05f, (std::min)(10.f, ly / (syk * cellH / 2.f)));
                const bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
                const bool control = (GetKeyState(VK_CONTROL) & 0x8000) != 0;

                if (shift && control) {
                    // Shift+Ctrl: 縦横を個別に変更する自由変形。
                    cs.t.sx = free_sx;
                    cs.t.sy = free_sy;
                } else if (shift) {
                    // Shift: 横倍率を固定し、縦だけ変更する。
                    cs.t.sx = g_drag_orig.sx;
                    cs.t.sy = free_sy;
                } else if (control) {
                    // Ctrl: 縦倍率を固定し、横だけ変更する。
                    cs.t.sx = free_sx;
                    cs.t.sy = g_drag_orig.sy;
                } else {
                    // 通常: ドラッグ開始時の縦横比を保ったまま拡大縮小する。
                    const float start_sx =
                        (std::max)(0.05f, (std::min)(10.f, fabsf(g_drag_orig.sx)));
                    const float start_sy =
                        (std::max)(0.05f, (std::min)(10.f, fabsf(g_drag_orig.sy)));
                    const float start_x = sxk * cellW * start_sx / 2.f;
                    const float start_y = syk * cellH * start_sy / 2.f;
                    const float length2 = start_x * start_x + start_y * start_y;
                    float factor = length2 > 0.0001f ?
                        (lx * start_x + ly * start_y) / length2 : 1.f;
                    if (!std::isfinite(factor)) factor = 1.f;
                    const float min_factor =
                        (std::max)(0.05f / start_sx, 0.05f / start_sy);
                    const float max_factor =
                        (std::min)(10.f / start_sx, 10.f / start_sy);
                    factor = (std::max)(min_factor, (std::min)(max_factor, factor));
                    cs.t.sx = start_sx * factor;
                    cs.t.sy = start_sy * factor;
                }
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
                if (reference == g_selected) {
                    show_selected_character_menu(hwnd, reference, screen);
                } else {
                    show_alignment_menu(hwnd, reference, screen);
                }
            }
            return 0;
        }
        case WM_DESTROY:
            KillTimer(hwnd, HOST_SYNC_TIMER_ID);
            g_focus_object.store(nullptr, std::memory_order_release);
            g_focus_layer.store(-1, std::memory_order_release);
            g_refresh_notify_pending.store(false, std::memory_order_release);
            g_hwnd = nullptr;
            g_host_hwnd = nullptr;
            if (g_font_ui) { DeleteObject(g_font_ui); g_font_ui = nullptr; }
            if (g_gdiplus_token) {
                Gdiplus::GdiplusShutdown(g_gdiplus_token);
                g_gdiplus_token = 0;
            }
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}
