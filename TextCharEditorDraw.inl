// 手書き編集・PNG入出力
enum class PngExportRange {
    CHARACTER,
    CHARACTER_MARGIN,
    FULL_TEXT,
    FULL_TEXT_MARGIN
};

static PngExportRange g_png_export_range = PngExportRange::CHARACTER;
static Gdiplus::PointF g_draw_image_points[4] = {};
static bool g_draw_image_transform_valid = false;

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

static bool valid_export_bitmap(const CharBitmap& bitmap) {
    return bitmap.width > 0 && bitmap.height > 0 &&
           bitmap.pixels.size() == (size_t)bitmap.width * (size_t)bitmap.height;
}

static bool copy_gdiplus_bitmap(Gdiplus::Bitmap& image, CharBitmap* bitmap) {
    if (!bitmap || image.GetLastStatus() != Gdiplus::Ok ||
        image.GetWidth() <= 0 || image.GetHeight() <= 0) return false;
    const int width = (int)image.GetWidth();
    const int height = (int)image.GetHeight();
    if (width > 16384 || height > 16384 ||
        (size_t)width > SIZE_MAX / (size_t)height ||
        (size_t)width * (size_t)height > 64u * 1024u * 1024u) return false;
    Gdiplus::BitmapData data = {};
    Gdiplus::Rect rect(0, 0, width, height);
    if (image.LockBits(&rect, Gdiplus::ImageLockModeRead,
                       PixelFormat32bppPARGB, &data) != Gdiplus::Ok) return false;
    bitmap->width = width;
    bitmap->height = height;
    bitmap->pixels.resize((size_t)width * (size_t)height);
    for (int y = 0; y < height; y++) {
        const BYTE* row = (const BYTE*)data.Scan0 + (ptrdiff_t)y * data.Stride;
        memcpy(bitmap->pixels.data() + (size_t)y * (size_t)width,
               row, (size_t)width * sizeof(std::uint32_t));
    }
    image.UnlockBits(&data);
    return true;
}

static bool save_png_bitmap(const std::wstring& path, const CharBitmap& bitmap) {
    if (!valid_export_bitmap(bitmap)) return false;
    Gdiplus::Bitmap image(bitmap.width, bitmap.height, bitmap.width * 4,
                          PixelFormat32bppPARGB,
                          reinterpret_cast<BYTE*>(
                              const_cast<std::uint32_t*>(bitmap.pixels.data())));
    CLSID encoder = {};
    return image.GetLastStatus() == Gdiplus::Ok && get_png_encoder(&encoder) &&
           image.Save(path.c_str(), &encoder, nullptr) == Gdiplus::Ok;
}

static bool build_character_export_bitmap(const CharState& state, bool margin,
                                          CharBitmap* output) {
    if (!output) return false;
    CharBitmap character = decorated_character_bitmap(state);
    if (!valid_export_bitmap(character)) return false;
    if (!margin) {
        *output = std::move(character);
        return true;
    }
    const int width = (int)ceilf((float)character.width * 1.5f);
    const int height = (int)ceilf((float)character.height * 1.5f);
    if (width <= 0 || height <= 0 || width > 16384 || height > 16384 ||
        (size_t)width > SIZE_MAX / (size_t)height ||
        (size_t)width * (size_t)height > 64u * 1024u * 1024u) return false;
    output->width = width;
    output->height = height;
    output->pixels.assign((size_t)width * (size_t)height, 0u);
    const int offset_x = (width - character.width) / 2;
    const int offset_y = (height - character.height) / 2;
    for (int y = 0; y < character.height; y++) {
        memcpy(output->pixels.data() + (size_t)(y + offset_y) * (size_t)width + offset_x,
               character.pixels.data() + (size_t)y * (size_t)character.width,
               (size_t)character.width * sizeof(std::uint32_t));
    }
    return true;
}

static void character_export_corners(const CharState& state, int image_width,
                                     int image_height, Gdiplus::PointF corners[4]) {
    const float center_x = state.base_cx + state.t.x;
    const float center_y = state.base_cy + state.t.y;
    const float half_width = (float)image_width * state.base_sx * state.t.sx * 0.5f;
    const float half_height = (float)image_height * state.base_sy * state.t.sy * 0.5f;
    const float radians = (state.base_rz + state.t.rz) * 3.14159265358979f / 180.f;
    const float cosine = cosf(radians);
    const float sine = sinf(radians);
    const Gdiplus::PointF local[4] = {
        { -half_width, -half_height }, { half_width, -half_height },
        { half_width, half_height }, { -half_width, half_height }
    };
    for (int index = 0; index < 4; index++) {
        corners[index].X = center_x + local[index].X * cosine - local[index].Y * sine;
        corners[index].Y = center_y + local[index].X * sine + local[index].Y * cosine;
    }
}

static COLORREF opposite_character_color(const CharBitmap& bitmap) {
    std::uint64_t alpha_sum = 0;
    std::uint64_t red_sum = 0;
    std::uint64_t green_sum = 0;
    std::uint64_t blue_sum = 0;
    for (std::uint32_t pixel : bitmap.pixels) {
        const unsigned alpha = pixel >> 24;
        if (!alpha) continue;
        const unsigned red = (std::min)(255u,
            (((pixel >> 16) & 255u) * 255u + alpha / 2u) / alpha);
        const unsigned green = (std::min)(255u,
            (((pixel >> 8) & 255u) * 255u + alpha / 2u) / alpha);
        const unsigned blue = (std::min)(255u,
            ((pixel & 255u) * 255u + alpha / 2u) / alpha);
        alpha_sum += alpha;
        red_sum += (std::uint64_t)red * alpha;
        green_sum += (std::uint64_t)green * alpha;
        blue_sum += (std::uint64_t)blue * alpha;
    }
    if (!alpha_sum) {
        return RGB(255 - GetRValue(g_pen_color), 255 - GetGValue(g_pen_color),
                   255 - GetBValue(g_pen_color));
    }
    const BYTE red = (BYTE)(red_sum / alpha_sum);
    const BYTE green = (BYTE)(green_sum / alpha_sum);
    const BYTE blue = (BYTE)(blue_sum / alpha_sum);
    return RGB(255 - red, 255 - green, 255 - blue);
}

static void recolor_character_bitmap(CharBitmap* bitmap, COLORREF color) {
    if (!bitmap) return;
    const unsigned red = GetRValue(color);
    const unsigned green = GetGValue(color);
    const unsigned blue = GetBValue(color);
    for (std::uint32_t& pixel : bitmap->pixels) {
        const unsigned alpha = pixel >> 24;
        if (!alpha) {
            pixel = 0;
            continue;
        }
        pixel = (alpha << 24) |
                (((red * alpha + 127) / 255) << 16) |
                (((green * alpha + 127) / 255) << 8) |
                ((blue * alpha + 127) / 255);
    }
}

struct FullTextExportLayout {
    int width = 0;
    int height = 0;
    float origin_x = 0.f;
    float origin_y = 0.f;
};

static bool world_point_to_character_source(const CharState& character,
                                            const Gdiplus::PointF& world,
                                            Gdiplus::PointF* source) {
    if (!source) return false;
    const float scale_x = character.base_sx * character.t.sx;
    const float scale_y = character.base_sy * character.t.sy;
    if (!std::isfinite(scale_x) || !std::isfinite(scale_y) ||
        fabsf(scale_x) < 0.0001f || fabsf(scale_y) < 0.0001f) return false;
    const float center_x = character.base_cx + character.t.x;
    const float center_y = character.base_cy + character.t.y;
    const float radians = (character.base_rz + character.t.rz) *
                          3.14159265358979f / 180.f;
    const float cosine = cosf(radians);
    const float sine = sinf(radians);
    const float delta_x = world.X - center_x;
    const float delta_y = world.Y - center_y;
    source->X = (delta_x * cosine + delta_y * sine) / scale_x;
    source->Y = (-delta_x * sine + delta_y * cosine) / scale_y;
    return std::isfinite(source->X) && std::isfinite(source->Y);
}

static bool character_export_corners_in_source_space(
        const CharState& source_character, const CharState& character,
        int image_width, int image_height, Gdiplus::PointF corners[4]) {
    Gdiplus::PointF world_corners[4];
    character_export_corners(character, image_width, image_height, world_corners);
    for (int index = 0; index < 4; index++) {
        if (!world_point_to_character_source(source_character, world_corners[index],
                                             &corners[index])) return false;
    }
    return true;
}

static bool make_centered_full_text_layout(float left, float top,
                                           float right, float bottom,
                                           bool margin,
                                           FullTextExportLayout* layout) {
    if (!layout || !(right > left) || !(bottom > top)) return false;
    const float half_width = (std::max)(fabsf(left), fabsf(right));
    const float half_height = (std::max)(fabsf(top), fabsf(bottom));
    const float centered_width = ceilf(half_width * 2.f);
    const float centered_height = ceilf(half_height * 2.f);
    const float output_width = margin ? ceilf(centered_width * 1.5f) : centered_width;
    const float output_height = margin ? ceilf(centered_height * 1.5f) : centered_height;
    if (!std::isfinite(output_width) || !std::isfinite(output_height) ||
        output_width < 1.f || output_height < 1.f ||
        output_width > 16384.f || output_height > 16384.f) return false;

    layout->width = (int)output_width;
    layout->height = (int)output_height;
    layout->origin_x = -output_width * 0.5f;
    layout->origin_y = -output_height * 0.5f;
    return true;
}

static bool build_full_text_export_bitmap(int selected_index, bool margin,
                                          CharBitmap* output) {
    if (!output || selected_index < 0 || selected_index >= (int)g_chars.size() ||
        g_chars.empty()) return false;
    const CharState& source_character = g_chars[(size_t)selected_index];
    float left = 0.f, top = 0.f, right = 0.f, bottom = 0.f;
    bool has_bounds = false;
    for (const CharState& state : g_chars) {
        int width = 0, height = 0;
        if (!state.hasBase || !character_visual_size(state, &width, &height)) return false;
        Gdiplus::PointF corners[4];
        if (!character_export_corners_in_source_space(
                source_character, state, width, height, corners)) return false;
        for (const Gdiplus::PointF& point : corners) {
            if (!has_bounds) {
                left = right = point.X;
                top = bottom = point.Y;
                has_bounds = true;
            } else {
                left = (std::min)(left, point.X);
                top = (std::min)(top, point.Y);
                right = (std::max)(right, point.X);
                bottom = (std::max)(bottom, point.Y);
            }
        }
    }
    FullTextExportLayout layout;
    if (!has_bounds || !make_centered_full_text_layout(
            left, top, right, bottom, margin, &layout)) return false;
    const int width = layout.width;
    const int height = layout.height;
    if (width <= 0 || height <= 0 || width > 16384 || height > 16384 ||
        (size_t)width > SIZE_MAX / (size_t)height ||
        (size_t)width * (size_t)height > 64u * 1024u * 1024u) return false;

    CharBitmap selected = decorated_character_bitmap(g_chars[(size_t)selected_index]);
    if (!valid_export_bitmap(selected)) return false;
    const COLORREF opposite = opposite_character_color(selected);

    Gdiplus::Bitmap canvas(width, height, PixelFormat32bppPARGB);
    if (canvas.GetLastStatus() != Gdiplus::Ok) return false;
    {
        Gdiplus::Graphics graphics(&canvas);
        graphics.SetCompositingMode(Gdiplus::CompositingModeSourceOver);
        graphics.SetCompositingQuality(Gdiplus::CompositingQualityHighQuality);
        graphics.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
        graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
        if (graphics.Clear(Gdiplus::Color(0, 0, 0, 0)) != Gdiplus::Ok) return false;

        for (size_t index = 0; index < g_chars.size(); index++) {
            const CharState& state = g_chars[index];
            CharBitmap character = index == (size_t)selected_index ?
                selected : decorated_character_bitmap(state);
            if (!valid_export_bitmap(character)) return false;
            if (index != (size_t)selected_index) recolor_character_bitmap(&character, opposite);
            Gdiplus::Bitmap image(character.width, character.height, character.width * 4,
                                  PixelFormat32bppPARGB,
                                  reinterpret_cast<BYTE*>(character.pixels.data()));
            if (image.GetLastStatus() != Gdiplus::Ok) return false;
            Gdiplus::PointF corners[4];
            if (!character_export_corners_in_source_space(
                    source_character, state, character.width, character.height,
                    corners)) return false;
            Gdiplus::PointF destination[3] = {
                { corners[0].X - layout.origin_x, corners[0].Y - layout.origin_y },
                { corners[1].X - layout.origin_x, corners[1].Y - layout.origin_y },
                { corners[3].X - layout.origin_x, corners[3].Y - layout.origin_y }
            };
            Gdiplus::ImageAttributes attributes;
            const float alpha = (std::max)(0.f, (std::min)(1.f, state.base_alpha));
            Gdiplus::ColorMatrix matrix = {
                1.f, 0.f, 0.f, 0.f, 0.f,
                0.f, 1.f, 0.f, 0.f, 0.f,
                0.f, 0.f, 1.f, 0.f, 0.f,
                0.f, 0.f, 0.f, alpha, 0.f,
                0.f, 0.f, 0.f, 0.f, 1.f
            };
            attributes.SetColorMatrix(&matrix, Gdiplus::ColorMatrixFlagsDefault,
                                      Gdiplus::ColorAdjustTypeBitmap);
            if (graphics.DrawImage(&image, destination, 3, 0.f, 0.f,
                                   (float)character.width, (float)character.height,
                                   Gdiplus::UnitPixel, &attributes) != Gdiplus::Ok) return false;
        }
    }
    return copy_gdiplus_bitmap(canvas, output);
}

static bool build_hand_export_bitmap(int char_index, PngExportRange range,
                                     CharBitmap* output) {
    if (!output || char_index < 0 || char_index >= (int)g_chars.size()) return false;
    switch (range) {
        case PngExportRange::CHARACTER:
            return build_character_export_bitmap(g_chars[(size_t)char_index], false, output);
        case PngExportRange::CHARACTER_MARGIN:
            return build_character_export_bitmap(g_chars[(size_t)char_index], true, output);
        case PngExportRange::FULL_TEXT:
            return build_full_text_export_bitmap(char_index, false, output);
        case PngExportRange::FULL_TEXT_MARGIN:
            return build_full_text_export_bitmap(char_index, true, output);
    }
    return false;
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
    if (!g_chars[char_index].bitmap || g_chars[char_index].bitmap->width <= 0 ||
        g_chars[char_index].bitmap->height <= 0) {
        MessageBoxW(owner, L"文字画像を取得できませんでした。再実行してください。",
                    L"文字装飾(手書き)", MB_OK | MB_ICONERROR);
        return false;
    }
    CharBitmap bitmap;
    if (!build_hand_export_bitmap(char_index, g_png_export_range, &bitmap)) {
        MessageBoxW(owner,
                    L"書き出し範囲を作成できませんでした。\r\n"
                    L"全文字モードでは、すべての文字画像の取得完了と16384px以内の範囲が必要です。",
                    L"文字装飾(手書き)", MB_OK | MB_ICONERROR);
        return false;
    }
    if (!save_png_bitmap(path, bitmap)) {
        MessageBoxW(owner, L"透過PNGを書き出せませんでした。", L"文字装飾(手書き)",
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
    if (!load_png_bitmap(path, true)) {
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
    if (!point || !g_draw_image_transform_valid) return false;
    const Gdiplus::PointF& origin = g_draw_image_points[0];
    const float axis_x_x = g_draw_image_points[1].X - origin.X;
    const float axis_x_y = g_draw_image_points[1].Y - origin.Y;
    const float axis_y_x = g_draw_image_points[3].X - origin.X;
    const float axis_y_y = g_draw_image_points[3].Y - origin.Y;
    const float determinant = axis_x_x * axis_y_y - axis_x_y * axis_y_x;
    if (!std::isfinite(determinant) || fabsf(determinant) < 0.0001f) return false;

    const float delta_x = (float)x - origin.X;
    const float delta_y = (float)y - origin.Y;
    float source_x = (delta_x * axis_y_y - delta_y * axis_y_x) / determinant;
    float source_y = (axis_x_x * delta_y - axis_x_y * delta_x) / determinant;
    if (!std::isfinite(source_x) || !std::isfinite(source_y)) return false;
    if (!clamp_point &&
        (source_x < 0.f || source_x > 1.f || source_y < 0.f || source_y > 1.f)) {
        return false;
    }
    source_x = (std::max)(0.f, (std::min)(1.f, source_x));
    source_y = (std::max)(0.f, (std::min)(1.f, source_y));
    point->x = source_x;
    point->y = source_y;
    return true;
}

static bool update_draw_preview_geometry(const CharState& state, int image_width,
                                         int image_height, const RECT& client_rect) {
    g_draw_image_rect = Gdiplus::RectF();
    g_draw_image_transform_valid = false;
    if (image_width <= 0 || image_height <= 0) return false;

    Gdiplus::PointF transformed[4];
    character_export_corners(state, image_width, image_height, transformed);
    const float object_center_x = state.base_cx + state.t.x;
    const float object_center_y = state.base_cy + state.t.y;
    float left = transformed[0].X - object_center_x;
    float top = transformed[0].Y - object_center_y;
    float right = left;
    float bottom = top;
    for (int index = 1; index < 4; index++) {
        const float local_x = transformed[index].X - object_center_x;
        const float local_y = transformed[index].Y - object_center_y;
        left = (std::min)(left, local_x);
        top = (std::min)(top, local_y);
        right = (std::max)(right, local_x);
        bottom = (std::max)(bottom, local_y);
    }
    const float bounds_width = right - left;
    const float bounds_height = bottom - top;
    if (!std::isfinite(bounds_width) || !std::isfinite(bounds_height) ||
        bounds_width <= 0.0001f || bounds_height <= 0.0001f) return false;

    const float available_width = (std::max)(1.f,
        (float)(client_rect.right - client_rect.left) - 20.f);
    const float available_height = (std::max)(1.f,
        (float)(client_rect.bottom - client_rect.top) - 94.f);
    const float preview_scale = (std::min)(available_width / bounds_width,
                                            available_height / bounds_height);
    if (!std::isfinite(preview_scale) || preview_scale <= 0.f) return false;

    const float local_center_x = (left + right) * 0.5f;
    const float local_center_y = (top + bottom) * 0.5f;
    const float screen_center_x = ((float)client_rect.left + (float)client_rect.right) * 0.5f;
    const float screen_center_y = (float)client_rect.top + 84.f + available_height * 0.5f;
    for (int index = 0; index < 4; index++) {
        const float local_x = transformed[index].X - object_center_x;
        const float local_y = transformed[index].Y - object_center_y;
        g_draw_image_points[index].X = screen_center_x +
            (local_x - local_center_x) * preview_scale;
        g_draw_image_points[index].Y = screen_center_y +
            (local_y - local_center_y) * preview_scale;
    }

    float screen_left = g_draw_image_points[0].X;
    float screen_top = g_draw_image_points[0].Y;
    float screen_right = screen_left;
    float screen_bottom = screen_top;
    for (int index = 1; index < 4; index++) {
        screen_left = (std::min)(screen_left, g_draw_image_points[index].X);
        screen_top = (std::min)(screen_top, g_draw_image_points[index].Y);
        screen_right = (std::max)(screen_right, g_draw_image_points[index].X);
        screen_bottom = (std::max)(screen_bottom, g_draw_image_points[index].Y);
    }
    g_draw_image_rect = Gdiplus::RectF(screen_left, screen_top,
                                       screen_right - screen_left,
                                       screen_bottom - screen_top);
    const float axis_x_x = g_draw_image_points[1].X - g_draw_image_points[0].X;
    const float axis_x_y = g_draw_image_points[1].Y - g_draw_image_points[0].Y;
    const float axis_y_x = g_draw_image_points[3].X - g_draw_image_points[0].X;
    const float axis_y_y = g_draw_image_points[3].Y - g_draw_image_points[0].Y;
    const float determinant = axis_x_x * axis_y_y - axis_x_y * axis_y_x;
    g_draw_image_transform_valid = std::isfinite(determinant) && fabsf(determinant) >= 0.0001f;
    return g_draw_image_transform_valid;
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
    g_draw_image_transform_valid = false;

    if (g_draw_char_index >= 0 && g_draw_char_index < (int)g_chars.size()) {
        const CharState& state = g_chars[g_draw_char_index];
        CharBitmap display = decorated_character_bitmap(state);
        if (valid_export_bitmap(display) &&
            update_draw_preview_geometry(state, display.width, display.height, rc)) {
            HBRUSH checker[2] = {
                CreateSolidBrush(RGB(245, 245, 245)), CreateSolidBrush(RGB(210, 210, 210))
            };
            const int left = (int)floorf(g_draw_image_rect.X);
            const int top = (int)floorf(g_draw_image_rect.Y);
            const int right = (int)ceilf(g_draw_image_rect.GetRight());
            const int bottom = (int)ceilf(g_draw_image_rect.GetBottom());
            POINT polygon[4];
            for (int index = 0; index < 4; index++) {
                polygon[index].x = (LONG)lroundf(g_draw_image_points[index].X);
                polygon[index].y = (LONG)lroundf(g_draw_image_points[index].Y);
            }
            HRGN image_region = CreatePolygonRgn(polygon, 4, WINDING);
            if (image_region) SelectClipRgn(mem, image_region);
            for (int y = top; y < bottom; y += 12) {
                for (int x = left; x < right; x += 12) {
                    RECT tile = { x, y, (std::min)(x + 12, right), (std::min)(y + 12, bottom) };
                    FillRect(mem, &tile, checker[((x - left) / 12 + (y - top) / 12) & 1]);
                }
            }
            if (image_region) {
                SelectClipRgn(mem, nullptr);
                DeleteObject(image_region);
            }
            DeleteObject(checker[0]);
            DeleteObject(checker[1]);

            Gdiplus::Bitmap image(display.width, display.height,
                                  display.width * 4, PixelFormat32bppPARGB,
                                  reinterpret_cast<BYTE*>(display.pixels.data()));
            Gdiplus::Graphics graphics(mem);
            graphics.SetInterpolationMode(Gdiplus::InterpolationModeNearestNeighbor);
            graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
            Gdiplus::PointF destination[3] = {
                g_draw_image_points[0], g_draw_image_points[1], g_draw_image_points[3]
            };
            graphics.DrawImage(&image, destination, 3, 0.f, 0.f,
                               (float)display.width, (float)display.height,
                               Gdiplus::UnitPixel);
            Gdiplus::Pen border(Gdiplus::Color(255, 80, 80, 80), 1.f);
            graphics.DrawPolygon(&border, g_draw_image_points, 4);
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
            HWND export_range = CreateWindowExW(
                0, WC_COMBOBOXW, L"",
                WS_VISIBLE | WS_CHILD | WS_TABSTOP | WS_VSCROLL | CBS_DROPDOWNLIST,
                8, 42, 218, 200, hwnd, (HMENU)(INT_PTR)IDC_DRAW_EXPORT_RANGE,
                GetModuleHandleW(nullptr), nullptr);
            HWND export_png = CreateWindowExW(0, WC_BUTTONW, L"PNG書き出し",
                                              WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
                                              230, 42, 96, 28, hwnd,
                                              (HMENU)(INT_PTR)IDC_DRAW_EXPORT,
                                              GetModuleHandleW(nullptr), nullptr);
            HWND import_png = CreateWindowExW(0, WC_BUTTONW, L"PNG読み込み",
                                              WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
                                              330, 42, 96, 28, hwnd,
                                              (HMENU)(INT_PTR)IDC_DRAW_IMPORT,
                                              GetModuleHandleW(nullptr), nullptr);
            for (HWND control : { pen, eraser, color, eyedropper, width_label, width,
                                  undo, redo, clear, export_range, export_png, import_png }) {
                SendMessageW(control, WM_SETFONT, (WPARAM)g_font_ui, TRUE);
            }
            for (LPCWSTR label : {
                    L"文字の大きさ", L"文字の大きさ+50%余白",
                    L"全テキスト範囲", L"全テキスト範囲+50%余白" }) {
                SendMessageW(export_range, CB_ADDSTRING, 0, (LPARAM)label);
            }
            SendMessageW(export_range, CB_SETCURSEL, (WPARAM)g_png_export_range, 0);
            SendMessageW(width, TBM_SETRANGE, TRUE, MAKELPARAM(5, 200));
            SendMessageW(width, TBM_SETPOS, TRUE, (LPARAM)lroundf(g_pen_width * 1000.f));
            CheckRadioButton(hwnd, IDC_DRAW_PEN, IDC_DRAW_ERASER,
                             g_pen_eraser ? IDC_DRAW_ERASER : IDC_DRAW_PEN);
            disable_plugin_ime(hwnd);
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
                case IDC_DRAW_EXPORT_RANGE:
                    if (HIWORD(wp) == CBN_SELCHANGE) {
                        const LRESULT selection = SendDlgItemMessageW(
                            hwnd, IDC_DRAW_EXPORT_RANGE, CB_GETCURSEL, 0, 0);
                        if (selection >= 0 && selection <= 3) {
                            g_png_export_range = (PngExportRange)selection;
                        }
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
            {
                StrokePoint hover_point;
                if (draw_mouse_point(GET_X_LPARAM(lp), GET_Y_LPARAM(lp), false,
                                     &hover_point)) {
                    SetCursor(LoadCursor(nullptr, IDC_CROSS));
                    return 0;
                }
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
        case WM_SYSKEYDOWN: {
            const UINT key = normalize_shortcut_virtual_key(hwnd, (UINT)wp);
            if (!g_hand_drawing && key == L'Z' && (GetKeyState(VK_CONTROL) & 0x8000)) {
                undo_hand_edit();
                return 0;
            }
            if (!g_hand_drawing && key == L'Y' && (GetKeyState(VK_CONTROL) & 0x8000)) {
                redo_hand_edit();
                return 0;
            }
            if (key == VK_ESCAPE) {
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
        }
        case WM_CHAR:
        case WM_SYSCHAR:
        case WM_IME_CHAR: {
            const UINT key = shortcut_virtual_key_from_fullwidth(wp);
            if (!g_hand_drawing && key == L'Z' && (GetKeyState(VK_CONTROL) & 0x8000)) {
                undo_hand_edit();
            } else if (!g_hand_drawing && key == L'Y' &&
                       (GetKeyState(VK_CONTROL) & 0x8000)) {
                redo_hand_edit();
            }
            return 0;
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
