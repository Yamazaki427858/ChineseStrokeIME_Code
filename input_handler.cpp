// input_handler.cpp - 輸入處理實作
#include "input_handler.h"
#include "buffer_manager.h"
#include "dictionary.h"
#include "window_manager.h"
#include "ime_manager.h"
#include <windows.h>

extern GlobalState g_state;
extern HHOOK g_hKeyboardHook;

namespace InputHandler {

// 確保目標視窗有焦點
void ensureTargetWindowFocused() {
    HWND hForeground = GetForegroundWindow();
    if (!hForeground || hForeground == g_state.hWnd || 
        hForeground == g_state.hCandWnd || hForeground == g_state.hInputWnd ||
        hForeground == g_state.hBufferWnd) {
        return;
    }
    
    DWORD currentThread = GetCurrentThreadId();
    DWORD targetThread = GetWindowThreadProcessId(hForeground, NULL);
    bool attached = false;
    
    if (currentThread != targetThread) {
        AttachThreadInput(currentThread, targetThread, TRUE);
        attached = true;
    }
    
    SetForegroundWindow(hForeground);
    SetActiveWindow(hForeground);
    
    // 嘗試設置焦點到編輯控件
    HWND hFocus = GetFocus();
    if (!hFocus || hFocus == hForeground) {
        SetFocus(hForeground);
    }
    
    if (attached) {
        AttachThreadInput(currentThread, targetThread, FALSE);
    }
    
    Sleep(50);
}

void sendTextDirectUnicode(const std::wstring& text) {
    if (text.empty()) return;
    
    bool wasBufferFocused = g_state.bufferHasFocus;
    bool wasBufferVisible = g_state.bufferMode && g_state.hBufferWnd && IsWindowVisible(g_state.hBufferWnd);
    RECT originalRect = {0};
    
    if (g_state.bufferMode && g_state.hBufferWnd && wasBufferVisible) {
        GetWindowRect(g_state.hBufferWnd, &originalRect);
        g_state.bufferHasFocus = false;
        ShowWindow(g_state.hBufferWnd, SW_HIDE);
        Sleep(50);
    }
    
    
    for (wchar_t ch : text) {
        INPUT input = {0};
        input.type = INPUT_KEYBOARD;
        input.ki.wVk = 0;
        input.ki.wScan = ch;
        input.ki.dwFlags = KEYEVENTF_UNICODE;
        input.ki.time = 0;
        input.ki.dwExtraInfo = 0;
        SendInput(1, &input, sizeof(INPUT));
        
        input.ki.dwFlags = KEYEVENTF_UNICODE | KEYEVENTF_KEYUP;
        SendInput(1, &input, sizeof(INPUT));
        
        
        INPUT flushInput = {0};
        flushInput.type = INPUT_KEYBOARD;
        flushInput.ki.wVk = VK_PACKET;
        flushInput.ki.dwFlags = KEYEVENTF_KEYUP;
        SendInput(1, &flushInput, sizeof(INPUT));
        
        Sleep(8);
    }
    
    if (g_state.bufferMode && g_state.hBufferWnd && wasBufferVisible) {
        Sleep(100);
        
        SetWindowPos(g_state.hBufferWnd, HWND_TOPMOST, 
                    originalRect.left, originalRect.top,
                    originalRect.right - originalRect.left, 
                    originalRect.bottom - originalRect.top,
                    SWP_SHOWWINDOW | SWP_NOACTIVATE);
        
        if (wasBufferFocused) {
            g_state.bufferHasFocus = true;
            SetTimer(g_state.hBufferWnd, 1, 500, NULL);
        }
    }
}

void toggleInputMode(GlobalState& state) {
    state.chineseMode = !state.chineseMode;
    state.input.clear();
    state.candidates.clear();
    state.candidateCodes.clear();
    state.showCand = false;
    state.isInputting = false;
    state.inputError = false;
    state.showPunctMenu = false;
    // 切換中英文模式時，回到空閒狀態並隱藏所有輸入相關視窗
    WindowManager::switchMode(state, InputMode::IDLE);

    // 注意：不再使用 IME API，改用鍵盤鉤子直接轉換字母

    std::wstring modeMsg = state.chineseMode ? L"中文+全形" : L"英文+半形";
    Utils::updateStatus(state, L"Shift切換到" + modeMsg + L"模式");
    if (state.hWnd) InvalidateRect(state.hWnd, nullptr, TRUE);
}

void handleEnterKeySmartly(GlobalState& state) {
    if (state.bufferMode) {
        if (state.showCand && state.isInputting) {
            // 如果有候選字，選擇第一個
            Dictionary::selectCandidate(state, 0);
            return;
        } else if (!state.showCand && !state.isInputting && !state.bufferText.empty()) {
            // 暫放模式下，無候選字且暫放區有內容時，發送內容（恢復原樣）
            BufferManager::sendBufferContent(state);
            return;
        }
    } else {
        if (state.showCand && state.isInputting) {
            // 非暫放模式下有候選字時，選擇第一個
            Dictionary::selectCandidate(state, 0);
            return;
        }
    }
}

std::wstring convertEnglishChar(wchar_t ch, bool toFullWidth) {
    if (!toFullWidth) {
        return std::wstring(1, ch);
    }
    
    if (ch >= L'!' && ch <= L'~') {
        return std::wstring(1, ch - L'!' + L'！');
    }
    if (ch == L' ') {
        return L"　";
    }
    
    return std::wstring(1, ch);
}

void processStroke(GlobalState& state, DWORD key) {
    if (!state.chineseMode) return;
    if (key == 'P') {
        showPunctMenu(state);
        return;
    }
    wchar_t inputChar = 0;
    
    // 筆劃鍵（固定對應 u i o j k）
    if (key == 'U' || key == VK_NUMPAD7) {
        inputChar = L'u';
    } else if (key == 'I' || key == VK_NUMPAD8) {
        inputChar = L'i';
    } else if (key == 'O' || key == VK_NUMPAD9) {
        inputChar = L'o';
    } else if (key == 'J' || key == VK_NUMPAD4) {
        inputChar = L'j';
    } else if (key == 'K' || key == VK_NUMPAD5) {
        inputChar = L'k';
    }
    
    // 3+3 模式萬用字元 *（可由設定檔指定按鍵，預設為 L 與 NumPad0）
    if (key == static_cast<DWORD>(state.wildcardKey1VK) ||
        key == static_cast<DWORD>(state.wildcardKey2VK)) {
        inputChar = L'*';
    }
    
    if (inputChar) {
        state.input += inputChar;
        Dictionary::updateCandidates(state);
        if (state.hWnd) InvalidateRect(state.hWnd, nullptr, TRUE);
    }
}

void showPunctMenu(GlobalState& state) {
    state.showPunctMenu = true;
    state.candidates = state.punctCandidates;
    state.candidateCodes.clear();
    for (size_t i = 0; i < state.candidates.size(); i++) {
        state.candidateCodes.push_back(L"P");
    }
    state.selected = 0;
    state.currentPage = 0;
    state.totalPages = (state.candidates.size() + CANDIDATES_PER_PAGE - 1) / CANDIDATES_PER_PAGE;
    state.showCand = true;

    // 標點選單視為一般候選模式：顯示候選視窗、隱藏聯想視窗
    WindowManager::switchMode(state, InputMode::CAND_MODE);
    WindowManager::positionWindowsOptimized(state);
    if (state.hCandWnd) {
        InvalidateRect(state.hCandWnd, nullptr, TRUE);
    }
    
    Utils::updateStatus(state, L"全形標點符號選單（按ESC關閉）");
}

void processPunctuator(GlobalState& state, DWORD key) {
    std::wstring punctChar = L"";
    bool isShiftPressed = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
    
    switch (key) {
        case VK_OEM_COMMA: punctChar = isShiftPressed ? L"<" : L","; break;
        case VK_OEM_PERIOD: punctChar = isShiftPressed ? L">" : L"."; break;
        case VK_OEM_2: punctChar = isShiftPressed ? L"?" : L"/"; break;
        case '1': if (isShiftPressed) punctChar = L"!"; break;
        case VK_OEM_1: punctChar = isShiftPressed ? L":" : L";"; break;
        case '9': if (isShiftPressed) punctChar = L"("; break;
        case '0': if (isShiftPressed) punctChar = L")"; break;
        case VK_OEM_4: punctChar = isShiftPressed ? L"{" : L"["; break;
        case VK_OEM_6: punctChar = isShiftPressed ? L"}" : L"]"; break;
        case VK_SPACE: punctChar = L" "; break;
        case VK_OEM_7: punctChar = isShiftPressed ? L"\"" : L"'"; break;
        case VK_OEM_MINUS: punctChar = isShiftPressed ? L"_" : L"-"; break;
        case VK_OEM_PLUS: punctChar = isShiftPressed ? L"+" : L"="; break;
        case VK_OEM_5: punctChar = isShiftPressed ? L"|" : L"\\"; break;
        case VK_OEM_3: punctChar = isShiftPressed ? L"~" : L"`"; break;
        case '2': if (isShiftPressed) punctChar = L"@"; break;
        case '3': if (isShiftPressed) punctChar = L"#"; break;
        case '4': if (isShiftPressed) punctChar = L"$"; break;
        case '5': if (isShiftPressed) punctChar = L"%"; break;
        case '6': if (isShiftPressed) punctChar = L"^"; break;
        case '7': if (isShiftPressed) punctChar = L"&"; break;
        case '8': if (isShiftPressed) punctChar = L"*"; break;
        // Num Lock 小鍵盤標點
        case VK_MULTIPLY: punctChar = L"*"; break;
        case VK_ADD: punctChar = L"+"; break;
        case VK_SUBTRACT: punctChar = L"-"; break;
        case VK_DECIMAL: punctChar = L"."; break;
        case VK_DIVIDE: punctChar = L"/"; break;
    }
    
    if (!punctChar.empty()) {
        // 修正問題1：英文模式下直接發送標點符號，不查表
        if (!state.chineseMode) {
            // 英文模式：直接發送半形標點
            sendTextDirectUnicode(punctChar);
            Utils::updateStatus(state, L"輸入標點：" + punctChar);
            return;
        }
        
        // 中文模式：使用標點表選擇全形或半形
        if (state.punct.count(punctChar)) {
            std::vector<std::wstring> options = state.punct[punctChar];
            if (!options.empty()) {
                std::wstring selectedPunct;
                if (punctChar == L" ") {
                    selectedPunct = L" ";
                } else if (punctChar == L"'") {
                    selectedPunct = state.chineseMode ? L"、" : L"'";
                } else {
                    if (punctChar[0] == L'"' || punctChar == L"[" || punctChar == L"]" || 
                        punctChar == L"{" || punctChar == L"}") {
                        selectedPunct = state.chineseMode ? options[0] : options.back();
                    } else {
                        selectedPunct = state.chineseMode ? options[0] : 
                                       (options.size() > 1 ? options[1] : options[0]);
                    }
                }
                sendTextDirectUnicode(selectedPunct);
                Utils::updateStatus(state, L"輸入標點：" + selectedPunct);
            }
        } else {
            // 如果標點表中沒有，直接發送
            sendTextDirectUnicode(punctChar);
            Utils::updateStatus(state, L"輸入標點：" + punctChar);
        }
    }
}

LRESULT CALLBACK KeyboardHookProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode >= 0) {
        KBDLLHOOKSTRUCT* pKeyboard = (KBDLLHOOKSTRUCT*)lParam;
        DWORD key = pKeyboard->vkCode;
        
        // 檢查 Win 鍵狀態（左Win鍵或右Win鍵）
        BOOL winLeft = (GetKeyState(VK_LWIN) & 0x8000) != 0;
        BOOL winRight = (GetKeyState(VK_RWIN) & 0x8000) != 0;
        BOOL winPressed = winLeft || winRight;
        
        // 檢查 Alt 鍵狀態（用於 Alt+Tab 等系統快捷鍵）
        BOOL alt = (GetKeyState(VK_MENU) & 0x8000) != 0;
        
        // Win 鍵或 Alt 鍵被按下時，直接放行所有按鍵，不攔截
        // 這樣可以確保 Win+R、Win+D、Alt+Tab 等系統快捷鍵正常工作
        if (winPressed || alt || key == VK_LWIN || key == VK_RWIN || key == VK_MENU) {
            return CallNextHookEx(g_hKeyboardHook, nCode, wParam, lParam);
        }
        
        // 先檢查 Ctrl 鍵狀態
        BOOL ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
        
        // 剪貼簿模式：檢測Ctrl+V貼上後清空暫放視窗（只有在暫放視窗沒有焦點時）
        if (ctrl && key == 'V' && wParam == WM_KEYDOWN) {
            if (g_state.clipboardMode && g_state.bufferMode && !g_state.bufferHasFocus) {
                BufferManager::handleClipboardPaste(g_state);
                // 繼續放行，讓系統正常處理Ctrl+V
                return CallNextHookEx(g_hKeyboardHook, nCode, wParam, lParam);
            }
        }
        
        if (ctrl) {
            // Ctrl+其他鍵都直接放行，不攔截
            return CallNextHookEx(g_hKeyboardHook, nCode, wParam, lParam);
        }
        
        // 中文模式按住 Shift + A-Z：先清空輸入框再直接輸出英文字母（須在 A-Z 轉英文與 Shift+P 之前）
        if (!g_state.bufferMode && wParam == WM_KEYDOWN && g_state.chineseMode) {
            bool shiftDown = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
            bool capsLock = (GetKeyState(VK_CAPITAL) & 0x0001) != 0;
            if (shiftDown && key >= 'A' && key <= 'Z') {
                if (!g_state.input.empty()) {
                    g_state.input.clear();
                    g_state.candidates.clear();
                    g_state.candidateCodes.clear();
                    g_state.showCand = false;
                    g_state.isInputting = false;
                    g_state.inputError = false;
                    if (g_state.hCandWnd) ShowWindow(g_state.hCandWnd, SW_HIDE);
                    if (g_state.hPredWnd) ShowWindow(g_state.hPredWnd, SW_HIDE);
                    if (g_state.hInputWnd) ShowWindow(g_state.hInputWnd, SW_HIDE);
                }
                bool uppercase = capsLock;
                wchar_t ch = uppercase ? (wchar_t)key : (wchar_t)(key + 32);
                std::wstring letter(1, ch);
                InputHandler::sendTextDirectUnicode(letter);
                return 1;
            }
        }
        
        // 🔥 新方案：在中文/英文模式下，將字母鍵直接轉換成英文字母輸出
        // 這樣即使 Windows 輸入法（如速成）在處理，也會被轉換成英文，避免衝突
        if (!g_state.bufferMode && wParam == WM_KEYDOWN) {
            // 檢查是否為字母鍵（A-Z）
            if (key >= 'A' && key <= 'Z') {
                bool shouldIntercept = false;
                
                if (g_state.chineseMode) {
                    // 中文模式：只攔截「不是筆劃鍵 / 萬用字元鍵」的英文字母
                    bool isWildcardKey =
                        key == static_cast<DWORD>(g_state.wildcardKey1VK) ||
                        key == static_cast<DWORD>(g_state.wildcardKey2VK);
                    bool isStrokeKey = (key == 'U' || key == 'I' || key == 'O' || 
                                       key == 'J' || key == 'K' || key == 'P' || isWildcardKey);
                    shouldIntercept = !isStrokeKey;
                } else {
                    // 英文模式：攔截所有字母鍵，統一由本輸入法送英文，避免與 Windows 自帶輸入法衝突
                    shouldIntercept = true;
                }
                
                // 如果應該攔截，直接轉換成英文字母輸出
                if (shouldIntercept) {
                    // 獲取 Shift 和 Caps Lock 狀態
                    bool shiftPressed = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
                    bool capsLock = (GetKeyState(VK_CAPITAL) & 0x0001) != 0;
                    bool isCapital = (shiftPressed != capsLock);
                    
                    // 轉換成對應的英文字母
                    wchar_t ch = isCapital ? key : (key + 32);
                    
                    // 攔截原按鍵，不讓它傳遞到系統
                    // 然後直接發送英文字母
                    HWND hForeground = GetForegroundWindow();
                    if (hForeground) {
                        // 使用 SendInput 發送 Unicode 字符
                        INPUT input[2] = {0};
                        
                        // 按下
                        input[0].type = INPUT_KEYBOARD;
                        input[0].ki.wVk = 0;
                        input[0].ki.wScan = ch;
                        input[0].ki.dwFlags = KEYEVENTF_UNICODE;
                        input[0].ki.time = 0;
                        input[0].ki.dwExtraInfo = 0;
                        
                        // 放開
                        input[1].type = INPUT_KEYBOARD;
                        input[1].ki.wVk = 0;
                        input[1].ki.wScan = ch;
                        input[1].ki.dwFlags = KEYEVENTF_UNICODE | KEYEVENTF_KEYUP;
                        input[1].ki.time = 0;
                        input[1].ki.dwExtraInfo = 0;
                        
                        SendInput(2, input, sizeof(INPUT));
                        
                        // 攔截原按鍵，不讓它繼續傳遞
                        return 1;
                    }
                }
            }
        }

        // Shift鍵狀態處理
        if (key == VK_SHIFT || key == VK_LSHIFT || key == VK_RSHIFT) {
            if (wParam == WM_KEYDOWN) {
                if (!g_state.shiftPressed) {
                    g_state.shiftPressed = true;
                    g_state.shiftUsedForCombo = false;
                    g_state.shiftPressTime = GetTickCount();
                }
            } else if (wParam == WM_KEYUP) {
                if (g_state.shiftPressed) {
                    g_state.shiftPressed = false;
                    DWORD pressDuration = GetTickCount() - g_state.shiftPressTime;
                    if (!g_state.shiftUsedForCombo && pressDuration < 500 && pressDuration > 30) {
                        toggleInputMode(g_state);
                    }
                    g_state.shiftUsedForCombo = false;
                }
            }
            return CallNextHookEx(g_hKeyboardHook, nCode, wParam, lParam);
        }
        
        if (wParam == WM_KEYDOWN) {
            if (g_state.shiftPressed && key != VK_SHIFT && key != VK_LSHIFT && key != VK_RSHIFT) {
                g_state.shiftUsedForCombo = true;
            }

            if (key == VK_ESCAPE) {
                // 右鍵選單顯示期間，一律讓 ESC 交給系統 / 選單處理，避免被輸入法攔截
                if (g_state.menuShowing) {
                    return CallNextHookEx(g_hKeyboardHook, nCode, wParam, lParam);
                }

                // 只有當輸入法真的需要處理 ESC 時才攔截
                if (g_state.showCand || g_state.showPunctMenu || g_state.isInputting) {
                    // 有候選字、標點選單或正在輸入時，由輸入法處理
                    PostMessage(g_state.hWnd, WM_USER+100, VK_ESCAPE, 0);
                    return 1;
                }
    
                // 暫放模式下有焦點時的特殊處理：讓 ESC 通過到目標應用程式
                if (g_state.bufferMode && g_state.bufferHasFocus) {
                    return CallNextHookEx(g_hKeyboardHook, nCode, wParam, lParam);
                }
    
                // 其他情況：輸入法沒有在使用，讓 ESC 通過給其他程式
                return CallNextHookEx(g_hKeyboardHook, nCode, wParam, lParam);
            }

            // 數字鍵優先用於選字（暫放模式下改由暫放專用邏輯處理，避免攔截 Shift+數字標點）
            if (!g_state.bufferMode && key >= '1' && key <= '9') {
                if (g_state.showCand && g_state.isInputting) {
                    PostMessage(g_state.hWnd, WM_USER+100, key, 0);
                    return 1;
                }
            }

            // 暫放視窗焦點判斷
            bool isBufferWindowActive = false;
            if (g_state.bufferMode && g_state.hBufferWnd) {
                HWND foregroundWnd = GetForegroundWindow();
                isBufferWindowActive = (IsWindowVisible(g_state.hBufferWnd) && 
                                       g_state.bufferHasFocus &&
                                       (foregroundWnd == g_state.hWnd || foregroundWnd == g_state.hBufferWnd));
            }

            // 暫放模式下攔截邏輯
            if (g_state.bufferMode) {
                bool shouldInterceptForBuffer = false;
                
                if (isBufferWindowActive) {
                    shouldInterceptForBuffer = true;
                } else {
                    // 主鍵盤 0 在暫放模式下一律攔截並寫入暫放；1–9 僅在無候選時攔截（有候選時留給選字）
                    // Num Lock 小鍵盤（0–9、* + - . /）一律攔截並寫入暫放
                    shouldInterceptForBuffer = (
                        (key >= 'A' && key <= 'Z') ||
                        (key >= '0' && key <= '9' && (!g_state.showCand || key == '0')) ||
                        (key >= VK_NUMPAD0 && key <= VK_DIVIDE) ||
                        (key == 'U' || key == 'I' || key == 'O' || key == 'J' || key == 'K' || key == 'L') ||
                        (key == VK_OEM_COMMA || key == VK_OEM_PERIOD || key == VK_OEM_2 ||
                         key == VK_OEM_1 || key == VK_OEM_4 || key == VK_OEM_6 || key == VK_OEM_7 ||
                         key == VK_OEM_MINUS || key == VK_OEM_PLUS || key == VK_OEM_5 || key == VK_OEM_3 ||
                         key == VK_SPACE)
                    );
                }
                
                if (shouldInterceptForBuffer) {
                    if (!isBufferWindowActive && g_state.hBufferWnd) {
                        g_state.bufferHasFocus = true;
                        SetTimer(g_state.hBufferWnd, 1, 500, NULL);
                        InvalidateRect(g_state.hBufferWnd, nullptr, TRUE);
                        isBufferWindowActive = true;
                    }
                }
            }

            // 暫放視窗輸入處理
            if (isBufferWindowActive) {
                // 如果有候選字或標點符號選單打開，數字鍵（未按 Shift）用於選擇候選項；Shift+數字則強制輸入標點到暫放視窗
                if ((g_state.showCand || g_state.showPunctMenu) && (key >= '1' && key <= '9') && !g_state.shiftPressed) {
                    PostMessage(g_state.hWnd, WM_USER+100, key, 0);
                    return 1;
                }
                
                // 方向鍵控制（含主鍵盤上下左右、Home/End，以及 NumLock 關閉時小鍵盤 2/4/6/8）
                DWORD navKey = 0;
                if (key == VK_LEFT || key == VK_RIGHT || key == VK_UP || key == VK_DOWN || key == VK_HOME || key == VK_END) {
                    navKey = key;
                } else if ((GetKeyState(VK_NUMLOCK) & 1) == 0) {
                    // NumLock 關閉時，小鍵盤 2/4/6/8 為下/左/右/上
                    switch (key) {
                        case VK_NUMPAD2: navKey = VK_DOWN; break;
                        case VK_NUMPAD4: navKey = VK_LEFT; break;
                        case VK_NUMPAD6: navKey = VK_RIGHT; break;
                        case VK_NUMPAD8: navKey = VK_UP; break;
                        default: break;
                    }
                }
                if (navKey != 0) {
                    if (navKey == VK_LEFT) {
                        BufferManager::moveCursor(g_state, -1);
                    } else if (navKey == VK_RIGHT) {
                        BufferManager::moveCursor(g_state, 1);
                    } else if (navKey == VK_HOME) {
                        g_state.bufferCursorPos = 0;
                        if (g_state.hBufferWnd) InvalidateRect(g_state.hBufferWnd, nullptr, TRUE);
                    } else if (navKey == VK_END) {
                        g_state.bufferCursorPos = g_state.bufferText.length();
                        if (g_state.hBufferWnd) InvalidateRect(g_state.hBufferWnd, nullptr, TRUE);
                    } else if (navKey == VK_UP || navKey == VK_DOWN) {
                        // 依目前光標位置計算座標，再往上一行或下一行移動
                        POINT pt = BufferManager::getPointFromTextPosition(g_state, g_state.bufferCursorPos);
                        int lineHeight = g_state.bufferFontSize + 2;
                        if (navKey == VK_UP) {
                            pt.y -= lineHeight;
                        } else {
                            pt.y += lineHeight;
                        }
                        BufferManager::setCursorPosition(g_state, pt.x, pt.y);
                    }
                    return 1;
                }
                
                // Backspace處理
                if (key == VK_BACK) {
                    if (g_state.shiftPressed) {
                        BufferManager::deleteCharAtCursor(g_state, false);
                        return 1;
                    } else if (g_state.isInputting && !g_state.input.empty()) {
                        PostMessage(g_state.hWnd, WM_USER+100, VK_BACK, 0);
                        return 1;
                    } else if (!g_state.bufferText.empty()) {
                        BufferManager::deleteCharAtCursor(g_state, false);
                        return 1;
                    }
                    return CallNextHookEx(g_hKeyboardHook, nCode, wParam, lParam);
                }
                
                // Delete處理
                if (key == VK_DELETE) {
                    if (!g_state.bufferText.empty()) {
                        BufferManager::deleteCharAtCursor(g_state, true);
                        return 1;
                    }
                    return CallNextHookEx(g_hKeyboardHook, nCode, wParam, lParam);
                }
                
                // NumPad筆劃輸入支援（包含萬用字元 * 對應的小鍵盤鍵）；暫放+中文模式下仍當筆劃
                bool isNumpadStrokeKey = (key == VK_NUMPAD7 || key == VK_NUMPAD8 || key == VK_NUMPAD9 || 
                                         key == VK_NUMPAD4 || key == VK_NUMPAD5 || key == VK_NUMPAD0 ||
                                         key == static_cast<DWORD>(g_state.wildcardKey2VK));
                if (isNumpadStrokeKey && g_state.chineseMode) {
                    PostMessage(g_state.hWnd, WM_USER+100, key, 0);
                    return 1;
                }
				
				// 處理所有 Numpad 鍵：中文模式下 7,8,9,4,5,0 可當筆劃；其餘為數字/運算符寫入暫放
				if ((key >= VK_NUMPAD0 && key <= VK_DIVIDE)) {
    if (g_state.chineseMode && !g_state.showCand) {
        bool isStrokeKey = (key == VK_NUMPAD7 || key == VK_NUMPAD8 || 
                           key == VK_NUMPAD9 || key == VK_NUMPAD4 || 
                           key == VK_NUMPAD5 || key == VK_NUMPAD0 ||
                           key == static_cast<DWORD>(g_state.wildcardKey2VK));
        if (isStrokeKey) {
            PostMessage(g_state.hWnd, WM_USER+100, key, 0);
            return 1;
        }
    }
    
    // 其餘情況：直接輸入對應數字/運算符
    wchar_t ch = 0;
    switch(key) {
        // 數字鍵
        case VK_NUMPAD0: ch = L'0'; break;
        case VK_NUMPAD1: ch = L'1'; break;
        case VK_NUMPAD2: ch = L'2'; break;
        case VK_NUMPAD3: ch = L'3'; break;
        case VK_NUMPAD4: ch = L'4'; break;
        case VK_NUMPAD5: ch = L'5'; break;
        case VK_NUMPAD6: ch = L'6'; break;
        case VK_NUMPAD7: ch = L'7'; break;
        case VK_NUMPAD8: ch = L'8'; break;
        case VK_NUMPAD9: ch = L'9'; break;
        // 運算符和標點
        case VK_MULTIPLY: ch = L'*'; break;
        case VK_ADD: ch = L'+'; break;
        case VK_SUBTRACT: ch = L'-'; break;
        case VK_DECIMAL: ch = L'.'; break;
        case VK_DIVIDE: ch = L'/'; break;
    }
    
    if (ch != 0) {
        BufferManager::insertTextAtCursor(g_state, std::wstring(1, ch));
        // 暫放模式：輸入標點時，同步清空/隱藏輸入視窗與候選/聯想視窗（效果類似按 ESC）
        if (ch == L'*' || ch == L'+' || ch == L'-' || ch == L'.' || ch == L'/') {
            g_state.input.clear();
            g_state.candidates.clear();
            g_state.candidateCodes.clear();
            g_state.showCand = false;
            g_state.isInputting = false;
            g_state.inputError = false;
            g_state.showPunctMenu = false;
            if (g_state.hCandWnd) ShowWindow(g_state.hCandWnd, SW_HIDE);
            if (g_state.hPredWnd) ShowWindow(g_state.hPredWnd, SW_HIDE);
            if (g_state.hInputWnd) ShowWindow(g_state.hInputWnd, SW_HIDE);
            if (g_state.hWnd) InvalidateRect(g_state.hWnd, nullptr, TRUE);
        }
        return 1;
    }
}
                
                // 普通筆劃輸入（包含P鍵用於標點選單，與萬用字元 * 鍵）
                bool isWildcardKey =
                    key == static_cast<DWORD>(g_state.wildcardKey1VK) ||
                    key == static_cast<DWORD>(g_state.wildcardKey2VK);
                bool isStrokeKey = (key == 'U' || key == 'I' || key == 'O' ||
                                    key == 'J' || key == 'K' || key == 'P' || isWildcardKey);
                if (isStrokeKey && g_state.chineseMode) {
                    PostMessage(g_state.hWnd, WM_USER+100, key, 0);
                    return 1;
                }
                
                // Shift+數字標點處理
                if (g_state.shiftPressed && ((key >= '0' && key <= '9') || key == VK_OEM_COMMA || key == VK_OEM_PERIOD)) {
                    std::wstring punctChar = L"";
                    switch(key) {
                        case '1': punctChar = g_state.chineseMode ? L"！" : L"!"; break;
                        case '2': punctChar = g_state.chineseMode ? L"＠" : L"@"; break;
                        case '3': punctChar = g_state.chineseMode ? L"＃" : L"#"; break;
                        case '4': punctChar = g_state.chineseMode ? L"＄" : L"$"; break;
                        case '5': punctChar = g_state.chineseMode ? L"％" : L"%"; break;
                        case '6': punctChar = g_state.chineseMode ? L"⌃" : L"^"; break;
                        case '7': punctChar = g_state.chineseMode ? L"＆" : L"&"; break;
                        case '8': punctChar = g_state.chineseMode ? L"＊" : L"*"; break;
                        case '9': punctChar = g_state.chineseMode ? L"（" : L"("; break;
                        case '0': punctChar = g_state.chineseMode ? L"）" : L")"; break;
                        case VK_OEM_COMMA: punctChar = g_state.chineseMode ? L"《" : L"<"; break;
                        case VK_OEM_PERIOD: punctChar = g_state.chineseMode ? L"》" : L">"; break;
                    }
                    if (!punctChar.empty()) {
                        BufferManager::insertTextAtCursor(g_state, punctChar);
                        // 暫放模式：Shift+數字/逗點/句點輸入標點後，同步清空/隱藏輸入相關視窗
                        g_state.input.clear();
                        g_state.candidates.clear();
                        g_state.candidateCodes.clear();
                        g_state.showCand = false;
                        g_state.isInputting = false;
                        g_state.inputError = false;
                        g_state.showPunctMenu = false;
                        if (g_state.hCandWnd) ShowWindow(g_state.hCandWnd, SW_HIDE);
                        if (g_state.hPredWnd) ShowWindow(g_state.hPredWnd, SW_HIDE);
                        if (g_state.hInputWnd) ShowWindow(g_state.hInputWnd, SW_HIDE);
                        if (g_state.hWnd) InvalidateRect(g_state.hWnd, nullptr, TRUE);
                        return 1;
                    }
                }
                
                // 數字輸入：主鍵盤 0 一律寫入暫放；1–9 僅在無候選時寫入（有候選時已在上方用於選字）
                if (key >= '0' && key <= '9' && !g_state.shiftPressed && (!g_state.showCand || key == '0')) {
					// 暫放模式下數字一律輸入半形
					wchar_t halfWidthNum = key;
					std::wstring converted(1, halfWidthNum);
					BufferManager::insertTextAtCursor(g_state, converted);
                    return 1;
                }
                
                // 英文字母輸入（排除中文模式下的P鍵）
                if (key >= 'A' && key <= 'Z') {
                    // 中文模式下P鍵用於標點符號選單，不當作普通字母處理
                    if (key == 'P' && g_state.chineseMode) {
                        PostMessage(g_state.hWnd, WM_USER+100, key, 0);
                        return 1;
                    }
                    
                    bool isCapital = ((GetKeyState(VK_CAPITAL) & 0x0001) != 0) ^ g_state.shiftPressed;
                    wchar_t ch = key;
                    if (!isCapital) {
                        ch = key + 32;
                    }
						// 暫放模式下英文字母一律輸入半形
						std::wstring converted(1, ch);
						BufferManager::insertTextAtCursor(g_state, converted);
						return 1;
                }
                
                // 標點符號處理
                bool isPunctKey = (key == VK_OEM_COMMA || key == VK_OEM_PERIOD || key == VK_OEM_2 ||
                                  key == VK_OEM_1 || key == VK_OEM_4 || key == VK_OEM_6 || key == VK_OEM_7 ||
                                  key == VK_OEM_MINUS || key == VK_OEM_PLUS || key == VK_OEM_5 || key == VK_OEM_3 ||
                                  key == VK_SPACE);
                
                if (isPunctKey && !g_state.shiftPressed) {
                    std::wstring punctChar = L"";
                    
                    switch (key) {
                        case VK_SPACE: punctChar = L" "; break; // 暫放模式下空白鍵一律輸入半形空格
                        case VK_OEM_COMMA: punctChar = g_state.chineseMode ? L"，" : L","; break;
                        case VK_OEM_PERIOD: punctChar = g_state.chineseMode ? L"。" : L"."; break;
                        case VK_OEM_2: punctChar = g_state.chineseMode ? L"／" : L"/"; break;
                        case VK_OEM_1: punctChar = g_state.chineseMode ? L"；" : L";"; break;
                        case VK_OEM_4: punctChar = g_state.chineseMode ? L"「" : L"["; break;
                        case VK_OEM_6: punctChar = g_state.chineseMode ? L"」" : L"]"; break;
                        case VK_OEM_7: punctChar = g_state.chineseMode ? L"、" : L"'"; break;
                        case VK_OEM_MINUS: punctChar = g_state.chineseMode ? L"－" : L"-"; break;
                        case VK_OEM_PLUS: punctChar = g_state.chineseMode ? L"＝" : L"="; break;
                        case VK_OEM_5: punctChar = g_state.chineseMode ? L"＼" : L"\\"; break;
                        case VK_OEM_3: punctChar = g_state.chineseMode ? L"`" : L"`"; break;
                    }
					
					// 特別處理：暫放模式下空白鍵強制輸入半形空格
					if (key == VK_SPACE) {
					punctChar = L" ";
					}
                    
                    if (!punctChar.empty()) {
                        BufferManager::insertTextAtCursor(g_state, punctChar);
                        // 暫放模式：主鍵盤標點（無 Shift）後也清空/隱藏輸入相關視窗
                        g_state.input.clear();
                        g_state.candidates.clear();
                        g_state.candidateCodes.clear();
                        g_state.showCand = false;
                        g_state.isInputting = false;
                        g_state.inputError = false;
                        g_state.showPunctMenu = false;
                        if (g_state.hCandWnd) ShowWindow(g_state.hCandWnd, SW_HIDE);
                        if (g_state.hPredWnd) ShowWindow(g_state.hPredWnd, SW_HIDE);
                        if (g_state.hInputWnd) ShowWindow(g_state.hInputWnd, SW_HIDE);
                        if (g_state.hWnd) InvalidateRect(g_state.hWnd, nullptr, TRUE);
                        return 1;
                    }
                }
                
                // Shift+其他標點符號
                if (g_state.shiftPressed && isPunctKey) {
                    std::wstring punctChar = L"";
                    
                        switch (key) {
							case VK_OEM_2: punctChar = g_state.chineseMode ? L"？" : L"?"; break;
							case VK_OEM_1: punctChar = g_state.chineseMode ? L"：" : L":"; break;
							case VK_OEM_4: punctChar = g_state.chineseMode ? L"『" : L"{"; break;
							case VK_OEM_6: punctChar = g_state.chineseMode ? L"』" : L"}"; break;
							case VK_OEM_7: punctChar = g_state.chineseMode ? L"＂" : L"\""; break;
							case VK_OEM_MINUS: punctChar = g_state.chineseMode ? L"＿" : L"_"; break;
							case VK_OEM_PLUS: punctChar = g_state.chineseMode ? L"＋" : L"+"; break;
							case VK_OEM_5: punctChar = g_state.chineseMode ? L"｜" : L"|"; break;
							case VK_OEM_3: punctChar = g_state.chineseMode ? L"～" : L"~"; break;
						}
    
						if (!punctChar.empty()) {
							BufferManager::insertTextAtCursor(g_state, punctChar);
                            // 暫放模式：主鍵盤 Shift+標點 後清空/隱藏輸入相關視窗
                            g_state.input.clear();
                            g_state.candidates.clear();
                            g_state.candidateCodes.clear();
                            g_state.showCand = false;
                            g_state.isInputting = false;
                            g_state.inputError = false;
                            g_state.showPunctMenu = false;
                            if (g_state.hCandWnd) ShowWindow(g_state.hCandWnd, SW_HIDE);
                            if (g_state.hPredWnd) ShowWindow(g_state.hPredWnd, SW_HIDE);
                            if (g_state.hInputWnd) ShowWindow(g_state.hInputWnd, SW_HIDE);
                            if (g_state.hWnd) InvalidateRect(g_state.hWnd, nullptr, TRUE);
							return 1;
                    }
                }
            }

            // 暫放模式：當候選/輸入視窗顯示（焦點不在暫放視窗）時，Shift+數字/逗號/句點仍強制寫入暫放視窗並清空視窗
            if (g_state.bufferMode && !isBufferWindowActive && g_state.shiftPressed &&
                ((key >= '0' && key <= '9') || key == VK_OEM_COMMA || key == VK_OEM_PERIOD)) {
                std::wstring punctChar = L"";
                switch (key) {
                    case '1': punctChar = g_state.chineseMode ? L"！" : L"!"; break;
                    case '2': punctChar = g_state.chineseMode ? L"＠" : L"@"; break;
                    case '3': punctChar = g_state.chineseMode ? L"＃" : L"#"; break;
                    case '4': punctChar = g_state.chineseMode ? L"＄" : L"$"; break;
                    case '5': punctChar = g_state.chineseMode ? L"％" : L"%"; break;
                    case '6': punctChar = g_state.chineseMode ? L"⌃" : L"^"; break;
                    case '7': punctChar = g_state.chineseMode ? L"＆" : L"&"; break;
                    case '8': punctChar = g_state.chineseMode ? L"＊" : L"*"; break;
                    case '9': punctChar = g_state.chineseMode ? L"（" : L"("; break;
                    case '0': punctChar = g_state.chineseMode ? L"）" : L")"; break;
                    case VK_OEM_COMMA: punctChar = g_state.chineseMode ? L"《" : L"<"; break;
                    case VK_OEM_PERIOD: punctChar = g_state.chineseMode ? L"》" : L">"; break;
                }
                if (!punctChar.empty()) {
                    BufferManager::insertTextAtCursor(g_state, punctChar);
                    g_state.input.clear();
                    g_state.candidates.clear();
                    g_state.candidateCodes.clear();
                    g_state.showCand = false;
                    g_state.isInputting = false;
                    g_state.inputError = false;
                    g_state.showPunctMenu = false;
                    if (g_state.hCandWnd) ShowWindow(g_state.hCandWnd, SW_HIDE);
                    if (g_state.hPredWnd) ShowWindow(g_state.hPredWnd, SW_HIDE);
                    if (g_state.hInputWnd) ShowWindow(g_state.hInputWnd, SW_HIDE);
                    if (g_state.hWnd) InvalidateRect(g_state.hWnd, nullptr, TRUE);
                    if (g_state.hBufferWnd) InvalidateRect(g_state.hBufferWnd, nullptr, TRUE);
                    return 1;
                }
            }

            // Enter鍵處理
            if (key == VK_RETURN) {
                if (isBufferWindowActive && !g_state.showCand && !g_state.isInputting && !g_state.bufferText.empty()) {
                    PostMessage(g_state.hWnd, WM_USER+100, VK_RETURN, 0);
                    return 1;
                }
            }

            // 正常中文輸入處理
            if (!g_state.bufferMode || !isBufferWindowActive) {
                bool isWildcardKey =
                    key == static_cast<DWORD>(g_state.wildcardKey1VK) ||
                    key == static_cast<DWORD>(g_state.wildcardKey2VK);
                bool isStrokeKey = (key == 'U' || key == 'I' || key == 'O' || key == 'J' || key == 'K' || key == 'P' || isWildcardKey ||
                                    key == VK_NUMPAD7 || key == VK_NUMPAD8 || key == VK_NUMPAD9 || 
                                    key == VK_NUMPAD4 || key == VK_NUMPAD5 || key == VK_NUMPAD0);

                bool isPunctKey = (key == VK_OEM_COMMA || key == VK_OEM_PERIOD || key == VK_OEM_2 ||
                       key == VK_OEM_1 || key == VK_OEM_4 || key == VK_OEM_6 || key == VK_OEM_7 ||
                       key == VK_OEM_MINUS || key == VK_OEM_PLUS || key == VK_OEM_5 || key == VK_OEM_3 ||
                       (key == '1' && g_state.shiftPressed) || (key == '2' && g_state.shiftPressed) ||
                       (key == '3' && g_state.shiftPressed) || (key == '4' && g_state.shiftPressed) ||
                       (key == '5' && g_state.shiftPressed) || (key == '6' && g_state.shiftPressed) ||
                       (key == '7' && g_state.shiftPressed) || (key == '8' && g_state.shiftPressed) ||
                       (key == '9' && g_state.shiftPressed) || (key == '0' && g_state.shiftPressed) ||
                       key == VK_MULTIPLY || key == VK_ADD || key == VK_SUBTRACT || key == VK_DECIMAL || key == VK_DIVIDE);
                
                bool isFunctionKey = ((g_state.isInputting || g_state.showPunctMenu) && 
                          (key == VK_SPACE || key == VK_BACK || key == VK_ESCAPE ||
                           key == VK_UP || key == VK_DOWN || key == VK_TAB ||
                           (key >= '1' && key <= '9')));

                if (g_state.chineseMode) {
                    // 中文模式：攔截筆劃鍵、標點符號和功能鍵
                    // 注意：Windows IME 已在函數開頭統一禁用，這裡不需要重複調用
                    if (isStrokeKey || isPunctKey || (key == VK_SPACE && g_state.isInputting) || isFunctionKey) {
                        PostMessage(g_state.hWnd, WM_USER+100, key, 0);
                        return 1;
                    }
                } else {
                    // 英文模式：只在有候選字或輸入狀態時才攔截功能鍵
                    // 標點符號直接放行，讓系統處理
                    if (isFunctionKey) {
                         PostMessage(g_state.hWnd, WM_USER+100, key, 0);
                         return 1;
                    }
                    // 英文模式下標點符號直接放行，不攔截
                }
            }
        }
        else if (wParam == WM_KEYUP) {
            if (key != VK_SHIFT && key != VK_LSHIFT && key != VK_RSHIFT && !g_state.shiftPressed) {
                g_state.shiftUsedForCombo = false;
            }
        }
    }
    
    return CallNextHookEx(g_hKeyboardHook, nCode, wParam, lParam);
}

} // namespace InputHandler
