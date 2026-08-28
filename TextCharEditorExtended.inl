// 拡張機能ウィンドウ
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
            HWND block_updates = CreateWindowExW(
                0, WC_BUTTONW, L"X/Y移動・他エフェクト操作の更新をブロック",
                WS_VISIBLE | WS_CHILD | BS_AUTOCHECKBOX,
                8, 76, 376, 24, hwnd,
                (HMENU)(INT_PTR)IDC_BLOCK_NON_TEXT_UPDATES,
                GetModuleHandleW(nullptr), nullptr);
            HWND list = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTBOXW, L"",
                                        WS_VISIBLE | WS_CHILD | WS_VSCROLL | LBS_NOTIFY |
                                        LBS_NOINTEGRALHEIGHT,
                                        8, 104, 360, 246, hwnd,
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
            HWND export_preset = CreateWindowExW(0, WC_BUTTONW, L"書き出し",
                                                 WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
                                                 8, 336, 176, 30, hwnd,
                                                 (HMENU)(INT_PTR)IDC_PRESET_EXPORT,
                                                 GetModuleHandleW(nullptr), nullptr);
            HWND import_preset = CreateWindowExW(0, WC_BUTTONW, L"読み込み",
                                                 WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
                                                 192, 336, 176, 30, hwnd,
                                                 (HMENU)(INT_PTR)IDC_PRESET_IMPORT,
                                                 GetModuleHandleW(nullptr), nullptr);
            for (HWND control : { label, disable, block_updates, list, draw, reset,
                                  export_preset, import_preset }) {
                SendMessageW(control, WM_SETFONT, (WPARAM)g_font_ui, TRUE);
            }
            CheckDlgButton(hwnd, IDC_SHORTCUT_DISABLE,
                           g_shortcuts_disabled ? BST_CHECKED : BST_UNCHECKED);
            CheckDlgButton(hwnd, IDC_BLOCK_NON_TEXT_UPDATES,
                           g_block_non_text_updates ? BST_CHECKED : BST_UNCHECKED);
            refresh_shortcut_list(hwnd);
            disable_plugin_ime(hwnd);
            return 0;
        }
        case WM_SIZE: {
            RECT rc;
            GetClientRect(hwnd, &rc);
            const int button_y = (std::max)(38, (int)rc.bottom - 76);
            const int preset_y = button_y + 38;
            const int preset_width = (std::max)(1, ((int)rc.right - 24) / 2);
            MoveWindow(GetDlgItem(hwnd, IDC_SHORTCUT_LABEL), 8, 8,
                       (std::max)(1, (int)rc.right - 16), 38, TRUE);
            MoveWindow(GetDlgItem(hwnd, IDC_SHORTCUT_DISABLE), 8, 48,
                       (std::max)(1, (int)rc.right - 16), 24, TRUE);
            MoveWindow(GetDlgItem(hwnd, IDC_BLOCK_NON_TEXT_UPDATES), 8, 76,
                       (std::max)(1, (int)rc.right - 16), 24, TRUE);
            MoveWindow(GetDlgItem(hwnd, IDC_SHORTCUT_LIST), 8, 104,
                       (std::max)(1, (int)rc.right - 16), (std::max)(1, button_y - 112), TRUE);
            MoveWindow(GetDlgItem(hwnd, IDC_HAND_DRAW), 8, button_y, 148, 30, TRUE);
            MoveWindow(GetDlgItem(hwnd, IDC_SHORTCUT_RESET), 164, button_y,
                       (std::max)(1, (int)rc.right - 172), 30, TRUE);
            MoveWindow(GetDlgItem(hwnd, IDC_PRESET_EXPORT), 8, preset_y,
                       preset_width, 30, TRUE);
            MoveWindow(GetDlgItem(hwnd, IDC_PRESET_IMPORT), 16 + preset_width, preset_y,
                       (std::max)(1, (int)rc.right - 24 - preset_width), 30, TRUE);
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
                case IDC_BLOCK_NON_TEXT_UPDATES:
                    if (HIWORD(wp) == BN_CLICKED) {
                        g_block_non_text_updates = IsDlgButtonChecked(
                            hwnd, IDC_BLOCK_NON_TEXT_UPDATES) == BST_CHECKED;
                        save_host_sync_settings();
                        // 比較方式が変わるため、次回同期時に署名を作り直す。
                        g_ui_source_signature.clear();
                        g_ui_frame = -1;
                        post_host_refresh();
                    }
                    break;
                case IDC_SHORTCUT_RESET:
                    if (HIWORD(wp) == BN_CLICKED) {
                        g_shortcut_capture = -1;
                        reset_shortcuts();
                        refresh_shortcut_list(hwnd);
                    }
                    break;
                case IDC_PRESET_EXPORT:
                    if (HIWORD(wp) == BN_CLICKED) export_text_preset(hwnd);
                    break;
                case IDC_PRESET_IMPORT:
                    if (HIWORD(wp) == BN_CLICKED) import_text_preset(hwnd);
                    break;
            }
            return 0;
        case WM_GETDLGCODE:
            return DLGC_WANTALLKEYS;
        case WM_KEYDOWN:
        case WM_SYSKEYDOWN: {
            const UINT key = normalize_shortcut_virtual_key(hwnd, (UINT)wp);
            if (g_shortcut_capture >= 0) {
                if (key == VK_ESCAPE) {
                    g_shortcut_capture = -1;
                    g_shortcut_pending_modifier_key = 0;
                } else if (key == VK_BACK) {
                    set_shortcut(g_shortcut_capture, 0, 0);
                    g_shortcut_capture = -1;
                    g_shortcut_pending_modifier_key = 0;
                } else if (is_modifier_key(key) &&
                           is_drag_lock_action(g_shortcut_capture)) {
                    if (!g_shortcut_pending_modifier_key) {
                        g_shortcut_pending_modifier_key = key;
                    }
                } else if (!is_modifier_key(key)) {
                    set_shortcut(g_shortcut_capture, key,
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
            if (key == VK_ESCAPE) {
                ShowWindow(hwnd, SW_HIDE);
                return 0;
            }
            break;
        }
        case WM_KEYUP:
        case WM_SYSKEYUP: {
            const UINT key = normalize_shortcut_virtual_key(hwnd, (UINT)wp);
            if (g_shortcut_capture >= 0 && is_drag_lock_action(g_shortcut_capture) &&
                g_shortcut_pending_modifier_key == key) {
                set_shortcut(g_shortcut_capture, key, 0);
                g_shortcut_capture = -1;
                g_shortcut_pending_modifier_key = 0;
                refresh_shortcut_list(hwnd);
                SetFocus(GetDlgItem(hwnd, IDC_SHORTCUT_LIST));
                return 0;
            }
            break;
        }
        case WM_CHAR:
        case WM_SYSCHAR:
        case WM_IME_CHAR: {
            const UINT key = shortcut_virtual_key_from_fullwidth(wp);
            if (g_shortcut_capture >= 0 && key) {
                set_shortcut(g_shortcut_capture, key, current_shortcut_modifiers());
                g_shortcut_capture = -1;
                g_shortcut_pending_modifier_key = 0;
                refresh_shortcut_list(hwnd);
                SetFocus(GetDlgItem(hwnd, IDC_SHORTCUT_LIST));
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
                                          L"拡張機能",
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
