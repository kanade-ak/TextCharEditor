// テキストオブジェクトのプリセット入出力

struct TextPresetSnapshot {
    std::string label_utf8;
    std::string object_name_utf8;
    std::string text_utf8;
    std::string font_utf8;
    std::string size_utf8;
    std::string character_data_utf8;
    std::string alias_utf8;
};

enum class TextPresetSnapshotStatus {
    OK,
    EDIT_UNAVAILABLE,
    NO_OBJECT,
    NOT_TEXT,
    NO_ALIAS,
};

static std::wstring compact_preset_label(const std::wstring& source) {
    std::wstring label;
    label.reserve((std::min)(source.size(), (size_t)64));
    bool pending_space = false;
    for (wchar_t c : source) {
        if (iswspace(c)) {
            pending_space = !label.empty();
            continue;
        }
        if (pending_space && label.size() < 64) label.push_back(L' ');
        pending_space = false;
        if (label.size() >= 64) break;
        label.push_back(c);
    }
    while (!label.empty() && iswspace(label.back())) label.pop_back();
    return label;
}

static TextPresetSnapshotStatus read_text_preset_snapshot(TextPresetSnapshot* snapshot) {
    if (!snapshot || !edit_handle) return TextPresetSnapshotStatus::EDIT_UNAVAILABLE;
    *snapshot = {};

    struct Context {
        TextPresetSnapshot* snapshot = nullptr;
        bool has_object = false;
        bool has_text = false;
        bool has_alias = false;
    } context { snapshot };

    const bool read = edit_handle->call_read_section_param(
        &context, [](void* param, EDIT_SECTION* edit) {
            auto* context = static_cast<Context*>(param);
            OBJECT_HANDLE object = edit->get_focus_object();
            if (!object) return;
            context->has_object = true;

            if (LPCSTR alias = edit->get_object_alias(object)) {
                context->snapshot->alias_utf8 = alias;
                context->has_alias = !context->snapshot->alias_utf8.empty();
            }
            if (LPCSTR text = edit->get_object_item_value(object, L"テキスト", L"テキスト")) {
                context->snapshot->text_utf8 = text;
                context->has_text = true;
            }
            if (!context->has_text) return;

            if (LPCSTR font = edit->get_object_item_value(object, L"テキスト", L"フォント")) {
                context->snapshot->font_utf8 = font;
            }
            if (LPCSTR size = edit->get_object_item_value(object, L"テキスト", L"サイズ")) {
                context->snapshot->size_utf8 = size;
            }
            if (LPCWSTR name = edit->get_object_name(object)) {
                context->snapshot->object_name_utf8 = wide_to_utf8(name);
            }
            if (EFFECT_HANDLE effect = edit->find_effect(object, EFFECT_NAME)) {
                if (LPCSTR data = edit->get_effect_item_value(effect, ITEM_DATA)) {
                    context->snapshot->character_data_utf8 = data;
                }
            }
        });

    if (!read) return TextPresetSnapshotStatus::EDIT_UNAVAILABLE;
    if (!context.has_object) return TextPresetSnapshotStatus::NO_OBJECT;
    if (!context.has_text) return TextPresetSnapshotStatus::NOT_TEXT;
    if (!context.has_alias) return TextPresetSnapshotStatus::NO_ALIAS;

    std::wstring label = utf8_to_wide(snapshot->object_name_utf8.c_str());
    if (label.empty()) {
        label = compact_preset_label(
            strip_control_tags(utf8_to_wide(snapshot->text_utf8.c_str())));
    }
    if (label.empty()) label = L"テキスト";
    snapshot->label_utf8 = wide_to_utf8(label.c_str());
    return TextPresetSnapshotStatus::OK;
}

static std::wstring safe_preset_filename(const std::string& label_utf8) {
    std::wstring source = utf8_to_wide(label_utf8.c_str());
    std::wstring name;
    name.reserve((std::min)(source.size(), (size_t)64));
    bool last_was_space = false;
    for (wchar_t c : source) {
        if (name.size() >= 64) break;
        const bool invalid = c < 0x20 || c == L'<' || c == L'>' || c == L':' ||
                             c == L'"' || c == L'/' || c == L'\\' || c == L'|' ||
                             c == L'?' || c == L'*';
        if (invalid || iswspace(c)) {
            if (!name.empty() && !last_was_space) name.push_back(L' ');
            last_was_space = true;
            continue;
        }
        name.push_back(c);
        last_was_space = false;
    }
    while (!name.empty() && (name.back() == L' ' || name.back() == L'.')) name.pop_back();
    while (!name.empty() && name.front() == L' ') name.erase(name.begin());
    if (name.empty()) name = L"TextPreset";

    std::wstring upper;
    upper.reserve(name.size());
    for (wchar_t c : name) upper.push_back((wchar_t)towupper(c));
    const bool reserved = upper == L"CON" || upper == L"PRN" || upper == L"AUX" ||
                          upper == L"NUL" ||
                          (upper.size() == 4 &&
                           ((upper.compare(0, 3, L"COM") == 0) ||
                            (upper.compare(0, 3, L"LPT") == 0)) &&
                           upper[3] >= L'1' && upper[3] <= L'9');
    if (reserved) name.insert(name.begin(), L'_');
    return name + L".tcepreset";
}

static std::string json_escape_utf8(const std::string& value) {
    static constexpr char hex[] = "0123456789abcdef";
    std::string escaped;
    escaped.reserve(value.size() + value.size() / 16);
    for (unsigned char c : value) {
        switch (c) {
            case '"': escaped += "\\\""; break;
            case '\\': escaped += "\\\\"; break;
            case '\b': escaped += "\\b"; break;
            case '\f': escaped += "\\f"; break;
            case '\n': escaped += "\\n"; break;
            case '\r': escaped += "\\r"; break;
            case '\t': escaped += "\\t"; break;
            default:
                if (c < 0x20) {
                    escaped += "\\u00";
                    escaped.push_back(hex[c >> 4]);
                    escaped.push_back(hex[c & 15]);
                } else {
                    escaped.push_back((char)c);
                }
                break;
        }
    }
    return escaped;
}

static std::string preset_saved_at_local() {
    SYSTEMTIME time = {};
    GetLocalTime(&time);
    char value[32];
    snprintf(value, sizeof(value), "%04u-%02u-%02uT%02u:%02u:%02u",
             time.wYear, time.wMonth, time.wDay,
             time.wHour, time.wMinute, time.wSecond);
    return value;
}

static std::string make_text_preset_json(const TextPresetSnapshot& snapshot) {
    std::string json;
    json.reserve(snapshot.alias_utf8.size() + snapshot.character_data_utf8.size() + 512);
    json += "{\n";
    json += "  \"format\": \"TextCharEditorPreset\",\n";
    json += "  \"version\": 1,\n";
    json += "  \"savedAtLocal\": \"" + json_escape_utf8(preset_saved_at_local()) + "\",\n";
    json += "  \"name\": \"" + json_escape_utf8(snapshot.label_utf8) + "\",\n";
    json += "  \"objectName\": \"" + json_escape_utf8(snapshot.object_name_utf8) + "\",\n";
    json += "  \"text\": \"" + json_escape_utf8(snapshot.text_utf8) + "\",\n";
    json += "  \"font\": \"" + json_escape_utf8(snapshot.font_utf8) + "\",\n";
    json += "  \"size\": \"" + json_escape_utf8(snapshot.size_utf8) + "\",\n";
    json += "  \"characterData\": \"" +
            json_escape_utf8(snapshot.character_data_utf8) + "\",\n";
    json += "  \"objectAlias\": \"" + json_escape_utf8(snapshot.alias_utf8) + "\"\n";
    json += "}\n";
    return json;
}

static int json_hex_value(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static bool append_utf8_codepoint(std::uint32_t codepoint, std::string* out) {
    if (!out || codepoint > 0x10ffff ||
        (codepoint >= 0xd800 && codepoint <= 0xdfff)) return false;
    if (codepoint <= 0x7f) {
        out->push_back((char)codepoint);
    } else if (codepoint <= 0x7ff) {
        out->push_back((char)(0xc0 | (codepoint >> 6)));
        out->push_back((char)(0x80 | (codepoint & 0x3f)));
    } else if (codepoint <= 0xffff) {
        out->push_back((char)(0xe0 | (codepoint >> 12)));
        out->push_back((char)(0x80 | ((codepoint >> 6) & 0x3f)));
        out->push_back((char)(0x80 | (codepoint & 0x3f)));
    } else {
        out->push_back((char)(0xf0 | (codepoint >> 18)));
        out->push_back((char)(0x80 | ((codepoint >> 12) & 0x3f)));
        out->push_back((char)(0x80 | ((codepoint >> 6) & 0x3f)));
        out->push_back((char)(0x80 | (codepoint & 0x3f)));
    }
    return true;
}

static bool parse_json_string(const std::string& json, size_t* position,
                              std::string* value) {
    if (!position || !value || *position >= json.size() || json[*position] != '"') {
        return false;
    }
    size_t p = *position + 1;
    value->clear();
    while (p < json.size()) {
        const unsigned char c = (unsigned char)json[p++];
        if (c == '"') {
            *position = p;
            return true;
        }
        if (c < 0x20) return false;
        if (c != '\\') {
            value->push_back((char)c);
            continue;
        }
        if (p >= json.size()) return false;
        const char escape = json[p++];
        switch (escape) {
            case '"': value->push_back('"'); break;
            case '\\': value->push_back('\\'); break;
            case '/': value->push_back('/'); break;
            case 'b': value->push_back('\b'); break;
            case 'f': value->push_back('\f'); break;
            case 'n': value->push_back('\n'); break;
            case 'r': value->push_back('\r'); break;
            case 't': value->push_back('\t'); break;
            case 'u': {
                if (p + 4 > json.size()) return false;
                std::uint32_t codepoint = 0;
                for (int i = 0; i < 4; i++) {
                    const int digit = json_hex_value(json[p++]);
                    if (digit < 0) return false;
                    codepoint = (codepoint << 4) | (std::uint32_t)digit;
                }
                if (codepoint >= 0xd800 && codepoint <= 0xdbff) {
                    if (p + 6 > json.size() || json[p] != '\\' || json[p + 1] != 'u') {
                        return false;
                    }
                    p += 2;
                    std::uint32_t low = 0;
                    for (int i = 0; i < 4; i++) {
                        const int digit = json_hex_value(json[p++]);
                        if (digit < 0) return false;
                        low = (low << 4) | (std::uint32_t)digit;
                    }
                    if (low < 0xdc00 || low > 0xdfff) return false;
                    codepoint = 0x10000 + ((codepoint - 0xd800) << 10) + (low - 0xdc00);
                } else if (codepoint >= 0xdc00 && codepoint <= 0xdfff) {
                    return false;
                }
                if (!append_utf8_codepoint(codepoint, value)) return false;
                break;
            }
            default:
                return false;
        }
    }
    return false;
}

static void skip_json_spaces(const std::string& json, size_t* position) {
    while (*position < json.size()) {
        const char c = json[*position];
        if (c != ' ' && c != '\t' && c != '\r' && c != '\n') break;
        (*position)++;
    }
}

// version 1 のトップレベルは文字列と整数のみ。文字列値は共通パーサーで
// 読み飛ばすため、エイリアス内に JSON 風の文字があっても誤認しない。
static bool find_preset_json_string(const std::string& json, const char* wanted,
                                    std::string* value) {
    size_t p = 0;
    skip_json_spaces(json, &p);
    if (p >= json.size() || json[p++] != '{') return false;
    while (p < json.size()) {
        skip_json_spaces(json, &p);
        if (p < json.size() && json[p] == '}') return false;

        std::string key;
        if (!parse_json_string(json, &p, &key)) return false;
        skip_json_spaces(json, &p);
        if (p >= json.size() || json[p++] != ':') return false;
        skip_json_spaces(json, &p);

        if (p < json.size() && json[p] == '"') {
            std::string parsed;
            if (!parse_json_string(json, &p, &parsed)) return false;
            if (key == wanted) {
                *value = std::move(parsed);
                return true;
            }
        } else {
            while (p < json.size() && json[p] != ',' && json[p] != '}') p++;
        }
        skip_json_spaces(json, &p);
        if (p < json.size() && json[p] == ',') {
            p++;
            continue;
        }
        if (p < json.size() && json[p] == '}') return false;
        return false;
    }
    return false;
}

static std::wstring preset_io_error(const wchar_t* action, DWORD error) {
    std::wstring message = action;
    message += L"できませんでした。 (Windows エラー ";
    message += std::to_wstring(error);
    message += L")";
    return message;
}

static bool write_text_preset_file(const std::wstring& path, const std::string& data,
                                   std::wstring* error_message) {
    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        if (error_message) *error_message = preset_io_error(L"ファイルへ書き込み", GetLastError());
        return false;
    }
    size_t offset = 0;
    bool success = true;
    while (offset < data.size()) {
        const DWORD request = (DWORD)(std::min)(data.size() - offset, (size_t)0x40000000);
        DWORD written = 0;
        if (!WriteFile(file, data.data() + offset, request, &written, nullptr) ||
            written == 0) {
            if (error_message) *error_message = preset_io_error(L"ファイルへ書き込み", GetLastError());
            success = false;
            break;
        }
        offset += written;
    }
    if (success && !FlushFileBuffers(file)) {
        if (error_message) *error_message = preset_io_error(L"ファイルを保存", GetLastError());
        success = false;
    }
    CloseHandle(file);
    return success;
}

static bool read_text_preset_file(const std::wstring& path, std::string* data,
                                  std::wstring* error_message) {
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                              OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        if (error_message) *error_message = preset_io_error(L"ファイルを読み込み", GetLastError());
        return false;
    }
    LARGE_INTEGER size = {};
    constexpr LONGLONG MAX_PRESET_BYTES = 256ll * 1024 * 1024;
    if (!GetFileSizeEx(file, &size) || size.QuadPart < 0 ||
        size.QuadPart > MAX_PRESET_BYTES) {
        if (error_message) {
            *error_message = size.QuadPart > MAX_PRESET_BYTES ?
                L"プリセットファイルが大きすぎます。" :
                preset_io_error(L"ファイルサイズを取得", GetLastError());
        }
        CloseHandle(file);
        return false;
    }
    data->assign((size_t)size.QuadPart, '\0');
    size_t offset = 0;
    bool success = true;
    while (offset < data->size()) {
        const DWORD request = (DWORD)(std::min)(data->size() - offset, (size_t)0x40000000);
        DWORD read = 0;
        if (!ReadFile(file, data->data() + offset, request, &read, nullptr) || read == 0) {
            if (error_message) *error_message = preset_io_error(L"ファイルを読み込み", GetLastError());
            success = false;
            break;
        }
        offset += read;
    }
    CloseHandle(file);
    if (!success) {
        data->clear();
        return false;
    }
    if (data->size() >= 3 && (unsigned char)(*data)[0] == 0xef &&
        (unsigned char)(*data)[1] == 0xbb && (unsigned char)(*data)[2] == 0xbf) {
        data->erase(0, 3);
    }
    return true;
}

static bool choose_text_preset_file(HWND owner, bool save, const std::wstring& suggested,
                                    std::wstring* path) {
    std::vector<wchar_t> buffer(32768, L'\0');
    if (save && !suggested.empty()) {
        wcsncpy_s(buffer.data(), buffer.size(), suggested.c_str(), _TRUNCATE);
    }
    OPENFILENAMEW dialog = {};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = owner;
    dialog.lpstrFilter = save ?
        L"TextCharEditorプリセット (*.tcepreset)\0*.tcepreset\0すべてのファイル (*.*)\0*.*\0\0" :
        L"テキストプリセット (*.tcepreset;*.object)\0*.tcepreset;*.object\0TextCharEditorプリセット (*.tcepreset)\0*.tcepreset\0AviUtl2オブジェクト (*.object)\0*.object\0すべてのファイル (*.*)\0*.*\0\0";
    dialog.lpstrFile = buffer.data();
    dialog.nMaxFile = (DWORD)buffer.size();
    dialog.lpstrDefExt = L"tcepreset";
    dialog.lpstrTitle = save ? L"テキストプリセットの書き出し" :
                               L"テキストプリセットの読み込み";
    dialog.Flags = OFN_EXPLORER | OFN_NOCHANGEDIR | OFN_PATHMUSTEXIST |
                   (save ? OFN_OVERWRITEPROMPT : OFN_FILEMUSTEXIST);
    const BOOL selected = save ? GetSaveFileNameW(&dialog) : GetOpenFileNameW(&dialog);
    if (!selected) return false;
    *path = buffer.data();
    if (save) {
        const size_t slash = path->find_last_of(L"\\/");
        const size_t dot = path->find_last_of(L'.');
        if (dot == std::wstring::npos || (slash != std::wstring::npos && dot < slash)) {
            *path += L".tcepreset";
        }
    }
    return true;
}

static bool looks_like_text_object_alias(const std::string& data) {
    const size_t first = data.find_first_not_of(" \t\r\n");
    return first != std::string::npos && data[first] == '[' &&
           data.find("effect.name=テキスト", first) != std::string::npos;
}

static bool export_text_preset(HWND owner) {
    TextPresetSnapshot snapshot;
    switch (read_text_preset_snapshot(&snapshot)) {
        case TextPresetSnapshotStatus::OK:
            break;
        case TextPresetSnapshotStatus::NO_OBJECT:
            MessageBoxW(owner, L"書き出すテキストオブジェクトを選択してください。",
                        L"テキストプリセット", MB_OK | MB_ICONINFORMATION);
            return false;
        case TextPresetSnapshotStatus::NOT_TEXT:
            MessageBoxW(owner, L"選択中のオブジェクトはテキストではありません。",
                        L"テキストプリセット", MB_OK | MB_ICONINFORMATION);
            return false;
        case TextPresetSnapshotStatus::NO_ALIAS:
            MessageBoxW(owner, L"選択中オブジェクトのエイリアスを取得できませんでした。",
                        L"テキストプリセット", MB_OK | MB_ICONERROR);
            return false;
        default:
            MessageBoxW(owner, L"現在はオブジェクト情報を読み取れません。",
                        L"テキストプリセット", MB_OK | MB_ICONERROR);
            return false;
    }

    std::wstring path;
    if (!choose_text_preset_file(owner, true,
                                 safe_preset_filename(snapshot.label_utf8), &path)) {
        return false;
    }
    std::wstring error;
    if (!write_text_preset_file(path, make_text_preset_json(snapshot), &error)) {
        MessageBoxW(owner, error.c_str(), L"テキストプリセット", MB_OK | MB_ICONERROR);
        return false;
    }
    return true;
}

struct TextPresetImportContext {
    std::string alias_utf8;
    std::wstring object_name;
    OBJECT_HANDLE created = nullptr;
};

static bool import_text_preset(HWND owner) {
    if (!edit_handle) return false;
    std::wstring path;
    if (!choose_text_preset_file(owner, false, {}, &path)) return false;

    std::string file_data;
    std::wstring error;
    if (!read_text_preset_file(path, &file_data, &error)) {
        MessageBoxW(owner, error.c_str(), L"テキストプリセット", MB_OK | MB_ICONERROR);
        return false;
    }

    TextPresetImportContext context;
    std::string format;
    std::string object_name_utf8;
    if (find_preset_json_string(file_data, "format", &format)) {
        if (format != "TextCharEditorPreset" ||
            !find_preset_json_string(file_data, "objectAlias", &context.alias_utf8)) {
            MessageBoxW(owner, L"対応していないテキストプリセットです。",
                        L"テキストプリセット", MB_OK | MB_ICONERROR);
            return false;
        }
        find_preset_json_string(file_data, "objectName", &object_name_utf8);
        context.object_name = utf8_to_wide(object_name_utf8.c_str());
    } else if (looks_like_text_object_alias(file_data)) {
        // AviUtl2標準の .object も入口として直接読み込めるようにする。
        context.alias_utf8 = std::move(file_data);
    } else {
        MessageBoxW(owner, L"テキストプリセット、またはテキストの .object ファイルではありません。",
                    L"テキストプリセット", MB_OK | MB_ICONERROR);
        return false;
    }

    if (!looks_like_text_object_alias(context.alias_utf8) ||
        context.alias_utf8.find('\0') != std::string::npos) {
        MessageBoxW(owner, L"プリセット内のオブジェクトエイリアスが不正です。",
                    L"テキストプリセット", MB_OK | MB_ICONERROR);
        return false;
    }

    const bool edited = edit_handle->call_edit_section_param(
        &context, [](void* param, EDIT_SECTION* edit) {
            auto* context = static_cast<TextPresetImportContext*>(param);
            const int layer = edit->info ? (std::max)(0, edit->info->layer) : 0;
            const int frame = edit->info ? (std::max)(0, edit->info->frame) : 0;
            context->created = edit->create_object_from_alias(
                context->alias_utf8.c_str(), layer, frame, 0);
            if (!context->created) return;
            if (!context->object_name.empty()) {
                edit->set_object_name(context->created, context->object_name.c_str());
            }
            const OBJECT_LAYER_FRAME location = edit->get_object_layer_frame(context->created);
            edit->set_cursor_layer_frame(location.layer, location.start);
            edit->set_focus_object(context->created);
        });
    if (!edited) {
        MessageBoxW(owner, L"現在はプロジェクトを編集できません。",
                    L"テキストプリセット", MB_OK | MB_ICONERROR);
        return false;
    }
    if (!context.created) {
        MessageBoxW(owner,
                    L"プリセットからオブジェクトを作成できませんでした。\r\n"
                    L"現在位置の重なりや、必要なエフェクトの有無を確認してください。",
                    L"テキストプリセット", MB_OK | MB_ICONERROR);
        return false;
    }

    g_ui_source_signature.clear();
    g_ui_frame = -1;
    post_host_refresh();
    return true;
}
