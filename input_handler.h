// input_handler.h - 鍵盤輸入處理
#ifndef INPUT_HANDLER_H
#define INPUT_HANDLER_H

#include "ime_core.h"

namespace InputHandler {
    // 鍵盤鉤子程序
    LRESULT CALLBACK KeyboardHookProc(int nCode, WPARAM wParam, LPARAM lParam);
    
    // 輸入模式切換
    void toggleInputMode(GlobalState& state);
    // 明確切換中文／英文筆劃模式（與雙格「劃／E」按鈕共用邏輯）
    void setChineseInputMode(GlobalState& state, bool chinese);
    
    // 智慧Enter鍵處理
    void handleEnterKeySmartly(GlobalState& state);
    
    // 筆劃輸入處理
    void processStroke(GlobalState& state, DWORD key);

    // 是否為「筆劃五鍵」的實體按鍵（依 useCustomStrokeKeys / strokeKey*VK）
    bool isStrokeLetterVK(const GlobalState& state, DWORD key);

    // 是否為「小鍵盤筆劃五鍵」（依 useCustomNumpadStrokeKeys / numpadStrokeKey*VK）
    bool isNumpadStrokeVK(const GlobalState& state, DWORD key);
    
    // 標點符號處理
    void processPunctuator(GlobalState& state, DWORD key);
    
    // 顯示標點選單
    void showPunctMenu(GlobalState& state);
    void switchPunctMenuMode(GlobalState& state, PunctMenuMode mode);
    void closePunctMenu(GlobalState& state);
    
    // 文字發送
    void sendTextDirectUnicode(const std::wstring& text);
    void queueTextDirectUnicode(const std::wstring& text);
    void processQueuedTextDirectUnicode();
    /// 向前景焦點視窗送出一個退格（用於刪除剛上屏的聯想字）
    void sendBackspaceToForeground();
    
    // 確保目標視窗有焦點（用於文字發送前）
    void ensureTargetWindowFocused();
    void refreshPunctMenuTargetIfNeeded();
    
    // 英文字元轉換（全形/半形）
    std::wstring convertEnglishChar(wchar_t ch, bool toFullWidth);
}

#endif // INPUT_HANDLER_H