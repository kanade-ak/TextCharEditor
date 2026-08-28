// 選択文字のフォント変更・一覧表示・履歴管理

struct TextFontEntry {
    std::wstring name;
    std::wstring folded_name;
    bool visible = true;
    bool windows_standard = false;
};

struct TextFontHistoryEntry {
    std::wstring name;
    bool pinned = false;
};

static std::vector<TextFontEntry> g_text_fonts;
static std::vector<TextFontHistoryEntry> g_text_font_history;
static bool g_text_fonts_loaded = false;
static bool g_text_font_history_loaded = false;
static bool g_text_font_list_refreshing = false;
static bool g_text_font_select_list_dirty = true;
static bool g_text_font_manage_list_dirty = true;
static bool g_text_font_history_list_dirty = true;
static bool g_text_font_ui_settings_loaded = false;
static bool g_text_font_preview_mode = false;
static int g_text_font_list_scale = 100;
static HFONT g_text_font_list_font = nullptr;
static std::vector<HFONT> g_text_font_preview_fonts;
static std::wstring g_text_font_coverage_glyph;
static std::vector<bool> g_text_font_coverage;
static OBJECT_HANDLE g_text_font_target_object = nullptr;
static int g_text_font_target_character = -1;
static bool g_text_font_target_valid = false;
static std::wstring g_text_font_target_glyph;
static std::wstring g_text_font_target_display_glyph;
static std::wstring g_text_font_target_base;
static std::wstring g_text_font_target_current;
static std::wstring g_text_font_chosen;

static constexpr LPCWSTR FONT_VISIBILITY_SECTION = L"FontVisibility";
static constexpr LPCWSTR FONT_HISTORY_SECTION = L"FontHistory";
static constexpr LPCWSTR FONT_UI_SECTION = L"FontUI";
static constexpr size_t FONT_HISTORY_UNPINNED_LIMIT = 20;
static constexpr UINT_PTR TEXT_FONT_SEARCH_TIMER = 1;
static constexpr UINT TEXT_FONT_SEARCH_DELAY_MS = 120;

// 接頭辞ではなく完全一致にして、Arial Nova等の追加フォントを誤分類しないよ～ん、多分
static bool is_windows_standard_font(const std::wstring& name) {
    static constexpr LPCWSTR standard_families[] = {
        L"Arial", L"Arial Black",
        L"Bahnschrift", L"Bahnschrift Condensed", L"Bahnschrift Light",
        L"Bahnschrift Light Condensed", L"Bahnschrift Light SemiCondensed",
        L"Bahnschrift SemiBold", L"Bahnschrift SemiBold Condensed",
        L"Bahnschrift SemiBold SemiConden", L"Bahnschrift SemiCondensed",
        L"Bahnschrift SemiLight", L"Bahnschrift SemiLight Condensed",
        L"Bahnschrift SemiLight SemiConde",
        L"BIZ UDGothic", L"BIZ UDMincho Medium", L"BIZ UDPGothic",
        L"BIZ UDPMincho Medium", L"Calibri", L"Calibri Light", L"Cambria",
        L"Cambria Math", L"Candara", L"Candara Light", L"Comic Sans MS",
        L"Consolas", L"Constantia", L"Corbel", L"Corbel Light", L"Courier",
        L"Courier New", L"Ebrima", L"FixedSys", L"Franklin Gothic Medium",
        L"Gabriola", L"Gadugi", L"Georgia", L"Impact", L"Ink Free",
        L"Javanese Text", L"Leelawadee UI", L"Leelawadee UI Semilight",
        L"Lucida Console", L"Lucida Sans Unicode", L"Malgun Gothic",
        L"Malgun Gothic Semilight", L"Meiryo", L"Meiryo UI",
        L"Microsoft Himalaya", L"Microsoft JhengHei", L"Microsoft JhengHei Light",
        L"Microsoft JhengHei UI", L"Microsoft JhengHei UI Light",
        L"Microsoft New Tai Lue", L"Microsoft PhagsPa", L"Microsoft Sans Serif",
        L"Microsoft Tai Le", L"Microsoft YaHei", L"Microsoft YaHei Light",
        L"Microsoft YaHei UI", L"Microsoft YaHei UI Light", L"Microsoft Yi Baiti",
        L"MingLiU_HKSCS-ExtB", L"MingLiU_MSCS-ExtB", L"MingLiU-ExtB",
        L"Modern", L"Mongolian Baiti", L"MS Gothic", L"MS Mincho",
        L"MS PGothic", L"MS PMincho", L"MS Sans Serif", L"MS Serif",
        L"MS UI Gothic", L"MV Boli", L"Myanmar Text", L"Nirmala Text",
        L"Nirmala Text Semilight", L"Nirmala UI", L"Nirmala UI Semilight",
        L"Noto Sans JP", L"Noto Sans JP Black", L"Noto Sans JP DemiLight",
        L"Noto Sans JP ExtraBold", L"Noto Sans JP ExtraLight", L"Noto Sans JP Light",
        L"Noto Sans JP Medium", L"Noto Sans JP SemiBold", L"Noto Sans JP Thin",
        L"Noto Serif JP", L"Noto Serif JP Black", L"Noto Serif JP ExtraLight",
        L"Noto Serif JP Light", L"Noto Serif JP Medium", L"Noto Serif JP SemiBold",
        L"NSimSun", L"Palatino Linotype", L"PMingLiU-ExtB", L"Roman",
        L"Sans Serif Collection", L"Script", L"Segoe Fluent Icons",
        L"Segoe MDL2 Assets", L"Segoe Print", L"Segoe Script", L"Segoe UI",
        L"Segoe UI Black", L"Segoe UI Emoji", L"Segoe UI Historic",
        L"Segoe UI Light", L"Segoe UI Semibold", L"Segoe UI Semilight",
        L"Segoe UI Symbol", L"Segoe UI Variable Display",
        L"Segoe UI Variable Display Light", L"Segoe UI Variable Display Semib",
        L"Segoe UI Variable Display Semil", L"Segoe UI Variable Small",
        L"Segoe UI Variable Small Light", L"Segoe UI Variable Small Semibol",
        L"Segoe UI Variable Small Semilig", L"Segoe UI Variable Text",
        L"Segoe UI Variable Text Light", L"Segoe UI Variable Text Semibold",
        L"Segoe UI Variable Text Semiligh", L"SimSun", L"SimSun-ExtB",
        L"SimSun-ExtG", L"Sitka Banner", L"Sitka Banner Semibold",
        L"Sitka Display", L"Sitka Display Semibold", L"Sitka Heading",
        L"Sitka Heading Semibold", L"Sitka Small", L"Sitka Small Semibold",
        L"Sitka Subheading", L"Sitka Subheading Semibold", L"Sitka Text",
        L"Sitka Text Semibold", L"Small Fonts", L"Sylfaen", L"Symbol",
        L"System", L"Tahoma", L"Terminal", L"Times New Roman", L"Trebuchet MS",
        L"Verdana", L"Webdings", L"Wingdings", L"Yu Gothic", L"Yu Gothic Light",
        L"Yu Gothic Medium", L"Yu Gothic UI", L"Yu Gothic UI Light",
        L"Yu Gothic UI Semibold", L"Yu Gothic UI Semilight", L"Yu Mincho",
        L"Yu Mincho Demibold", L"Yu Mincho Light",
        // 同じファイルを日本語環境で列挙した場合の名称
        L"BIZ UDゴシック", L"BIZ UDPゴシック",
        L"BIZ UD明朝 Medium", L"BIZ UDP明朝 Medium",
        L"ＭＳ ゴシック", L"ＭＳ Ｐゴシック", L"ＭＳ ＵＩゴシック",
        L"ＭＳ 明朝", L"ＭＳ Ｐ明朝", L"メイリオ", L"メイリオ UI",
        L"游ゴシック", L"游ゴシック Light", L"游ゴシック Medium",
        L"游ゴシック UI", L"游ゴシック UI Light", L"游ゴシック UI Semibold",
        L"游ゴシック UI Semilight", L"游明朝", L"游明朝 Demibold", L"游明朝 Light"
    };
    return std::any_of(std::begin(standard_families), std::end(standard_families),
                       [&](LPCWSTR family) {
                           return CompareStringOrdinal(name.c_str(), -1, family, -1, TRUE) ==
                                  CSTR_EQUAL;
                       });
}

static std::wstring text_font_profile_key(const std::wstring& name) {
    const std::string encoded = hex_encode(wide_to_utf8(name.c_str()));
    return std::wstring(encoded.begin(), encoded.end());
}

static std::wstring fold_text_font_name(std::wstring name) {
    std::transform(name.begin(), name.end(), name.begin(),
                   [](wchar_t value) { return (wchar_t)towlower(value); });
    return name;
}

static void save_text_font_visibility(const TextFontEntry& font) {
    if (g_settings_path.empty()) return;
    const std::wstring key = text_font_profile_key(font.name);
    WritePrivateProfileStringW(FONT_VISIBILITY_SECTION, key.c_str(),
                               font.visible ? nullptr : L"0", g_settings_path.c_str());
}

static std::vector<std::wstring> load_hidden_text_font_keys() {
    std::vector<std::wstring> hidden;
    if (g_settings_path.empty()) return hidden;
    std::vector<wchar_t> buffer(4096);
    DWORD length = 0;
    for (;;) {
        length = GetPrivateProfileSectionW(FONT_VISIBILITY_SECTION, buffer.data(),
                                           (DWORD)buffer.size(), g_settings_path.c_str());
        if (length + 2 < buffer.size() || buffer.size() >= 1024 * 1024) break;
        buffer.resize(buffer.size() * 2);
    }
    const wchar_t* entry = buffer.data();
    const wchar_t* end = buffer.data() + length;
    while (entry < end && *entry) {
        const size_t entry_length = wcslen(entry);
        const wchar_t* equals = wcschr(entry, L'=');
        if (equals && equals < entry + entry_length && equals[1] == L'0') {
            hidden.emplace_back(entry, equals);
        }
        entry += entry_length + 1;
    }
    std::sort(hidden.begin(), hidden.end());
    return hidden;
}

static void save_all_text_font_visibility() {
    if (g_settings_path.empty()) return;
    std::vector<wchar_t> section;
    for (const TextFontEntry& font : g_text_fonts) {
        if (font.visible) continue;
        const std::wstring value = text_font_profile_key(font.name) + L"=0";
        section.insert(section.end(), value.begin(), value.end());
        section.push_back(L'\0');
    }
    if (section.empty()) {
        WritePrivateProfileStringW(FONT_VISIBILITY_SECTION, nullptr, nullptr,
                                   g_settings_path.c_str());
        return;
    }
    section.push_back(L'\0');
    WritePrivateProfileSectionW(FONT_VISIBILITY_SECTION, section.data(),
                                g_settings_path.c_str());
}

static void load_text_font_ui_settings() {
    if (g_text_font_ui_settings_loaded) return;
    g_text_font_ui_settings_loaded = true;
    if (g_settings_path.empty()) return;
    g_text_font_preview_mode = GetPrivateProfileIntW(
        FONT_UI_SECTION, L"Preview", 0, g_settings_path.c_str()) != 0;
    g_text_font_list_scale = (std::max)(60, (std::min)(200,
        (int)GetPrivateProfileIntW(FONT_UI_SECTION, L"Scale", 100,
                                   g_settings_path.c_str())));
}

static void save_text_font_ui_setting(LPCWSTR key, int value) {
    if (g_settings_path.empty()) return;
    const std::wstring text = std::to_wstring(value);
    WritePrivateProfileStringW(FONT_UI_SECTION, key, text.c_str(),
                               g_settings_path.c_str());
}

static void clear_text_font_preview_fonts() {
    for (HFONT font : g_text_font_preview_fonts) {
        if (font) DeleteObject(font);
    }
    g_text_font_preview_fonts.assign(g_text_fonts.size(), nullptr);
}

static HFONT create_scaled_text_font(LPCWSTR face_name = nullptr) {
    LOGFONTW description = {};
    HFONT source = g_font_ui ? g_font_ui :
        (HFONT)GetStockObject(DEFAULT_GUI_FONT);
    if (!source || GetObjectW(source, sizeof(description), &description) !=
                       sizeof(description)) return nullptr;
    description.lfHeight = MulDiv(description.lfHeight, g_text_font_list_scale, 100);
    if (!description.lfHeight) description.lfHeight = -1;
    description.lfQuality = CLEARTYPE_QUALITY;
    if (face_name && *face_name) {
        wcsncpy_s(description.lfFaceName, face_name, _TRUNCATE);
        description.lfCharSet = DEFAULT_CHARSET;
    }
    return CreateFontIndirectW(&description);
}

static HFONT text_font_preview_font(size_t font_index) {
    if (font_index >= g_text_fonts.size()) return g_text_font_list_font;
    if (g_text_font_preview_fonts.size() != g_text_fonts.size()) {
        clear_text_font_preview_fonts();
    }
    HFONT& cached = g_text_font_preview_fonts[font_index];
    if (!cached) cached = create_scaled_text_font(g_text_fonts[font_index].name.c_str());
    return cached ? cached : g_text_font_list_font;
}

static void refresh_text_font_zoom_controls(HWND hwnd) {
    const std::wstring label = std::to_wstring(g_text_font_list_scale) + L"%";
    SetDlgItemTextW(hwnd, IDC_FONT_ZOOM_LABEL, label.c_str());
    EnableWindow(GetDlgItem(hwnd, IDC_FONT_ZOOM_OUT), g_text_font_list_scale > 60);
    EnableWindow(GetDlgItem(hwnd, IDC_FONT_ZOOM_IN), g_text_font_list_scale < 200);
}

static void apply_text_font_list_scale(HWND hwnd) {
    HFONT next = create_scaled_text_font();
    if (!next) return;
    HFONT previous = g_text_font_list_font;
    g_text_font_list_font = next;
    for (int control : { IDC_FONT_SELECT_LIST, IDC_FONT_MANAGE_LIST,
                         IDC_FONT_HISTORY_LIST }) {
        HWND list = GetDlgItem(hwnd, control);
        if (list) SendMessageW(list, WM_SETFONT, (WPARAM)g_text_font_list_font, TRUE);
    }
    if (previous) DeleteObject(previous);
    clear_text_font_preview_fonts();
    refresh_text_font_zoom_controls(hwnd);
}

static void change_text_font_list_scale(HWND hwnd, int difference) {
    const int next = (std::max)(60, (std::min)(200,
        g_text_font_list_scale + difference));
    if (next == g_text_font_list_scale) return;
    g_text_font_list_scale = next;
    save_text_font_ui_setting(L"Scale", g_text_font_list_scale);
    apply_text_font_list_scale(hwnd);
}

static std::wstring read_text_font_profile_value(LPCWSTR section, LPCWSTR key) {
    if (g_settings_path.empty()) return {};
    std::vector<wchar_t> buffer(256);
    for (;;) {
        const DWORD length = GetPrivateProfileStringW(
            section, key, L"", buffer.data(), (DWORD)buffer.size(), g_settings_path.c_str());
        if (length + 1 < buffer.size() || buffer.size() >= 4096) {
            return std::wstring(buffer.data(), length);
        }
        buffer.resize(buffer.size() * 2);
    }
}

static void trim_text_font_history() {
    size_t unpinned = 0;
    for (auto iterator = g_text_font_history.begin(); iterator != g_text_font_history.end();) {
        if (!iterator->pinned && ++unpinned > FONT_HISTORY_UNPINNED_LIMIT) {
            iterator = g_text_font_history.erase(iterator);
        } else {
            ++iterator;
        }
    }
}

static void save_text_font_history() {
    if (g_settings_path.empty()) return;
    WritePrivateProfileStringW(FONT_HISTORY_SECTION, nullptr, nullptr,
                               g_settings_path.c_str());
    const std::wstring count = std::to_wstring(g_text_font_history.size());
    WritePrivateProfileStringW(FONT_HISTORY_SECTION, L"Count", count.c_str(),
                               g_settings_path.c_str());
    for (size_t index = 0; index < g_text_font_history.size(); index++) {
        const std::string encoded = hex_encode(
            wide_to_utf8(g_text_font_history[index].name.c_str()));
        const std::wstring value(encoded.begin(), encoded.end());
        const std::wstring item_key = L"Item" + std::to_wstring(index);
        const std::wstring pinned_key = L"Pinned" + std::to_wstring(index);
        WritePrivateProfileStringW(FONT_HISTORY_SECTION, item_key.c_str(), value.c_str(),
                                   g_settings_path.c_str());
        WritePrivateProfileStringW(FONT_HISTORY_SECTION, pinned_key.c_str(),
                                   g_text_font_history[index].pinned ? L"1" : L"0",
                                   g_settings_path.c_str());
    }
}

static void load_text_font_history() {
    if (g_text_font_history_loaded) return;
    g_text_font_history_loaded = true;
    g_text_font_history.clear();
    if (g_settings_path.empty()) return;
    int count = GetPrivateProfileIntW(FONT_HISTORY_SECTION, L"Count", 0,
                                      g_settings_path.c_str());
    count = (std::max)(0, (std::min)(count, 256));
    for (int index = 0; index < count; index++) {
        const std::wstring item_key = L"Item" + std::to_wstring(index);
        const std::wstring pinned_key = L"Pinned" + std::to_wstring(index);
        const std::wstring encoded = read_text_font_profile_value(
            FONT_HISTORY_SECTION, item_key.c_str());
        if (encoded.empty()) continue;
        const std::string decoded = hex_decode(encoded.data(), encoded.data() + encoded.size());
        const std::wstring name = utf8_to_wide(decoded.c_str());
        if (name.empty()) continue;
        const bool duplicate = std::any_of(
            g_text_font_history.begin(), g_text_font_history.end(),
            [&](const TextFontHistoryEntry& entry) { return same_font_name(entry.name, name); });
        if (duplicate) continue;
        const bool pinned = GetPrivateProfileIntW(
            FONT_HISTORY_SECTION, pinned_key.c_str(), 0, g_settings_path.c_str()) != 0;
        g_text_font_history.push_back({ name, pinned });
    }
    std::stable_sort(g_text_font_history.begin(), g_text_font_history.end(),
                     [](const TextFontHistoryEntry& left,
                        const TextFontHistoryEntry& right) {
                         return left.pinned && !right.pinned;
                     });
    trim_text_font_history();
}

static void load_text_font_entries() {
    if (g_text_fonts_loaded) return;
    g_text_fonts_loaded = true;
    const std::vector<std::wstring> names = enumerate_text_fonts();
    const std::vector<std::wstring> hidden = load_hidden_text_font_keys();
    g_text_fonts.clear();
    g_text_fonts.reserve(names.size());
    for (const std::wstring& name : names) {
        const std::wstring key = text_font_profile_key(name);
        const bool visible = !std::binary_search(hidden.begin(), hidden.end(), key);
        g_text_fonts.push_back({ name, fold_text_font_name(name), visible,
                                 is_windows_standard_font(name) });
    }
    g_text_font_select_list_dirty = true;
    g_text_font_manage_list_dirty = true;
    g_text_font_coverage_glyph.clear();
    g_text_font_coverage.clear();
}

static int find_text_font_entry(const std::wstring& name) {
    const auto iterator = std::lower_bound(
        g_text_fonts.begin(), g_text_fonts.end(), name,
        [](const TextFontEntry& font, const std::wstring& value) {
            return CompareStringOrdinal(font.name.c_str(), -1, value.c_str(), -1, TRUE) ==
                   CSTR_LESS_THAN;
        });
    return iterator != g_text_fonts.end() && same_font_name(iterator->name, name) ?
        (int)(iterator - g_text_fonts.begin()) : -1;
}

static void record_text_font_history(const std::wstring& name) {
    if (name.empty()) return;
    bool pinned = false;
    for (auto iterator = g_text_font_history.begin(); iterator != g_text_font_history.end();
         ++iterator) {
        if (same_font_name(iterator->name, name)) {
            pinned = iterator->pinned;
            g_text_font_history.erase(iterator);
            break;
        }
    }
    auto insertion = std::find_if(
        g_text_font_history.begin(), g_text_font_history.end(),
        [](const TextFontHistoryEntry& entry) { return !entry.pinned; });
    if (!pinned) {
        g_text_font_history.insert(insertion, { name, false });
    } else {
        g_text_font_history.insert(g_text_font_history.begin(), { name, true });
    }
    trim_text_font_history();
    save_text_font_history();
    g_text_font_history_list_dirty = true;
}

static std::wstring text_font_search_query(HWND hwnd) {
    HWND edit = GetDlgItem(hwnd, IDC_FONT_SEARCH);
    const int length = edit ? GetWindowTextLengthW(edit) : 0;
    if (length <= 0) return {};
    std::wstring query((size_t)length + 1, L'\0');
    GetWindowTextW(edit, query.data(), length + 1);
    query.resize((size_t)length);
    std::transform(query.begin(), query.end(), query.begin(),
                   [](wchar_t value) { return (wchar_t)towlower(value); });
    return query;
}

static bool text_font_matches_query(const std::wstring& folded_name,
                                    const std::wstring& folded_query) {
    return folded_query.empty() || folded_name.find(folded_query) != std::wstring::npos;
}

static int text_font_list_param(HWND list, int row) {
    if (!list || row < 0) return -1;
    LVITEMW item = {};
    item.mask = LVIF_PARAM;
    item.iItem = row;
    return SendMessageW(list, LVM_GETITEMW, 0, (LPARAM)&item) ? (int)item.lParam : -1;
}

static int insert_text_font_list_item(HWND list, LVITEMW* item) {
    return (int)SendMessageW(list, LVM_INSERTITEMW, 0, (LPARAM)item);
}

static void set_text_font_list_item_text(HWND list, int row, int column,
                                         const std::wstring& text) {
    LVITEMW item = {};
    item.iSubItem = column;
    item.pszText = const_cast<LPWSTR>(text.c_str());
    SendMessageW(list, LVM_SETITEMTEXTW, (WPARAM)row, (LPARAM)&item);
}

static std::wstring text_font_name_from_font_list(HWND list, int row) {
    const int index = text_font_list_param(list, row);
    if (index < 0 || index >= (int)g_text_fonts.size()) return {};
    return g_text_fonts[(size_t)index].name;
}

static std::wstring text_font_name_from_history_list(HWND list, int row) {
    const int index = text_font_list_param(list, row);
    if (index < 0 || index >= (int)g_text_font_history.size()) return {};
    return g_text_font_history[(size_t)index].name;
}

static int text_font_preview_index(HWND list, int row, UINT_PTR control_id) {
    if (control_id == IDC_FONT_HISTORY_LIST) {
        const std::wstring name = text_font_name_from_history_list(list, row);
        return name.empty() ? -1 : find_text_font_entry(name);
    }
    const int index = text_font_list_param(list, row);
    return index >= 0 && index < (int)g_text_fonts.size() ? index : -1;
}

static LRESULT draw_text_font_list_preview(NMLVCUSTOMDRAW* draw) {
    if (!draw || !g_text_font_preview_mode) return CDRF_DODEFAULT;
    if (draw->nmcd.dwDrawStage == CDDS_PREPAINT) return CDRF_NOTIFYITEMDRAW;
    if (draw->nmcd.dwDrawStage == CDDS_ITEMPREPAINT) {
        return CDRF_NOTIFYSUBITEMDRAW;
    }
    if (draw->nmcd.dwDrawStage != (CDDS_ITEMPREPAINT | CDDS_SUBITEM)) {
        return CDRF_DODEFAULT;
    }

    const UINT_PTR control_id = draw->nmcd.hdr.idFrom;
    const bool preview_column =
        ((control_id == IDC_FONT_SELECT_LIST || control_id == IDC_FONT_MANAGE_LIST) &&
         draw->iSubItem == 0) ||
        (control_id == IDC_FONT_HISTORY_LIST && draw->iSubItem == 1);
    HFONT font = g_text_font_list_font;
    if (preview_column) {
        const int index = text_font_preview_index(
            draw->nmcd.hdr.hwndFrom, (int)draw->nmcd.dwItemSpec, control_id);
        if (index >= 0) font = text_font_preview_font((size_t)index);
    }
    if (!font) return CDRF_DODEFAULT;
    SelectObject(draw->nmcd.hdc, font);
    return CDRF_NEWFONT;
}

static std::wstring text_font_safe_glyph(const std::wstring& glyph) {
    std::wstring display = glyph;
    for (wchar_t& character : display) {
        if (character == L'\r' || character == L'\n' || character == L'\t') {
            character = L' ';
        }
    }
    if (display.size() > 12) display = display.substr(0, 12) + L"…";
    return display.empty() ? L"?" : display;
}

static bool text_font_ignores_coverage_codepoint(std::uint32_t codepoint) {
    return codepoint == 0x200du || codepoint == 0xfe0eu || codepoint == 0xfe0fu ||
           (codepoint >= 0xe0100u && codepoint <= 0xe01efu);
}

static bool text_font_dc_supports_glyph(HDC dc, const std::wstring& glyph) {
    if (!dc || glyph.empty()) return true;
    bool checked = false;
    for (size_t position = 0; position < glyph.size();) {
        wchar_t units[2] = { glyph[position], L'\0' };
        int unit_count = 1;
        std::uint32_t codepoint = (std::uint32_t)glyph[position++];
        if (codepoint >= 0xd800u && codepoint <= 0xdbffu && position < glyph.size()) {
            const std::uint32_t low = (std::uint32_t)glyph[position];
            if (low >= 0xdc00u && low <= 0xdfffu) {
                units[1] = glyph[position++];
                unit_count = 2;
                codepoint = 0x10000u + ((codepoint - 0xd800u) << 10) +
                            (low - 0xdc00u);
            }
        }
        if (text_font_ignores_coverage_codepoint(codepoint)) continue;
        WORD indices[2] = { 0xffffu, 0xffffu };
        if (GetGlyphIndicesW(dc, units, unit_count, indices,
                             GGI_MARK_NONEXISTING_GLYPHS) == GDI_ERROR) {
            return false;
        }
        bool supported = false;
        for (int index = 0; index < unit_count; index++) {
            if (indices[index] != 0xffffu) supported = true;
        }
        if (!supported) return false;
        checked = true;
    }
    return checked || !glyph.empty();
}

static void refresh_text_font_coverage() {
    if (g_text_font_coverage_glyph == g_text_font_target_display_glyph &&
        g_text_font_coverage.size() == g_text_fonts.size()) return;
    g_text_font_coverage_glyph = g_text_font_target_display_glyph;
    g_text_font_coverage.assign(g_text_fonts.size(), true);
    if (g_text_font_coverage_glyph.empty()) return;

    HDC dc = CreateCompatibleDC(nullptr);
    if (!dc) return;
    for (size_t index = 0; index < g_text_fonts.size(); index++) {
        HFONT font = create_scaled_text_font(g_text_fonts[index].name.c_str());
        if (!font) continue;
        HGDIOBJ previous = SelectObject(dc, font);
        if (previous && previous != HGDI_ERROR) {
            g_text_font_coverage[index] =
                text_font_dc_supports_glyph(dc, g_text_font_coverage_glyph);
            SelectObject(dc, previous);
        }
        DeleteObject(font);
    }
    DeleteDC(dc);
}

static std::wstring text_font_list_label(const std::wstring& font_name) {
    if (!g_text_font_preview_mode) return font_name;
    return font_name + L" : " + text_font_safe_glyph(g_text_font_target_display_glyph);
}

static bool refresh_text_font_target_from_host() {
    CharacterFontSource source;
    CharacterGlyphLocation location;
    if (!read_character_font_source(&source) ||
        source.object != g_text_font_target_object ||
        !locate_character_glyph(source.text, g_text_font_target_character,
                                source.base_font, &location)) {
        g_text_font_target_valid = false;
        return false;
    }
    const std::wstring glyph = source.text.substr(location.glyph_begin,
                                                   location.glyph_end - location.glyph_begin);
    if (glyph != g_text_font_target_glyph) {
        g_text_font_target_valid = false;
        return false;
    }
    g_text_font_target_valid = true;
    g_text_font_target_base = source.base_font;
    g_text_font_target_current = location.current_font;
    return true;
}

static bool set_text_font_target(int character_index) {
    CharacterFontSource source;
    CharacterGlyphLocation location;
    if (!read_character_font_source(&source) ||
        !locate_character_glyph(source.text, character_index, source.base_font, &location)) {
        return false;
    }
    g_text_font_target_object = source.object;
    g_text_font_target_character = character_index;
    g_text_font_target_glyph = source.text.substr(location.glyph_begin,
                                                   location.glyph_end - location.glyph_begin);
    std::wstring display_glyph;
    if (character_index >= 0 && character_index < (int)g_chars.size()) {
        display_glyph = g_chars[(size_t)character_index].glyph;
    } else {
        display_glyph = g_text_font_target_glyph;
    }
    if (g_text_font_preview_mode && display_glyph != g_text_font_target_display_glyph) {
        g_text_font_select_list_dirty = true;
        g_text_font_manage_list_dirty = true;
        g_text_font_history_list_dirty = true;
    }
    if (display_glyph != g_text_font_target_display_glyph) {
        g_text_font_coverage_glyph.clear();
        g_text_font_coverage.clear();
        g_text_font_select_list_dirty = true;
    }
    g_text_font_target_display_glyph = std::move(display_glyph);
    g_text_font_target_base = source.base_font;
    g_text_font_target_current = location.current_font;
    g_text_font_target_valid = true;
    g_text_font_chosen = location.current_font;
    return true;
}

static void refresh_text_font_target_label(HWND hwnd) {
    std::wstring label;
    if (g_text_font_target_valid) {
        label = L"対象: 「" + text_font_safe_glyph(g_text_font_target_display_glyph) +
                L"」    現在: " +
                (g_text_font_target_current.empty() ? L"(未指定)" : g_text_font_target_current) +
                L"    オブジェクト: " +
                (g_text_font_target_base.empty() ? L"(未指定)" : g_text_font_target_base);
    } else {
        label = L"対象テキストが変更されました。文字を選択し直してください。";
    }
    SetDlgItemTextW(hwnd, IDC_FONT_TARGET, label.c_str());
    EnableWindow(GetDlgItem(hwnd, IDC_FONT_APPLY), g_text_font_target_valid);
    EnableWindow(GetDlgItem(hwnd, IDC_FONT_OBJECT_DEFAULT), g_text_font_target_valid);
    std::wstring title = L"フォント変更";
    if (g_text_font_target_valid) {
        title += L" - 「" + text_font_safe_glyph(g_text_font_target_display_glyph) + L"」";
    }
    SetWindowTextW(hwnd, title.c_str());
}

static void begin_text_font_list_update(HWND list) {
    if (list) SendMessageW(list, WM_SETREDRAW, FALSE, 0);
}

static void end_text_font_list_update(HWND list) {
    if (!list) return;
    SendMessageW(list, WM_SETREDRAW, TRUE, 0);
    RedrawWindow(list, nullptr, nullptr,
                 RDW_INVALIDATE | RDW_ERASE | RDW_FRAME | RDW_ALLCHILDREN);
}

static void refresh_text_font_select_list(HWND hwnd) {
    HWND list = GetDlgItem(hwnd, IDC_FONT_SELECT_LIST);
    if (!list) return;
    const std::wstring query = text_font_search_query(hwnd);
    refresh_text_font_coverage();
    g_text_font_list_refreshing = true;
    begin_text_font_list_update(list);
    ListView_DeleteAllItems(list);
    int selected_row = -1;
    int row = 0;
    for (size_t index = 0; index < g_text_fonts.size(); index++) {
        const TextFontEntry& font = g_text_fonts[index];
        if (!font.visible || !g_text_font_coverage[index] ||
            !text_font_matches_query(font.folded_name, query)) continue;
        const std::wstring label = text_font_list_label(font.name);
        LVITEMW item = {};
        item.mask = LVIF_TEXT | LVIF_PARAM;
        item.iItem = row;
        item.pszText = const_cast<LPWSTR>(label.c_str());
        item.lParam = (LPARAM)index;
        const int inserted = insert_text_font_list_item(list, &item);
        if (inserted >= 0) {
            set_text_font_list_item_text(
                list, inserted, 1,
                font.windows_standard ? L"Windows標準" : L"追加フォント");
            if (same_font_name(font.name, g_text_font_target_current)) selected_row = inserted;
            row++;
        }
    }
    if (selected_row >= 0) {
        ListView_SetItemState(list, selected_row, LVIS_SELECTED | LVIS_FOCUSED,
                              LVIS_SELECTED | LVIS_FOCUSED);
        ListView_EnsureVisible(list, selected_row, FALSE);
        g_text_font_chosen = text_font_name_from_font_list(list, selected_row);
    } else {
        g_text_font_chosen.clear();
    }
    g_text_font_select_list_dirty = false;
    end_text_font_list_update(list);
    g_text_font_list_refreshing = false;
}

static void sync_text_font_select_list_selection(HWND hwnd) {
    HWND list = GetDlgItem(hwnd, IDC_FONT_SELECT_LIST);
    if (!list || g_text_font_select_list_dirty) return;
    const int count = ListView_GetItemCount(list);
    int selected_row = -1;
    for (int row = 0; row < count; row++) {
        const int index = text_font_list_param(list, row);
        if (index >= 0 && index < (int)g_text_fonts.size() &&
            same_font_name(g_text_fonts[(size_t)index].name,
                           g_text_font_target_current)) {
            selected_row = row;
            break;
        }
    }
    g_text_font_list_refreshing = true;
    ListView_SetItemState(list, -1, 0, LVIS_SELECTED | LVIS_FOCUSED);
    if (selected_row >= 0) {
        ListView_SetItemState(list, selected_row, LVIS_SELECTED | LVIS_FOCUSED,
                              LVIS_SELECTED | LVIS_FOCUSED);
        ListView_EnsureVisible(list, selected_row, FALSE);
        g_text_font_chosen = text_font_name_from_font_list(list, selected_row);
    } else {
        g_text_font_chosen.clear();
    }
    g_text_font_list_refreshing = false;
}

static void refresh_text_font_manage_list(HWND hwnd) {
    HWND list = GetDlgItem(hwnd, IDC_FONT_MANAGE_LIST);
    if (!list) return;
    const std::wstring query = text_font_search_query(hwnd);
    g_text_font_list_refreshing = true;
    begin_text_font_list_update(list);
    ListView_DeleteAllItems(list);
    int row = 0;
    for (size_t index = 0; index < g_text_fonts.size(); index++) {
        const TextFontEntry& font = g_text_fonts[index];
        if (!text_font_matches_query(font.folded_name, query)) continue;
        const std::wstring label = text_font_list_label(font.name);
        LVITEMW item = {};
        item.mask = LVIF_TEXT | LVIF_PARAM;
        item.iItem = row;
        item.pszText = const_cast<LPWSTR>(label.c_str());
        item.lParam = (LPARAM)index;
        const int inserted = insert_text_font_list_item(list, &item);
        if (inserted >= 0) {
            set_text_font_list_item_text(
                list, inserted, 1,
                font.windows_standard ? L"Windows標準" : L"追加フォント");
            ListView_SetCheckState(list, inserted, font.visible ? TRUE : FALSE);
            row++;
        }
    }
    g_text_font_manage_list_dirty = false;
    end_text_font_list_update(list);
    g_text_font_list_refreshing = false;
}

static void sync_text_font_manage_checks(HWND hwnd) {
    HWND list = GetDlgItem(hwnd, IDC_FONT_MANAGE_LIST);
    if (!list || g_text_font_manage_list_dirty) return;
    g_text_font_list_refreshing = true;
    begin_text_font_list_update(list);
    const int count = ListView_GetItemCount(list);
    for (int row = 0; row < count; row++) {
        const int index = text_font_list_param(list, row);
        if (index >= 0 && index < (int)g_text_fonts.size()) {
            ListView_SetCheckState(list, row,
                                   g_text_fonts[(size_t)index].visible ? TRUE : FALSE);
        }
    }
    end_text_font_list_update(list);
    g_text_font_list_refreshing = false;
}

static void refresh_text_font_history_list(HWND hwnd) {
    HWND list = GetDlgItem(hwnd, IDC_FONT_HISTORY_LIST);
    if (!list) return;
    g_text_font_list_refreshing = true;
    begin_text_font_list_update(list);
    ListView_DeleteAllItems(list);
    for (size_t index = 0; index < g_text_font_history.size(); index++) {
        const TextFontHistoryEntry& history = g_text_font_history[index];
        const bool installed = find_text_font_entry(history.name) >= 0;
        std::wstring font_label = text_font_list_label(history.name);
        if (!installed) font_label += L" (未インストール)";
        LVITEMW item = {};
        item.mask = LVIF_TEXT | LVIF_PARAM;
        item.iItem = (int)index;
        item.pszText = const_cast<LPWSTR>(history.pinned ? L"固定" : L"");
        item.lParam = (LPARAM)index;
        const int inserted = insert_text_font_list_item(list, &item);
        if (inserted >= 0) {
            set_text_font_list_item_text(list, inserted, 1, font_label);
        }
    }
    g_text_font_history_list_dirty = false;
    end_text_font_list_update(list);
    g_text_font_list_refreshing = false;
}

static int text_font_aggregate_state(bool windows_standard_only) {
    size_t matched = 0;
    size_t visible = 0;
    for (const TextFontEntry& font : g_text_fonts) {
        if (windows_standard_only && !font.windows_standard) continue;
        matched++;
        if (font.visible) visible++;
    }
    if (!matched || !visible) return BST_UNCHECKED;
    if (visible == matched) return BST_CHECKED;
    return BST_INDETERMINATE;
}

static void refresh_text_font_batch_checks(HWND hwnd) {
    CheckDlgButton(hwnd, IDC_FONT_SELECT_ALL, text_font_aggregate_state(false));
    CheckDlgButton(hwnd, IDC_FONT_SELECT_WIN11, text_font_aggregate_state(true));
}

static void refresh_text_font_tab(HWND hwnd) {
    const int tab = TabCtrl_GetCurSel(GetDlgItem(hwnd, IDC_FONT_TAB));
    const bool manage = tab == 1;
    ShowWindow(GetDlgItem(hwnd, IDC_FONT_SELECT_LIST), manage ? SW_HIDE : SW_SHOW);
    ShowWindow(GetDlgItem(hwnd, IDC_FONT_MANAGE_LIST), manage ? SW_SHOW : SW_HIDE);
    ShowWindow(GetDlgItem(hwnd, IDC_FONT_SELECT_ALL), manage ? SW_SHOW : SW_HIDE);
    ShowWindow(GetDlgItem(hwnd, IDC_FONT_SELECT_WIN11), manage ? SW_SHOW : SW_HIDE);
}

static bool text_font_manage_tab_active(HWND hwnd) {
    return TabCtrl_GetCurSel(GetDlgItem(hwnd, IDC_FONT_TAB)) == 1;
}

static void refresh_text_font_active_list(HWND hwnd) {
    if (text_font_manage_tab_active(hwnd)) {
        if (g_text_font_manage_list_dirty) refresh_text_font_manage_list(hwnd);
    } else {
        if (g_text_font_select_list_dirty) refresh_text_font_select_list(hwnd);
    }
}

static void refresh_text_font_window(HWND hwnd) {
    refresh_text_font_target_from_host();
    refresh_text_font_target_label(hwnd);
    refresh_text_font_tab(hwnd);
    refresh_text_font_active_list(hwnd);
    if (!text_font_manage_tab_active(hwnd)) sync_text_font_select_list_selection(hwnd);
    if (g_text_font_history_list_dirty) refresh_text_font_history_list(hwnd);
    refresh_text_font_batch_checks(hwnd);
}

static void set_text_font_preview_mode(HWND hwnd, bool enabled) {
    if (g_text_font_preview_mode == enabled) return;
    g_text_font_preview_mode = enabled;
    save_text_font_ui_setting(L"Preview", enabled ? 1 : 0);
    g_text_font_select_list_dirty = true;
    g_text_font_manage_list_dirty = true;
    g_text_font_history_list_dirty = true;
    refresh_text_font_active_list(hwnd);
    refresh_text_font_history_list(hwnd);
    for (int control : { IDC_FONT_SELECT_LIST, IDC_FONT_MANAGE_LIST,
                         IDC_FONT_HISTORY_LIST }) {
        if (HWND list = GetDlgItem(hwnd, control)) InvalidateRect(list, nullptr, TRUE);
    }
}

static bool apply_text_font(HWND hwnd, const std::wstring& name) {
    const bool object_default = same_font_name(name, g_text_font_target_base);
    if (!object_default && (name.empty() || find_text_font_entry(name) < 0)) {
        MessageBoxW(hwnd, L"このフォントは現在インストールされていません。",
                    L"フォント変更", MB_OK | MB_ICONINFORMATION);
        return false;
    }

    CharacterFontSource source;
    CharacterGlyphLocation location;
    if (!read_character_font_source(&source) ||
        source.object != g_text_font_target_object ||
        !locate_character_glyph(source.text, g_text_font_target_character,
                                source.base_font, &location) ||
        source.text.substr(location.glyph_begin, location.glyph_end - location.glyph_begin) !=
            g_text_font_target_glyph) {
        g_text_font_target_valid = false;
        refresh_text_font_target_label(hwnd);
        MessageBoxW(hwnd, L"対象テキストが変更されました。文字を選択し直してください。",
                    L"フォント変更", MB_OK | MB_ICONINFORMATION);
        return false;
    }

    if (!same_font_name(location.current_font, name) &&
        !change_character_font(hwnd, g_text_font_target_character, source, name)) {
        return false;
    }
    record_text_font_history(name);
    g_text_font_target_current = name;
    refresh_text_font_target_label(hwnd);
    if (!text_font_manage_tab_active(hwnd)) sync_text_font_select_list_selection(hwnd);
    if (g_text_font_history_list_dirty) refresh_text_font_history_list(hwnd);
    return true;
}

static void apply_selected_text_font(HWND hwnd) {
    if (g_text_font_chosen.empty()) {
        MessageBoxW(hwnd, L"適用するフォントを一覧または履歴から選択してください。",
                    L"フォント変更", MB_OK | MB_ICONINFORMATION);
        return;
    }
    apply_text_font(hwnd, g_text_font_chosen);
}

static void set_text_font_visibility_batch(HWND hwnd, bool windows_standard_only) {
    const bool make_visible = text_font_aggregate_state(windows_standard_only) != BST_CHECKED;
    bool changed = false;
    for (TextFontEntry& font : g_text_fonts) {
        if (windows_standard_only && !font.windows_standard) continue;
        if (font.visible == make_visible) continue;
        font.visible = make_visible;
        changed = true;
    }
    if (changed) {
        save_all_text_font_visibility();
        g_text_font_select_list_dirty = true;
        sync_text_font_manage_checks(hwnd);
    }
    refresh_text_font_batch_checks(hwnd);
}

static void choose_text_font_list_row(HWND hwnd, HWND list, int row, bool history) {
    const std::wstring name = history ? text_font_name_from_history_list(list, row) :
                                        text_font_name_from_font_list(list, row);
    if (name.empty()) return;
    g_text_font_chosen = name;
    g_text_font_list_refreshing = true;
    HWND other = GetDlgItem(hwnd, history ? IDC_FONT_SELECT_LIST : IDC_FONT_HISTORY_LIST);
    ListView_SetItemState(other, -1, 0, LVIS_SELECTED | LVIS_FOCUSED);
    g_text_font_list_refreshing = false;
}

static void toggle_text_font_history_pin(HWND hwnd, int history_index) {
    if (history_index < 0 || history_index >= (int)g_text_font_history.size()) return;
    TextFontHistoryEntry entry = g_text_font_history[(size_t)history_index];
    g_text_font_history.erase(g_text_font_history.begin() + history_index);
    entry.pinned = !entry.pinned;
    auto first_unpinned = std::find_if(
        g_text_font_history.begin(), g_text_font_history.end(),
        [](const TextFontHistoryEntry& value) { return !value.pinned; });
    if (entry.pinned) {
        g_text_font_history.insert(g_text_font_history.begin(), std::move(entry));
    } else {
        g_text_font_history.insert(first_unpinned, std::move(entry));
    }
    save_text_font_history();
    g_text_font_history_list_dirty = true;
    refresh_text_font_history_list(hwnd);
}

static void show_text_font_history_menu(HWND hwnd, HWND list, POINT screen) {
    POINT client = screen;
    ScreenToClient(list, &client);
    LVHITTESTINFO hit = {};
    hit.pt = client;
    const int row = ListView_SubItemHitTest(list, &hit);
    const int history_index = text_font_list_param(list, row);
    if (history_index < 0 || history_index >= (int)g_text_font_history.size()) return;
    ListView_SetItemState(list, row, LVIS_SELECTED | LVIS_FOCUSED,
                          LVIS_SELECTED | LVIS_FOCUSED);
    choose_text_font_list_row(hwnd, list, row, true);

    HMENU menu = CreatePopupMenu();
    if (!menu) return;
    AppendMenuW(menu, MF_STRING, IDM_FONT_HISTORY_PIN,
                g_text_font_history[(size_t)history_index].pinned ?
                    L"ピン留め解除" : L"ピン留め");
    SetForegroundWindow(hwnd);
    const UINT command = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON,
                                        screen.x, screen.y, 0, hwnd, nullptr);
    DestroyMenu(menu);
    if (command == IDM_FONT_HISTORY_PIN) {
        toggle_text_font_history_pin(hwnd, history_index);
    }
}

static void insert_text_font_list_columns(HWND list, bool history) {
    LVCOLUMNW column = {};
    column.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
    if (history) {
        column.pszText = const_cast<LPWSTR>(L"状態");
        column.cx = 50;
        SendMessageW(list, LVM_INSERTCOLUMNW, 0, (LPARAM)&column);
        column.iSubItem = 1;
        column.pszText = const_cast<LPWSTR>(L"フォント名");
        column.cx = 200;
        SendMessageW(list, LVM_INSERTCOLUMNW, 1, (LPARAM)&column);
    } else {
        column.pszText = const_cast<LPWSTR>(L"フォント名");
        column.cx = 300;
        SendMessageW(list, LVM_INSERTCOLUMNW, 0, (LPARAM)&column);
        column.iSubItem = 1;
        column.pszText = const_cast<LPWSTR>(L"分類");
        column.cx = 100;
        SendMessageW(list, LVM_INSERTCOLUMNW, 1, (LPARAM)&column);
    }
}

static void layout_text_font_window(HWND hwnd) {
    RECT client;
    GetClientRect(hwnd, &client);
    const int width = (std::max)(1, (int)client.right);
    const int height = (std::max)(1, (int)client.bottom);
    const int margin = 10;
    const int gap = 10;
    const int footer_y = (std::max)(120, height - 42);
    const int history_width = (std::max)(230, (std::min)(320, width / 3));
    const int left_width = (std::max)(350, width - margin * 2 - gap - history_width);
    const int history_x = margin + left_width + gap;
    const int tab_y = 66;
    const int content_height = (std::max)(80, footer_y - tab_y - 8);
    const int toolbar_right = margin + left_width;
    const int zoom_in_x = toolbar_right - 32;
    const int zoom_label_x = zoom_in_x - 50;
    const int zoom_out_x = zoom_label_x - 36;
    const int preview_x = zoom_out_x - 122;
    const int search_x = margin + 56;

    MoveWindow(GetDlgItem(hwnd, IDC_FONT_TARGET), margin, 8,
               (std::max)(1, width - margin * 2), 22, TRUE);
    MoveWindow(GetDlgItem(hwnd, IDC_FONT_SEARCH), search_x, 34,
               (std::max)(48, preview_x - search_x - 8), 24, TRUE);
    MoveWindow(GetDlgItem(hwnd, IDC_FONT_PREVIEW), preview_x, 34, 116, 24, TRUE);
    MoveWindow(GetDlgItem(hwnd, IDC_FONT_ZOOM_OUT), zoom_out_x, 34, 32, 24, TRUE);
    MoveWindow(GetDlgItem(hwnd, IDC_FONT_ZOOM_LABEL), zoom_label_x, 36, 46, 20, TRUE);
    MoveWindow(GetDlgItem(hwnd, IDC_FONT_ZOOM_IN), zoom_in_x, 34, 32, 24, TRUE);
    MoveWindow(GetDlgItem(hwnd, IDC_FONT_TAB), margin, tab_y, left_width,
               content_height, TRUE);

    const int inner_x = margin + 8;
    const int inner_y = tab_y + 31;
    const int inner_width = (std::max)(1, left_width - 16);
    const int inner_height = (std::max)(1, content_height - 39);
    MoveWindow(GetDlgItem(hwnd, IDC_FONT_SELECT_LIST), inner_x, inner_y,
               inner_width, inner_height, TRUE);
    MoveWindow(GetDlgItem(hwnd, IDC_FONT_SELECT_ALL), inner_x, inner_y,
               inner_width, 24, TRUE);
    MoveWindow(GetDlgItem(hwnd, IDC_FONT_SELECT_WIN11), inner_x, inner_y + 26,
               inner_width, 24, TRUE);
    MoveWindow(GetDlgItem(hwnd, IDC_FONT_MANAGE_LIST), inner_x, inner_y + 54,
               inner_width, (std::max)(1, inner_height - 54), TRUE);

    MoveWindow(GetDlgItem(hwnd, IDC_FONT_HISTORY_LABEL), history_x, tab_y,
               history_width, 22, TRUE);
    MoveWindow(GetDlgItem(hwnd, IDC_FONT_HISTORY_LIST), history_x, tab_y + 26,
               history_width, (std::max)(1, content_height - 26), TRUE);

    MoveWindow(GetDlgItem(hwnd, IDC_FONT_APPLY), margin, footer_y, 100, 30, TRUE);
    MoveWindow(GetDlgItem(hwnd, IDC_FONT_OBJECT_DEFAULT), margin + 108, footer_y,
               (std::max)(150, left_width - 108), 30, TRUE);
    MoveWindow(GetDlgItem(hwnd, IDC_FONT_CLOSE), (std::max)(margin, width - 100),
               footer_y, 90, 30, TRUE);

    ListView_SetColumnWidth(GetDlgItem(hwnd, IDC_FONT_SELECT_LIST), 0,
                            (std::max)(120, inner_width - 108));
    ListView_SetColumnWidth(GetDlgItem(hwnd, IDC_FONT_SELECT_LIST), 1, 104);
    ListView_SetColumnWidth(GetDlgItem(hwnd, IDC_FONT_MANAGE_LIST), 0,
                            (std::max)(120, inner_width - 108));
    ListView_SetColumnWidth(GetDlgItem(hwnd, IDC_FONT_MANAGE_LIST), 1, 104);
    ListView_SetColumnWidth(GetDlgItem(hwnd, IDC_FONT_HISTORY_LIST), 0, 48);
    ListView_SetColumnWidth(GetDlgItem(hwnd, IDC_FONT_HISTORY_LIST), 1,
                            (std::max)(100, history_width - 52));
}

static LRESULT CALLBACK font_wnd_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_CREATE: {
            load_text_font_ui_settings();
            HINSTANCE instance = GetModuleHandleW(nullptr);
            HWND target = CreateWindowExW(
                0, WC_STATICW, L"", WS_VISIBLE | WS_CHILD | SS_LEFT | SS_ENDELLIPSIS,
                10, 8, 700, 22, hwnd, (HMENU)(INT_PTR)IDC_FONT_TARGET, instance, nullptr);
            HWND search_label = CreateWindowExW(
                0, WC_STATICW, L"検索", WS_VISIBLE | WS_CHILD | SS_LEFT,
                10, 38, 48, 22, hwnd, nullptr, instance, nullptr);
            HWND search = CreateWindowExW(
                WS_EX_CLIENTEDGE, WC_EDITW, L"",
                WS_VISIBLE | WS_CHILD | WS_TABSTOP | ES_AUTOHSCROLL,
                66, 34, 360, 24, hwnd, (HMENU)(INT_PTR)IDC_FONT_SEARCH,
                instance, nullptr);
            HWND preview = CreateWindowExW(
                0, WC_BUTTONW, L"プレビュー",
                WS_VISIBLE | WS_CHILD | WS_TABSTOP | BS_AUTOCHECKBOX,
                430, 34, 116, 24, hwnd, (HMENU)(INT_PTR)IDC_FONT_PREVIEW,
                instance, nullptr);
            HWND zoom_out = CreateWindowExW(
                0, WC_BUTTONW, L"A−",
                WS_VISIBLE | WS_CHILD | WS_TABSTOP | BS_PUSHBUTTON,
                550, 34, 32, 24, hwnd, (HMENU)(INT_PTR)IDC_FONT_ZOOM_OUT,
                instance, nullptr);
            HWND zoom_label = CreateWindowExW(
                0, WC_STATICW, L"100%",
                WS_VISIBLE | WS_CHILD | SS_CENTER,
                586, 36, 46, 20, hwnd, (HMENU)(INT_PTR)IDC_FONT_ZOOM_LABEL,
                instance, nullptr);
            HWND zoom_in = CreateWindowExW(
                0, WC_BUTTONW, L"A＋",
                WS_VISIBLE | WS_CHILD | WS_TABSTOP | BS_PUSHBUTTON,
                636, 34, 32, 24, hwnd, (HMENU)(INT_PTR)IDC_FONT_ZOOM_IN,
                instance, nullptr);
            HWND tab = CreateWindowExW(
                0, WC_TABCONTROLW, L"", WS_VISIBLE | WS_CHILD | WS_CLIPSIBLINGS,
                10, 66, 500, 420, hwnd, (HMENU)(INT_PTR)IDC_FONT_TAB, instance, nullptr);
            TCITEMW tab_item = {};
            tab_item.mask = TCIF_TEXT;
            tab_item.pszText = const_cast<LPWSTR>(L"フォント選択");
            SendMessageW(tab, TCM_INSERTITEMW, 0, (LPARAM)&tab_item);
            tab_item.pszText = const_cast<LPWSTR>(L"表示/非表示管理");
            SendMessageW(tab, TCM_INSERTITEMW, 1, (LPARAM)&tab_item);

            const DWORD list_style = WS_VISIBLE | WS_CHILD | WS_TABSTOP | WS_VSCROLL |
                                     LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS;
            HWND select_list = CreateWindowExW(
                WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"", list_style,
                18, 98, 484, 376, hwnd, (HMENU)(INT_PTR)IDC_FONT_SELECT_LIST,
                instance, nullptr);
            HWND manage_list = CreateWindowExW(
                WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"", list_style,
                18, 152, 484, 322, hwnd, (HMENU)(INT_PTR)IDC_FONT_MANAGE_LIST,
                instance, nullptr);
            HWND select_all = CreateWindowExW(
                0, WC_BUTTONW, L"全選択（一覧に表示）",
                WS_CHILD | BS_3STATE,
                18, 98, 484, 24, hwnd, (HMENU)(INT_PTR)IDC_FONT_SELECT_ALL,
                instance, nullptr);
            HWND select_win11 = CreateWindowExW(
                0, WC_BUTTONW, L"Windows標準フォントのみON/OFFにする",
                WS_CHILD | BS_3STATE,
                18, 124, 484, 24, hwnd, (HMENU)(INT_PTR)IDC_FONT_SELECT_WIN11,
                instance, nullptr);
            HWND history_label = CreateWindowExW(
                0, WC_STATICW, L"使用したフォント履歴（右クリックでピン留め）",
                WS_VISIBLE | WS_CHILD | SS_LEFT,
                520, 66, 280, 22, hwnd, (HMENU)(INT_PTR)IDC_FONT_HISTORY_LABEL,
                instance, nullptr);
            HWND history_list = CreateWindowExW(
                WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"", list_style,
                520, 92, 280, 394, hwnd, (HMENU)(INT_PTR)IDC_FONT_HISTORY_LIST,
                instance, nullptr);
            HWND apply = CreateWindowExW(
                0, WC_BUTTONW, L"適用", WS_VISIBLE | WS_CHILD | WS_TABSTOP | BS_DEFPUSHBUTTON,
                10, 500, 100, 30, hwnd, (HMENU)(INT_PTR)IDC_FONT_APPLY, instance, nullptr);
            HWND object_default = CreateWindowExW(
                0, WC_BUTTONW, L"オブジェクトのフォントに戻す",
                WS_VISIBLE | WS_CHILD | WS_TABSTOP | BS_PUSHBUTTON,
                118, 500, 260, 30, hwnd,
                (HMENU)(INT_PTR)IDC_FONT_OBJECT_DEFAULT, instance, nullptr);
            HWND close = CreateWindowExW(
                0, WC_BUTTONW, L"閉じる", WS_VISIBLE | WS_CHILD | WS_TABSTOP | BS_PUSHBUTTON,
                710, 500, 90, 30, hwnd, (HMENU)(INT_PTR)IDC_FONT_CLOSE, instance, nullptr);

            for (HWND control : { target, search_label, search, preview, zoom_out,
                                  zoom_label, zoom_in, tab, select_list, manage_list,
                                  select_all, select_win11, history_label, history_list,
                                  apply, object_default, close }) {
                SendMessageW(control, WM_SETFONT, (WPARAM)g_font_ui, TRUE);
            }
            const DWORD common_style = LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER |
                                       LVS_EX_LABELTIP;
            ListView_SetExtendedListViewStyle(select_list, common_style);
            ListView_SetExtendedListViewStyle(manage_list, common_style | LVS_EX_CHECKBOXES);
            ListView_SetExtendedListViewStyle(history_list, common_style);
            insert_text_font_list_columns(select_list, false);
            insert_text_font_list_columns(manage_list, false);
            insert_text_font_list_columns(history_list, true);
            TabCtrl_SetCurSel(tab, 0);
            CheckDlgButton(hwnd, IDC_FONT_PREVIEW,
                           g_text_font_preview_mode ? BST_CHECKED : BST_UNCHECKED);
            apply_text_font_list_scale(hwnd);
            disable_plugin_ime(hwnd);
            refresh_text_font_window(hwnd);
            layout_text_font_window(hwnd);
            return 0;
        }
        case WM_SIZE:
            layout_text_font_window(hwnd);
            return 0;
        case WM_GETMINMAXINFO: {
            auto* info = reinterpret_cast<MINMAXINFO*>(lp);
            info->ptMinTrackSize = { 720, 480 };
            return 0;
        }
        case WM_COMMAND:
            switch (LOWORD(wp)) {
                case IDC_FONT_SEARCH:
                    if (HIWORD(wp) == EN_CHANGE) {
                        g_text_font_select_list_dirty = true;
                        g_text_font_manage_list_dirty = true;
                        KillTimer(hwnd, TEXT_FONT_SEARCH_TIMER);
                        SetTimer(hwnd, TEXT_FONT_SEARCH_TIMER,
                                 TEXT_FONT_SEARCH_DELAY_MS, nullptr);
                    }
                    break;
                case IDC_FONT_SELECT_ALL:
                    if (HIWORD(wp) == BN_CLICKED) set_text_font_visibility_batch(hwnd, false);
                    break;
                case IDC_FONT_SELECT_WIN11:
                    if (HIWORD(wp) == BN_CLICKED) set_text_font_visibility_batch(hwnd, true);
                    break;
                case IDC_FONT_PREVIEW:
                    if (HIWORD(wp) == BN_CLICKED) {
                        set_text_font_preview_mode(
                            hwnd, IsDlgButtonChecked(hwnd, IDC_FONT_PREVIEW) == BST_CHECKED);
                    }
                    break;
                case IDC_FONT_ZOOM_OUT:
                    if (HIWORD(wp) == BN_CLICKED) change_text_font_list_scale(hwnd, -10);
                    break;
                case IDC_FONT_ZOOM_IN:
                    if (HIWORD(wp) == BN_CLICKED) change_text_font_list_scale(hwnd, 10);
                    break;
                case IDC_FONT_APPLY:
                    if (HIWORD(wp) == BN_CLICKED) apply_selected_text_font(hwnd);
                    break;
                case IDC_FONT_OBJECT_DEFAULT:
                    if (HIWORD(wp) == BN_CLICKED) {
                        apply_text_font(hwnd, g_text_font_target_base);
                    }
                    break;
                case IDC_FONT_CLOSE:
                    if (HIWORD(wp) == BN_CLICKED) ShowWindow(hwnd, SW_HIDE);
                    break;
            }
            return 0;
        case WM_TIMER:
            if (wp == TEXT_FONT_SEARCH_TIMER) {
                KillTimer(hwnd, TEXT_FONT_SEARCH_TIMER);
                refresh_text_font_active_list(hwnd);
                return 0;
            }
            break;
        case WM_NOTIFY: {
            auto* header = reinterpret_cast<NMHDR*>(lp);
            if (!header) break;
            if ((header->idFrom == IDC_FONT_SELECT_LIST ||
                 header->idFrom == IDC_FONT_MANAGE_LIST ||
                 header->idFrom == IDC_FONT_HISTORY_LIST) &&
                header->code == NM_CUSTOMDRAW) {
                return draw_text_font_list_preview(reinterpret_cast<NMLVCUSTOMDRAW*>(lp));
            }
            if (header->idFrom == IDC_FONT_TAB && header->code == TCN_SELCHANGE) {
                refresh_text_font_tab(hwnd);
                refresh_text_font_active_list(hwnd);
                if (!text_font_manage_tab_active(hwnd)) {
                    sync_text_font_select_list_selection(hwnd);
                }
                return 0;
            }
            if (header->idFrom == IDC_FONT_MANAGE_LIST &&
                header->code == LVN_ITEMCHANGED && !g_text_font_list_refreshing) {
                auto* changed = reinterpret_cast<NMLISTVIEW*>(lp);
                const UINT old_check = (changed->uOldState & LVIS_STATEIMAGEMASK) >> 12;
                const UINT new_check = (changed->uNewState & LVIS_STATEIMAGEMASK) >> 12;
                if ((changed->uChanged & LVIF_STATE) && old_check != new_check && new_check) {
                    const int index = text_font_list_param(header->hwndFrom, changed->iItem);
                    if (index >= 0 && index < (int)g_text_fonts.size()) {
                        TextFontEntry& font = g_text_fonts[(size_t)index];
                        font.visible = new_check == 2;
                        save_text_font_visibility(font);
                        g_text_font_select_list_dirty = true;
                        refresh_text_font_batch_checks(hwnd);
                    }
                }
                return 0;
            }
            if ((header->idFrom == IDC_FONT_SELECT_LIST ||
                 header->idFrom == IDC_FONT_HISTORY_LIST) &&
                header->code == LVN_ITEMCHANGED && !g_text_font_list_refreshing) {
                auto* changed = reinterpret_cast<NMLISTVIEW*>(lp);
                if ((changed->uChanged & LVIF_STATE) &&
                    (changed->uNewState & LVIS_SELECTED) &&
                    !(changed->uOldState & LVIS_SELECTED)) {
                    choose_text_font_list_row(
                        hwnd, header->hwndFrom, changed->iItem,
                        header->idFrom == IDC_FONT_HISTORY_LIST);
                }
                return 0;
            }
            if ((header->idFrom == IDC_FONT_SELECT_LIST ||
                 header->idFrom == IDC_FONT_HISTORY_LIST) && header->code == NM_DBLCLK) {
                auto* activate = reinterpret_cast<NMITEMACTIVATE*>(lp);
                if (activate->iItem >= 0) {
                    const bool history = header->idFrom == IDC_FONT_HISTORY_LIST;
                    const std::wstring name = history ?
                        text_font_name_from_history_list(header->hwndFrom, activate->iItem) :
                        text_font_name_from_font_list(header->hwndFrom, activate->iItem);
                    apply_text_font(hwnd, name);
                }
                return 0;
            }
            if (header->idFrom == IDC_FONT_HISTORY_LIST && header->code == NM_RCLICK) {
                POINT screen;
                GetCursorPos(&screen);
                show_text_font_history_menu(hwnd, header->hwndFrom, screen);
                return 0;
            }
            break;
        }
        case WM_ACTIVATE:
            if (LOWORD(wp) != WA_INACTIVE && IsWindowVisible(hwnd)) {
                const bool was_valid = g_text_font_target_valid;
                const std::wstring previous_font = g_text_font_target_current;
                refresh_text_font_target_from_host();
                refresh_text_font_target_label(hwnd);
                if (!text_font_manage_tab_active(hwnd) &&
                    (!was_valid || !g_text_font_target_valid ||
                     !same_font_name(previous_font, g_text_font_target_current))) {
                    sync_text_font_select_list_selection(hwnd);
                }
            }
            break;
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
            ShowWindow(hwnd, SW_HIDE);
            return 0;
        case WM_DESTROY:
            KillTimer(hwnd, TEXT_FONT_SEARCH_TIMER);
            clear_text_font_preview_fonts();
            if (g_text_font_list_font) {
                DeleteObject(g_text_font_list_font);
                g_text_font_list_font = nullptr;
            }
            g_font_hwnd = nullptr;
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

static void show_font_editor(HWND owner, int character_index) {
    load_text_font_ui_settings();
    if (!set_text_font_target(character_index)) {
        MessageBoxW(owner, L"選択文字のフォント情報を取得できませんでした。",
                    L"フォント変更", MB_OK | MB_ICONINFORMATION);
        return;
    }
    load_text_font_entries();
    load_text_font_history();
    if (!g_font_hwnd) {
        RECT owner_rect = {};
        GetWindowRect(owner, &owner_rect);
        const int window_width = 880;
        const int window_height = 600;
        int x = owner_rect.right + 8;
        int y = owner_rect.top;
        MONITORINFO monitor = { sizeof(monitor) };
        if (GetMonitorInfoW(MonitorFromWindow(owner, MONITOR_DEFAULTTONEAREST), &monitor)) {
            if (x + window_width > monitor.rcWork.right) {
                x = (std::max)((int)monitor.rcWork.left,
                               (int)owner_rect.left - window_width - 8);
            }
            y = (std::max)((int)monitor.rcWork.top,
                           (std::min)(y, (int)monitor.rcWork.bottom - window_height));
        }
        g_font_hwnd = CreateWindowExW(
            WS_EX_TOOLWINDOW, FONT_WINDOW_NAME, L"フォント変更",
            WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_THICKFRAME | WS_CLIPCHILDREN,
            x, y, window_width, window_height, owner, nullptr,
            GetModuleHandleW(nullptr), nullptr);
        if (!g_font_hwnd) return;
    } else {
        refresh_text_font_window(g_font_hwnd);
    }
    ShowWindow(g_font_hwnd, SW_SHOWNORMAL);
    SetForegroundWindow(g_font_hwnd);
    SetFocus(GetDlgItem(g_font_hwnd, IDC_FONT_SEARCH));
}
