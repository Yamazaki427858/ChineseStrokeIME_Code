// window_manager.cpp - 視窗管理與繪製實作（基於原始2083行代碼的最小修復）
#include "window_manager.h"
#include "buffer_manager.h"
#include "dictionary.h"
#include "dict_updater.h"
#include "input_handler.h"
#include "config_loader.h"
#include "screen_manager.h"
#include "position_manager.h"
#include "tray_manager.h"
#include "ime_manager.h"
#include <algorithm>
#include <fstream>

// 前向宣告
extern TrayManager::TrayIconData g_trayIcon;
extern HHOOK g_hKeyboardHook;

// 進階優化：滑鼠觸發重繪節流（延遲 50ms 合併，減少 WM_PAINT 次數）
static const UINT DEFERRED_INVALIDATE_TIMER_ID = 992;
static const UINT DEFERRED_MAIN = 1, DEFERRED_CAND = 2, DEFERRED_PRED = 4, DEFERRED_BUFFER = 8;
static UINT g_deferredInvalidateFlags = 0;
static bool g_deferredInvalidateScheduled = false;
static void scheduleDeferredInvalidate(UINT mask) {
    g_deferredInvalidateFlags |= mask;
    if (!g_deferredInvalidateScheduled && g_state.hWnd) {
        g_deferredInvalidateScheduled = true;
        SetTimer(g_state.hWnd, DEFERRED_INVALIDATE_TIMER_ID, 50, NULL);
    }
}

namespace WindowManager {

// 輸入模式切換的統一入口：負責維護視窗顯示與舊旗標同步
void switchMode(GlobalState& state, InputMode newMode) {
    if (state.currentMode == newMode) {
        return;
    }

    state.currentMode = newMode;

    switch (newMode) {
    case InputMode::IDLE:
        // 空閒：所有相關視窗隱藏
        if (state.hCandWnd) ShowWindow(state.hCandWnd, SW_HIDE);
        if (state.hPredWnd) ShowWindow(state.hPredWnd, SW_HIDE);
        if (state.hInputWnd) ShowWindow(state.hInputWnd, SW_HIDE);
        state.showCand = false;
        state.isPredictionMode = false;
        break;

    case InputMode::CAND_MODE:
        // 字碼候選模式：顯示候選視窗，隱藏聯想視窗
        if (state.hPredWnd) ShowWindow(state.hPredWnd, SW_HIDE);
        if (state.hCandWnd) ShowWindow(state.hCandWnd, SW_SHOW);
        state.showCand = true;
        state.isPredictionMode = false;
        break;

    case InputMode::PRED_MODE:
        // 聯想字模式：顯示聯想視窗，隱藏候選視窗
        if (state.hCandWnd) ShowWindow(state.hCandWnd, SW_HIDE);
        if (state.hPredWnd) ShowWindow(state.hPredWnd, SW_SHOW);
        state.showCand = true;
        state.isPredictionMode = true;
        break;
    }
}


// ========== 【日後修改關於對話框內容請修改此函數】 ==========
// 此函數用於顯示「關於」對話框，所有入口（主菜單、托盤菜單、主窗口按鈕）都調用此函數
// 
// 注意：此函數中的版本號來自 ime_core.h 中的 APP_VERSION
//       如需更新版本號，請修改 ime_core.h 中的 APP_VERSION（第12行）
//       同時也需要更新 Makefile 中的 VERSION（第41行）保持一致性
//
void showAboutDialog(HWND hwnd) {
    // 当前版本号（從 ime_core.h 的 APP_VERSION 讀取）
    std::string currentVersion = APP_VERSION;
    std::wstring currentVersionW = Utils::utf8ToWstr(currentVersion);
    
    // 构建关于对话框内容，包含版本号
    std::wstring aboutText = L"中文筆劃輸入法 V" + currentVersionW + L"\n\n";
    aboutText += L"開發者: Cursor AI IDE\n";
    aboutText += L"測試專員: 山崎大叔\n\n";
    aboutText += L"\n";
    aboutText += L"✨ 主要特色\n";
    aboutText += L"✓ 免安裝、免Admin權限\n";
    aboutText += L"✓ 隨身攜帶、即開即用\n";
    aboutText += L"✓ 暫放模式功能\n\n";
    aboutText += L"⌨️ 基本操作\n";
    aboutText += L"• U I O J K：基本筆劃輸入\n";
    aboutText += L"• 1-9數字鍵：選擇候選字\n";
    aboutText += L"• Shift鍵：切換中英文模式\n";
    aboutText += L"• 右鍵托盤圖示：快捷選單\n\n";
    aboutText += L"🔗 GitHub 專案\n";
    aboutText += L"https://github.com/Yamazaki427858/ChineseStrokeIME\n\n";
    aboutText += L"感謝您的使用！";
    
    // 显示关于对话框
    MessageBoxW(hwnd, aboutText.c_str(), L"關於 - 中文筆劃輸入法", MB_OK | MB_ICONINFORMATION);
    
    // 询问用户是否要检查更新
    int checkResult = MessageBoxW(hwnd, 
        L"是否檢查版本更新？",
        L"檢查更新", MB_YESNO | MB_ICONQUESTION);
    
    // 如果用户选择检查更新
    if (checkResult == IDYES) {
        // 手动检查更新（强制检查，忽略缓存，获取最新版本）
        std::string versionCachePath = Utils::wstrToUtf8(g_state.systemDir + L"version_cache.txt");
        std::string remoteVersion = DictUpdater::getRemoteVersion(nullptr, true, 24, versionCachePath.c_str());
        
        if (remoteVersion.empty()) {
            // 无法获取远程版本（网络错误或缓存过期且网络不可用）
            MessageBoxW(hwnd, 
                L"無法檢查更新，請檢查網路連接。\n\n可稍後再試或直接訪問 GitHub 查看最新版本。",
                L"檢查更新失敗", MB_OK | MB_ICONWARNING);
        } else if (remoteVersion != currentVersion) {
            // 发现新版本
            std::wstring remoteVersionW = Utils::utf8ToWstr(remoteVersion);
            std::wstring updateMsg = L"發現新版本可用！\n\n";
            updateMsg += L"當前版本：V" + currentVersionW + L"\n";
            updateMsg += L"最新版本：V" + remoteVersionW + L"\n\n";
            updateMsg += L"是否前往 GitHub 下載最新版本？\n\n";
            updateMsg += L"https://github.com/Yamazaki427858/ChineseStrokeIME";
            
            int result = MessageBoxW(hwnd, updateMsg.c_str(), 
                L"版本更新通知", MB_YESNO | MB_ICONINFORMATION);
            
            if (result == IDYES) {
                ShellExecuteW(NULL, L"open", L"https://github.com/Yamazaki427858/ChineseStrokeIME", NULL, NULL, SW_SHOWNORMAL);
            }
        } else {
            // 已是最新版本
            std::wstring latestMsg = L"✓ 您已使用最新版本！\n\n當前版本：V" + currentVersionW;
            MessageBoxW(hwnd, latestMsg.c_str(), 
                L"版本檢查", MB_OK | MB_ICONINFORMATION);
        }
    }
}
// ========== 【關於對話框內容修改結束】 ==========




// 進階優化：候選字視窗字型快取（避免每次 WM_PAINT 建立/刪除 GDI 字型）
static HFONT s_candFont = NULL;
static int s_candFontSize = -1;
static std::wstring s_candFontName;

void drawCandidate(HWND hwnd, HDC hdc, const GlobalState& state) {
    if (state.candidates.empty()) return;
    
    RECT rc;
    GetClientRect(hwnd, &rc);
    
    // 背景和邊框
    HBRUSH hBg = CreateSolidBrush(state.candidateBackgroundColor);
    FillRect(hdc, &rc, hBg);
    DeleteObject(hBg);
    
    HPEN hPen = CreatePen(PS_SOLID, 1, RGB(180,180,180));
    HPEN hOldPen = (HPEN)SelectObject(hdc, hPen);
    HBRUSH hOldBrush = (HBRUSH)SelectObject(hdc, GetStockObject(NULL_BRUSH));
    Rectangle(hdc, 0, 0, rc.right, rc.bottom);
    SelectObject(hdc, hOldBrush);
    SelectObject(hdc, hOldPen);
    DeleteObject(hPen);
    
    SetBkMode(hdc, TRANSPARENT);
    
    // 字型快取：僅在字型參數變更時重建
    if (!s_candFont || s_candFontSize != state.candidateFontSize || s_candFontName != state.candidateFontName) {
        if (s_candFont) DeleteObject(s_candFont);
        s_candFont = CreateFontW(
            state.candidateFontSize, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
            state.candidateFontName.c_str()
        );
        s_candFontSize = state.candidateFontSize;
        s_candFontName = state.candidateFontName;
    }
    HFONT hOld = (HFONT)SelectObject(hdc, s_candFont);
    
    int lineHeight = state.candidateFontSize + 6;
    int startIndex = state.currentPage * CANDIDATES_PER_PAGE;
    int endIndex = std::min(startIndex + CANDIDATES_PER_PAGE, (int)state.candidates.size());
    
    // 繪製候選字列表（優先顯示滑鼠 hover，其次才是鍵盤選取）
    for (int i = 0; i < endIndex - startIndex; ++i) {
        int actualIndex = startIndex + i;
        
        if (actualIndex == state.hoverCandidateIndex) {
            // 滑鼠 hover 行優先（包含第一個候選字）
            RECT bgRect = {8, 8 + i * lineHeight, rc.right - 8, 8 + (i + 1) * lineHeight};
            COLORREF hoverBg = state.candidateHoverBackgroundColor ?
                state.candidateHoverBackgroundColor : state.candidateBackgroundColor;
            HBRUSH hBrush = CreateSolidBrush(hoverBg);
            FillRect(hdc, &bgRect, hBrush);
            DeleteObject(hBrush);
            COLORREF hoverText = state.candidateHoverTextColor ?
                state.candidateHoverTextColor : state.candidateTextColor;
            SetTextColor(hdc, hoverText);
        } else if (i == state.selected) {
            // 鍵盤選取行（在沒有 hover 時才顯示）
            RECT bgRect = {8, 8 + i * lineHeight, rc.right - 8, 8 + (i + 1) * lineHeight};
            HBRUSH hBrush = CreateSolidBrush(state.selectedCandidateBackgroundColor);
            FillRect(hdc, &bgRect, hBrush);
            DeleteObject(hBrush);
            SetTextColor(hdc, state.selectedCandidateTextColor);
        } else {
            SetTextColor(hdc, state.candidateTextColor);
        }
        
        std::wstring txt;
        if (state.showPunctMenu) {
            txt = std::to_wstring(i+1) + L". " + state.candidates[actualIndex];
        } else {
            txt = std::to_wstring(i+1) + L". " + state.candidates[actualIndex];
            // 先顯示字碼（非 ⚐/⚑ 時）
            if (actualIndex < (int)state.candidateCodes.size() && !state.candidateCodes[actualIndex].empty()) {
                const std::wstring& code = state.candidateCodes[actualIndex];
                if (code != L"⚐" && code != L"⚑" && state.showCandidateCode) {
                    txt += L"[" + code + L"]";
                }
            }
            // ★ = user_dict（用戶詞庫／永久詞），(n) = 使用次數
            std::wstring detailInfo = L"";
            if (state.wordFreq.find(state.candidates[actualIndex]) != state.wordFreq.end()) {
                const WordInfo& info = state.wordFreq.at(state.candidates[actualIndex]);
                detailInfo = info.isPermanent ? L" ★" : L" (" + std::to_wstring(info.frequency) + L")";
            }
            txt += detailInfo;
            // ⚐ pinned / ⚑ locked 固定放在 ★ 或 (次數) 後面
            if (actualIndex < (int)state.candidateCodes.size() && !state.candidateCodes[actualIndex].empty()) {
                const std::wstring& code = state.candidateCodes[actualIndex];
                if (code == L"⚐" || code == L"⚑") {
                    txt += L" " + code;
                }
            }
        }
        
        TextOutW(hdc, 15, 10 + i * lineHeight, txt.c_str(), (int)txt.size());
    }
    
    // 繪製翻頁控制區域（如果有多頁）
    if (state.totalPages > 1) {
        int controlY = 10 + CANDIDATES_PER_PAGE * lineHeight + 25;
        int buttonSize = 20;
        int buttonY = controlY;
        
        // 分隔線
        HPEN hSepPen = CreatePen(PS_SOLID, 1, RGB(200,200,200));
        HPEN hOldSepPen = (HPEN)SelectObject(hdc, hSepPen);
        MoveToEx(hdc, 10, controlY - 4, NULL);
        LineTo(hdc, rc.right - 10, controlY - 4);
        SelectObject(hdc, hOldSepPen);
        DeleteObject(hSepPen);
        
        // 向上翻頁按鈕
        const_cast<GlobalState&>(state).prevPageButtonRect = {10, buttonY, 10 + buttonSize, buttonY + buttonSize};
        COLORREF prevBtnColor = (state.currentPage > 0) ? 
            (state.prevPageButtonHover ? RGB(180,180,180) : RGB(220,220,220)) : RGB(240,240,240);
        HBRUSH hPrevBrush = CreateSolidBrush(prevBtnColor);
        FillRect(hdc, &const_cast<GlobalState&>(state).prevPageButtonRect, hPrevBrush);
        DeleteObject(hPrevBrush);
        
        // 向上按鈕邊框
        HPEN hBtnPen = CreatePen(PS_SOLID, 1, RGB(160,160,160));
        HPEN hOldBtnPen = (HPEN)SelectObject(hdc, hBtnPen);
        SelectObject(hdc, GetStockObject(NULL_BRUSH));
        Rectangle(hdc, const_cast<GlobalState&>(state).prevPageButtonRect.left, 
                 const_cast<GlobalState&>(state).prevPageButtonRect.top,
                 const_cast<GlobalState&>(state).prevPageButtonRect.right, 
                 const_cast<GlobalState&>(state).prevPageButtonRect.bottom);
        
        // 向上箭頭
        SetTextColor(hdc, (state.currentPage > 0) ? RGB(60,60,60) : RGB(180,180,180));
        RECT upArrowRect = const_cast<GlobalState&>(state).prevPageButtonRect;
        DrawTextW(hdc, L"▲", -1, &upArrowRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        
        // 向下翻頁按鈕
        const_cast<GlobalState&>(state).nextPageButtonRect = {35, buttonY, 35 + buttonSize, buttonY + buttonSize};
        COLORREF nextBtnColor = (state.currentPage < state.totalPages - 1) ? 
            (state.nextPageButtonHover ? RGB(180,180,180) : RGB(220,220,220)) : RGB(240,240,240);
        HBRUSH hNextBrush = CreateSolidBrush(nextBtnColor);
        FillRect(hdc, &const_cast<GlobalState&>(state).nextPageButtonRect, hNextBrush);
        DeleteObject(hNextBrush);
        
        // 向下按鈕邊框
        Rectangle(hdc, const_cast<GlobalState&>(state).nextPageButtonRect.left, 
                 const_cast<GlobalState&>(state).nextPageButtonRect.top,
                 const_cast<GlobalState&>(state).nextPageButtonRect.right, 
                 const_cast<GlobalState&>(state).nextPageButtonRect.bottom);
        
        SelectObject(hdc, hOldBtnPen);
        DeleteObject(hBtnPen);
        
        // 向下箭頭
        SetTextColor(hdc, (state.currentPage < state.totalPages - 1) ? RGB(60,60,60) : RGB(180,180,180));
        RECT downArrowRect = const_cast<GlobalState&>(state).nextPageButtonRect;
        DrawTextW(hdc, L"▼", -1, &downArrowRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        
        // 頁數和統計信息
        const_cast<GlobalState&>(state).pageInfoRect = {65, buttonY, rc.right - 10, buttonY + buttonSize};
        SetTextColor(hdc, RGB(100, 100, 100));
        std::wstring pageInfo = std::to_wstring(state.currentPage + 1) + L"/" + 
                               std::to_wstring(state.totalPages) + L" (共" + 
                               std::to_wstring(state.candidates.size()) + L"個)";
        
        RECT pageTextRect = const_cast<GlobalState&>(state).pageInfoRect;
        DrawTextW(hdc, pageInfo.c_str(), -1, &pageTextRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    }
    
    SelectObject(hdc, hOld);
    // 字型使用快取，不 DeleteObject
}

// 聯想字視窗字型快取
static HFONT s_predFont = NULL;
static int s_predFontSize = -1;
static std::wstring s_predFontName;

// 聯想字視窗繪製函數（與 drawCandidate 類似，但使用獨立顏色配置）
void drawPrediction(HWND hwnd, HDC hdc, const GlobalState& state) {
    if (state.candidates.empty()) return;
    
    RECT rc;
    GetClientRect(hwnd, &rc);
    
    // 背景和邊框
    HBRUSH hBg = CreateSolidBrush(state.predictionBackgroundColor);
    FillRect(hdc, &rc, hBg);
    DeleteObject(hBg);
    
    HPEN hPen = CreatePen(PS_SOLID, 1, RGB(180,180,180));
    HPEN hOldPen = (HPEN)SelectObject(hdc, hPen);
    HBRUSH hOldBrush = (HBRUSH)SelectObject(hdc, GetStockObject(NULL_BRUSH));
    Rectangle(hdc, 0, 0, rc.right, rc.bottom);
    SelectObject(hdc, hOldBrush);
    SelectObject(hdc, hOldPen);
    DeleteObject(hPen);
    
    SetBkMode(hdc, TRANSPARENT);
    
    // 字型快取（與候選字視窗相同參數）
    if (!s_predFont || s_predFontSize != state.candidateFontSize || s_predFontName != state.candidateFontName) {
        if (s_predFont) DeleteObject(s_predFont);
        s_predFont = CreateFontW(
            state.candidateFontSize, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
            state.candidateFontName.c_str()
        );
        s_predFontSize = state.candidateFontSize;
        s_predFontName = state.candidateFontName;
    }
    HFONT hOld = (HFONT)SelectObject(hdc, s_predFont);
    
    int lineHeight = state.candidateFontSize + 6;
    int startIndex = state.currentPage * CANDIDATES_PER_PAGE;
    int endIndex = std::min(startIndex + CANDIDATES_PER_PAGE, (int)state.candidates.size());
    
    // 繪製聯想字列表
    for (int i = 0; i < endIndex - startIndex; ++i) {
        int actualIndex = startIndex + i;
        
        if (actualIndex == state.hoverCandidateIndex) {
            // hover 行
            RECT bgRect = {8, 8 + i * lineHeight, rc.right - 8, 8 + (i + 1) * lineHeight};
            HBRUSH hBrush = CreateSolidBrush(state.predictionHoverBgColor);
            FillRect(hdc, &bgRect, hBrush);
            DeleteObject(hBrush);
            SetTextColor(hdc, state.predictionHoverTextColor);
        } else if (i == 0) {
            // 第一個聯想字特殊底色
            RECT bgRect = {8, 8, rc.right - 8, 8 + lineHeight};
            HBRUSH hBrush = CreateSolidBrush(state.predictionFirstItemBgColor);
            FillRect(hdc, &bgRect, hBrush);
            DeleteObject(hBrush);
            SetTextColor(hdc, state.predictionTextColor);
        } else {
            SetTextColor(hdc, state.predictionTextColor);
        }
        
        std::wstring txt;
        txt = std::to_wstring(i+1) + L". " + state.candidates[actualIndex];
        
        // 先顯示字碼（非 ⚐/⚑ 時，且開啟 showCandidateCode）
        if (actualIndex < (int)state.candidateCodes.size() && !state.candidateCodes[actualIndex].empty()) {
            const std::wstring& code = state.candidateCodes[actualIndex];
            if (code != L"⚐" && code != L"⚑" && state.showCandidateCode) {
                txt += L"[" + code + L"]";
            }
        }
        
        // ★ = user_dict（用戶詞庫／永久詞），(n) = 使用次數
        std::wstring detailInfo = L"";
        if (state.wordFreq.find(state.candidates[actualIndex]) != state.wordFreq.end()) {
            const WordInfo& info = state.wordFreq.at(state.candidates[actualIndex]);
            detailInfo = info.isPermanent ? L" ★" : L" (" + std::to_wstring(info.frequency) + L")";
        }
        txt += detailInfo;
        
        // ⚐ pinned / ⚑ locked 固定放在 ★ 或 (次數) 後面
        if (actualIndex < (int)state.candidateCodes.size() && !state.candidateCodes[actualIndex].empty()) {
            const std::wstring& code = state.candidateCodes[actualIndex];
            if (code == L"⚐" || code == L"⚑") {
                txt += L" " + code;
            }
        }
        
        TextOutW(hdc, 15, 10 + i * lineHeight, txt.c_str(), (int)txt.size());
    }
    
    // 翻頁控制區域（與 drawCandidate 相同）
    if (state.totalPages > 1) {
        int controlY = 10 + CANDIDATES_PER_PAGE * lineHeight + 25;
        int buttonSize = 20;
        int buttonY = controlY;
        
        HPEN hSepPen = CreatePen(PS_SOLID, 1, RGB(200,200,200));
        HPEN hOldSepPen = (HPEN)SelectObject(hdc, hSepPen);
        MoveToEx(hdc, 10, controlY - 4, NULL);
        LineTo(hdc, rc.right - 10, controlY - 4);
        SelectObject(hdc, hOldSepPen);
        DeleteObject(hSepPen);
        
        const_cast<GlobalState&>(state).prevPageButtonRect = {10, buttonY, 10 + buttonSize, buttonY + buttonSize};
        COLORREF prevBtnColor = (state.currentPage > 0) ? 
            (state.prevPageButtonHover ? RGB(180,180,180) : RGB(220,220,220)) : RGB(240,240,240);
        HBRUSH hPrevBrush = CreateSolidBrush(prevBtnColor);
        FillRect(hdc, &const_cast<GlobalState&>(state).prevPageButtonRect, hPrevBrush);
        DeleteObject(hPrevBrush);
        
        HPEN hBtnPen = CreatePen(PS_SOLID, 1, RGB(160,160,160));
        HPEN hOldBtnPen = (HPEN)SelectObject(hdc, hBtnPen);
        SelectObject(hdc, GetStockObject(NULL_BRUSH));
        Rectangle(hdc, const_cast<GlobalState&>(state).prevPageButtonRect.left, 
                 const_cast<GlobalState&>(state).prevPageButtonRect.top,
                 const_cast<GlobalState&>(state).prevPageButtonRect.right, 
                 const_cast<GlobalState&>(state).prevPageButtonRect.bottom);
        
        SetTextColor(hdc, (state.currentPage > 0) ? RGB(60,60,60) : RGB(180,180,180));
        RECT upArrowRect = const_cast<GlobalState&>(state).prevPageButtonRect;
        DrawTextW(hdc, L"▲", -1, &upArrowRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        
        const_cast<GlobalState&>(state).nextPageButtonRect = {35, buttonY, 35 + buttonSize, buttonY + buttonSize};
        COLORREF nextBtnColor = (state.currentPage < state.totalPages - 1) ? 
            (state.nextPageButtonHover ? RGB(180,180,180) : RGB(220,220,220)) : RGB(240,240,240);
        HBRUSH hNextBrush = CreateSolidBrush(nextBtnColor);
        FillRect(hdc, &const_cast<GlobalState&>(state).nextPageButtonRect, hNextBrush);
        DeleteObject(hNextBrush);
        
        Rectangle(hdc, const_cast<GlobalState&>(state).nextPageButtonRect.left, 
                 const_cast<GlobalState&>(state).nextPageButtonRect.top,
                 const_cast<GlobalState&>(state).nextPageButtonRect.right, 
                 const_cast<GlobalState&>(state).nextPageButtonRect.bottom);
        
        SelectObject(hdc, hOldBtnPen);
        DeleteObject(hBtnPen);
        
        SetTextColor(hdc, (state.currentPage < state.totalPages - 1) ? RGB(60,60,60) : RGB(180,180,180));
        RECT downArrowRect = const_cast<GlobalState&>(state).nextPageButtonRect;
        DrawTextW(hdc, L"▼", -1, &downArrowRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        
        const_cast<GlobalState&>(state).pageInfoRect = {65, buttonY, rc.right - 10, buttonY + buttonSize};
        SetTextColor(hdc, RGB(100, 100, 100));
        std::wstring pageInfo = std::to_wstring(state.currentPage + 1) + L"/" + 
                               std::to_wstring(state.totalPages) + L" (共" + 
                               std::to_wstring(state.candidates.size()) + L"個)";
        
        RECT pageTextRect = const_cast<GlobalState&>(state).pageInfoRect;
        DrawTextW(hdc, pageInfo.c_str(), -1, &pageTextRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    }
    
    SelectObject(hdc, hOld);
    // 字型使用快取，不 DeleteObject
}

// 聯想字視窗定位函數
// 跟隨滑鼠模式（⿻ 恢復）：用滑鼠位置；使用者固定位置模式：以 hInputWnd 為錨點，避免分離錯位。
void positionPredictionWindow(GlobalState& state) {
    if (!state.hPredWnd) return;
    if (state.candidates.empty()) return;
    
    int candWidth  = calculateOptimalWindowWidth(state);
    int candHeight = calculateCandidateWindowHeight(state);
    
    POINT basePos;
    RECT inputRect = {};
    bool hasInput = (state.hInputWnd != nullptr) && IsWindow(state.hInputWnd);
    
    // 跟隨滑鼠模式（g_useUserPosition == false）：與候選窗一致，用滑鼠位置，讓「⿻」恢復有效
    if (!PositionManager::g_useUserPosition) {
        basePos = PositionManager::getCurrentMousePosition();
    }
    // 使用者固定位置模式：以輸入窗為錨點，聯想窗緊貼不分離
    else if (hasInput) {
        GetWindowRect(state.hInputWnd, &inputRect);
        basePos.x = inputRect.left;
        basePos.y = inputRect.bottom + WINDOW_SPACING;
        if (state.hCandWnd && state.showCand && IsWindowVisible(state.hCandWnd)) {
            RECT candRect;
            GetWindowRect(state.hCandWnd, &candRect);
            basePos.y = candRect.bottom + WINDOW_SPACING;
        }
    } else {
        if (PositionManager::g_userCandPos.isValid) {
            basePos.x = PositionManager::g_userCandPos.x;
            basePos.y = PositionManager::g_userCandPos.y;
        } else {
            basePos = PositionManager::getCurrentMousePosition();
        }
    }
    
    ScreenManager::updateMonitorInfo();
    ScreenManager::MonitorInfo currentMonitor = ScreenManager::getMonitorFromPoint(basePos);
    RECT workArea = currentMonitor.workArea;
    const int margin = 5;
    
    if (basePos.x + candWidth > workArea.right - margin)
        basePos.x = workArea.right - candWidth - margin;
    if (basePos.x < workArea.left + margin)
        basePos.x = workArea.left + margin;
    if (basePos.y + candHeight > workArea.bottom - margin)
        basePos.y = workArea.bottom - candHeight - margin;
    if (basePos.y < workArea.top + margin)
        basePos.y = workArea.top + margin;
    
    if (!state.menuShowing) {
        SetWindowPos(state.hPredWnd, HWND_TOPMOST,
                    basePos.x, basePos.y,
                    candWidth, candHeight,
                    SWP_NOACTIVATE | SWP_SHOWWINDOW);
    } else {
        SetWindowPos(state.hPredWnd, HWND_NOTOPMOST,
                    basePos.x, basePos.y,
                    candWidth, candHeight,
                    SWP_NOACTIVATE | SWP_SHOWWINDOW);
    }
    
    InvalidateRect(state.hPredWnd, nullptr, TRUE);
    UpdateWindow(state.hPredWnd);
}

// 字碼候選視窗定位函數（原 positionWindowsOptimized 中 CAND 模式邏輯）
void positionCandidateWindow(GlobalState& state) {
    // 修復：標點選單模式下也需要調整視窗
    if (!state.isInputting && !state.showPunctMenu) return; // 不在輸入狀態且非標點選單時直接返回
    
    ScreenManager::updateMonitorInfo();
    
    // 修改：即使沒有候選字也要定位字碼視窗
    if (!state.showCand) {
        positionInputWindow(state);
        return;
    }
    
    int candWidth = calculateOptimalWindowWidth(state);
    int candHeight = calculateCandidateWindowHeight(state);
    
    POINT basePos;
    
    // 如果用戶設定了固定位置，使用用戶位置；否則跟隨滑鼠（以滑鼠為錨點放置 hCandWnd）
    if (PositionManager::g_useUserPosition && PositionManager::g_userCandPos.isValid) {
        basePos.x = PositionManager::g_userCandPos.x;
        basePos.y = PositionManager::g_userCandPos.y;
    } else {
        // 跟隨滑鼠：以當前滑鼠位置為錨點，候選窗與字碼窗整塊跟隨
        basePos = PositionManager::getCurrentMousePosition();
        ScreenManager::MonitorInfo currentMonitor = ScreenManager::getMonitorFromPoint(basePos);
        RECT workArea = currentMonitor.workArea;
        const int margin = 5;
        int totalHeight = candHeight + INPUT_WINDOW_HEIGHT + 5;
        
        if (basePos.x + candWidth > workArea.right - margin)
            basePos.x = workArea.right - candWidth - margin;
        if (basePos.x < workArea.left + margin)
            basePos.x = workArea.left + margin;
        if (basePos.y + totalHeight > workArea.bottom - margin) {
            int newY = basePos.y - totalHeight - PositionManager::g_verticalOffset;
            if (newY >= workArea.top + margin)
                basePos.y = newY + INPUT_WINDOW_HEIGHT + 5;
            else
                basePos.y = workArea.top + margin + INPUT_WINDOW_HEIGHT + 5;
        }
        if (basePos.y < workArea.top + INPUT_WINDOW_HEIGHT + margin)
            basePos.y = workArea.top + INPUT_WINDOW_HEIGHT + margin;
    }
    
    // 定位候選字視窗
    if (state.hCandWnd && state.showCand && candHeight > 0) {
        if (!state.menuShowing) {
            SetWindowPos(state.hCandWnd, HWND_TOPMOST,
                        basePos.x, basePos.y,
                        candWidth, candHeight,
                        SWP_NOACTIVATE | SWP_SHOWWINDOW);
        } else {
            SetWindowPos(state.hCandWnd, HWND_NOTOPMOST,
                        basePos.x, basePos.y,
                        candWidth, candHeight,
                        SWP_NOACTIVATE | SWP_SHOWWINDOW);
        }
        InvalidateRect(state.hCandWnd, nullptr, TRUE);
        UpdateWindow(state.hCandWnd);
        
        RECT knownListRect = { basePos.x, basePos.y, basePos.x + candWidth, basePos.y + candHeight };
        positionInputWindow(state, &knownListRect);
    } else {
        positionInputWindow(state);
    }
}

int calculateOptimalWindowWidth(const GlobalState& state) {
    // 如果配置檔案有設定，優先使用配置檔案的寬度（固定寬度）
    int configWidth = state.candidateWidth;
    if (configWidth >= 200 && configWidth <= 1000) {
        return configWidth;
    }
    
    // 否則動態計算（僅在配置檔案未設定時）
    int baseWidth = 300;
    
    // 根據候選字內容調整所需寬度
    if (!state.candidates.empty()) {
        int maxContentWidth = 0;
        for (size_t i = 0; i < std::min(state.candidates.size(), (size_t)CANDIDATES_PER_PAGE); ++i) {
            int contentWidth = 60;
            contentWidth += (int)state.candidates[i].length() * 18;
            
            if (state.showCandidateCode && i < state.candidateCodes.size()) {
                contentWidth += 30 + (int)state.candidateCodes[i].length() * 10;
            }
            
            if (state.wordFreq.find(state.candidates[i]) != state.wordFreq.end()) {
                contentWidth += 30;
            }
            
            maxContentWidth = std::max(maxContentWidth, contentWidth);
        }
        baseWidth = std::max(baseWidth, maxContentWidth + 20);
    }
    
    if (state.totalPages > 1) {
        baseWidth = std::max(baseWidth, 350);
    }
    
    // 限制最大寬度，但不小於配置檔案設定
    return std::max(configWidth, std::min(baseWidth, 600));
}

int calculateCandidateWindowHeight(const GlobalState& state) {
    if (!state.showCand || state.candidates.empty()) return 0;
    
    int lineHeight = state.candidateFontSize + 8;
    int contentLines = std::min(CANDIDATES_PER_PAGE, (int)state.candidates.size());
    int baseHeight = 16; // 上下邊距
    
    // 候選字列表高度
    baseHeight += contentLines * lineHeight;
    
    // 翻頁控制區域高度（如果有多頁）
    if (state.totalPages > 1) {
        baseHeight += 50; // 分隔線 + 翻頁按鈕區域高度
    }
    
    return baseHeight;
}

// 修復：改進視窗定位邏輯，確保字碼和候選字視窗同步
void positionWindowsOptimized(GlobalState& state) {
    switch (state.currentMode) {
    case InputMode::CAND_MODE:
        positionCandidateWindow(state);
        break;
    case InputMode::PRED_MODE:
        // 先定位聯想視窗（依滑鼠或使用者位置），再讓字碼視窗貼在其上方，
        // 確保 hInputWnd 與 hPredWnd 始終成對，不會分離。
        positionPredictionWindow(state);
        positionInputWindow(state);
        break;
    case InputMode::IDLE:
    default:
        // 空閒模式不需特別定位
        break;
    }
}

void positionMainWindow(GlobalState& state) {
    // OptimizedUI 主視窗實際為工具列 290x35，勿用 config 的 580x70 以免被放大、右側空白且易被工作列遮蓋
    int w = state.windowWidth;
    int h = state.windowHeight;
    if (state.useOptimizedUI) {
        w = TOOLBAR_WIDTH;
        h = TOOLBAR_HEIGHT;
    }
    
    // 以「目前工具列位置所在螢幕」的工作區做邊界，避免接上副螢幕時被當成越界而推到主螢幕邊緣
    POINT toolbarPt = { PositionManager::g_toolbarPos.x, PositionManager::g_toolbarPos.y };
    RECT workArea = ScreenManager::getMonitorFromPoint(toolbarPt).workArea;
    int x = PositionManager::g_toolbarPos.x;
    int y = PositionManager::g_toolbarPos.y;
    const int margin = 5;
    
    if (x + w > workArea.right - margin)
        x = workArea.right - w - margin;
    if (x < workArea.left + margin)
        x = workArea.left + margin;
    if (y + h > workArea.bottom - margin)
        y = workArea.bottom - h - margin;
    if (y < workArea.top + margin)
        y = workArea.top + margin;
    
    PositionManager::g_toolbarPos.x = x;
    PositionManager::g_toolbarPos.y = y;
    
    SetWindowPos(state.hWnd, HWND_TOPMOST, x, y, w, h, SWP_NOACTIVATE);
}

// 注意：传统UI按钮检测函数（isPointInCloseButton, isPointInModeButton, 
// isPointInCreditsButton, isPointInRefreshButton, isPointInBufferButton）已移除

bool isPointInSendButton(int x, int y, const GlobalState& state) {
    return x >= state.sendButtonRect.left && x <= state.sendButtonRect.right &&
           y >= state.sendButtonRect.top && y <= state.sendButtonRect.bottom;
}

bool isPointInClearButton(int x, int y, const GlobalState& state) {
    return x >= state.clearButtonRect.left && x <= state.clearButtonRect.right &&
           y >= state.clearButtonRect.top && y <= state.clearButtonRect.bottom;
}

bool isPointInSaveButton(int x, int y, const GlobalState& state) {
    return x >= state.saveButtonRect.left && x <= state.saveButtonRect.right &&
           y >= state.saveButtonRect.top && y <= state.saveButtonRect.bottom;
}

bool isPointInClipboardModeButton(int x, int y, const GlobalState& state) {
    return x >= state.clipboardModeButtonRect.left && x <= state.clipboardModeButtonRect.right &&
           y >= state.clipboardModeButtonRect.top && y <= state.clipboardModeButtonRect.bottom;
}

bool isPointInPrevPageButton(int x, int y, const GlobalState& state) {
    return x >= state.prevPageButtonRect.left && x <= state.prevPageButtonRect.right &&
           y >= state.prevPageButtonRect.top && y <= state.prevPageButtonRect.bottom;
}

bool isPointInNextPageButton(int x, int y, const GlobalState& state) {
    return x >= state.nextPageButtonRect.left && x <= state.nextPageButtonRect.right &&
           y >= state.nextPageButtonRect.top && y <= state.nextPageButtonRect.bottom;
}


// 處理鍵盤輸入消息 (WM_USER+100)
LRESULT handleKeyboardInput(HWND hwnd, WPARAM wp) {
    DWORD key = (DWORD)wp;
    if (g_state.chineseMode) {
        // 筆劃鍵與萬用字元 * 按鍵（3+3 用，Wildcard 鍵可在 [InputSettings] 中自訂）
        bool isStrokeOrWildcardKey =
            key == 'U' || key == 'I' || key == 'O' || key == 'J' || key == 'K' || key == 'P' ||
            key == VK_NUMPAD7 || key == VK_NUMPAD8 || key == VK_NUMPAD9 ||
            key == VK_NUMPAD4 || key == VK_NUMPAD5 ||
            key == static_cast<DWORD>(g_state.wildcardKey1VK) ||
            key == static_cast<DWORD>(g_state.wildcardKey2VK);
        if (isStrokeOrWildcardKey) {
            InputHandler::processStroke(g_state, key);
            return 0;
        }
        if (key == VK_OEM_COMMA || key == VK_OEM_PERIOD || key == VK_OEM_2 ||
            key == VK_OEM_1 || key == VK_OEM_4 || key == VK_OEM_6 || key == VK_OEM_7 ||
            key == VK_SPACE || key == VK_OEM_MINUS || key == VK_OEM_PLUS || key == VK_OEM_5 || key == VK_OEM_3 ||
            (key == '1' && (GetKeyState(VK_SHIFT) & 0x8000)) || (key == '2' && (GetKeyState(VK_SHIFT) & 0x8000)) ||
            (key == '3' && (GetKeyState(VK_SHIFT) & 0x8000)) || (key == '4' && (GetKeyState(VK_SHIFT) & 0x8000)) ||
            (key == '5' && (GetKeyState(VK_SHIFT) & 0x8000)) || (key == '6' && (GetKeyState(VK_SHIFT) & 0x8000)) ||
            (key == '7' && (GetKeyState(VK_SHIFT) & 0x8000)) || (key == '8' && (GetKeyState(VK_SHIFT) & 0x8000)) ||
            (key == '9' && (GetKeyState(VK_SHIFT) & 0x8000)) || (key == '0' && (GetKeyState(VK_SHIFT) & 0x8000)) ||
            key == VK_MULTIPLY || key == VK_ADD || key == VK_SUBTRACT || key == VK_DECIMAL || key == VK_DIVIDE) {
            InputHandler::processPunctuator(g_state, key);
            // 中文模式下輸入標點符號後清空視窗（與按 ESC 效果一致）
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
            InvalidateRect(hwnd, nullptr, TRUE);
            return 0;
        }
    }
    
    // 功能鍵處理
    if (key == VK_DOWN) { Dictionary::changePage(g_state, 1); return 0; }
    if (key == VK_UP) { Dictionary::changePage(g_state, -1); return 0; }
    if (key >= '1' && key <= '9') { Dictionary::selectCandidate(g_state, key - '1'); return 0; }
    if (key == VK_BACK) {
        if (!g_state.input.empty()) {
            g_state.input.pop_back();
            Dictionary::updateCandidates(g_state);
            InvalidateRect(hwnd, nullptr, TRUE);
        }
        return 0;
    }
    if (key == VK_SPACE) { Dictionary::selectCandidate(g_state, 0); return 0; }
    if (key == VK_RETURN) { InputHandler::handleEnterKeySmartly(g_state); return 0; }
    if (key == VK_ESCAPE) {
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
        // 🔥 恢復 Windows 輸入法狀態（取消輸入後）
        IMEManager::restoreWindowsIME();
        Utils::updateStatus(g_state, L"輸入已取消");
        InvalidateRect(hwnd, nullptr, TRUE);
        return 0;
    }
    return 0;
}

// 處理托盤消息 (WM_USER+200)
LRESULT handleTrayMessage(HWND hwnd, LPARAM lp) {
    TrayManager::processTrayMessage(hwnd, lp, g_state);
    return 0;
}

// 處理命令消息 (WM_COMMAND)
LRESULT handleCommand(HWND hwnd, WPARAM wp) {
    switch (LOWORD(wp)) {
        case 1001: InputHandler::showPunctMenu(g_state); break;
        case 1002: 
            Dictionary::loadMainDict(g_state);
            Utils::updateStatus(g_state, L"字碼表已重新載入");
            break;
        case 1003:
            showAboutDialog(hwnd);
            break;
        case 1005:
            ConfigLoader::refreshConfigs(g_state);
            MessageBoxW(hwnd, L"配置已重新載入！", L"提示", MB_OK | MB_ICONINFORMATION);
            break;
        case 1008: // 從GitHub更新字碼表
        {
            // 檢查是否存在現有字碼表文件（system/ 目錄）
            std::string dictPath = Utils::wstrToUtf8(g_state.systemDir + L"Zi-Ma-Biao.txt");
            std::ifstream checkFile(dictPath);
            bool fileExists = checkFile.is_open();
            checkFile.close();
            
            // 如果文件存在，顯示確認對話框
            if (fileExists) {
                std::wstring confirmMsg = L"確定要從 GitHub 下載字碼表嗎？\n\n";
                confirmMsg += L"注意：下載會覆蓋 system 目錄內的 Zi-Ma-Biao.txt\n\n";
                confirmMsg += L"如果您已自定義過字碼表，請先自行備份原文件\n\n";
                confirmMsg += L"點擊「是」開始下載更新，點擊「否」取消操作";
                
                int result = MessageBoxW(hwnd, confirmMsg.c_str(), 
                    L"確認下載字碼表", MB_YESNO | MB_ICONWARNING);
                
                if (result == IDYES) {
                    // 用戶確認，開始下載
                    Dictionary::updateDictFromGitHub(g_state, true);
                } else {
                    // 用戶取消
                    Utils::updateStatus(g_state, L"已取消下載字碼表");
                }
            } else {
                // 文件不存在，直接下載（無需確認）
                Dictionary::updateDictFromGitHub(g_state, true);
            }
            break;
        }
        case 1007: {
            // 切換半透明顯示
            // 先從配置文件讀取最新的transparency_alpha值（用戶可能手動修改了配置文件）
            ConfigLoader::updateTransparencyAlphaFromConfig(g_state);
            g_state.enableTransparency = !g_state.enableTransparency;
            applyTransparency(g_state);
            ConfigLoader::saveInterfaceConfig(g_state);
            Utils::updateStatus(g_state, g_state.enableTransparency ? 
                L"半透明顯示已開啟" : L"半透明顯示已關閉");
            break;
        }
        case 1010: {
            // 切換聯想字功能
            g_state.enableWordPrediction = !g_state.enableWordPrediction;
            ConfigLoader::saveInterfaceConfig(g_state);
            Utils::updateStatus(g_state, g_state.enableWordPrediction ? 
                L"聯想字功能已開啟" : L"聯想字功能已關閉");
            break;
        }
        case 1011: {
            // 切換筆劃符號顯示（輸入框顯示一丨丿丶フ 或 uiojk）
            g_state.showStrokeSymbols = !g_state.showStrokeSymbols;
            ConfigLoader::saveInterfaceConfig(g_state);
            Utils::updateStatus(g_state, g_state.showStrokeSymbols ? 
                L"筆劃符號顯示已開啟" : L"筆劃符號顯示已關閉");
            if (g_state.hInputWnd) InvalidateRect(g_state.hInputWnd, nullptr, TRUE);
            break;
        }
        case 1101: // 候選右鍵：切換 pinned
        case 1102: // 候選右鍵：切換 locked
        case 1103: // 候選右鍵：刪除此聯想（可再次學回）
        case 1104: { // 候選右鍵：永不再顯示 / 取消封鎖
            int idx = g_state.contextMenuCandIndex;
            if (idx < 0 || idx >= (int)g_state.candidates.size()) break;
            if (g_state.lastSelected.empty()) break;
            std::wstring prev = g_state.lastSelected;
            std::wstring next = g_state.candidates[idx];
            std::pair<std::wstring, std::wstring> key(prev, next);
            
            if (LOWORD(wp) == 1101) {
                // pinned toggle
                bool isPinned = g_state.pinnedContext.find(key) != g_state.pinnedContext.end();
                Dictionary::setContextPinned(g_state, prev, next, !isPinned);
            } else if (LOWORD(wp) == 1102) {
                // locked toggle
                bool isLocked = g_state.lockedContext.find(key) != g_state.lockedContext.end();
                Dictionary::setContextLocked(g_state, prev, next, !isLocked);
            } else if (LOWORD(wp) == 1103) {
                // 刪除此聯想：從 contextLearning 及 locked/pinned 中移除
                auto itPrev = g_state.contextLearning.find(prev);
                if (itPrev != g_state.contextLearning.end()) {
                    itPrev->second.erase(next);
                    if (itPrev->second.empty()) {
                        g_state.contextLearning.erase(itPrev);
                    }
                }
                g_state.lockedContext.erase(key);
                g_state.pinnedContext.erase(key);
            } else if (LOWORD(wp) == 1104) {
                // 永不再顯示：加入或移除 blockedContext
                auto it = g_state.blockedContext.find(key);
                if (it == g_state.blockedContext.end()) {
                    // 新增封鎖：也一併從學習/locked/pinned 中移除
                    g_state.blockedContext.insert(key);
                    auto itPrev = g_state.contextLearning.find(prev);
                    if (itPrev != g_state.contextLearning.end()) {
                        itPrev->second.erase(next);
                        if (itPrev->second.empty()) {
                            g_state.contextLearning.erase(itPrev);
                        }
                    }
                    g_state.lockedContext.erase(key);
                    g_state.pinnedContext.erase(key);
                } else {
                    // 取消封鎖
                    g_state.blockedContext.erase(it);
                }
            }
            
            // 儲存並重新顯示該字的聯想
            Dictionary::saveContextLearning(g_state);
            Dictionary::showPredictionsAfterSelection(g_state, prev);
            break;
        }
        case 1009: {
            // 暫停/啟用輸入法功能（釋放/重新設置鍵盤鉤子）
            if (g_state.imePaused) {
                // 重新啟用輸入法：重新設置鍵盤鉤子
                if (!g_hKeyboardHook) {
                    HINSTANCE hInstance = GetModuleHandle(NULL);
                    g_hKeyboardHook = SetWindowsHookEx(WH_KEYBOARD_LL, InputHandler::KeyboardHookProc, hInstance, 0);
                    if (g_hKeyboardHook) {
                        g_state.imePaused = false;
                        Utils::updateStatus(g_state, L"輸入法已啟用，鍵盤鉤子已重新設置");
                    } else {
                        MessageBoxW(hwnd, L"無法重新設置鍵盤鉤子，可能需要管理員權限", L"錯誤", MB_OK | MB_ICONERROR);
                        Utils::updateStatus(g_state, L"重新設置鍵盤鉤子失敗");
                    }
                } else {
                    // 鉤子已存在，直接更新狀態
                    g_state.imePaused = false;
                    Utils::updateStatus(g_state, L"輸入法已啟用");
                }
            } else {
                // 暫停輸入法：釋放鍵盤鉤子
                if (g_hKeyboardHook) {
                    if (UnhookWindowsHookEx(g_hKeyboardHook)) {
                        g_hKeyboardHook = NULL;
                        g_state.imePaused = true;
                        
                        // 清空字碼視窗和候選字/聯想字視窗的顯示
                        g_state.input.clear();
                        g_state.candidates.clear();
                        g_state.candidateCodes.clear();
                        g_state.showCand = false;
                        g_state.isInputting = false;
                        g_state.inputError = false;
                        g_state.showPunctMenu = false;
                        g_state.selected = 0;
                        g_state.currentPage = 0;
                        if (g_state.hCandWnd) ShowWindow(g_state.hCandWnd, SW_HIDE);
                        if (g_state.hPredWnd) ShowWindow(g_state.hPredWnd, SW_HIDE);
                        if (g_state.hInputWnd) ShowWindow(g_state.hInputWnd, SW_HIDE);
                        
                        Utils::updateStatus(g_state, L"輸入法已暫停，鍵盤鉤子已釋放，按鍵回復正常");
                    } else {
                        MessageBoxW(hwnd, L"無法釋放鍵盤鉤子", L"錯誤", MB_OK | MB_ICONERROR);
                        Utils::updateStatus(g_state, L"釋放鍵盤鉤子失敗");
                    }
                } else {
                    // 鉤子不存在，直接更新狀態
                    g_state.imePaused = true;
                    
                    // 清空字碼視窗和候選字/聯想字視窗的顯示
                    g_state.input.clear();
                    g_state.candidates.clear();
                    g_state.candidateCodes.clear();
                    g_state.showCand = false;
                    g_state.isInputting = false;
                    g_state.inputError = false;
                    g_state.showPunctMenu = false;
                    g_state.selected = 0;
                    g_state.currentPage = 0;
                    if (g_state.hCandWnd) ShowWindow(g_state.hCandWnd, SW_HIDE);
                    if (g_state.hPredWnd) ShowWindow(g_state.hPredWnd, SW_HIDE);
                    if (g_state.hInputWnd) ShowWindow(g_state.hInputWnd, SW_HIDE);
                    
                    Utils::updateStatus(g_state, L"輸入法已暫停");
                }
            }
            // 重繪工具列以更新狀態指示器
            if (g_state.hWnd) {
                InvalidateRect(g_state.hWnd, nullptr, TRUE);
            }
            break;
        }
            
        // 托盤選單命令處理
        case 2001: TrayManager::showFromTray(hwnd, &g_trayIcon); break;
        case 2002: 
            PositionManager::g_useUserPosition = false;
            PositionManager::savePositions(g_state);
            Utils::updateStatus(g_state, L"已重置為滑鼠跟隨模式");
            break;
        case 2003: PostMessage(hwnd, WM_CLOSE, 0, 0); break;
        case 2004: InputHandler::toggleInputMode(g_state); break;
        case 2005: InputHandler::showPunctMenu(g_state); break;
        case 2006: ConfigLoader::refreshConfigs(g_state); break;
        case 2007: 
            PositionManager::g_useUserPosition = false;
            PositionManager::savePositions(g_state);
            Utils::updateStatus(g_state, L"已取消固定位置");
            break;
        case 2008: 
            showAboutDialog(hwnd);
            break;
        case 2013: // 從GitHub更新字碼表
        {
            // 檢查是否存在現有字碼表文件（system/ 目錄）
            std::string dictPath = Utils::wstrToUtf8(g_state.systemDir + L"Zi-Ma-Biao.txt");
            std::ifstream checkFile(dictPath);
            bool fileExists = checkFile.is_open();
            checkFile.close();
            
            // 如果文件存在，顯示確認對話框
            if (fileExists) {
                std::wstring confirmMsg = L"確定要從 GitHub 下載字碼表嗎？\n\n";
                confirmMsg += L"⚠️ 注意：下載會覆蓋 system 目錄內的 Zi-Ma-Biao.txt\n\n";
                confirmMsg += L"如果您已自定義過字碼表，請先自行備份原文件\n\n";
                confirmMsg += L"點擊「是」開始下載更新，點擊「否」取消操作";
                
                int result = MessageBoxW(hwnd, confirmMsg.c_str(), 
                    L"確認下載字碼表", MB_YESNO | MB_ICONWARNING);
                
                if (result == IDYES) {
                    // 用戶確認，開始下載
                    Dictionary::updateDictFromGitHub(g_state, true);
                } else {
                    // 用戶取消
                    Utils::updateStatus(g_state, L"已取消下載字碼表");
                }
            } else {
                // 文件不存在，直接下載（無需確認）
                Dictionary::updateDictFromGitHub(g_state, true);
            }
            break;
        }
        case 2009: // 重啟輸入法
            if (MessageBoxW(hwnd, L"確定要重啟輸入法嗎？\n\n重啟後將保留所有設定和學習記錄。", 
                L"確認重啟", MB_YESNO | MB_ICONQUESTION) == IDYES) {
                // 直接實現重啟功能
                wchar_t exePath[MAX_PATH];
                GetModuleFileNameW(NULL, exePath, MAX_PATH);
                    
                STARTUPINFOW si = {0};
                PROCESS_INFORMATION pi = {0};
                si.cb = sizeof(STARTUPINFOW);
                    
                if (CreateProcessW(exePath, NULL, NULL, NULL, FALSE, 
                                  0, NULL, NULL, &si, &pi)) {
                    CloseHandle(pi.hProcess);
                    CloseHandle(pi.hThread);
                        
                    Sleep(500);
                        
                    PostQuitMessage(0);
                } else {
                    MessageBoxW(NULL, L"重啟輸入法失敗", L"錯誤", MB_OK | MB_ICONERROR);
                }
            }
            break;
        case 2010: BufferManager::sendBufferContent(g_state); break;
        case 2011: BufferManager::clearBufferWithConfirm(g_state); break;
        case 2012: BufferManager::toggleBufferMode(g_state); break;
    }
    return 0;
}

// 處理屏幕模式變更 (WM_DISPLAYCHANGE)
LRESULT handleDisplayChange(HWND hwnd) {
    // 延遲處理，等待系統完成切換
    static UINT_PTR delayTimerId = 0;
    if (delayTimerId) {
        KillTimer(hwnd, delayTimerId);
    }
    delayTimerId = SetTimer(hwnd, 997, 500, NULL);
    return 0;
}

// 處理窗口銷毀 (WM_DESTROY)
LRESULT handleWindowDestroy(HWND hwnd) {
    // 清理資源
    if (g_hKeyboardHook) {
        UnhookWindowsHookEx(g_hKeyboardHook);
        g_hKeyboardHook = NULL;
    }
    
    // 銷毀所有視窗（OptimizedUI模式需要）
    if (g_state.hInputWnd) {
        DestroyWindow(g_state.hInputWnd);
        g_state.hInputWnd = NULL;
    }
    if (g_state.hCandWnd) {
        DestroyWindow(g_state.hCandWnd);
        g_state.hCandWnd = NULL;
    }
    if (g_state.hPredWnd) {
        DestroyWindow(g_state.hPredWnd);
        g_state.hPredWnd = NULL;
    }
    if (g_state.hBufferWnd) {
        DestroyWindow(g_state.hBufferWnd);
        g_state.hBufferWnd = NULL;
    }
    
    // 定時器清理
    KillTimer(hwnd, DEFERRED_INVALIDATE_TIMER_ID);
    KillTimer(hwnd, 999);
    
    // 儲存用戶設定
    Dictionary::saveUserDict(g_state);
    Dictionary::saveContextLearning(g_state);  // 儲存智能聯想引擎的個人上下文學習記錄
    PositionManager::savePositions(g_state);
    
    // 移除系統托盤圖示
    TrayManager::removeTrayIcon(&g_trayIcon);
    
    PostQuitMessage(0);
    return 0;
}



// 統一的候選字窗口過程
LRESULT CALLBACK CandProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_ERASEBKGND:
            return 1;
            
        case WM_PAINT: { 
            PAINTSTRUCT ps; 
            HDC hdc = BeginPaint(hwnd, &ps); 
            RECT rc;
            GetClientRect(hwnd, &rc);
            // 進階優化：雙緩衝繪製，減少閃爍
            HDC memDC = CreateCompatibleDC(hdc);
            HBITMAP memBitmap = CreateCompatibleBitmap(hdc, rc.right, rc.bottom);
            HBITMAP oldBitmap = (HBITMAP)SelectObject(memDC, memBitmap);
            drawCandidate(hwnd, memDC, g_state); 
            BitBlt(hdc, 0, 0, rc.right, rc.bottom, memDC, 0, 0, SRCCOPY);
            SelectObject(memDC, oldBitmap);
            DeleteObject(memBitmap);
            DeleteDC(memDC);
            EndPaint(hwnd, &ps); 
            return 0; 
        }
            
        case WM_LBUTTONDOWN: { 
            int x = LOWORD(lp);
            int y = HIWORD(lp);
            
            // ★ 只處理翻頁按鈕點擊（移除候選字點擊功能）
            if (g_state.totalPages > 1) {
                if (isPointInPrevPageButton(x, y, g_state) && g_state.currentPage > 0) {
                    Dictionary::changePage(g_state, -1);
                    return 0;
                }
                
                if (isPointInNextPageButton(x, y, g_state) && g_state.currentPage < g_state.totalPages - 1) {
                    Dictionary::changePage(g_state, 1);
                    return 0;
                }
            }
            
            // OptimizedUI 模式：從候選字視窗拖曳整塊（錨點為候選視窗上的點擊處）
            if (g_state.useOptimizedUI) {
                g_state.dragState.isCandDragging = true;
                SetCapture(hwnd);
                POINT pt;
                GetCursorPos(&pt);
                RECT candRect;
                GetWindowRect(hwnd, &candRect);
                g_state.dragState.dragOffset.x = pt.x - candRect.left;
                g_state.dragState.dragOffset.y = pt.y - candRect.top;
            }
            return 0; 
        }
        
        case WM_RBUTTONDOWN: {
            // 在候選列表上顯示 pinned / locked / del / block 右鍵選單
            if (!g_state.showCand || g_state.candidates.empty()) return 0;
            
            int x = LOWORD(lp);
            int y = HIWORD(lp);
            
            RECT rc;
            GetClientRect(hwnd, &rc);
            
            int lineHeight = g_state.candidateFontSize + 6;
            int startIndex = g_state.currentPage * CANDIDATES_PER_PAGE;
            int endIndex = std::min(startIndex + CANDIDATES_PER_PAGE, (int)g_state.candidates.size());
            int countThisPage = endIndex - startIndex;
            
            // 候選列 Y 範圍大約從 8 開始（與 drawCandidate 保持一致）
            if (y < 8) return 0;
            int row = (y - 8) / lineHeight;
            if (row < 0 || row >= countThisPage) return 0;
            
            int actualIndex = startIndex + row;
            if (actualIndex < 0 || actualIndex >= (int)g_state.candidates.size()) return 0;
            
            g_state.contextMenuCandIndex = actualIndex;
            
            HMENU hMenu = CreatePopupMenu();
            
            // 依當前狀態動態決定 pinned / locked / block 文案
            std::wstring prev = g_state.lastSelected;
            std::wstring next = g_state.candidates[actualIndex];
            std::pair<std::wstring, std::wstring> key(prev, next);
            bool isPinned = g_state.pinnedContext.find(key) != g_state.pinnedContext.end();
            bool isLocked = g_state.lockedContext.find(key) != g_state.lockedContext.end();
            bool isBlocked = g_state.blockedContext.find(key) != g_state.blockedContext.end();

            // 在選單上方顯示目前設定的聯想對象提示：「設定聯想：prev → next」
            std::wstring headerText = L"設定聯想：" + prev + L" → " + next;
            AppendMenuW(hMenu, MF_STRING | MF_DISABLED | MF_GRAYED, 0, headerText.c_str());
            AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
            
            AppendMenuW(hMenu, MF_STRING, 1101,
                isPinned ? L"取消置頂 (unpin)" : L"⚐ 設為置頂 (pinned)");
            AppendMenuW(hMenu, MF_STRING, 1102,
                isLocked ? L"取消鎖定 (unlock)" : L"⚑ 設為鎖定 (locked)");
            AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
            AppendMenuW(hMenu, MF_STRING, 1103, L"刪除此聯想（可再次學回）");
            AppendMenuW(hMenu, MF_STRING, 1104,
                isBlocked ? L"取消永不再顯示" : L"永不再顯示此聯想");
            
            POINT pt = { x, y };
            ClientToScreen(hwnd, &pt);
            
            // 顯示選單期間標記 menuShowing，讓鍵盤鉤子放行 ESC 給選單處理
            g_state.menuShowing = true;

            // 設置候選視窗為前台，確保鍵盤事件（包含 ESC）送到選單
            SetForegroundWindow(hwnd);
            MSG msg;
            for (int i = 0; i < 15; ++i) {
                if (!PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) break;
                if (msg.message == WM_QUIT) break;
                TranslateMessage(&msg);
                DispatchMessage(&msg);
            }

            int cmd = TrackPopupMenuEx(
                hMenu,
                TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_LEFTALIGN | TPM_VERTICAL,
                pt.x, pt.y,
                hwnd,
                nullptr
            );
            DestroyMenu(hMenu);

            g_state.menuShowing = false;

            // 發送空消息穩定焦點狀態
            PostMessage(hwnd, WM_NULL, 0, 0);
            
            if (cmd >= 1101 && cmd <= 1104 && g_state.hWnd) {
                PostMessageW(g_state.hWnd, WM_COMMAND, cmd, 0);
            }
            return 0;
        }
        
        case WM_MOUSEMOVE: {
            // OptimizedUI 模式：拖曳時只處理位移，不更新 hover 以減少遲緩
            if (g_state.useOptimizedUI && g_state.dragState.isCandDragging) {
                POINT pt;
                GetCursorPos(&pt);
                int newCandX = pt.x - g_state.dragState.dragOffset.x;
                int newCandY = pt.y - g_state.dragState.dragOffset.y;
                
                RECT candRect;
                GetWindowRect(hwnd, &candRect);
                int candWidth = candRect.right - candRect.left;
                int candHeight = candRect.bottom - candRect.top;
                
                RECT workArea = ScreenManager::getMonitorFromPoint(pt).workArea;
                int margin = 4;
                if (newCandX < workArea.left + margin) newCandX = workArea.left + margin;
                if (newCandY < workArea.top + margin) newCandY = workArea.top + margin;
                if (newCandX + candWidth > workArea.right - margin) newCandX = workArea.right - margin - candWidth;
                if (newCandY + candHeight > workArea.bottom - margin) newCandY = workArea.bottom - margin - candHeight;
                
                if (!g_state.menuShowing) {
                    SetWindowPos(hwnd, HWND_TOPMOST,
                                newCandX, newCandY, candWidth, candHeight,
                                SWP_NOACTIVATE | SWP_SHOWWINDOW);
                } else {
                    SetWindowPos(hwnd, HWND_NOTOPMOST,
                                newCandX, newCandY, candWidth, candHeight,
                                SWP_NOACTIVATE | SWP_SHOWWINDOW);
                }
                
                if (g_state.hInputWnd) {
                    int inputY = newCandY - INPUT_WINDOW_HEIGHT - WINDOW_SPACING;
                    RECT inputRect;
                    GetWindowRect(g_state.hInputWnd, &inputRect);
                    int inputWidth = inputRect.right - inputRect.left;
                    if (!g_state.menuShowing) {
                        SetWindowPos(g_state.hInputWnd, HWND_TOPMOST,
                                    newCandX, inputY, inputWidth, INPUT_WINDOW_HEIGHT,
                                    SWP_NOACTIVATE | SWP_SHOWWINDOW);
                    } else {
                        SetWindowPos(g_state.hInputWnd, HWND_NOTOPMOST,
                                    newCandX, inputY, inputWidth, INPUT_WINDOW_HEIGHT,
                                    SWP_NOACTIVATE | SWP_SHOWWINDOW);
                    }
                }
                
                if (g_state.hPredWnd && IsWindowVisible(g_state.hPredWnd) && g_state.enableWordPrediction) {
                    RECT predRect;
                    GetWindowRect(g_state.hPredWnd, &predRect);
                    int predWidth  = predRect.right  - predRect.left;
                    int predHeight = predRect.bottom - predRect.top;
                    int predY = newCandY + candHeight + WINDOW_SPACING;
                    SetWindowPos(g_state.hPredWnd,
                                g_state.menuShowing ? HWND_NOTOPMOST : HWND_TOPMOST,
                                newCandX, predY, predWidth, predHeight,
                                SWP_NOACTIVATE | SWP_SHOWWINDOW);
                }
                return 0;
            }
            
            int x = LOWORD(lp);
            int y = HIWORD(lp);
            bool ctrlPressed = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
            bool needRedraw = false;
            
            if (ctrlPressed) {
                // 處理翻頁按鈕懸停效果（僅在按住 Ctrl 時）
                if (g_state.totalPages > 1) {
                    bool newPrevHover = isPointInPrevPageButton(x, y, g_state) && g_state.currentPage > 0;
                    bool newNextHover = isPointInNextPageButton(x, y, g_state) && g_state.currentPage < g_state.totalPages - 1;
                    
                    if (newPrevHover != g_state.prevPageButtonHover || newNextHover != g_state.nextPageButtonHover) {
                        needRedraw = true;
                    }
                    
                    g_state.prevPageButtonHover = newPrevHover;
                    g_state.nextPageButtonHover = newNextHover;
                }
                
                // 更新候選 hover 行（僅在有候選且非拖曳時，且按住 Ctrl）
                if (g_state.showCand && !g_state.candidates.empty()) {
                    int lineHeight = g_state.candidateFontSize + 6;
                    int startIndex = g_state.currentPage * CANDIDATES_PER_PAGE;
                    int endIndex = std::min(startIndex + CANDIDATES_PER_PAGE, (int)g_state.candidates.size());
                    int countThisPage = endIndex - startIndex;
                    int newHoverIndex = -1;
                    if (y >= 8) {
                        int row = (y - 8) / lineHeight;
                        if (row >= 0 && row < countThisPage) {
                            newHoverIndex = startIndex + row;
                        }
                    }
                    if (newHoverIndex != g_state.hoverCandidateIndex) {
                        g_state.hoverCandidateIndex = newHoverIndex;
                        needRedraw = true;
                    }
                }
            } else {
                // 未按 Ctrl 時清除候選視窗的 hover 狀態
                if (g_state.prevPageButtonHover || g_state.nextPageButtonHover || g_state.hoverCandidateIndex >= 0) {
                    g_state.prevPageButtonHover = false;
                    g_state.nextPageButtonHover = false;
                    g_state.hoverCandidateIndex = -1;
                    needRedraw = true;
                }
            }
            
            if (needRedraw) {
                scheduleDeferredInvalidate(DEFERRED_CAND);
            }
            
            return 0;
        }
        
        case WM_LBUTTONUP: {
            // OptimizedUI模式：處理拖拽結束
            if (g_state.useOptimizedUI && g_state.dragState.isCandDragging) {
                g_state.dragState.isCandDragging = false;
                ReleaseCapture();
                
                // 記錄使用者自定義位置（基於字碼輸入視窗位置）
                if (g_state.hInputWnd) {
                    RECT inputRect;
                    GetWindowRect(g_state.hInputWnd, &inputRect);
                    PositionManager::g_userCandPos.x = inputRect.left;
                    PositionManager::g_userCandPos.y = inputRect.top;
                    PositionManager::g_userCandPos.isValid = true;
                    PositionManager::g_useUserPosition = true;
                    PositionManager::savePositions(g_state);
                    
                    Utils::updateStatus(g_state, L"已切換到使用者位置模式");
                }
                
                return 0;
            }
            break;
        }
    }
    
    return DefWindowProc(hwnd, msg, wp, lp);
}

// 聯想字窗口過程（目前複製 CandProc，行為完全相同。
// 後續步驟會逐步改為僅處理聯想字視窗邏輯。）
LRESULT CALLBACK PredProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_ERASEBKGND:
            return 1;
            
        case WM_PAINT: { 
            PAINTSTRUCT ps; 
            HDC hdc = BeginPaint(hwnd, &ps); 
            RECT rc;
            GetClientRect(hwnd, &rc);
            // 進階優化：雙緩衝繪製，減少閃爍
            HDC memDC = CreateCompatibleDC(hdc);
            HBITMAP memBitmap = CreateCompatibleBitmap(hdc, rc.right, rc.bottom);
            HBITMAP oldBitmap = (HBITMAP)SelectObject(memDC, memBitmap);
            drawPrediction(hwnd, memDC, g_state); 
            BitBlt(hdc, 0, 0, rc.right, rc.bottom, memDC, 0, 0, SRCCOPY);
            SelectObject(memDC, oldBitmap);
            DeleteObject(memBitmap);
            DeleteDC(memDC);
            EndPaint(hwnd, &ps); 
            return 0; 
        }
            
        case WM_LBUTTONDOWN: { 
            int x = LOWORD(lp);
            int y = HIWORD(lp);
            
            // ★ 只處理翻頁按鈕點擊（移除候選字點擊功能）
            if (g_state.totalPages > 1) {
                if (isPointInPrevPageButton(x, y, g_state) && g_state.currentPage > 0) {
                    Dictionary::changePage(g_state, -1);
                    return 0;
                }
                
                if (isPointInNextPageButton(x, y, g_state) && g_state.currentPage < g_state.totalPages - 1) {
                    Dictionary::changePage(g_state, 1);
                    return 0;
                }
            }
            
            // OptimizedUI 模式：從聯想字視窗拖曳整塊（錨點為聯想視窗上的點擊處）
            if (g_state.useOptimizedUI) {
                g_state.dragState.isPredDragging = true;
                SetCapture(hwnd);
                
                POINT pt;
                GetCursorPos(&pt);
                RECT predRect;
                GetWindowRect(hwnd, &predRect);
                g_state.dragState.dragOffset.x = pt.x - predRect.left;
                g_state.dragState.dragOffset.y = pt.y - predRect.top;
            }
            
            return 0; 
        }
        
        case WM_RBUTTONDOWN: {
            // 聯想字視窗右鍵選單：只在有候選且有 lastSelected 時才有意義
            if (!g_state.showCand || g_state.candidates.empty()) return 0;
            if (g_state.lastSelected.empty()) return 0;
            
            int x = LOWORD(lp);
            int y = HIWORD(lp);
            
            RECT rc;
            GetClientRect(hwnd, &rc);
            
            int lineHeight = g_state.candidateFontSize + 6;
            int startIndex = g_state.currentPage * CANDIDATES_PER_PAGE;
            int endIndex = std::min(startIndex + CANDIDATES_PER_PAGE, (int)g_state.candidates.size());
            int countThisPage = endIndex - startIndex;
            
            if (y < 8) return 0;
            int row = (y - 8) / lineHeight;
            if (row < 0 || row >= countThisPage) return 0;
            
            int actualIndex = startIndex + row;
            if (actualIndex < 0 || actualIndex >= (int)g_state.candidates.size()) return 0;
            
            g_state.contextMenuCandIndex = actualIndex;
            
            HMENU hMenu = CreatePopupMenu();
            
            std::wstring prev = g_state.lastSelected;
            std::wstring next = g_state.candidates[actualIndex];
            std::pair<std::wstring, std::wstring> key(prev, next);
            bool isPinned  = g_state.pinnedContext.find(key)  != g_state.pinnedContext.end();
            bool isLocked  = g_state.lockedContext.find(key)  != g_state.lockedContext.end();
            bool isBlocked = g_state.blockedContext.find(key) != g_state.blockedContext.end();

            // 在選單上方顯示目前設定的聯想對象提示：「設定聯想：prev → next」
            std::wstring headerText = L"設定聯想：" + prev + L" → " + next;
            AppendMenuW(hMenu, MF_STRING | MF_DISABLED | MF_GRAYED, 0, headerText.c_str());
            AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
            
            AppendMenuW(hMenu, MF_STRING, 1101,
                isPinned ? L"取消置頂 (unpin)" : L"⚐ 設為置頂 (pinned)");
            AppendMenuW(hMenu, MF_STRING, 1102,
                isLocked ? L"取消鎖定 (unlock)" : L"⚑ 設為鎖定 (locked)");
            AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
            AppendMenuW(hMenu, MF_STRING, 1103, L"刪除此聯想（可再次學回）");
            AppendMenuW(hMenu, MF_STRING, 1104,
                isBlocked ? L"取消永不再顯示" : L"永不再顯示此聯想");
            
            POINT pt = { x, y };
            ClientToScreen(hwnd, &pt);
            
            // 顯示選單期間暫時降為 NOTOPMOST，避免候選窗遮住選單
            g_state.menuShowing = true;
            SetWindowPos(hwnd, HWND_NOTOPMOST, 0, 0, 0, 0,
                         SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);

            // 設置聯想視窗為前台，確保鍵盤事件（包含 ESC）送到選單
            SetForegroundWindow(hwnd);
            MSG msg;
            for (int i = 0; i < 15; ++i) {
                if (!PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) break;
                if (msg.message == WM_QUIT) break;
                TranslateMessage(&msg);
                DispatchMessage(&msg);
            }
            
            int cmd = TrackPopupMenuEx(
                hMenu,
                TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_LEFTALIGN | TPM_VERTICAL,
                pt.x, pt.y,
                hwnd,
                nullptr
            );
            DestroyMenu(hMenu);
            
            // 選單結束後恢復 TOPMOST 狀態
            SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0,
                         SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
            g_state.menuShowing = false;
            
            if (cmd >= 1101 && cmd <= 1104 && g_state.hWnd) {
                PostMessageW(g_state.hWnd, WM_COMMAND, cmd, 0);
            }
            return 0;
        }
        
        case WM_MOUSEMOVE: {
            // 拖曳時只處理位移，不更新 hover 以減少遲緩
            if (g_state.useOptimizedUI && g_state.dragState.isPredDragging) {
                POINT pt;
                GetCursorPos(&pt);
                int newPredX = pt.x - g_state.dragState.dragOffset.x;
                int newPredY = pt.y - g_state.dragState.dragOffset.y;
                
                RECT predRect;
                GetWindowRect(hwnd, &predRect);
                int predWidth  = predRect.right  - predRect.left;
                int predHeight = predRect.bottom - predRect.top;
                
                RECT workArea = ScreenManager::getMonitorFromPoint(pt).workArea;
                int margin = 4;
                if (newPredX < workArea.left + margin) newPredX = workArea.left + margin;
                if (newPredY < workArea.top + margin) newPredY = workArea.top + margin;
                if (newPredX + predWidth > workArea.right - margin) newPredX = workArea.right - margin - predWidth;
                if (newPredY + predHeight > workArea.bottom - margin) newPredY = workArea.bottom - margin - predHeight;
                
                if (!g_state.menuShowing) {
                    SetWindowPos(hwnd, HWND_TOPMOST,
                                newPredX, newPredY, predWidth, predHeight,
                                SWP_NOACTIVATE | SWP_SHOWWINDOW);
                } else {
                    SetWindowPos(hwnd, HWND_NOTOPMOST,
                                newPredX, newPredY, predWidth, predHeight,
                                SWP_NOACTIVATE | SWP_SHOWWINDOW);
                }
                
                int candHeight = 0;
                if (g_state.hCandWnd && IsWindowVisible(g_state.hCandWnd)) {
                    RECT cr;
                    GetWindowRect(g_state.hCandWnd, &cr);
                    candHeight = cr.bottom - cr.top;
                }
                int candY = (candHeight > 0) ? (newPredY - candHeight - WINDOW_SPACING) : 0;
                int inputY = (candHeight > 0) ? (candY - INPUT_WINDOW_HEIGHT - WINDOW_SPACING) : (newPredY - INPUT_WINDOW_HEIGHT - WINDOW_SPACING);
                
                if (g_state.hInputWnd) {
                    RECT inputRect;
                    GetWindowRect(g_state.hInputWnd, &inputRect);
                    int inputWidth = inputRect.right - inputRect.left;
                    if (!g_state.menuShowing) {
                        SetWindowPos(g_state.hInputWnd, HWND_TOPMOST,
                                    newPredX, inputY, inputWidth, INPUT_WINDOW_HEIGHT,
                                    SWP_NOACTIVATE | SWP_SHOWWINDOW);
                    } else {
                        SetWindowPos(g_state.hInputWnd, HWND_NOTOPMOST,
                                    newPredX, inputY, inputWidth, INPUT_WINDOW_HEIGHT,
                                    SWP_NOACTIVATE | SWP_SHOWWINDOW);
                    }
                }
                if (g_state.hCandWnd && g_state.showCand && candHeight > 0 && !g_state.menuShowing) {
                    RECT candRect;
                    GetWindowRect(g_state.hCandWnd, &candRect);
                    int candWidth = candRect.right - candRect.left;
                    SetWindowPos(g_state.hCandWnd, HWND_TOPMOST,
                                newPredX, candY, candWidth, candHeight,
                                SWP_NOACTIVATE | SWP_SHOWWINDOW);
                }
                return 0;
            }
            
            int y = HIWORD(lp);
            bool ctrlPressed = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
            
            if (ctrlPressed) {
                // Hover 高亮聯想候選（僅在按住 Ctrl 時）
                if (g_state.showCand && !g_state.candidates.empty()) {
                    int lineHeight   = g_state.candidateFontSize + 6;
                    int startIndex   = g_state.currentPage * CANDIDATES_PER_PAGE;
                    int endIndex     = std::min(startIndex + CANDIDATES_PER_PAGE,
                                                (int)g_state.candidates.size());
                    int countThisPage = endIndex - startIndex;
                    int newHoverIndex = -1;
                    if (y >= 8) {
                        int row = (y - 8) / lineHeight;
                        if (row >= 0 && row < countThisPage) {
                            newHoverIndex = startIndex + row;
                        }
                    }
                    if (newHoverIndex != g_state.hoverCandidateIndex) {
                        g_state.hoverCandidateIndex = newHoverIndex;
                        scheduleDeferredInvalidate(DEFERRED_PRED);
                    }
                }
            } else {
                if (g_state.hoverCandidateIndex >= 0) {
                    g_state.hoverCandidateIndex = -1;
                    scheduleDeferredInvalidate(DEFERRED_PRED);
                }
            }
            
            return 0;
        }
        
        case WM_LBUTTONUP: {
            if (g_state.useOptimizedUI && g_state.dragState.isPredDragging) {
                g_state.dragState.isPredDragging = false;
                ReleaseCapture();
                if (g_state.hInputWnd) {
                    RECT inputRect;
                    GetWindowRect(g_state.hInputWnd, &inputRect);
                    PositionManager::g_userCandPos.x = inputRect.left;
                    PositionManager::g_userCandPos.y = inputRect.top;
                    PositionManager::g_userCandPos.isValid = true;
                    PositionManager::g_useUserPosition = true;
                    PositionManager::savePositions(g_state);
                    Utils::updateStatus(g_state, L"已切換到使用者位置模式");
                }
                return 0;
            }
            break;
        }
    }
    
    return DefWindowProc(hwnd, msg, wp, lp);
}

// 新增函數：繪製帶選取高亮的文字
void drawTextWithSelection(HDC hdc, RECT textArea, GlobalState& state) {
    if (state.bufferText.empty()) return;
    
    // 設定字體
    HFONT hFont = CreateFontW(state.bufferFontSize, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, state.bufferFontName.c_str());
    HFONT hOldFont = (HFONT)SelectObject(hdc, hFont);
    
    SetBkMode(hdc, TRANSPARENT);
    
    int currentX = textArea.left;
    int currentY = textArea.top;
    int lineHeight = state.bufferFontSize + 2;
    
    int selStart = state.hasSelection ? std::min(state.selectionStart, state.selectionEnd) : -1;
    int selEnd = state.hasSelection ? std::max(state.selectionStart, state.selectionEnd) : -1;
    
    for (int i = 0; i < (int)state.bufferText.length(); i++) {
        wchar_t ch = state.bufferText[i];
        SIZE charSize;
        GetTextExtentPoint32W(hdc, &ch, 1, &charSize);
        
        // 檢查是否需要換行
        if (currentX + charSize.cx > textArea.right) {
            currentX = textArea.left;
            currentY += lineHeight;
            
            // 檢查是否超出顯示區域
            if (currentY + state.bufferFontSize > textArea.bottom) {
                break; // 不再繪製
            }
        }
        
        // 繪製選取背景
        if (state.hasSelection && i >= selStart && i < selEnd) {
            RECT charRect = {currentX, currentY, currentX + charSize.cx, currentY + state.bufferFontSize};
            HBRUSH hSelBrush = CreateSolidBrush(RGB(51, 153, 255)); // 藍色選取背景
            FillRect(hdc, &charRect, hSelBrush);
            DeleteObject(hSelBrush);
            
            // 選取文字使用白色
            SetTextColor(hdc, RGB(255, 255, 255));
        } else {
            // 正常文字顏色
            SetTextColor(hdc, state.bufferTextColor);
        }
        
        // 繪製字符
        TextOutW(hdc, currentX, currentY, &ch, 1);
        
        currentX += charSize.cx;
    }
    
    SelectObject(hdc, hOldFont);
    DeleteObject(hFont);
}

// 暫放視窗的繪製和處理
void drawBufferWindow(HDC hdc, RECT rc, GlobalState& state) {
    HBRUSH hBg = CreateSolidBrush(state.bufferBackgroundColor);
    FillRect(hdc, &rc, hBg);
    DeleteObject(hBg);
    
    HPEN hBorderPen = CreatePen(PS_SOLID, 1, RGB(128, 128, 128));
    HPEN hOldBorderPen = (HPEN)SelectObject(hdc, hBorderPen);
    HBRUSH hOldBorderBrush = (HBRUSH)SelectObject(hdc, GetStockObject(NULL_BRUSH));
    Rectangle(hdc, 0, 0, rc.right, rc.bottom);
    SelectObject(hdc, hOldBorderBrush);
    SelectObject(hdc, hOldBorderPen);
    DeleteObject(hBorderPen);
    
    RECT textArea = {10, 10, rc.right - 10, rc.bottom - CONTROL_BAR_HEIGHT - 2};
    
    HFONT hFont = CreateFontW(state.bufferFontSize, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, state.bufferFontName.c_str());
    HFONT hOldFont = (HFONT)SelectObject(hdc, hFont);
    
    SetTextColor(hdc, state.bufferTextColor);
    SetBkMode(hdc, TRANSPARENT);
    
    if (!state.bufferText.empty()) {
		drawTextWithSelection(hdc, textArea, state);
	}
    
   // 繪製游標（只在沒有選取時顯示）
	if (state.bufferHasFocus && state.bufferShowCursor && !state.hasSelection) {
    // 使用新的座標轉換函數
    POINT cursorPos = BufferManager::getPointFromTextPosition(state, state.bufferCursorPos); 
        
        HPEN hCursorPen = CreatePen(PS_SOLID, 1, state.bufferCursorColor);
        HPEN hOldCursorPen = (HPEN)SelectObject(hdc, hCursorPen);
        MoveToEx(hdc, cursorPos.x, cursorPos.y, NULL);  // ✅ 正确
         LineTo(hdc, cursorPos.x, cursorPos.y + state.bufferFontSize);  // ✅ 正确
        SelectObject(hdc, hOldCursorPen);
        DeleteObject(hCursorPen);
    }
    
    SelectObject(hdc, hOldFont);
    DeleteObject(hFont);
    
    int controlY = rc.bottom - CONTROL_BAR_HEIGHT;
    RECT controlRect = {0, controlY, rc.right, rc.bottom};
    HBRUSH hControlBg = CreateSolidBrush(RGB(245, 245, 245));
    FillRect(hdc, &controlRect, hControlBg);
    DeleteObject(hControlBg);
    
    std::wstring statsText = L"字數: " + std::to_wstring(state.bufferText.length()) + L" | 位置: " + std::to_wstring(state.bufferCursorPos);
    SetTextColor(hdc, RGB(100, 100, 100));
    TextOutW(hdc, 10, controlY + 2, statsText.c_str(), statsText.length());
    
    // 按鈕放在狀態文字下方
    int buttonY = controlY + 20;              // 狀態文字下方
    int buttonHeight = 20;                    // 按鈕高度
    int buttonWidth = 70;                     // 適中的按鈕寬度
    int buttonSpacing = 8;                    // 按鈕間距
    int leftMargin = 15;                      // 左邊距，與狀態文字對齊
    
    // 確保按鈕不會超出控制列底部
    if (buttonY + buttonHeight > rc.bottom - 3) {
        buttonY = rc.bottom - buttonHeight - 3;
    }
    
    // 從左往右排列按鈕（更符合閱讀習慣）
    int saveButtonX = leftMargin;
    int clearButtonX = saveButtonX + buttonWidth + buttonSpacing;
    int sendButtonX = clearButtonX + buttonWidth + buttonSpacing;
    
    // 剪貼簿模式按鈕：細小的圓形圖案，放在Enter發送按鈕右側
    int clipboardButtonSize = 16;  // 圓形按鈕直徑
    int clipboardButtonX = sendButtonX + buttonWidth + 5;  // 緊貼Enter發送按鈕
    int clipboardButtonY = buttonY + (buttonHeight - clipboardButtonSize) / 2;  // 垂直居中
    
    // 檢查最右邊按鈕是否超出視窗，如果超出就縮小按鈕
    int totalButtonWidth = sendButtonX + buttonWidth;
    if (totalButtonWidth + clipboardButtonSize + 10 > rc.right - 10) {
        // 重新計算更緊湊的布局
        buttonWidth = 50;
        buttonSpacing = 5;
        saveButtonX = leftMargin;
        clearButtonX = saveButtonX + buttonWidth + buttonSpacing;
        sendButtonX = clearButtonX + buttonWidth + buttonSpacing;
        clipboardButtonX = sendButtonX + buttonWidth + 5;
        clipboardButtonY = buttonY + (buttonHeight - clipboardButtonSize) / 2;
    }
    
    // === 儲存按鈕 ===
    state.saveButtonRect = {saveButtonX, buttonY, saveButtonX + buttonWidth, buttonY + buttonHeight};
    COLORREF newSaveColor = state.saveButtonHover ? RGB(100, 150, 255) : RGB(220, 220, 220);
    HBRUSH hNewSaveBrush = CreateSolidBrush(newSaveColor);
    FillRect(hdc, &state.saveButtonRect, hNewSaveBrush);
    DeleteObject(hNewSaveBrush);
    
    HPEN hNewSavePen = CreatePen(PS_SOLID, 1, RGB(150, 150, 150));
    HPEN hNewSaveOldPen = (HPEN)SelectObject(hdc, hNewSavePen);
    SelectObject(hdc, GetStockObject(NULL_BRUSH));
    Rectangle(hdc, state.saveButtonRect.left, state.saveButtonRect.top, 
              state.saveButtonRect.right, state.saveButtonRect.bottom);
    SelectObject(hdc, hNewSaveOldPen);
    DeleteObject(hNewSavePen);
    
    SetTextColor(hdc, RGB(80, 80, 80));
    DrawTextW(hdc, L"儲存", -1, &state.saveButtonRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    
    // === 清空按鈕 ===
    state.clearButtonRect = {clearButtonX, buttonY, clearButtonX + buttonWidth, buttonY + buttonHeight};
    COLORREF newClearColor = state.clearButtonHover ? RGB(255, 100, 100) : RGB(220, 220, 220);
    HBRUSH hNewClearBrush = CreateSolidBrush(newClearColor);
    FillRect(hdc, &state.clearButtonRect, hNewClearBrush);
    DeleteObject(hNewClearBrush);
    
    HPEN hNewClearPen = CreatePen(PS_SOLID, 1, RGB(150, 150, 150));
    HPEN hNewClearOldPen = (HPEN)SelectObject(hdc, hNewClearPen);
    SelectObject(hdc, GetStockObject(NULL_BRUSH));
    Rectangle(hdc, state.clearButtonRect.left, state.clearButtonRect.top, 
              state.clearButtonRect.right, state.clearButtonRect.bottom);
    SelectObject(hdc, hNewClearOldPen);
    DeleteObject(hNewClearPen);
    
    DrawTextW(hdc, L"清空", -1, &state.clearButtonRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    
    // === 發送按鈕 ===
    state.sendButtonRect = {sendButtonX, buttonY, sendButtonX + buttonWidth, buttonY + buttonHeight};
    // 始終顯示綠色或灰色（恢復原樣）
    COLORREF newSendColor = state.sendButtonHover ? RGB(100, 180, 100) : RGB(220, 220, 220);
    HBRUSH hNewSendBrush = CreateSolidBrush(newSendColor);
    FillRect(hdc, &state.sendButtonRect, hNewSendBrush);
    DeleteObject(hNewSendBrush);
    
    HPEN hNewSendPen = CreatePen(PS_SOLID, 1, RGB(150, 150, 150));
    HPEN hNewSendOldPen = (HPEN)SelectObject(hdc, hNewSendPen);
    SelectObject(hdc, GetStockObject(NULL_BRUSH));
    Rectangle(hdc, state.sendButtonRect.left, state.sendButtonRect.top, 
              state.sendButtonRect.right, state.sendButtonRect.bottom);
    SelectObject(hdc, hNewSendOldPen);
    DeleteObject(hNewSendPen);
    
    // 始終顯示"Enter發送"
    DrawTextW(hdc, L"Enter發送", -1, &state.sendButtonRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    
    // === 剪貼簿模式開關按鈕（細小圓形圖案）===
    // 設置按鈕矩形區域（用於點擊檢測）
    state.clipboardModeButtonRect = {clipboardButtonX, clipboardButtonY, 
                                      clipboardButtonX + clipboardButtonSize, 
                                      clipboardButtonY + clipboardButtonSize};
    
    // 綠色表示開啟，灰色表示關閉
    COLORREF clipboardModeColor = state.clipboardMode ? RGB(100, 200, 100) : RGB(200, 200, 200);
    if (state.clipboardModeButtonHover && !state.clipboardMode) {
        clipboardModeColor = RGB(180, 180, 180);
    }
    
    // 繪製圓形按鈕
    HBRUSH hClipboardModeBrush = CreateSolidBrush(clipboardModeColor);
    HPEN hClipboardModePen = CreatePen(PS_SOLID, 1, RGB(150, 150, 150));
    HPEN hClipboardModeOldPen = (HPEN)SelectObject(hdc, hClipboardModePen);
    HBRUSH hClipboardModeOldBrush = (HBRUSH)SelectObject(hdc, hClipboardModeBrush);
    
    // 繪製圓形
    Ellipse(hdc, clipboardButtonX, clipboardButtonY, 
            clipboardButtonX + clipboardButtonSize, 
            clipboardButtonY + clipboardButtonSize);
    
    SelectObject(hdc, hClipboardModeOldPen);
    SelectObject(hdc, hClipboardModeOldBrush);
    DeleteObject(hClipboardModeBrush);
    DeleteObject(hClipboardModePen);
    
    // 剪貼簿模式：在圓形按鈕右側顯示狀態指示器
    if (state.clipboardMode) {
        // 確保狀態指示器區域在圓形按鈕之後計算
        int indicatorX = state.clipboardModeButtonRect.right + 8;  // 放在圓形按鈕右側，稍微間隔
        int indicatorY = buttonY + (buttonHeight - 14) / 2;
        
        // 設置字體以正確顯示Unicode字符
        // 優先使用Segoe UI Symbol或Segoe UI Emoji以確保Unicode符號正確顯示
        std::wstring fontName = L"Segoe UI Symbol";
        HFONT hIndicatorFont = CreateFontW(14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, 
                                           DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                           DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, 
                                           fontName.c_str());
        
        // 如果Segoe UI Symbol創建失敗，嘗試使用系統默認字體
        if (!hIndicatorFont) {
            hIndicatorFont = CreateFontW(14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, 
                                        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                        DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, 
                                        state.fontName.c_str());
        }
        
        HFONT hOldIndicatorFont = (HFONT)SelectObject(hdc, hIndicatorFont);
        SetBkMode(hdc, TRANSPARENT);
        
        // 確定顯示內容和顏色
        COLORREF indicatorColor;
        std::wstring indicatorTextStr;
        
        if (state.clipboardInputting) {
            // 正在輸入中：顯示"..."
            indicatorColor = RGB(100, 100, 100);
            indicatorTextStr = L"...";
        } else {
            // 不在輸入中：顯示"☑"（剪貼簿模式開啟時始終顯示）
            indicatorColor = RGB(100, 200, 100);
            indicatorTextStr = L"☑";  // Unicode字符 U+2611
        }
        
        // 繪製狀態指示器（剪貼簿模式開啟時始終顯示）
        SetTextColor(hdc, indicatorColor);
        
        // 使用DrawTextW以確保Unicode字符正確顯示
        RECT indicatorRect = {indicatorX, indicatorY, indicatorX + 30, indicatorY + 20};
        DrawTextW(hdc, indicatorTextStr.c_str(), -1, &indicatorRect, 
                 DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOCLIP);
        
        SelectObject(hdc, hOldIndicatorFont);
        DeleteObject(hIndicatorFont);
    }
}

LRESULT CALLBACK BufferProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
		 case WM_ERASEBKGND:
            return 1;
          case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            RECT rc;
            GetClientRect(hwnd, &rc);
            
            // 双缓冲绘制
            HDC memDC = CreateCompatibleDC(hdc);
            HBITMAP memBitmap = CreateCompatibleBitmap(hdc, rc.right, rc.bottom);
            HBITMAP oldBitmap = (HBITMAP)SelectObject(memDC, memBitmap);
            
            drawBufferWindow(memDC, rc, g_state);
            
            BitBlt(hdc, 0, 0, rc.right, rc.bottom, memDC, 0, 0, SRCCOPY);
            
            SelectObject(memDC, oldBitmap);
            DeleteObject(memBitmap);
            DeleteDC(memDC);
            
            EndPaint(hwnd, &ps);
            return 0;
        }
        
        case WM_LBUTTONDOWN: {
    int x = LOWORD(lp); 
    int y = HIWORD(lp);
    
    // 檢查按鈕點擊（優先處理）
    if (isPointInSendButton(x, y, g_state)) {
        // 始終發送文字（恢復原樣）
        BufferManager::sendBufferContent(g_state);
        return 0;
    }
    
    if (isPointInClearButton(x, y, g_state)) {
        BufferManager::clearBufferWithConfirm(g_state);
        return 0;
    }
    
    if (isPointInSaveButton(x, y, g_state)) {
        BufferManager::saveBufferToTimestampedFile(g_state);
        return 0;
    }
    
    // 檢查圓形剪貼簿按鈕點擊（使用距離計算）
    int clipBtnCenterX = (g_state.clipboardModeButtonRect.left + g_state.clipboardModeButtonRect.right) / 2;
    int clipBtnCenterY = (g_state.clipboardModeButtonRect.top + g_state.clipboardModeButtonRect.bottom) / 2;
    int clipBtnRadius = (g_state.clipboardModeButtonRect.right - g_state.clipboardModeButtonRect.left) / 2;
    int dx = x - clipBtnCenterX;
    int dy = y - clipBtnCenterY;
    if (dx * dx + dy * dy <= clipBtnRadius * clipBtnRadius) {
        // 切換剪貼簿模式
        g_state.clipboardMode = !g_state.clipboardMode;
        
        // 如果開啟剪貼簿模式，立即複製當前暫放文字到剪貼簿
        if (g_state.clipboardMode) {
            if (!g_state.bufferText.empty()) {
                BufferManager::updateClipboardInMode(g_state);
            }
            // 設置狀態：不在輸入中，已複製狀態（顯示☑）
            g_state.clipboardInputting = false;
            g_state.clipboardCopied = true;
        } else {
            // 關閉時重置狀態
            g_state.clipboardInputting = false;
            g_state.clipboardCopied = false;
        }
        
        // 保存配置到interface_config.ini
        ConfigLoader::saveInterfaceConfig(g_state);
        InvalidateRect(hwnd, nullptr, TRUE);
        Utils::updateStatus(g_state, g_state.clipboardMode ? L"剪貼簿模式已開啟" : L"剪貼簿模式已關閉");
        return 0;
    }
    
    // 清除之前的選取
    BufferManager::clearSelection(g_state);
    
    // 開始新的選取或設定游標
    SetCapture(hwnd);
    g_state.bufferHasFocus = true;
    SetTimer(hwnd, 1, 500, NULL);
    g_state.bufferShowCursor = true;
    
    BufferManager::startSelection(g_state, x, y);
    BufferManager::setCursorPosition(g_state, x, y);
    return 0;
}
        
        case WM_MOUSEMOVE: {
    int x = LOWORD(lp); 
    int y = HIWORD(lp);
    
    bool wasSendHover = g_state.sendButtonHover;
    bool wasClearHover = g_state.clearButtonHover;
    bool wasSaveHover = g_state.saveButtonHover;
    bool wasClipboardModeHover = g_state.clipboardModeButtonHover;
    
    g_state.sendButtonHover = isPointInSendButton(x, y, g_state);
    g_state.clearButtonHover = isPointInClearButton(x, y, g_state);
    g_state.saveButtonHover = isPointInSaveButton(x, y, g_state);
    int clipBtnCenterX = (g_state.clipboardModeButtonRect.left + g_state.clipboardModeButtonRect.right) / 2;
    int clipBtnCenterY = (g_state.clipboardModeButtonRect.top + g_state.clipboardModeButtonRect.bottom) / 2;
    int clipBtnRadius = (g_state.clipboardModeButtonRect.right - g_state.clipboardModeButtonRect.left) / 2;
    int dx = x - clipBtnCenterX;
    int dy = y - clipBtnCenterY;
    g_state.clipboardModeButtonHover = (dx * dx + dy * dy <= clipBtnRadius * clipBtnRadius);
    
    if (g_state.isSelecting) {
        BufferManager::updateSelection(g_state, x, y);
    }
    
    if (wasSendHover != g_state.sendButtonHover || 
        wasClearHover != g_state.clearButtonHover || 
        wasSaveHover != g_state.saveButtonHover ||
        wasClipboardModeHover != g_state.clipboardModeButtonHover) {
        scheduleDeferredInvalidate(DEFERRED_BUFFER);
    }
    return 0;
}

case WM_LBUTTONUP: {
    if (GetCapture() == hwnd) {
        ReleaseCapture();
        BufferManager::endSelection(g_state);
    }
    return 0;
}

case WM_RBUTTONDOWN: {
    int x = LOWORD(lp);
    int y = HIWORD(lp);
    
    // 創建右鍵選單
    HMENU hMenu = CreatePopupMenu();
    
    if (g_state.hasSelection) {
        AppendMenu(hMenu, MF_STRING, 1001, L"複製 (Ctrl+C)");
        AppendMenu(hMenu, MF_STRING, 1002, L"剪下 (Ctrl+X)");
        AppendMenu(hMenu, MF_STRING, 1003, L"刪除");
        AppendMenu(hMenu, MF_SEPARATOR, 0, NULL);
    }
    
    AppendMenu(hMenu, MF_STRING, 1004, L"全選 (Ctrl+A)");
    AppendMenu(hMenu, MF_STRING, 1006, L"貼上 (Ctrl+V)");
    
    if (!g_state.bufferText.empty()) {
        AppendMenu(hMenu, MF_SEPARATOR, 0, NULL);
        AppendMenu(hMenu, MF_STRING, 1005, L"清空全部");
    }
    
    POINT pt = {x, y};
    ClientToScreen(hwnd, &pt);
    
    // 確保選單在頂層顯示，不被暫放視窗遮蓋
    // 設置標誌，防止計時器在選單顯示時重新設置TOPMOST
    g_state.menuShowing = true;
    
    // 臨時移除窗口的TOPMOST屬性（如果有的話），避免與輸入法頂層功能衝突
    LONG_PTR exStyle = GetWindowLongPtr(hwnd, GWL_EXSTYLE);
    bool wasTopmost = (exStyle & WS_EX_TOPMOST) != 0;
    if (wasTopmost) {
        SetWindowPos(hwnd, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    }
    
    // 設置窗口為前台窗口
    SetForegroundWindow(hwnd);
    // 讓系統完成前景切換與重繪，避免選單初次顯示白屏
    MSG msg;
    for (int i = 0; i < 15; ++i) {
        if (!PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) break;
        if (msg.message == WM_QUIT) break;
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    
    // 使用TrackPopupMenuEx（TPM_NOANIMATION 避免動畫導致初次不繪製）
    TPMPARAMS tpmParams = {0};
    tpmParams.cbSize = sizeof(TPMPARAMS);
    tpmParams.rcExclude.left = pt.x - 1;
    tpmParams.rcExclude.top = pt.y - 1;
    tpmParams.rcExclude.right = pt.x + 1;
    tpmParams.rcExclude.bottom = pt.y + 1;
    
    int cmd = TrackPopupMenuEx(hMenu, TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_LEFTALIGN | TPM_VERTICAL | TPM_NOANIMATION,
                              pt.x, pt.y, hwnd, &tpmParams);
    
    // 清除選單顯示標誌
    g_state.menuShowing = false;
    
    // 選單關閉後恢復窗口的TOPMOST狀態（如果原來是TOPMOST）
    if (wasTopmost) {
        SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    }
    
    // 發送消息確保焦點正確
    PostMessage(hwnd, WM_NULL, 0, 0);
    
    DestroyMenu(hMenu);
    
    // 處理選單命令
    switch (cmd) {
        case 1001: BufferManager::copySelection(g_state); break;
        case 1002: BufferManager::cutSelection(g_state); break;
        case 1003: BufferManager::deleteSelection(g_state); break;
        case 1004: BufferManager::selectAll(g_state); break;
        case 1005: BufferManager::clearBufferWithConfirm(g_state); break;
        case 1006: {
            // 貼上功能
            if (g_state.hasSelection) {
                BufferManager::deleteSelection(g_state);
            }
            if (OpenClipboard(hwnd)) {
                HANDLE hData = GetClipboardData(CF_UNICODETEXT);
                if (hData) {
                    wchar_t* pszText = (wchar_t*)GlobalLock(hData);
                    if (pszText) {
                        std::wstring pastedText(pszText);
                        BufferManager::insertTextAtCursor(g_state, pastedText);
                        Utils::updateStatus(g_state, L"已貼上 " + std::to_wstring(pastedText.length()) + L" 個字符");
                        GlobalUnlock(hData);
                    }
                }
                CloseClipboard();
            }
            break;
        }
    }
    
    return 0;
}
    

case WM_KEYDOWN: {
    bool ctrlPressed = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
    
    switch (wp) {
		case 'Z':
            if (ctrlPressed) {
                BufferManager::undo(g_state);
                return 0;
            }
            break;
            
        case 'Y':
            if (ctrlPressed) {
                BufferManager::redo(g_state);
                return 0;
            }
            break;
            		
        case 'A':
            if (ctrlPressed) {
                BufferManager::selectAll(g_state);
                return 0;
            }
            break;
            
        case 'C':
            if (ctrlPressed && g_state.hasSelection) {
                BufferManager::copySelection(g_state);
                return 0;
            }
            break;
            
        case 'X':
            if (ctrlPressed && g_state.hasSelection) {
                BufferManager::cutSelection(g_state);
                return 0;
            }
            break;
            
        case 'V':
            if (ctrlPressed) {
                // 在暫放視窗內按Ctrl+V：正常貼上行為（剪貼簿模式下不會清空，由鍵盤鉤子處理窗口外的情況）
                // 如果有選取文字，先刪除
                if (g_state.hasSelection) {
                    BufferManager::deleteSelection(g_state);
                }
                
                // 從剪貼簿貼上
                if (OpenClipboard(hwnd)) {
                    HANDLE hData = GetClipboardData(CF_UNICODETEXT);
                    if (hData) {
                        wchar_t* pszText = (wchar_t*)GlobalLock(hData);
                        if (pszText) {
                            std::wstring pastedText(pszText);
                            BufferManager::insertTextAtCursor(g_state, pastedText);
                            Utils::updateStatus(g_state, L"已貼上 " + std::to_wstring(pastedText.length()) + L" 個字符");
                            GlobalUnlock(hData);
                        }
                    }
                    CloseClipboard();
                }
                return 0;
            }
            break;
            
        case VK_DELETE:
            if (g_state.hasSelection) {
                BufferManager::deleteSelection(g_state);
                return 0;
            }
            break;
            
        case VK_ESCAPE:
            if (g_state.hasSelection) {
                BufferManager::clearSelection(g_state);
                return 0;
            }
            break;
    }
    break;
}    
        case WM_TIMER: {
            if (wp == 999) {
                // 延遲保存用戶字典（避免頻繁寫入文件）
                KillTimer(hwnd, 999);
                Dictionary::saveUserDict(g_state);
                Dictionary::saveContextLearning(g_state);  // 儲存智能聯想引擎的個人上下文學習記錄
                return 0;
            }
            if (wp == 1) {
                g_state.bufferShowCursor = !g_state.bufferShowCursor;
                // 檢查輸入結束（剪貼簿模式）
                bool wasInputting = g_state.clipboardInputting;
                BufferManager::checkInputEnd(g_state);
                // 如果狀態改變，需要重繪以更新指示器
                if (wasInputting != g_state.clipboardInputting) {
                    InvalidateRect(hwnd, nullptr, TRUE);
                } else {
                    InvalidateRect(hwnd, nullptr, TRUE);  // 總是重繪以更新游標和指示器
                }
            }
            return 0;
        }
        
        case WM_SETFOCUS: {
            g_state.bufferHasFocus = true;
            SetTimer(hwnd, 1, 500, NULL);
            g_state.bufferShowCursor = true;
            InvalidateRect(hwnd, nullptr, TRUE);
            return 0;
        }
        
        case WM_KILLFOCUS: {
            g_state.bufferHasFocus = false;
            KillTimer(hwnd, 1);
            
            // 剪貼簿模式：失去焦點時立即複製當前文字到剪貼簿
            if (g_state.clipboardMode && !g_state.bufferText.empty()) {
                BufferManager::updateClipboardInMode(g_state);
                g_state.clipboardInputting = false;
                g_state.clipboardCopied = true;
            }
            
            InvalidateRect(hwnd, nullptr, TRUE);
            return 0;
        }
    }
    
    return DefWindowProc(hwnd, msg, wp, lp);
}

// OptimizedUI工具列繪製函數
void drawOptimizedToolbar(HDC hdc, RECT rc, GlobalState& state) {
    // 背景
    HBRUSH hBg = CreateSolidBrush(state.uiColors.toolbarBgColor);
    FillRect(hdc, &rc, hBg);
    DeleteObject(hBg);
    
    // 邊框
    HPEN hPen = CreatePen(PS_SOLID, 1, state.uiColors.toolbarBorderColor);
    HPEN hOldPen = (HPEN)SelectObject(hdc, hPen);
    HBRUSH hOldBrush = (HBRUSH)SelectObject(hdc, GetStockObject(NULL_BRUSH));
    Rectangle(hdc, 0, 0, rc.right, rc.bottom);
    SelectObject(hdc, hOldBrush);
    SelectObject(hdc, hOldPen);
    DeleteObject(hPen);
    
    SetBkMode(hdc, TRANSPARENT);
    
    int x = 5;
    int y = (rc.bottom - BUTTON_HEIGHT) / 2;
    
    // 筆劃標識
    HFONT hFont = CreateFontW(18, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Microsoft JhengHei");
    HFONT hOldFont = (HFONT)SelectObject(hdc, hFont);
    
    SetTextColor(hdc, RGB(60, 60, 60));
    TextOutW(hdc, x, y + 3, L"筆劃", 2);
    x += 50;
    
    // 模式指示器
    state.toolbarElements.modeIndicatorRect = {x, y, x + MODE_BUTTON_WIDTH, y + BUTTON_HEIGHT};
    COLORREF modeColor = state.chineseMode ? state.uiColors.modeActiveColor : state.uiColors.modeInactiveColor;
    if (state.toolbarElements.modeIndicatorHover) modeColor = state.uiColors.buttonHoverColor;
    
    HBRUSH hModeBrush = CreateSolidBrush(modeColor);
    FillRect(hdc, &state.toolbarElements.modeIndicatorRect, hModeBrush);
    DeleteObject(hModeBrush);
    
    SetTextColor(hdc, RGB(255, 255, 255));
    std::wstring modeText = state.chineseMode ? L"中" : L"EN";
    RECT modeTextRect = state.toolbarElements.modeIndicatorRect;
    DrawTextW(hdc, modeText.c_str(), -1, &modeTextRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    x += 40;
    
    // 狀態指示器
    state.toolbarElements.statusIndicatorRect = {x, y + 8, x + 6, y + 14};
    COLORREF statusColor = state.uiColors.statusReadyColor;
    if (state.imePaused) {
        // 暫停狀態：顯示為灰色（不亮）
        statusColor = state.uiColors.statusPausedColor;
    } else if (state.inputError) {
        statusColor = state.uiColors.statusErrorColor;
    } else if (state.isInputting) {
        // 輸入中狀態：只有在有輸入內容或正在顯示聯想字時才亮黃燈
        // 修復：當沒有開啟聯想字時，輸入完成後（input為空且showCand為false）不應該亮黃燈
        if (!state.input.empty() || (state.showCand && state.enableWordPrediction)) {
            statusColor = state.uiColors.statusInputColor;
        }
    }
    
    HBRUSH hStatusBrush = CreateSolidBrush(statusColor);
    HPEN hStatusPen = CreatePen(PS_SOLID, 1, RGB(100, 100, 100));
    SelectObject(hdc, hStatusPen);
    SelectObject(hdc, hStatusBrush);
    Ellipse(hdc, state.toolbarElements.statusIndicatorRect.left, state.toolbarElements.statusIndicatorRect.top,
            state.toolbarElements.statusIndicatorRect.right, state.toolbarElements.statusIndicatorRect.bottom);
    DeleteObject(hStatusBrush);
    DeleteObject(hStatusPen);
    x += 12;
    
    // 選單按鈕
    state.toolbarElements.menuButtonRect = {x, y, x + 40, y + BUTTON_HEIGHT};
    if (state.toolbarElements.menuButtonHover) {
        HBRUSH hMenuBrush = CreateSolidBrush(state.uiColors.buttonHoverColor);
        FillRect(hdc, &state.toolbarElements.menuButtonRect, hMenuBrush);
        DeleteObject(hMenuBrush);
    }
    SetTextColor(hdc, RGB(60, 60, 60));
    DrawTextW(hdc, L"☰", -1, &state.toolbarElements.menuButtonRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    x += 45;
    
    // 暫放模式按鈕
    state.toolbarElements.bufferButtonRect = {x, y, x + SMALL_BUTTON_WIDTH, y + BUTTON_HEIGHT};
    COLORREF bufferColor = state.bufferMode ? state.uiColors.bufferButtonActiveColor : state.uiColors.bufferButtonInactiveColor;
    if (state.toolbarElements.bufferButtonHover) bufferColor = state.uiColors.buttonHoverColor;
    
    HBRUSH hBufferBrush = CreateSolidBrush(bufferColor);
    FillRect(hdc, &state.toolbarElements.bufferButtonRect, hBufferBrush);
    DeleteObject(hBufferBrush);

    HFONT hBufferFont = CreateFontW(25, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Microsoft JhengHei");
    HFONT hOldBufferFont = (HFONT)SelectObject(hdc, hBufferFont);
    SetTextColor(hdc, RGB(60, 60, 60));
    DrawTextW(hdc, L"⌘", -1, &state.toolbarElements.bufferButtonRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    SelectObject(hdc, hOldBufferFont);
    DeleteObject(hBufferFont);
    x += 40;
    
    // 恢復跟隨鼠標位置按鈕
    state.toolbarElements.restoreButtonRect = {x, y, x + SMALL_BUTTON_WIDTH, y + BUTTON_HEIGHT};
    if (state.toolbarElements.restoreButtonHover) {
        HBRUSH hResetBrush = CreateSolidBrush(state.uiColors.buttonHoverColor);
        FillRect(hdc, &state.toolbarElements.restoreButtonRect, hResetBrush);
        DeleteObject(hResetBrush);
    }

    HFONT hResetFont = CreateFontW(22, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Microsoft JhengHei");
    HFONT hOldResetFont = (HFONT)SelectObject(hdc, hResetFont);
    SetTextColor(hdc, RGB(60, 60, 60));
    DrawTextW(hdc, L"⿻", -1, &state.toolbarElements.restoreButtonRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    SelectObject(hdc, hOldResetFont);
    DeleteObject(hResetFont);
    x += 40;
    
    // 最小化按鈕
    state.toolbarElements.minimizeButtonRect = {x, y, x + 20, y + BUTTON_HEIGHT};
    if (state.toolbarElements.minimizeButtonHover) {
        HBRUSH hMinBrush = CreateSolidBrush(state.uiColors.buttonHoverColor);
        FillRect(hdc, &state.toolbarElements.minimizeButtonRect, hMinBrush);
        DeleteObject(hMinBrush);
    }
    SetTextColor(hdc, RGB(60, 60, 60));
    DrawTextW(hdc, L"－", -1, &state.toolbarElements.minimizeButtonRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    x += 25;
    
    // 關閉按鈕
    state.toolbarElements.closeButtonRect = {x, y, x + 20, y + BUTTON_HEIGHT};
    COLORREF closeColor = state.toolbarElements.closeButtonHover ? RGB(255, 70, 70) : state.uiColors.closeButtonColor;
    HBRUSH hCloseBrush = CreateSolidBrush(closeColor);
    FillRect(hdc, &state.toolbarElements.closeButtonRect, hCloseBrush);
    DeleteObject(hCloseBrush);
    
    SetTextColor(hdc, RGB(255, 255, 255));
    DrawTextW(hdc, L"×", -1, &state.toolbarElements.closeButtonRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    
    SelectObject(hdc, hOldFont);
    DeleteObject(hFont);
}

// OptimizedUI按鈕點擊檢測
bool isPointInOptimizedButton(int x, int y, const RECT& buttonRect) {
    return Utils::isPointInRect(x, y, buttonRect);
}

// OptimizedUI按鈕懸停狀態更新
void updateOptimizedButtonHover(int x, int y, GlobalState& state) {
    bool newModeHover = isPointInOptimizedButton(x, y, state.toolbarElements.modeIndicatorRect);
    bool newMenuHover = isPointInOptimizedButton(x, y, state.toolbarElements.menuButtonRect);
    bool newBufferHover = isPointInOptimizedButton(x, y, state.toolbarElements.bufferButtonRect);
    bool newRestoreHover = isPointInOptimizedButton(x, y, state.toolbarElements.restoreButtonRect);
    bool newMinimizeHover = isPointInOptimizedButton(x, y, state.toolbarElements.minimizeButtonRect);
    bool newCloseHover = isPointInOptimizedButton(x, y, state.toolbarElements.closeButtonRect);
    
    bool needRedraw = false;
    if (newModeHover != state.toolbarElements.modeIndicatorHover || 
        newMenuHover != state.toolbarElements.menuButtonHover ||
        newBufferHover != state.toolbarElements.bufferButtonHover ||
        newRestoreHover != state.toolbarElements.restoreButtonHover || 
        newMinimizeHover != state.toolbarElements.minimizeButtonHover || 
        newCloseHover != state.toolbarElements.closeButtonHover) {
        needRedraw = true;
    }
    
    state.toolbarElements.modeIndicatorHover = newModeHover;
    state.toolbarElements.menuButtonHover = newMenuHover;
    state.toolbarElements.bufferButtonHover = newBufferHover;
    state.toolbarElements.restoreButtonHover = newRestoreHover;
    state.toolbarElements.minimizeButtonHover = newMinimizeHover;
    state.toolbarElements.closeButtonHover = newCloseHover;
    
    if (needRedraw && state.hWnd) {
        scheduleDeferredInvalidate(DEFERRED_MAIN);
    }
}

// 暫放視窗跟隨工具列移動
void updateBufferWindowPosition(GlobalState& state) {
    if (!state.bufferMode || !state.hBufferWnd) {
        return;
    }
    
    // 獲得工具列位置
    RECT toolbarRect;
    GetWindowRect(state.hWnd, &toolbarRect);
    
    // 計算暫放視窗應該的位置（工具列正下方）
    int bufferX = toolbarRect.left;
    int bufferY = toolbarRect.bottom + 5;
    
    // 計算暫放視窗高度
    int windowHeight = BufferManager::calculateBufferWindowHeight(state);
    
    // 暫放視窗始終設置為TOPMOST（與工具列一樣）
    // 通過調整設置順序來控制Z-order：後設置的窗口會顯示在前面
    if (!state.menuShowing) {
        SetWindowPos(state.hBufferWnd, HWND_TOPMOST, 
                    bufferX, bufferY, FIXED_WIDTH, windowHeight,
                    SWP_NOACTIVATE | SWP_SHOWWINDOW);
        
        // 應用透明度設置（如果已啟用）
        if (state.enableTransparency) {
            LONG_PTR exStyle = GetWindowLongPtr(state.hBufferWnd, GWL_EXSTYLE);
            SetWindowLongPtr(state.hBufferWnd, GWL_EXSTYLE, exStyle | WS_EX_LAYERED);
            SetLayeredWindowAttributes(state.hBufferWnd, 0, state.transparencyAlpha, LWA_ALPHA);
        }
        
        // 如果有字碼視窗或候選字視窗顯示，將它們設置在暫放視窗之上（通過後設置TOPMOST）
        // 這樣可以確保字碼視窗和候選字視窗顯示在暫放視窗前面，但暫放視窗仍然是TOPMOST
        if (state.hInputWnd && IsWindowVisible(state.hInputWnd)) {
            SetWindowPos(state.hInputWnd, HWND_TOPMOST, 0, 0, 0, 0,
                        SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        }
        if (state.hCandWnd && IsWindowVisible(state.hCandWnd)) {
            SetWindowPos(state.hCandWnd, HWND_TOPMOST, 0, 0, 0, 0,
                        SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        }
    } else {
        // 菜單顯示時，暫放視窗設置為NOTOPMOST
        SetWindowPos(state.hBufferWnd, HWND_NOTOPMOST, 
                    bufferX, bufferY, FIXED_WIDTH, windowHeight,
                    SWP_NOACTIVATE | SWP_SHOWWINDOW);
    }
    
    InvalidateRect(state.hBufferWnd, nullptr, TRUE);
}

// OptimizedUI工具列拖拽處理
void handleOptimizedToolbarDrag(HWND hwnd, POINT currentPos, GlobalState& state) {
    // 計算新位置
    int newX = currentPos.x - state.dragState.dragOffset.x;
    int newY = currentPos.y - state.dragState.dragOffset.y;
    
    // 移動工具列
    SetWindowPos(hwnd, NULL, newX, newY, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
    
    // 更新全域位置記錄
    PositionManager::g_toolbarPos.x = newX;
    PositionManager::g_toolbarPos.y = newY;
    
    // 暫放視窗跟隨移動
    updateBufferWindowPosition(state);
}

// OptimizedUI候選字視窗拖拽處理
void handleOptimizedCandidateDrag(HWND hwnd, POINT currentPos, GlobalState& state) {
    int newX = currentPos.x - state.dragState.dragOffset.x;
    int newY = currentPos.y - state.dragState.dragOffset.y;
    
    // 移動候選字視窗
    RECT candRect;
    GetWindowRect(hwnd, &candRect);
    int candWidth = candRect.right - candRect.left;
    int candHeight = candRect.bottom - candRect.top;
    
    SetWindowPos(hwnd, NULL, newX, newY, candWidth, candHeight, SWP_NOZORDER | SWP_SHOWWINDOW);
}

// OptimizedUI視窗類別註冊
bool registerOptimizedWindowClasses(HINSTANCE hInstance) {
    // 註冊主視窗類別
    WNDCLASSW wc = {0};
    wc.lpfnWndProc = OptimizedWndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = L"IME_MAIN_OPTIMIZED";
    wc.hbrBackground = CreateSolidBrush(RGB(240,240,240));
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    if (!RegisterClassW(&wc)) {
        return false;
    }
    
    // 註冊候選視窗類別（字碼候選字）
    WNDCLASSW wc2 = {0};
    wc2.lpfnWndProc = CandProc;  // 使用統一的候選字窗口過程
    wc2.hInstance = hInstance;
    wc2.lpszClassName = L"IME_CAND_OPTIMIZED";
    wc2.hbrBackground = NULL;
    wc2.hCursor = LoadCursor(NULL, IDC_ARROW);
    if (!RegisterClassW(&wc2)) {
        return false;
    }
    
    // 註冊聯想字視窗類別（目前行為與 CandProc 相同，後續再微調）
    WNDCLASSW wcPred = {0};
    wcPred.lpfnWndProc = PredProc;
    wcPred.hInstance = hInstance;
    wcPred.lpszClassName = L"IMEPREDICT";
    wcPred.hbrBackground = NULL;
    wcPred.hCursor = LoadCursor(NULL, IDC_ARROW);
    if (!RegisterClassW(&wcPred)) {
        return false;
    }
    
    // 註冊暫放視窗類別
    WNDCLASSW wc3 = {0};
    wc3.lpfnWndProc = BufferProc;
    wc3.hInstance = hInstance;
    wc3.lpszClassName = L"IME_BUFFER";
    wc3.hbrBackground = CreateSolidBrush(RGB(255,255,255));
    wc3.hCursor = LoadCursor(NULL, IDC_IBEAM);
    if (!RegisterClassW(&wc3)) {
        return false;
    }
    
    return true;
}

// 創建字碼輸入視窗
bool createInputWindow(HINSTANCE hInstance, GlobalState& state) {
    // 註冊字碼視窗類別
    WNDCLASSW wc = {0};
    wc.lpfnWndProc = InputProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = L"IME_INPUT";
    wc.hbrBackground = CreateSolidBrush(state.inputBackgroundColor); // 使用配置的背景色
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    if (!RegisterClassW(&wc)) {
        return false;
    }
    
    // 創建字碼視窗，使用配置的尺寸
    state.hInputWnd = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
        L"IME_INPUT",
        L"",
        WS_POPUP | WS_BORDER,
        100, 150, 
        state.inputWindowWidth,   // 使用配置的寬度
        state.inputWindowHeight,  // 使用配置的高度
        NULL, NULL, hInstance, NULL
    );
    
    return state.hInputWnd != NULL;
}

// 修復：字碼輸入視窗繪製 - 加入3+3提示
void drawInputWindow(HDC hdc, RECT rc, const GlobalState& state) {
    // 使用配置的背景色（替代寫死的白色）
    HBRUSH hBg = CreateSolidBrush(state.inputBackgroundColor);
    FillRect(hdc, &rc, hBg);
    DeleteObject(hBg);
    
    // 使用配置的邊框色（替代寫死的灰色）
    HPEN hPen = CreatePen(PS_SOLID, 1, state.inputBorderColor);
    HPEN hOldPen = (HPEN)SelectObject(hdc, hPen);
    HBRUSH hOldBrush = (HBRUSH)SelectObject(hdc, GetStockObject(NULL_BRUSH));
    Rectangle(hdc, 0, 0, rc.right, rc.bottom);
    SelectObject(hdc, hOldBrush);
    SelectObject(hdc, hOldPen);
    DeleteObject(hPen);
    
    // 使用配置的字型大小和名稱（替代寫死的16和Microsoft JhengHei）
    HFONT hFont = CreateFontW(state.inputFontSize, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, state.inputFontName.c_str());
    HFONT hOldFont = (HFONT)SelectObject(hdc, hFont);
    
    SetBkMode(hdc, TRANSPARENT);
    
    // 使用 getInputDisplay 來獲取包含3+3提示的顯示文字
    std::wstring displayText;
    if (state.input.empty()) {
        // 如果正在顯示聯想字，依狀態提示用戶
        if (state.showCand && state.isInputting && state.enableWordPrediction) {
            if (state.lastSelected.empty()) {
                // 尚未有前字：提示先輸入前字
                displayText = state.chineseMode ? L"請先輸入前字，再選擇聯想字或輸入筆劃代碼 (u i o j k l)" : L"English Input Mode";
            } else {
                // 已有前字：提示可直接選擇聯想字或輸入筆劃
                displayText = state.chineseMode ? L"請選擇聯想字或輸入筆劃代碼 (u i o j k l)" : L"English Input Mode";
            }
        } else {
            displayText = state.chineseMode ? L"請輸入筆劃代碼 (u i o j k l)" : L"English Input Mode";
        }
        SetTextColor(hdc, state.inputHintTextColor); // 使用配置的提示文字顏色
    } else {
        displayText = Dictionary::getInputDisplay(state);
        
        if (state.inputError) {
            SetTextColor(hdc, state.inputErrorTextColor); // 使用配置的錯誤文字顏色
        } else {
            SetTextColor(hdc, state.inputTextColor); // 使用配置的正常文字顏色
        }
    }
    
    // 處理過長文字的顯示
    int maxWidth = rc.right - 20;
    SIZE textSize;
    GetTextExtentPoint32W(hdc, displayText.c_str(), displayText.length(), &textSize);
    
    if (textSize.cx > maxWidth) {
        // 文字過長時縮小字體
        DeleteObject(hFont);
        int smallerSize = (maxWidth * state.inputFontSize) / textSize.cx;
        if (smallerSize < 10) smallerSize = 10;
        
        hFont = CreateFontW(smallerSize, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, state.inputFontName.c_str());
        SelectObject(hdc, hFont);
    }
    
    TextOutW(hdc, 10, 6, displayText.c_str(), displayText.length());
    
    SelectObject(hdc, hOldFont);
    DeleteObject(hFont);
}

// OptimizedUI視窗建立
bool createOptimizedWindows(HINSTANCE hInstance, GlobalState& state) {
    // 創建主工具列視窗
    state.hWnd = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW, 
        L"IME_MAIN_OPTIMIZED", 
        L"中文筆劃輸入法",
        WS_POPUP | WS_BORDER, 
        PositionManager::g_toolbarPos.x, 
        PositionManager::g_toolbarPos.y, 
        TOOLBAR_WIDTH, 
        TOOLBAR_HEIGHT,
        NULL, NULL, hInstance, NULL
    );
    
    if (!state.hWnd) return false;
    
    // 創建候選視窗（字碼候選字）
    state.hCandWnd = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW, 
        L"IME_CAND_OPTIMIZED", 
        L"",
        WS_POPUP | WS_BORDER, 
        100, 180, state.candidateWidth, state.candidateHeight,
        NULL, NULL, hInstance, NULL
    );
    
    if (!state.hCandWnd) return false;
    
    // 創建聯想字視窗（目前預設隱藏，後續步驟再使用）
    state.hPredWnd = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
        L"IMEPREDICT",
        L"",
        WS_POPUP | WS_BORDER,
        100, 220,
        state.candidateWidth,
        state.candidateHeight,
        NULL, NULL, hInstance, NULL
    );
    if (!state.hPredWnd) return false;
    ShowWindow(state.hPredWnd, SW_HIDE);
    
    // 創建字碼輸入視窗
    if (!createInputWindow(hInstance, state)) {
        return false;
    }
    
    // 創建暫放視窗（使用既有函數）
    state.hBufferWnd = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW, 
        L"IME_BUFFER", 
        L"",
        WS_POPUP | WS_BORDER, 
        100, 280, FIXED_WIDTH, MIN_HEIGHT,
        NULL, NULL, hInstance, NULL
    );
    
    if (!state.hBufferWnd) return false;
    
    return true;
}

// UI模式切換
void switchToOptimizedUI(GlobalState& state) {
    state.useOptimizedUI = true;
    if (state.hWnd) InvalidateRect(state.hWnd, nullptr, TRUE);
}

// 注意：switchToClassicUI 已移除，程序现在只使用 OptimizedUI

// OptimizedUI主視窗程序
LRESULT CALLBACK OptimizedWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_ERASEBKGND:
            return 1;  // 避免背景擦除造成的閃爍
        
		case WM_DESTROY:
            return handleWindowDestroy(hwnd);
	
        case WM_CREATE: {
            // 使用配置檔案中的間隔設定
            if (g_state.forceStayOnTop) {
                SetTimer(hwnd, 999, g_state.topmostCheckInterval, NULL);
            }
            return 0;
        }	
        
		case WM_TIMER: {
			// 進階優化：延遲重繪計時器（合併滑鼠 hover 觸發的重繪）
			if (wp == DEFERRED_INVALIDATE_TIMER_ID) {
				KillTimer(hwnd, DEFERRED_INVALIDATE_TIMER_ID);
				g_deferredInvalidateScheduled = false;
				if (g_deferredInvalidateFlags & DEFERRED_MAIN && g_state.hWnd) InvalidateRect(g_state.hWnd, nullptr, TRUE);
				if (g_deferredInvalidateFlags & DEFERRED_CAND && g_state.hCandWnd) InvalidateRect(g_state.hCandWnd, nullptr, TRUE);
				if (g_deferredInvalidateFlags & DEFERRED_PRED && g_state.hPredWnd) InvalidateRect(g_state.hPredWnd, nullptr, TRUE);
				if (g_deferredInvalidateFlags & DEFERRED_BUFFER && g_state.hBufferWnd) InvalidateRect(g_state.hBufferWnd, nullptr, TRUE);
				g_deferredInvalidateFlags = 0;
				return 0;
			}
			if (wp == 995) {
				// 延遲保存用戶字典（避免頻繁寫入文件，提升性能）
				KillTimer(hwnd, 995);
				Dictionary::saveUserDict(g_state);
				Dictionary::saveContextLearning(g_state);  // 儲存智能聯想引擎的個人上下文學習記錄
				return 0;
			}
			if (wp == 997) {
				// 新增：延遲處理螢幕模式變更
				KillTimer(hwnd, 997);
        
				ScreenManager::handleDisplayChange();
				PositionManager::adjustPositionForScreenMode(g_state);
				// 重新定位所有輸入法視窗至工作區內（避免被工作列遮蓋）
				ScreenManager::updateMonitorInfo();
				WindowManager::positionMainWindow(g_state);
				WindowManager::positionWindowsOptimized(g_state);
        
				// 確保視窗可見
				if (!IsWindowVisible(hwnd)) {
					ShowWindow(hwnd, SW_SHOW);
				}
        
				// 強制重繪
				InvalidateRect(hwnd, nullptr, TRUE);
				UpdateWindow(hwnd);
        
				return 0;
			}
			else if (wp == 995) {
				// 延遲保存用戶字典（避免頻繁寫入文件，提升性能）
				KillTimer(hwnd, 995);
				Dictionary::saveUserDict(g_state);
				return 0;
			}
			else if (wp == 998) {
				// 新增：重試定位
				KillTimer(hwnd, 998);
				PositionManager::adjustPositionForScreenMode(g_state);
				return 0;
			}
			else if (wp == 993) {
				// 延遲恢復 TOPMOST（由 WM_USER+300 觸發），避免 Sleep 阻塞導致 toolbar 卡死
				KillTimer(hwnd, 993);
				SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0,
				            SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
				return 0;
			}
			else if (wp == 994) {
				// 啟動時自動檢查版本更新（僅在程序啟動時執行一次）
				KillTimer(hwnd, 994);
				
				// 檢查版本（使用緩存，但如果緩存過期會自動從 GitHub 獲取）
				std::string currentVersion = APP_VERSION;
				std::string versionCachePath = Utils::wstrToUtf8(g_state.systemDir + L"version_cache.txt");
				std::string remoteVersion = DictUpdater::getRemoteVersion(nullptr, false, 24, versionCachePath.c_str());
				
				// 如果發現新版本，顯示通知
				if (!remoteVersion.empty() && remoteVersion != currentVersion) {
					std::wstring currentVersionW = Utils::utf8ToWstr(currentVersion);
					std::wstring remoteVersionW = Utils::utf8ToWstr(remoteVersion);
					
					std::wstring updateMsg = L"發現新版本可用！\n\n";
					updateMsg += L"當前版本：V" + currentVersionW + L"\n";
					updateMsg += L"最新版本：V" + remoteVersionW + L"\n\n";
					updateMsg += L"是否前往 GitHub 下載最新版本？\n\n";
					updateMsg += L"https://github.com/Yamazaki427858/ChineseStrokeIME";
					
					int result = MessageBoxW(hwnd, updateMsg.c_str(), 
						L"版本更新通知", MB_YESNO | MB_ICONINFORMATION);
					
					if (result == IDYES) {
						ShellExecuteW(NULL, L"open", L"https://github.com/Yamazaki427858/ChineseStrokeIME", NULL, NULL, SW_SHOWNORMAL);
					}
				}
				
				return 0;
			}
			else if (wp == 996) {
				// 新增：初始化位置檢查
				KillTimer(hwnd, 996);
        
				// 確保視窗在可見位置
				if (!PositionManager::isPositionVisible(g_state)) {
					PositionManager::forceResetToSafePosition(g_state);
				}
        
				// 確保視窗顯示
				ShowWindow(g_state.hWnd, SW_SHOW);
				SetForegroundWindow(g_state.hWnd);
        
				return 0;
			}
			else if (wp == 999) {
                // 如果選單正在顯示，跳過TOPMOST設置以避免衝突
                if (g_state.menuShowing) {
                    return 0;
                }
            
                // 強制保持前置（主工具列）
                SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0, 
                            SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
                // 同步保持候選/聯想視窗前置（若可見且未在顯示選單）
                if (g_state.hCandWnd && IsWindowVisible(g_state.hCandWnd)) {
                    SetWindowPos(g_state.hCandWnd, HWND_TOPMOST, 0, 0, 0, 0,
                                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
                }
                if (g_state.hPredWnd && IsWindowVisible(g_state.hPredWnd)) {
                    SetWindowPos(g_state.hPredWnd, HWND_TOPMOST, 0, 0, 0, 0,
                                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
                }
                
                // 檢查並修正主視窗位置是否在工作區內（避免被工作列遮蓋）
                RECT workArea = ScreenManager::getWorkArea(g_state.hWnd);
                RECT currentRect;
                GetWindowRect(g_state.hWnd, &currentRect);
                bool needReposition = false;
                int x = currentRect.left;
                int y = currentRect.top;
                int w = currentRect.right - currentRect.left;
                int h = currentRect.bottom - currentRect.top;
                const int margin = 5;
                if (currentRect.right > workArea.right - margin) {
                    x = workArea.right - w - margin;
                    needReposition = true;
                }
                if (currentRect.bottom > workArea.bottom - margin) {
                    y = workArea.bottom - h - margin;
                    needReposition = true;
                }
                if (currentRect.left < workArea.left + margin) {
                    x = workArea.left + margin;
                    needReposition = true;
                }
                if (currentRect.top < workArea.top + margin) {
                    y = workArea.top + margin;
                    needReposition = true;
                }
                if (needReposition) {
                    PositionManager::g_toolbarPos.x = x;
                    PositionManager::g_toolbarPos.y = y;
                    SetWindowPos(g_state.hWnd, HWND_TOPMOST, x, y, 0, 0,
                                 SWP_NOSIZE | SWP_NOACTIVATE);
                }
                
                // 同時確保其他關鍵視窗也保持前置
                // 通過調整設置順序來控制Z-order：先設置暫放視窗，再設置字碼視窗和候選字視窗
                // 這樣可以確保字碼視窗和候選字視窗顯示在暫放視窗前面，但暫放視窗仍然是TOPMOST
                if (g_state.hBufferWnd && IsWindowVisible(g_state.hBufferWnd) && !g_state.menuShowing) {
                    SetWindowPos(g_state.hBufferWnd, HWND_TOPMOST, 0, 0, 0, 0,
                                SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
                }
                
                // 字碼視窗和候選字視窗設置在暫放視窗之後，這樣它們會顯示在暫放視窗前面
                if (g_state.hCandWnd && IsWindowVisible(g_state.hCandWnd) && !g_state.menuShowing) {
                    SetWindowPos(g_state.hCandWnd, HWND_TOPMOST, 0, 0, 0, 0,
                                SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
                }
                if (g_state.hInputWnd && IsWindowVisible(g_state.hInputWnd) && !g_state.menuShowing) {
                    SetWindowPos(g_state.hInputWnd, HWND_TOPMOST, 0, 0, 0, 0,
                                SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
                }
                
                // 注意：不再使用 IME API，改用鍵盤鉤子直接轉換字母
            }
            return 0;
        }
		
		case WM_DISPLAYCHANGE:
            ScreenManager::updateMonitorInfo();
            positionMainWindow(g_state);
            positionWindowsOptimized(g_state);
            return handleDisplayChange(hwnd);

        case WM_SETTINGCHANGE:
            // 工作列位置變更時，重新定位至工作區內
            ScreenManager::updateMonitorInfo();
            positionMainWindow(g_state);
            positionWindowsOptimized(g_state);
            return 0;

        // 注意：WM_USER + 500 已移除（自動檢查更新功能已禁用，避免 GitHub API 訪問次數限制）

        case WM_USER+100:
            return handleKeyboardInput(hwnd, wp);
		
		case WM_USER+200:
			return handleTrayMessage(hwnd, lp);
            
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            RECT rc;
            GetClientRect(hwnd, &rc);
            
            // 使用雙緩衝繪製以避免閃爍
            HDC memDC = CreateCompatibleDC(hdc);
            HBITMAP memBitmap = CreateCompatibleBitmap(hdc, rc.right, rc.bottom);
            HBITMAP oldBitmap = (HBITMAP)SelectObject(memDC, memBitmap);
            
            // 在記憶體DC上繪製
            drawOptimizedToolbar(memDC, rc, g_state);
            
            // 將記憶體DC的內容複製到實際DC
            BitBlt(hdc, 0, 0, rc.right, rc.bottom, memDC, 0, 0, SRCCOPY);
            
            // 清理資源
            SelectObject(memDC, oldBitmap);
            DeleteObject(memBitmap);
            DeleteDC(memDC);
            
            EndPaint(hwnd, &ps);
            return 0;
        }
            
        case WM_LBUTTONDOWN: {
            int x = LOWORD(lp); 
            int y = HIWORD(lp);
            
            // OptimizedUI按鈕點擊檢測
            if (isPointInOptimizedButton(x, y, g_state.toolbarElements.closeButtonRect)) {
                if (MessageBoxW(hwnd, L"確定要關閉輸入法嗎？", L"確認關閉", MB_YESNO | MB_ICONQUESTION) == IDYES) {
                    PostMessage(hwnd, WM_CLOSE, 0, 0);
                }
                return 0;
            }
            
            if (isPointInOptimizedButton(x, y, g_state.toolbarElements.modeIndicatorRect)) {
                InputHandler::toggleInputMode(g_state);
                return 0;
            }
            
            if (isPointInOptimizedButton(x, y, g_state.toolbarElements.menuButtonRect)) {
                HMENU hMenu = CreatePopupMenu();
                AppendMenu(hMenu, MF_STRING, 1001, L"標點符號選單");
                AppendMenu(hMenu, MF_STRING, 1002, L"重新載入字碼表");
                AppendMenu(hMenu, MF_STRING, 1008, L"從GitHub更新字碼表");
                AppendMenu(hMenu, MF_STRING, 1005, L"重新載入配置");
                AppendMenu(hMenu, MF_SEPARATOR, 0, NULL);
                std::wstring transparencyText = g_state.enableTransparency ? L"✓ 半透明顯示" : L"半透明顯示";
                AppendMenu(hMenu, MF_STRING, 1007, transparencyText.c_str());
                std::wstring predictionText = g_state.enableWordPrediction ? L"✓ 聯想字功能" : L"聯想字功能";
                AppendMenu(hMenu, MF_STRING, 1010, predictionText.c_str());
                std::wstring strokeSymbolText = g_state.showStrokeSymbols ? L"✓ 筆劃符號：開" : L"筆劃符號：關";
                AppendMenu(hMenu, MF_STRING, 1011, strokeSymbolText.c_str());
                AppendMenu(hMenu, MF_SEPARATOR, 0, NULL);
                // 暫停/啟用輸入法功能
                std::wstring pauseText = g_state.imePaused ? L"▶ 啟用輸入法" : L"❚❚ 暫停輸入法";
                AppendMenu(hMenu, MF_STRING, 1009, pauseText.c_str());
                AppendMenu(hMenu, MF_SEPARATOR, 0, NULL);
                AppendMenu(hMenu, MF_STRING, 1003, L"關於");
                
                POINT pt;
                GetCursorPos(&pt);
                
                // 確保選單在頂層顯示，不被視窗遮蓋
                // 設置標誌，防止計時器在選單顯示時重新設置TOPMOST
                g_state.menuShowing = true;
                
                // 臨時移除窗口的TOPMOST屬性（如果有的話），避免與輸入法頂層功能衝突
                LONG_PTR exStyle = GetWindowLongPtr(hwnd, GWL_EXSTYLE);
                bool wasTopmost = (exStyle & WS_EX_TOPMOST) != 0;
                if (wasTopmost) {
                    SetWindowPos(hwnd, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
                }
                
                // 設置窗口為前台窗口
                SetForegroundWindow(hwnd);
                // 讓系統完成前景切換與重繪，避免選單初次顯示白屏（滑鼠移過才正常）
                MSG msg;
                for (int i = 0; i < 15; ++i) {
                    if (!PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) break;
                    if (msg.message == WM_QUIT) break;
                    TranslateMessage(&msg);
                    DispatchMessage(&msg);
                }
                
                // 使用TrackPopupMenuEx來更好地控制選單位置和顯示（TPM_NOANIMATION 避免動畫導致初次不繪製）
                TPMPARAMS tpmParams = {0};
                tpmParams.cbSize = sizeof(TPMPARAMS);
                tpmParams.rcExclude.left = pt.x - 1;
                tpmParams.rcExclude.top = pt.y - 1;
                tpmParams.rcExclude.right = pt.x + 1;
                tpmParams.rcExclude.bottom = pt.y + 1;
                
                int cmd = TrackPopupMenuEx(hMenu, TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_LEFTALIGN | TPM_VERTICAL | TPM_NOANIMATION,
                                          pt.x, pt.y, hwnd, &tpmParams);
                
                // 清除選單顯示標誌
                g_state.menuShowing = false;
                
                // 選單關閉後恢復窗口的TOPMOST狀態（如果原來是TOPMOST）
                if (wasTopmost) {
                    SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
                }
                
                // 發送消息確保焦點正確
                PostMessage(hwnd, WM_NULL, 0, 0);
                
                // 處理選單命令
                if (cmd != 0) {
                    PostMessage(hwnd, WM_COMMAND, cmd, 0);
                }
                
                DestroyMenu(hMenu);
                return 0;
            }
            
            // ⌘暫放按鈕點擊處理
            if (isPointInOptimizedButton(x, y, g_state.toolbarElements.bufferButtonRect)) {
                BufferManager::toggleBufferMode(g_state);
                InvalidateRect(hwnd, nullptr, TRUE);  // 重繪工具列以更新按鈕狀態
                return 0;
            }
            
            if (isPointInOptimizedButton(x, y, g_state.toolbarElements.restoreButtonRect)) {
                PositionManager::g_useUserPosition = false;
                PositionManager::savePositions(g_state);
                // 僅切換為「跟隨滑鼠模式」，不立即移動現有視窗。
                return 0;
            }
            
            if (isPointInOptimizedButton(x, y, g_state.toolbarElements.minimizeButtonRect)) {
                TrayManager::hideToTray(hwnd, &g_trayIcon);
                return 0;
            }
            
            // 開始拖曳工具列
            g_state.dragState.isToolbarDragging = true;
            SetCapture(hwnd);
            
            POINT pt;
            GetCursorPos(&pt);
            RECT toolbarRect;
            GetWindowRect(hwnd, &toolbarRect);
            g_state.dragState.dragOffset.x = pt.x - toolbarRect.left;
            g_state.dragState.dragOffset.y = pt.y - toolbarRect.top;
            
            return 0;
        }

        case WM_MOUSEMOVE: {
            if (g_state.dragState.isToolbarDragging) {
                POINT pt;
                GetCursorPos(&pt);
                handleOptimizedToolbarDrag(hwnd, pt, g_state);
                return 0;
            }
            else {
                int x = LOWORD(lp); 
                int y = HIWORD(lp);
                updateOptimizedButtonHover(x, y, g_state);
                TRACKMOUSEEVENT tme = {0};
                tme.cbSize = sizeof(TRACKMOUSEEVENT);
                tme.dwFlags = TME_LEAVE;
                tme.hwndTrack = hwnd;
                TrackMouseEvent(&tme);
            }
            return 0;
        }
        
        case WM_MOUSELEAVE: {
            // 鼠標離開工具列窗口時，清除所有按鈕的懸停狀態
            bool needRedraw = false;
            if (g_state.toolbarElements.modeIndicatorHover) {
                g_state.toolbarElements.modeIndicatorHover = false;
                needRedraw = true;
            }
            if (g_state.toolbarElements.menuButtonHover) {
                g_state.toolbarElements.menuButtonHover = false;
                needRedraw = true;
            }
            if (g_state.toolbarElements.bufferButtonHover) {
                g_state.toolbarElements.bufferButtonHover = false;
                needRedraw = true;
            }
            if (g_state.toolbarElements.restoreButtonHover) {
                g_state.toolbarElements.restoreButtonHover = false;
                needRedraw = true;
            }
            if (g_state.toolbarElements.minimizeButtonHover) {
                g_state.toolbarElements.minimizeButtonHover = false;
                needRedraw = true;
            }
            if (g_state.toolbarElements.closeButtonHover) {
                g_state.toolbarElements.closeButtonHover = false;
                needRedraw = true;
            }
            
            if (needRedraw && g_state.hWnd) {
                scheduleDeferredInvalidate(DEFERRED_MAIN);
            }
            return 0;
        }

        case WM_LBUTTONUP: { 
            if (g_state.dragState.isToolbarDragging) { 
                g_state.dragState.isToolbarDragging = false; 
                ReleaseCapture(); 
                PositionManager::savePositions(g_state);
                return 0; 
            } 
            break; 
        }
		
		case WM_USER + 301: { // 新增：自定義螢幕模式變更消息
			//bool isExtended = (wp == 1);
    
			// 立即處理模式變更
			PositionManager::adjustPositionForScreenMode(g_state);
    
			// 確保視窗在正確位置
			SetWindowPos(hwnd, HWND_TOPMOST,
				PositionManager::g_toolbarPos.x,
				PositionManager::g_toolbarPos.y,
				0, 0, SWP_NOSIZE | SWP_SHOWWINDOW);
    
			return 0;
		}
            
        case WM_COMMAND:
            return handleCommand(hwnd, wp);
        
        case WM_USER + 300: {
            // 以計時器延遲執行，避免在訊息處理中 Sleep 導致 toolbar 卡死
            if (g_state.refocusDelay <= 0) {
                SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0,
                            SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
            } else {
                SetTimer(hwnd, 993, (UINT)g_state.refocusDelay, NULL);
            }
            return 0;
        }
        
        case WM_SETFOCUS: {
            // 注意：不再使用 IME API，改用鍵盤鉤子直接轉換字母
            return 0;
        }
        
        case WM_KILLFOCUS: {
            // 失去焦點時立即重新設置前置
            if (g_state.forceStayOnTop) {
                PostMessage(hwnd, WM_USER + 300, 0, 0);
            }
            return 0;
        }	
		
    }
    
    return DefWindowProc(hwnd, msg, wp, lp);
}

// OptimizedUI候選字視窗程序
// OptimizedCandProc 已合併到統一的 CandProc 中，此處已刪除


// 字碼輸入視窗程序實現
LRESULT CALLBACK InputProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            RECT rc;
            GetClientRect(hwnd, &rc);
            drawInputWindow(hdc, rc, g_state);
            EndPaint(hwnd, &ps);
            return 0;
        }
        
        case WM_LBUTTONDOWN: {
            // 開始拖拽字碼輸入視窗
            g_state.dragState.isInputDragging = true;
            SetCapture(hwnd);
            
            POINT pt;
            GetCursorPos(&pt);
            RECT inputRect;
            GetWindowRect(hwnd, &inputRect);
            g_state.dragState.dragOffset.x = pt.x - inputRect.left;
            g_state.dragState.dragOffset.y = pt.y - inputRect.top;
            
            return 0;
        }
        
        case WM_MOUSEMOVE: {
            if (g_state.dragState.isInputDragging) {
                POINT pt;
                GetCursorPos(&pt);
                
                int newX = pt.x - g_state.dragState.dragOffset.x;
                int newY = pt.y - g_state.dragState.dragOffset.y;
                
                RECT inputRect;
                GetWindowRect(hwnd, &inputRect);
                int inputWidth = inputRect.right - inputRect.left;
                
                RECT workArea = ScreenManager::getMonitorFromPoint(pt).workArea;
                int margin = 4;
                if (newX < workArea.left + margin) newX = workArea.left + margin;
                if (newY < workArea.top + margin) newY = workArea.top + margin;
                if (newX + inputWidth > workArea.right - margin) newX = workArea.right - margin - inputWidth;
                if (newY + INPUT_WINDOW_HEIGHT > workArea.bottom - margin) newY = workArea.bottom - margin - INPUT_WINDOW_HEIGHT;
                
                // 移動字碼輸入視窗（只有在菜單未顯示時才設置TOPMOST）
                if (!g_state.menuShowing) {
                    SetWindowPos(hwnd, HWND_TOPMOST,
                                newX, newY, inputWidth, INPUT_WINDOW_HEIGHT,
                                SWP_NOACTIVATE | SWP_SHOWWINDOW);
                } else {
                    SetWindowPos(hwnd, HWND_NOTOPMOST,
                                newX, newY, inputWidth, INPUT_WINDOW_HEIGHT,
                                SWP_NOACTIVATE | SWP_SHOWWINDOW);
                }
                
                // 僅在字碼候選模式時讓候選視窗跟隨；聯想模式時讓聯想視窗跟隨，避免誤顯示 hCandWnd
                if (!g_state.menuShowing) {
                    if (g_state.currentMode == InputMode::CAND_MODE && g_state.hCandWnd) {
                        RECT candRect;
                        GetWindowRect(g_state.hCandWnd, &candRect);
                        int candWidth = candRect.right - candRect.left;
                        int candHeight = candRect.bottom - candRect.top;
                        SetWindowPos(g_state.hCandWnd, HWND_TOPMOST,
                                    newX, newY + INPUT_WINDOW_HEIGHT + WINDOW_SPACING,
                                    candWidth, candHeight,
                                    SWP_NOACTIVATE | SWP_SHOWWINDOW);
                    } else if (g_state.currentMode == InputMode::PRED_MODE && g_state.hPredWnd) {
                        RECT predRect;
                        GetWindowRect(g_state.hPredWnd, &predRect);
                        int predWidth = predRect.right - predRect.left;
                        int predHeight = predRect.bottom - predRect.top;
                        SetWindowPos(g_state.hPredWnd, HWND_TOPMOST,
                                    newX, newY + INPUT_WINDOW_HEIGHT + WINDOW_SPACING,
                                    predWidth, predHeight,
                                    SWP_NOACTIVATE | SWP_SHOWWINDOW);
                    }
                }
                
                return 0;
            }
            break;
        }
        
        case WM_LBUTTONUP: {
            if (g_state.dragState.isInputDragging) {
                g_state.dragState.isInputDragging = false;
                ReleaseCapture();
                
                // 記錄用戶自定義位置
                RECT inputRect;
                GetWindowRect(hwnd, &inputRect);
                PositionManager::g_userCandPos.x = inputRect.left;
                PositionManager::g_userCandPos.y = inputRect.top;
                PositionManager::g_userCandPos.isValid = true;
                PositionManager::g_useUserPosition = true;
                PositionManager::savePositions(g_state);
                
                Utils::updateStatus(g_state, L"已切換到用戶位置模式");
                return 0;
            }
            break;
        }
    }
    return DefWindowProc(hwnd, msg, wp, lp);
}

// 優化跳動：字碼在列表上方/下方的切換滯後，避免邊界附近反覆切換
static bool g_lastInputWasAboveList = true;
const int PLACEMENT_HYSTERESIS = 15;

// 修復：字碼輸入視窗位置調整（knownListRect 不為空時直接使用，避免 GetWindowRect 時序導致 listRectValid 搖擺）
void positionInputWindow(GlobalState& state, const RECT* knownListRect) {
    if (!state.hInputWnd) {
        return;
    }
    
    if (!state.isInputting || (state.input.empty() && !state.showCand)) {
        ShowWindow(state.hInputWnd, SW_HIDE);
        return;
    }
    
    HWND listWnd = NULL;
    bool listRectValid = false;
    RECT listRect = {};
    
    if (knownListRect && knownListRect->right > knownListRect->left && knownListRect->bottom > knownListRect->top) {
        listRect = *knownListRect;
        listRectValid = true;
    }
    if (!listRectValid) {
        if (state.currentMode == InputMode::CAND_MODE && state.hCandWnd) {
            listWnd = state.hCandWnd;
            if (!IsWindowVisible(state.hCandWnd)) ShowWindow(state.hCandWnd, SW_SHOW);
        } else if (state.currentMode == InputMode::PRED_MODE && state.hPredWnd) {
            listWnd = state.hPredWnd;
            if (!IsWindowVisible(state.hPredWnd)) ShowWindow(state.hPredWnd, SW_SHOW);
        }
        if (listWnd) {
            UpdateWindow(listWnd);
            GetWindowRect(listWnd, &listRect);
            listRectValid = (listRect.right > listRect.left && listRect.bottom > listRect.top);
        }
    }
    
    if (listRectValid) {
        int inputWidth = listRect.right - listRect.left;
        int inputX = listRect.left;
        const int inputListGap = 2;
        int inputY = listRect.top - INPUT_WINDOW_HEIGHT - inputListGap;
        ScreenManager::MonitorInfo monitor = ScreenManager::getMonitorFromPoint({inputX, inputY});
        int workTop = monitor.workArea.top;
        // 滯後：僅在明顯超出上邊界時才切換到「字碼在列表下方」，避免邊界附近反覆上下跳
        bool putInputBelow = false;
        if (inputY < workTop - PLACEMENT_HYSTERESIS)
            putInputBelow = true;
        else if (inputY >= workTop)
            putInputBelow = false;
        else
            putInputBelow = !g_lastInputWasAboveList;
        if (putInputBelow) {
            inputY = listRect.bottom + inputListGap;
            g_lastInputWasAboveList = false;
        } else {
            if (inputY < workTop) inputY = workTop;
            g_lastInputWasAboveList = true;
        }
        if (!state.menuShowing) {
            SetWindowPos(state.hInputWnd, HWND_TOPMOST,
                        inputX, inputY, inputWidth, INPUT_WINDOW_HEIGHT,
                        SWP_NOACTIVATE | SWP_SHOWWINDOW);
        } else {
            SetWindowPos(state.hInputWnd, HWND_NOTOPMOST,
                        inputX, inputY, inputWidth, INPUT_WINDOW_HEIGHT,
                        SWP_NOACTIVATE | SWP_SHOWWINDOW);
        }
    }
    if (!listWnd || !listRectValid) {
        // 沒有候選字時，優先使用候選字視窗的保存位置
        int inputX, inputY, inputWidth;
        bool useSavedPosition = false;
        
        // 優先檢查：是否有用戶設定的固定位置
        if (PositionManager::g_useUserPosition && PositionManager::g_userCandPos.isValid) {
            inputX = PositionManager::g_userCandPos.x;
            inputY = PositionManager::g_userCandPos.y;
            // 嘗試從候選字視窗獲取寬度，如果不存在則使用配置的寬度
            if (state.hCandWnd && IsWindowVisible(state.hCandWnd)) {
                RECT candRect;
                GetWindowRect(state.hCandWnd, &candRect);
                inputWidth = candRect.right - candRect.left;
            } else {
                inputWidth = state.inputWindowWidth;
            }
            useSavedPosition = true;
        }
        // 其次檢查：候選字視窗是否存在且已定位（即使不可見）
        else if (state.hCandWnd) {
            RECT candRect;
            GetWindowRect(state.hCandWnd, &candRect);
            // 檢查視窗位置是否有效（不是 (0,0) 且尺寸有效）
            if (!(candRect.left == 0 && candRect.top == 0 && 
                  (candRect.right == 0 || candRect.bottom == 0))) {
                inputX = candRect.left;
                inputY = candRect.top;
                inputWidth = candRect.right - candRect.left;
                useSavedPosition = true;
            }
        }
        
        if (useSavedPosition) {
            // 使用保存的位置，字碼視窗緊貼列表上方（與 WINDOW_SPACING 一致）
            int savedCandY = inputY;  // 保存候選字視窗的原始Y位置
            const int inputListGapFallback = 2;
            inputY = savedCandY - INPUT_WINDOW_HEIGHT - inputListGapFallback;
            
            // 確保字碼視窗在螢幕可見範圍內
            ScreenManager::MonitorInfo monitor = ScreenManager::getMonitorFromPoint({inputX, inputY});
            if (inputY < monitor.workArea.top) {
                inputY = savedCandY + INPUT_WINDOW_HEIGHT + inputListGapFallback; // 如果上方放不下，放到候選字視窗位置下方
            }
        } else {
            // 只有在沒有任何位置信息時，才使用滑鼠位置
            POINT mousePos = PositionManager::getCurrentMousePosition();
            ScreenManager::MonitorInfo monitor = ScreenManager::getMonitorFromPoint(mousePos);
            
            inputX = mousePos.x;
            inputY = mousePos.y - 35;
            inputWidth = state.inputWindowWidth;  // 使用配置的寬度
            
            // 確保視窗在螢幕範圍內
            if (inputX + inputWidth > monitor.workArea.right) {
                inputX = monitor.workArea.right - inputWidth - 10;
            }
            if (inputX < monitor.workArea.left) {
                inputX = monitor.workArea.left + 10;
            }
            if (inputY < monitor.workArea.top) {
                inputY = monitor.workArea.top + 10;
            }
        }
        
        // 只有在菜單未顯示時才設置TOPMOST
        if (!state.menuShowing) {
            // 先確保暫放視窗是TOPMOST（如果顯示的話）
            if (state.hBufferWnd && IsWindowVisible(state.hBufferWnd)) {
                SetWindowPos(state.hBufferWnd, HWND_TOPMOST, 0, 0, 0, 0,
                            SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
            }
            
            // 然後設置字碼視窗為TOPMOST，這樣它會顯示在暫放視窗前面
            SetWindowPos(state.hInputWnd, HWND_TOPMOST,
                inputX, inputY, 
                inputWidth, INPUT_WINDOW_HEIGHT,
                SWP_NOACTIVATE | SWP_SHOWWINDOW);
        } else {
            SetWindowPos(state.hInputWnd, HWND_NOTOPMOST,
                inputX, inputY, 
                inputWidth, INPUT_WINDOW_HEIGHT,
                SWP_NOACTIVATE | SWP_SHOWWINDOW);
        }
    }
    
    InvalidateRect(state.hInputWnd, nullptr, TRUE);
}

// 應用半透明效果
void applyTransparency(GlobalState& state) {
    // 應用於工具列視窗
    if (state.hWnd) {
        LONG_PTR exStyle = GetWindowLongPtr(state.hWnd, GWL_EXSTYLE);
        if (state.enableTransparency) {
            // 啟用半透明：添加WS_EX_LAYERED樣式
            SetWindowLongPtr(state.hWnd, GWL_EXSTYLE, exStyle | WS_EX_LAYERED);
            SetLayeredWindowAttributes(state.hWnd, 0, state.transparencyAlpha, LWA_ALPHA);
        } else {
            // 關閉半透明：移除WS_EX_LAYERED樣式
            SetWindowLongPtr(state.hWnd, GWL_EXSTYLE, exStyle & ~WS_EX_LAYERED);
        }
        InvalidateRect(state.hWnd, nullptr, TRUE);
    }
    
    // 應用於暫放視窗
    if (state.hBufferWnd) {
        LONG_PTR exStyle = GetWindowLongPtr(state.hBufferWnd, GWL_EXSTYLE);
        if (state.enableTransparency) {
            // 啟用半透明：添加WS_EX_LAYERED樣式
            SetWindowLongPtr(state.hBufferWnd, GWL_EXSTYLE, exStyle | WS_EX_LAYERED);
            SetLayeredWindowAttributes(state.hBufferWnd, 0, state.transparencyAlpha, LWA_ALPHA);
        } else {
            // 關閉半透明：移除WS_EX_LAYERED樣式
            SetWindowLongPtr(state.hBufferWnd, GWL_EXSTYLE, exStyle & ~WS_EX_LAYERED);
        }
        InvalidateRect(state.hBufferWnd, nullptr, TRUE);
    }
}

} // namespace WindowManager