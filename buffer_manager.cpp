// buffer_manager.cpp - 暫放管理實作
#include "buffer_manager.h"
#include "input_handler.h"
#include "window_manager.h"
#include <fstream>
#include <ctime>
#include <iomanip>
#include <windows.h>
namespace BufferManager {

static const size_t MAX_BUFFER_FILE_BYTES = 2 * 1024 * 1024;   // 防止誤讀到大型二進位檔
static const size_t MAX_BUFFER_TEXT_CHARS = 200000;            // 防止異常內容撐爆 UI

static bool isHighSurrogate(wchar_t ch) {
    return ch >= 0xD800 && ch <= 0xDBFF;
}

static bool isLowSurrogate(wchar_t ch) {
    return ch >= 0xDC00 && ch <= 0xDFFF;
}

static int normalizeCursorPos(const std::wstring& text, int pos) {
    if (pos < 0) pos = 0;
    if (pos > (int)text.length()) pos = (int)text.length();
    // 避免游標停在代理對中間（高代理之後、低代理之前）
    if (pos > 0 && pos < (int)text.length() &&
        isLowSurrogate(text[pos]) && isHighSurrogate(text[pos - 1])) {
        pos++;
        if (pos > (int)text.length()) pos = (int)text.length();
    }
    return pos;
}

static int codeUnitStepForward(const std::wstring& text, int pos) {
    if (pos < 0 || pos >= (int)text.length()) return 0;
    if (pos + 1 < (int)text.length() &&
        isHighSurrogate(text[pos]) && isLowSurrogate(text[pos + 1])) {
        return 2;
    }
    return 1;
}

static int codeUnitStepBackward(const std::wstring& text, int pos) {
    if (pos <= 0 || pos > (int)text.length()) return 0;
    if (pos - 2 >= 0 &&
        isHighSurrogate(text[pos - 2]) && isLowSurrogate(text[pos - 1])) {
        return 2;
    }
    return 1;
}

static bool decodeUtf8Strict(const std::string& utf8, std::wstring& out) {
    out.clear();
    if (utf8.empty()) return true;

    int required = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
        utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
    if (required <= 0) return false;

    out.assign(required, L'\0');
    int converted = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
        utf8.data(), static_cast<int>(utf8.size()), &out[0], required);
    return converted == required;
}

int calculateBufferWindowHeight(const GlobalState& state) {
    // 确保最小高度足够容纳控制列和按钮
    int minRequiredHeight = 60 + CONTROL_BAR_HEIGHT; // 60px文字区域 + 控制列
    
    if (state.bufferText.empty()) {
        return std::max(MIN_HEIGHT, minRequiredHeight);
    }
    
    int lineCount = (state.bufferText.length() + CHARS_PER_LINE - 1) / CHARS_PER_LINE;
    lineCount = std::max(1, lineCount);
    
    int contentHeight = lineCount * LINE_HEIGHT + 30; // 从20增加到30
    int totalHeight = contentHeight + CONTROL_BAR_HEIGHT;
    
    // 强制确保最小高度
    totalHeight = std::max(totalHeight, minRequiredHeight);
    
    return std::min(totalHeight, MAX_HEIGHT);
}

void saveBufferToFile(const GlobalState& state) {
    try {
        std::string path = Utils::wstrToUtf8(state.userDir + L"text_buffer.txt");
        std::ofstream file(path, std::ios::out | std::ios::binary);
        if (file.is_open()) {
            const char bom[] = {static_cast<char>(0xEF), 
                               static_cast<char>(0xBB), 
                               static_cast<char>(0xBF)};
            file.write(bom, 3);
            
            std::string utf8_text = Utils::wstrToUtf8(state.bufferText);
            file.write(utf8_text.c_str(), utf8_text.length());
            file.close();
        }
    } catch (...) {}
}

void loadBufferFromFile(GlobalState& state) {
    try {
        std::string path = Utils::wstrToUtf8(state.userDir + L"text_buffer.txt");
        std::ifstream file(path, std::ios::binary);
        if (file.is_open()) {
            std::string content((std::istreambuf_iterator<char>(file)),
                              std::istreambuf_iterator<char>());
            file.close();

            if (content.size() > MAX_BUFFER_FILE_BYTES) {
                state.bufferText.clear();
                state.bufferCursorPos = 0;
                saveBufferToFile(state);  // 清掉疑似誤寫入的大型檔案
                Utils::updateStatus(state, L"偵測到異常暫放檔案，已自動清空");
                return;
            }
            
            if (content.length() >= 3 && 
                content[0] == static_cast<char>(0xEF) &&
                content[1] == static_cast<char>(0xBB) &&
                content[2] == static_cast<char>(0xBF)) {
                content = content.substr(3);
            }

            std::wstring decoded;
            if (!decodeUtf8Strict(content, decoded) || decoded.size() > MAX_BUFFER_TEXT_CHARS) {
                state.bufferText.clear();
                state.bufferCursorPos = 0;
                saveBufferToFile(state);  // 清掉非 UTF-8/異常內容，避免每次啟動都壞掉
                Utils::updateStatus(state, L"暫放內容格式異常，已自動重置");
                return;
            }

            state.bufferText = decoded;
            state.bufferCursorPos = state.bufferText.length();
        }
    } catch (...) {}
}

void saveBufferToTimestampedFile(const GlobalState& state) {
    try {
        time_t now = time(nullptr);
        struct tm* timeinfo = localtime(&now);
        
        wchar_t filename[256];
        swprintf_s(filename, 256, L"stroke_%04d%02d%02d_%02d%02d%02d.txt",
                   timeinfo->tm_year + 1900,
                   timeinfo->tm_mon + 1,
                   timeinfo->tm_mday,
                   timeinfo->tm_hour,
                   timeinfo->tm_min,
                   timeinfo->tm_sec);
        
        std::wstring fullPath = state.userDir + filename;
        std::string pathStr = Utils::wstrToUtf8(fullPath);
        std::ofstream file(pathStr, std::ios::out | std::ios::binary);
        if (file.is_open()) {
            const char bom[] = {static_cast<char>(0xEF), 
                               static_cast<char>(0xBB), 
                               static_cast<char>(0xBF)};
            file.write(bom, 3);
            
            std::string content = Utils::wstrToUtf8(state.bufferText);
            file.write(content.c_str(), content.length());
            file.close();
            
            std::wstring successMsg = L"已儲存到檔案: " + std::wstring(filename);
            Utils::updateStatus(const_cast<GlobalState&>(state), successMsg);
        }
    } catch (...) {
        Utils::updateStatus(const_cast<GlobalState&>(state), L"儲存失敗：無法建立檔案");
    }
}

void sendBufferContent(GlobalState& state) {
    if (!state.bufferText.empty()) {
        bool wasBufferVisible = state.bufferMode && IsWindowVisible(state.hBufferWnd);
        RECT bufferRect;
        if (wasBufferVisible) {
            GetWindowRect(state.hBufferWnd, &bufferRect);
        }
        
        // 發送文字
        InputHandler::sendTextDirectUnicode(state.bufferText);
        Utils::updateStatus(state, L"已發送暫放文字：" + std::to_wstring(state.bufferText.length()) + L"字");
        
        // 清空暫放區
        state.bufferText.clear();
        state.bufferCursorPos = 0;
        saveBufferToFile(state);
        
        // 更新暫放視窗
        if (state.hBufferWnd) {
            int newHeight = calculateBufferWindowHeight(state);
            if (state.useOptimizedUI) {
                // OptimizedUI模式下更新位置
                WindowManager::updateBufferWindowPosition(state);
            } else {
                SetWindowPos(state.hBufferWnd, NULL, 0, 0, FIXED_WIDTH, newHeight, 
                            SWP_NOMOVE | SWP_NOZORDER);
            }
            
            if (wasBufferVisible && state.bufferMode) {
                ShowWindow(state.hBufferWnd, SW_SHOWNOACTIVATE);
                state.bufferHasFocus = true;
                SetTimer(state.hBufferWnd, 1, 500, NULL);
            }
            
            InvalidateRect(state.hBufferWnd, nullptr, TRUE);
        }
    } else {
        Utils::updateStatus(state, L"暫放區為空，無內容可發送");
    }
}


void clearBufferWithConfirm(GlobalState& state) {
    if (!state.bufferText.empty()) {
        if (state.bufferMode) {
            state.bufferText.clear();
            state.bufferCursorPos = 0;
            saveBufferToFile(state);
            Utils::updateStatus(state, L"暫放文字已清空");
            
            if (state.hBufferWnd) {
                SetWindowPos(state.hBufferWnd, NULL, 0, 0, FIXED_WIDTH, MIN_HEIGHT, 
                            SWP_NOMOVE | SWP_NOZORDER);
                InvalidateRect(state.hBufferWnd, nullptr, TRUE);
            }
        } else {
            int result = MessageBoxW(state.hWnd, 
                L"確定要清空暫放文字嗎？\n清空後將無法恢復。", 
                L"確認清空", 
                MB_YESNO | MB_ICONQUESTION);
            
            if (result == IDYES) {
                state.bufferText.clear();
                state.bufferCursorPos = 0;
                saveBufferToFile(state);
                Utils::updateStatus(state, L"暫放文字已清空");
                
                if (state.hBufferWnd) {
                    SetWindowPos(state.hBufferWnd, NULL, 0, 0, FIXED_WIDTH, MIN_HEIGHT, 
                                SWP_NOMOVE | SWP_NOZORDER);
                    InvalidateRect(state.hBufferWnd, nullptr, TRUE);
                }
            }
        }
    }
}

void toggleBufferMode(GlobalState& state) {
    state.bufferMode = !state.bufferMode;
    
    if (state.bufferMode) {
        loadBufferFromFile(state);
        state.bufferHasFocus = true;
        
        if (state.hBufferWnd) {
            // 確保暫放視窗始終顯示在工具列下方
            WindowManager::updateBufferWindowPosition(state);
            
            // 設定計時器用於游標閃爍
            SetTimer(state.hBufferWnd, 1, 500, NULL);
        }
        
        Utils::updateStatus(state, L"已進入暫放模式（支持中英文直接編輯）");
    } else {
        state.bufferHasFocus = false;
        if (state.hBufferWnd) {
            KillTimer(state.hBufferWnd, 1);
            ShowWindow(state.hBufferWnd, SW_HIDE);
        }
		clearHistory(state);
        
        Utils::updateStatus(state, L"已退出暫放模式");
    }
    
    // 更新工具列按鈕狀態（OptimizedUI）
    if (state.useOptimizedUI && state.hWnd) {
        InvalidateRect(state.hWnd, nullptr, TRUE);
    }
}

void insertTextAtCursor(GlobalState& state, const std::wstring& text) {
	saveSnapshot(state);
    state.bufferCursorPos = normalizeCursorPos(state.bufferText, state.bufferCursorPos);
    
    state.bufferText.insert(state.bufferCursorPos, text);
    state.bufferCursorPos += text.length();
    state.bufferCursorPos = normalizeCursorPos(state.bufferText, state.bufferCursorPos);
    
    saveBufferToFile(state);
    
    // 剪貼簿模式：無論是否有焦點，都要檢測輸入狀態並更新剪貼簿
    if (state.clipboardMode) {
        state.clipboardInputting = true;
        state.clipboardCopied = false;
        state.clipboardLastInputTime = GetTickCount();
        
        // 只有在暫放視窗沒有焦點時才立即更新剪貼簿（用戶在其他位置操作時）
        // 如果有焦點，會在失去焦點時更新剪貼簿（見WM_KILLFOCUS處理）
        if (!state.bufferHasFocus) {
            updateClipboardInMode(state);
        }
        
        // 確保窗口重繪以顯示狀態指示器變化
        if (state.hBufferWnd) {
            InvalidateRect(state.hBufferWnd, nullptr, TRUE);
            UpdateWindow(state.hBufferWnd);  // 立即更新窗口
        }
    }
    
    if (state.hBufferWnd) {
        int windowHeight = calculateBufferWindowHeight(state);
        
        // 新增：OptimizedUI模式下的特殊處理
        if (state.useOptimizedUI) {
            // 保持相對於工具列的位置，只調整高度
            RECT currentBufferRect;
            GetWindowRect(state.hBufferWnd, &currentBufferRect);
            
            SetWindowPos(state.hBufferWnd, NULL, 
                        currentBufferRect.left, currentBufferRect.top, 
                        FIXED_WIDTH, windowHeight, 
                        SWP_NOZORDER);
        } else {
            SetWindowPos(state.hBufferWnd, NULL, 0, 0, FIXED_WIDTH, windowHeight, 
                        SWP_NOMOVE | SWP_NOZORDER);
        }
        
        InvalidateRect(state.hBufferWnd, nullptr, TRUE);
    }
}

void deleteCharAtCursor(GlobalState& state, bool forward) {
    if (state.bufferText.empty()) return;
	
	saveSnapshot(state);
    
	    //如果有選取文字，優先刪除選取的內容
    if (state.hasSelection) {
        deleteSelection(state);
        // 剪貼簿模式：無論是否有焦點，都要檢測輸入狀態並更新剪貼簿
        if (state.clipboardMode) {
            state.clipboardInputting = true;
            state.clipboardCopied = false;
            state.clipboardLastInputTime = GetTickCount();
            
            // 只有在暫放視窗沒有焦點時才立即更新剪貼簿
            if (!state.bufferHasFocus) {
                updateClipboardInMode(state);
            }
            
            if (state.hBufferWnd) {
                InvalidateRect(state.hBufferWnd, nullptr, TRUE);
                UpdateWindow(state.hBufferWnd);  // 立即更新窗口
            }
        }
        return;
    }
	
    state.bufferCursorPos = normalizeCursorPos(state.bufferText, state.bufferCursorPos);

    if (forward) {
        if (state.bufferCursorPos < (int)state.bufferText.length()) {
            int eraseLen = codeUnitStepForward(state.bufferText, state.bufferCursorPos);
            if (eraseLen <= 0) eraseLen = 1;
            state.bufferText.erase(state.bufferCursorPos, eraseLen);
        }
    } else {
        if (state.bufferCursorPos > 0) {
            int step = codeUnitStepBackward(state.bufferText, state.bufferCursorPos);
            if (step <= 0) step = 1;
            int eraseStart = state.bufferCursorPos - step;
            state.bufferText.erase(eraseStart, step);
            state.bufferCursorPos = eraseStart;
        }
    }

    state.bufferCursorPos = normalizeCursorPos(state.bufferText, state.bufferCursorPos);
    
    saveBufferToFile(state);
    
    // 剪貼簿模式：無論是否有焦點，都要檢測輸入狀態並更新剪貼簿
    if (state.clipboardMode) {
        state.clipboardInputting = true;
        state.clipboardCopied = false;
        state.clipboardLastInputTime = GetTickCount();
        
        // 只有在暫放視窗沒有焦點時才立即更新剪貼簿
        if (!state.bufferHasFocus) {
            updateClipboardInMode(state);
        }
        
        // 確保窗口重繪以顯示狀態指示器變化
        if (state.hBufferWnd) {
            InvalidateRect(state.hBufferWnd, nullptr, TRUE);
            UpdateWindow(state.hBufferWnd);  // 立即更新窗口
        }
    }
    
    if (state.hBufferWnd) {
        int windowHeight = calculateBufferWindowHeight(state);
        
        // 新增：OptimizedUI模式下的特殊處理
        if (state.useOptimizedUI) {
            RECT currentBufferRect;
            GetWindowRect(state.hBufferWnd, &currentBufferRect);
            
            SetWindowPos(state.hBufferWnd, NULL, 
                        currentBufferRect.left, currentBufferRect.top, 
                        FIXED_WIDTH, windowHeight, 
                        SWP_NOZORDER);
        } else {
            SetWindowPos(state.hBufferWnd, NULL, 0, 0, FIXED_WIDTH, windowHeight, 
                        SWP_NOMOVE | SWP_NOZORDER);
        }
        
        InvalidateRect(state.hBufferWnd, nullptr, TRUE);
    }
}

void moveCursor(GlobalState& state, int direction) {
    int newPos = normalizeCursorPos(state.bufferText, state.bufferCursorPos);
    if (direction > 0) {
        int step = codeUnitStepForward(state.bufferText, newPos);
        if (step > 0) newPos += step;
    } else if (direction < 0) {
        int step = codeUnitStepBackward(state.bufferText, newPos);
        if (step > 0) newPos -= step;
    }
    state.bufferCursorPos = normalizeCursorPos(state.bufferText, newPos);
    
    if (state.hBufferWnd) {
        InvalidateRect(state.hBufferWnd, nullptr, TRUE);
    }
}

void setCursorPosition(GlobalState& state, int x, int y) {
    if (!state.hBufferWnd) return;
    
    HDC hdc = GetDC(state.hBufferWnd);
    if (!hdc) return;
    
    HFONT hFont = CreateFontW(state.bufferFontSize, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, state.bufferFontName.c_str());
    HFONT hOldFont = (HFONT)SelectObject(hdc, hFont);
    
    int clickX = x - 10;
    int clickY = y - 10;
    
    if (clickX < 0) clickX = 0;
    if (clickY < 0) clickY = 0;
    
    int bestPos = 0;
    int minDistance = INT_MAX;
    
    int currentX = 0;
    int currentY = 0;
    int lineHeight = state.bufferFontSize + 2;
    
    for (int i = 0; i <= (int)state.bufferText.length();) {
        int distance = abs(currentX - clickX) + abs(currentY - clickY) * 2;
        if (distance < minDistance) {
            minDistance = distance;
            bestPos = i;
        }
        
        if (i < (int)state.bufferText.length()) {
            int step = codeUnitStepForward(state.bufferText, i);
            if (step <= 0) step = 1;
            const wchar_t* ch = state.bufferText.c_str() + i;
            SIZE charSize;
            GetTextExtentPoint32W(hdc, ch, step, &charSize);
            
            currentX += charSize.cx;
            
            if (currentX > (FIXED_WIDTH - 30)) {
                currentX = charSize.cx;
                currentY += lineHeight;
            }
            i += step;
        } else {
            break;
        }
    }
    
    state.bufferCursorPos = normalizeCursorPos(state.bufferText, bestPos);
    
    SelectObject(hdc, hOldFont);
    DeleteObject(hFont);
    ReleaseDC(state.hBufferWnd, hdc);
    
    InvalidateRect(state.hBufferWnd, nullptr, TRUE);
}
void startSelection(GlobalState& state, int x, int y) {
    int position = getTextPositionFromPoint(state, x, y);
    if (position >= 0) {
        state.isSelecting = true;
        state.selectionStart = position;
        state.selectionEnd = position;
        state.hasSelection = false;
        state.selectionStartPoint = {x, y};
        state.selectionEndPoint = {x, y};
        
        if (state.hBufferWnd) {
            InvalidateRect(state.hBufferWnd, nullptr, TRUE);
        }
    }
}

void updateSelection(GlobalState& state, int x, int y) {
    if (!state.isSelecting) return;
    
    int position = getTextPositionFromPoint(state, x, y);
    if (position >= 0) {
        state.selectionEnd = position;
        state.selectionEndPoint = {x, y};
        state.hasSelection = (state.selectionStart != state.selectionEnd);
        
        if (state.hBufferWnd) {
            InvalidateRect(state.hBufferWnd, nullptr, TRUE);
        }
    }
}

void endSelection(GlobalState& state) {
    state.isSelecting = false;
    
    // 確保選取範圍正確
    if (state.selectionStart > state.selectionEnd) {
        std::swap(state.selectionStart, state.selectionEnd);
    }
    
    state.hasSelection = (state.selectionStart != state.selectionEnd);
    
    if (state.hBufferWnd) {
        InvalidateRect(state.hBufferWnd, nullptr, TRUE);
    }
}

void clearSelection(GlobalState& state) {
    state.isSelecting = false;
    state.hasSelection = false;
    state.selectionStart = -1;
    state.selectionEnd = -1;
    
    if (state.hBufferWnd) {
        InvalidateRect(state.hBufferWnd, nullptr, TRUE);
    }
}

void selectAll(GlobalState& state) {
    if (!state.bufferText.empty()) {
        state.selectionStart = 0;
        state.selectionEnd = state.bufferText.length();
        state.hasSelection = true;
        state.isSelecting = false;
        
        if (state.hBufferWnd) {
            InvalidateRect(state.hBufferWnd, nullptr, TRUE);
        }
        
        Utils::updateStatus(state, L"已全選 " + std::to_wstring(state.bufferText.length()) + L" 個字符");
    }
}

void copySelection(GlobalState& state) {
    if (!state.hasSelection) return;
    
    std::wstring selectedText = getSelectedText(state);
    if (selectedText.empty()) return;
    
    // 複制到系統剪貼簿
    if (OpenClipboard(state.hBufferWnd)) {
        EmptyClipboard();
        
        size_t size = (selectedText.length() + 1) * sizeof(wchar_t);
        HGLOBAL hClipboardData = GlobalAlloc(GMEM_MOVEABLE, size);
        
        if (hClipboardData) {
            wchar_t* pchData = (wchar_t*)GlobalLock(hClipboardData);
            if (pchData) {
                wcscpy_s(pchData, selectedText.length() + 1, selectedText.c_str());
                GlobalUnlock(hClipboardData);
                SetClipboardData(CF_UNICODETEXT, hClipboardData);
            }
        }
        
        CloseClipboard();
        
        Utils::updateStatus(state, L"已複制 " + std::to_wstring(selectedText.length()) + L" 個字符");
    }
}

void cutSelection(GlobalState& state) {
    if (!state.hasSelection) return;
    
    copySelection(state);  // 先複制
    deleteSelection(state); // 再刪除
    
    Utils::updateStatus(state, L"已剪下選取的文字");
}

void deleteSelection(GlobalState& state) {
    if (!state.hasSelection) return;
	saveSnapshot(state);
    
    int start = std::min(state.selectionStart, state.selectionEnd);
    int end = std::max(state.selectionStart, state.selectionEnd);
    
    if (start >= 0 && end <= (int)state.bufferText.length()) {
        state.bufferText.erase(start, end - start);
        state.bufferCursorPos = start;
        
        clearSelection(state);
        saveBufferToFile(state);
        
        // 剪貼簿模式：無論是否有焦點，都要檢測輸入狀態並更新剪貼簿
        if (state.clipboardMode) {
            state.clipboardInputting = true;
            state.clipboardCopied = false;
            state.clipboardLastInputTime = GetTickCount();
            
            // 只有在暫放視窗沒有焦點時才立即更新剪貼簿
            if (!state.bufferHasFocus) {
                updateClipboardInMode(state);
            }
            
            if (state.hBufferWnd) {
                InvalidateRect(state.hBufferWnd, nullptr, TRUE);
                UpdateWindow(state.hBufferWnd);  // 立即更新窗口
            }
        }
        
        if (state.hBufferWnd) {
            int windowHeight = calculateBufferWindowHeight(state);
            SetWindowPos(state.hBufferWnd, NULL, 0, 0, FIXED_WIDTH, windowHeight, 
                        SWP_NOMOVE | SWP_NOZORDER);
            InvalidateRect(state.hBufferWnd, nullptr, TRUE);
        }
    }
}

void pasteFromClipboard(GlobalState& state) {
    if (!state.hBufferWnd) return;
    
    // 保存快照以便撤銷
    saveSnapshot(state);
    
    // 從剪貼簿讀取內容
    std::wstring clipboardText = L"";
    if (OpenClipboard(state.hBufferWnd)) {
        HANDLE hData = GetClipboardData(CF_UNICODETEXT);
        if (hData) {
            wchar_t* pszText = (wchar_t*)GlobalLock(hData);
            if (pszText) {
                clipboardText = std::wstring(pszText);
                GlobalUnlock(hData);
            }
        }
        CloseClipboard();
    }
    
    if (clipboardText.empty()) {
        Utils::updateStatus(state, L"剪貼簿為空，無內容可貼上");
        return;
    }
    
    // 插入到當前游標位置
    insertTextAtCursor(state, clipboardText);
    saveBufferToFile(state);
    
    // 更新暫放視窗
    if (state.hBufferWnd) {
        int newHeight = calculateBufferWindowHeight(state);
        if (state.useOptimizedUI) {
            WindowManager::updateBufferWindowPosition(state);
        } else {
            SetWindowPos(state.hBufferWnd, NULL, 0, 0, FIXED_WIDTH, newHeight, 
                        SWP_NOMOVE | SWP_NOZORDER);
        }
        InvalidateRect(state.hBufferWnd, nullptr, TRUE);
    }
    
    Utils::updateStatus(state, L"已貼上 " + std::to_wstring(clipboardText.length()) + L" 個字符");
}

void updateClipboardInMode(GlobalState& state) {
    if (!state.clipboardMode || state.bufferText.empty()) return;
    
    // 將暫放文字複製到剪貼簿
    if (OpenClipboard(state.hBufferWnd ? state.hBufferWnd : NULL)) {
        EmptyClipboard();
        
        size_t size = (state.bufferText.length() + 1) * sizeof(wchar_t);
        HGLOBAL hClipboardData = GlobalAlloc(GMEM_MOVEABLE, size);
        
        if (hClipboardData) {
            wchar_t* pchData = (wchar_t*)GlobalLock(hClipboardData);
            if (pchData) {
                wcscpy_s(pchData, state.bufferText.length() + 1, state.bufferText.c_str());
                GlobalUnlock(hClipboardData);
                SetClipboardData(CF_UNICODETEXT, hClipboardData);
            }
        }
        
        CloseClipboard();
    }
}

void checkInputEnd(GlobalState& state) {
    if (!state.clipboardMode) return;
    
    // 如果正在輸入，檢查是否超過500毫秒沒有輸入（輸入結束）
    if (state.clipboardInputting) {
        DWORD currentTime = GetTickCount();
        DWORD timeSinceLastInput = currentTime - state.clipboardLastInputTime;
        
        if (timeSinceLastInput > 500) {  // 500毫秒無輸入視為輸入結束
            // 確保剪貼簿已更新
            if (!state.bufferText.empty()) {
                updateClipboardInMode(state);
            }
            
            state.clipboardInputting = false;
            state.clipboardCopied = true;
            
            // 強制重繪窗口以更新狀態指示器
            if (state.hBufferWnd) {
                InvalidateRect(state.hBufferWnd, nullptr, TRUE);
                UpdateWindow(state.hBufferWnd);  // 立即更新窗口
            }
        }
    }
}

// 處理Ctrl+V貼上後清空暫放視窗
void handleClipboardPaste(GlobalState& state) {
    if (!state.clipboardMode) return;
    
    // 清空暫放視窗
    state.bufferText.clear();
    state.bufferCursorPos = 0;
    clearSelection(state);
    saveBufferToFile(state);
    
    // 重置狀態
    state.clipboardInputting = false;
    state.clipboardCopied = false;
    
    // 更新暫放視窗
    if (state.hBufferWnd) {
        int newHeight = calculateBufferWindowHeight(state);
        if (state.useOptimizedUI) {
            WindowManager::updateBufferWindowPosition(state);
        } else {
            SetWindowPos(state.hBufferWnd, NULL, 0, 0, FIXED_WIDTH, newHeight, 
                        SWP_NOMOVE | SWP_NOZORDER);
        }
        InvalidateRect(state.hBufferWnd, nullptr, TRUE);
    }
    
    Utils::updateStatus(state, L"已貼上文字並清空暫放視窗");
}

std::wstring getSelectedText(const GlobalState& state) {
    if (!state.hasSelection || state.bufferText.empty()) return L"";
    
    int start = std::min(state.selectionStart, state.selectionEnd);
    int end = std::max(state.selectionStart, state.selectionEnd);
    
    if (start >= 0 && end <= (int)state.bufferText.length() && start < end) {
        return state.bufferText.substr(start, end - start);
    }
    
    return L"";
}

int getTextPositionFromPoint(const GlobalState& state, int x, int y) {
    if (!state.hBufferWnd || state.bufferText.empty()) return -1;
    
    HDC hdc = GetDC(state.hBufferWnd);
    if (!hdc) return -1;
    
    HFONT hFont = CreateFontW(state.bufferFontSize, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, state.bufferFontName.c_str());
    HFONT hOldFont = (HFONT)SelectObject(hdc, hFont);
    
    int clickX = x - 10;
    int clickY = y - 10;
    
    if (clickX < 0) clickX = 0;
    if (clickY < 0) clickY = 0;
    
    int bestPos = 0;
    int minDistance = INT_MAX;
    
    int currentX = 0;
    int currentY = 0;
    int lineHeight = state.bufferFontSize + 2;
    
    for (int i = 0; i <= (int)state.bufferText.length();) {
        int distance = abs(currentX - clickX) + abs(currentY - clickY);
        if (distance < minDistance) {
            minDistance = distance;
            bestPos = i;
        }
        
        if (i < (int)state.bufferText.length()) {
            int step = codeUnitStepForward(state.bufferText, i);
            if (step <= 0) step = 1;
            const wchar_t* ch = state.bufferText.c_str() + i;
            SIZE charSize;
            GetTextExtentPoint32W(hdc, ch, step, &charSize);
            
            currentX += charSize.cx;
            
            if (currentX > (FIXED_WIDTH - 30)) {
                currentX = charSize.cx;
                currentY += lineHeight;
            }
            i += step;
        } else {
            break;
        }
    }
    
    SelectObject(hdc, hOldFont);
    DeleteObject(hFont);
    ReleaseDC(state.hBufferWnd, hdc);
    
    return normalizeCursorPos(state.bufferText, bestPos);
}

POINT getPointFromTextPosition(const GlobalState& state, int position) {
    POINT pt = {10, 10};
    
    int normalizedPos = normalizeCursorPos(state.bufferText, position);
    if (!state.hBufferWnd || normalizedPos < 0 || normalizedPos > (int)state.bufferText.length()) {
        return pt;
    }
    
    HDC hdc = GetDC(state.hBufferWnd);
    if (!hdc) return pt;
    
    HFONT hFont = CreateFontW(state.bufferFontSize, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, state.bufferFontName.c_str());
    HFONT hOldFont = (HFONT)SelectObject(hdc, hFont);
    
    int currentX = 10;
    int currentY = 10;
    int lineHeight = state.bufferFontSize + 2;
    
    for (int i = 0; i < normalizedPos && i < (int)state.bufferText.length();) {
        int step = codeUnitStepForward(state.bufferText, i);
        if (step <= 0) step = 1;
        if (i + step > normalizedPos) break;
        const wchar_t* ch = state.bufferText.c_str() + i;
        SIZE charSize;
        GetTextExtentPoint32W(hdc, ch, step, &charSize);
        
        currentX += charSize.cx;
        
        if (currentX > (FIXED_WIDTH - 30)) {
            currentX = 10 + charSize.cx;
            currentY += lineHeight;
        }
        i += step;
    }
    
    pt.x = currentX;
    pt.y = currentY;
    
    SelectObject(hdc, hOldFont);
    DeleteObject(hFont);
    ReleaseDC(state.hBufferWnd, hdc);
    
    return pt;
}

//歷史記錄管理函數

void saveSnapshot(GlobalState& state) {
    GlobalState::TextSnapshot snapshot;
    snapshot.text = state.bufferText;
    snapshot.cursorPos = state.bufferCursorPos;
    
    state.undoHistory.push_back(snapshot);
    
    // 限制歷史記錄大小
    if (state.undoHistory.size() > (size_t)state.maxHistorySize) {
        state.undoHistory.erase(state.undoHistory.begin());
    }
    
    // 新操作會清空 redo 歷史
    state.redoHistory.clear();
}

void undo(GlobalState& state) {
    if (state.undoHistory.empty()) {
        Utils::updateStatus(state, L"沒有可復原的操作");
        return;
    }
    
    // 保存當前狀態到 redo 歷史
    GlobalState::TextSnapshot currentSnapshot;
    currentSnapshot.text = state.bufferText;
    currentSnapshot.cursorPos = state.bufferCursorPos;
    state.redoHistory.push_back(currentSnapshot);
    
    // 恢復上一個狀態
    GlobalState::TextSnapshot snapshot = state.undoHistory.back();
    state.undoHistory.pop_back();
    
    state.bufferText = snapshot.text;
    state.bufferCursorPos = snapshot.cursorPos;
    
    // 更新視窗
    if (state.hBufferWnd) {
        int windowHeight = calculateBufferWindowHeight(state);
        
        if (state.useOptimizedUI) {
            RECT currentBufferRect;
            GetWindowRect(state.hBufferWnd, &currentBufferRect);
            SetWindowPos(state.hBufferWnd, NULL, 
                        currentBufferRect.left, currentBufferRect.top, 
                        FIXED_WIDTH, windowHeight, 
                        SWP_NOZORDER);
        } else {
            SetWindowPos(state.hBufferWnd, NULL, 0, 0, FIXED_WIDTH, windowHeight, 
                        SWP_NOMOVE | SWP_NOZORDER);
        }
        
        InvalidateRect(state.hBufferWnd, nullptr, TRUE);
    }
    
    saveBufferToFile(state);
    Utils::updateStatus(state, L"已復原");
}

void redo(GlobalState& state) {
    if (state.redoHistory.empty()) {
        Utils::updateStatus(state, L"沒有可重做的操作");
        return;
    }
    
    // 保存當前狀態到 undo 歷史
    GlobalState::TextSnapshot currentSnapshot;
    currentSnapshot.text = state.bufferText;
    currentSnapshot.cursorPos = state.bufferCursorPos;
    state.undoHistory.push_back(currentSnapshot);
    
    // 恢復下一個狀態
    GlobalState::TextSnapshot snapshot = state.redoHistory.back();
    state.redoHistory.pop_back();
    
    state.bufferText = snapshot.text;
    state.bufferCursorPos = snapshot.cursorPos;
    
    // 更新視窗
    if (state.hBufferWnd) {
        int windowHeight = calculateBufferWindowHeight(state);
        
        if (state.useOptimizedUI) {
            RECT currentBufferRect;
            GetWindowRect(state.hBufferWnd, &currentBufferRect);
            SetWindowPos(state.hBufferWnd, NULL, 
                        currentBufferRect.left, currentBufferRect.top, 
                        FIXED_WIDTH, windowHeight, 
                        SWP_NOZORDER);
        } else {
            SetWindowPos(state.hBufferWnd, NULL, 0, 0, FIXED_WIDTH, windowHeight, 
                        SWP_NOMOVE | SWP_NOZORDER);
        }
        
        InvalidateRect(state.hBufferWnd, nullptr, TRUE);
    }
    
    saveBufferToFile(state);
    Utils::updateStatus(state, L"已重做");
}

void clearHistory(GlobalState& state) {
    state.undoHistory.clear();
    state.redoHistory.clear();
}

} // namespace BufferManager
