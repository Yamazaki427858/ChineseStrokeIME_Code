// ime_core.h - 核心定義與全域狀態 (OptimizedUI支援版)
#ifndef IME_CORE_H
#define IME_CORE_H

#include <windows.h>
#include <string>
#include <vector>
#include <map>
#include <set>
#include <unordered_set>
#include <ctime>

// 輸入模式枚舉：用於統一管理候選/聯想/空閒狀態
enum class InputMode {
    IDLE,       // 無輸入，所有相關視窗隱藏
    CAND_MODE,  // 字碼候選模式（顯示 hCandWnd）
    PRED_MODE   // 聯想字模式（顯示 hPredWnd）
};

// ========== 【重要：更新版本號請修改此處】 ==========
// 當前版本號 - 此版本號會顯示在「關於」對話框中，並用於版本更新檢查
// 更新版本號時，請確保與 Makefile 中的 VERSION 保持一致
// 
// 版本號格式：支持任意字符串（可以是數字、字母、符號等組合）
// 例如：3.1.0、Beta4.0.a、3.0.0-alpha、v2.0.0-beta.1 等均可
// 版本比較：使用字符串比較，只要字符串不同即判定為不同版本
//
#define APP_VERSION "3.2.0"
// ========== 【版本號定義結束】 ==========

// 常數定義
const int CANDIDATES_PER_PAGE = 9;
const int FIXED_WIDTH = 380;
const int CHARS_PER_LINE = 10;
const int MIN_HEIGHT = 80;
const int MAX_HEIGHT = 200;
const int LINE_HEIGHT = 20;
const int CONTROL_BAR_HEIGHT = 50;
const int INPUT_WINDOW_HEIGHT = 30;
const int WINDOW_SPACING = 2; 

// OptimizedUI工具列常數
const int TOOLBAR_WIDTH = 290;
const int TOOLBAR_HEIGHT = 35;
const int BUTTON_HEIGHT = 22;
const int SMALL_BUTTON_WIDTH = 35;
const int MODE_BUTTON_WIDTH = 35;
// 迷你「劃／E」工具列（僅兩鍵，類舊版語言列按鈕尺寸）；四邊留白均等
const int MINI_TOOLBAR_PAD = 5;
const int MINI_TOOLBAR_CELL_W = 34;
const int MINI_TOOLBAR_CELL_H = 22;
const int MINI_TOOLBAR_WIDTH = MINI_TOOLBAR_PAD * 2 + MINI_TOOLBAR_CELL_W * 2;
const int MINI_TOOLBAR_HEIGHT = MINI_TOOLBAR_PAD * 2 + MINI_TOOLBAR_CELL_H;

// 位置記憶結構
struct Position {
    int x;
    int y;
    bool isValid;
    Position() : x(0), y(0), isValid(false) {}
};

struct ScreenModePositions {
    Position extendedModePos;
    Position mirroredModePos;
    bool hasExtendedPos;
    bool hasMirroredPos;
    ScreenModePositions() : hasExtendedPos(false), hasMirroredPos(false) {}
};

// 詞頻資訊結構
struct WordInfo {
    int frequency;
    time_t lastUsed;
    int tempCount;
    bool isPermanent;
};

// UI元素位置結構
struct ToolbarElements {
    RECT modeIndicatorRect = {0};
    RECT strokeBadgeRect = {0};
    RECT englishBadgeRect = {0};
    RECT statusIndicatorRect = {0};
    RECT menuButtonRect = {0};
    RECT bufferButtonRect = {0};    // ⌘暫放按鈕
    RECT restoreButtonRect = {0};
    RECT minimizeButtonRect = {0};
    RECT closeButtonRect = {0};
    
    // 懸停狀態
    bool modeIndicatorHover = false;
    bool strokeBadgeHover = false;
    bool englishBadgeHover = false;
    bool menuButtonHover = false;
    bool bufferButtonHover = false;
    bool restoreButtonHover = false;
    bool minimizeButtonHover = false;
    bool closeButtonHover = false;
};

// 拖拽狀態結構
struct DragState {
    bool isToolbarDragging = false;
    bool isInputDragging = false;
    bool isCandDragging = false;
    bool isPredDragging = false;
    POINT dragOffset = {0};
};

// UI顏色配置
struct UIColors {
    COLORREF toolbarBgColor = RGB(240,240,240);
    COLORREF toolbarBorderColor = RGB(160,160,160);
    COLORREF modeActiveColor = RGB(0,120,215);
    COLORREF modeInactiveColor = RGB(160,160,160);
    COLORREF statusReadyColor = RGB(0,150,0);
    COLORREF statusErrorColor = RGB(220,50,50);
    COLORREF statusInputColor = RGB(255,165,0);
    COLORREF statusBufferColor = RGB(255,140,0);
    COLORREF statusPausedColor = RGB(160,160,160);  // 暫停狀態：灰色（不亮）
    COLORREF buttonHoverColor = RGB(200,200,200);
    COLORREF closeButtonColor = RGB(180,180,180);
    COLORREF bufferButtonActiveColor = RGB(255,165,0);
    COLORREF bufferButtonInactiveColor = RGB(180,180,180);
};

// 全域狀態結構
struct GlobalState {
    // 路徑：exe 所在目錄下的 system\ 與 user\ 子目錄（程式啟動時初始化）
    std::wstring systemDir;  // exe所在目錄 + "system\\"
    std::wstring userDir;    // exe所在目錄 + "user\\"

    // 視窗控制代碼
    HWND hWnd = NULL;
    HWND hCandWnd = NULL;      // 字碼候選字視窗
    HWND hPredWnd = NULL;      // 聯想字視窗（之後會獨立使用，目前預設隱藏）
    HWND hBufferWnd = NULL;
	HWND hInputWnd = NULL;
	
	// 候選字視窗翻頁按鈕
    RECT prevPageButtonRect = {0};
    RECT nextPageButtonRect = {0};
    RECT pageInfoRect = {0};
    
    bool prevPageButtonHover = false;
    bool nextPageButtonHover = false;
    
    // 輸入狀態
    std::wstring input = L"";
    std::vector<std::wstring> candidates;
    std::vector<std::wstring> candidateCodes;
    int selected = 0;
    int currentPage = 0;
    int totalPages = 0;
    bool showCand = false;
    bool chineseMode = true;
    bool isInputting = false;
    bool inputError = false;
    bool showPunctMenu = false;
	
	// 文字選取狀態
    bool isSelecting = false;           // 是否正在選取
    int selectionStart = -1;           // 選取起始位置
    int selectionEnd = -1;             // 選取結束位置
    bool hasSelection = false;         // 是否有選取的文字
    POINT selectionStartPoint = {0};   // 選取起始座標
    POINT selectionEndPoint = {0};     // 選取結束座標
    
    // 右鍵選單相關（主視窗 / 候選視窗共用）
    bool showContextMenu = false;      // 是否顯示右鍵選單（目前主要用於主視窗）
    RECT contextMenuRect = {0};        // 右鍵選單位置
    int contextMenuCandIndex = -1;     // 右鍵點選的候選索引（用於 pinned/locked/del/blocked）
    std::wstring predictionQueryWord;  // 當前聯想視窗的查詢前文（用於右鍵選單 prev key）

    
    // 字典資料
    std::map<std::wstring, std::vector<std::wstring>> dict;
    std::map<std::wstring, std::vector<std::wstring>> punct;
    std::map<std::wstring, WordInfo> wordFreq;
    
    // 智能聯想引擎資料結構
    // bigramIndex: 從 Zi-Ma-Biao.txt 預建的相鄰字對索引（前字 -> 後字 -> 共現次數）
    std::map<wchar_t, std::map<wchar_t, int>> bigramIndex;
    
    // contextLearning: 個人上下文學習（前字/詞 -> 後字/詞 -> 使用次數）
    // 改寫：從 map<wstring, vector<wstring>> 改為 map<wstring, map<wstring, int>>
    std::map<std::wstring, std::map<std::wstring, int>> contextLearning;
    
    // lockedContext: 標記 locked/pinned 條目（前字, 後字/詞）對
    // locked: score = ∞（永遠置頂）
    // pinned: score = count + bonus（bonus 預設值可在設定檔調整）
    std::set<std::pair<std::wstring, std::wstring>> lockedContext;
    std::set<std::pair<std::wstring, std::wstring>> pinnedContext;
    // blockedContext: 使用者選擇「永不再顯示」的聯想條目（在候選列表中完全忽略）
    std::set<std::pair<std::wstring, std::wstring>> blockedContext;
    
    // 最近一次選字（單字或詞），用於 contextLearning[單字]->後字/詞
    std::wstring lastSelected = L"";
    // 最近兩個選字組成的片語，例如「電腦」「系統」，用於 contextLearning[片語]->後字/詞
    std::wstring lastBigram = L"";
    // 最近連續選字的滾動前文（最多9字），用於多字前綴聯想查詢
    std::wstring lastContext = L"";
    // 聯想字退格糾正：可還原剛學入的關聯與 lastContext/lastBigram/lastSelected
    bool         pendingUnlearnFromPrediction = false;
    std::wstring pendingUnlearnContextSnap;   // learn 前的 lastContext
    std::wstring pendingUnlearnBigramSnap;     // learn 前的 lastBigram
    std::wstring pendingUnlearnPrev;          // learn 前的 lastSelected
    std::wstring pendingUnlearnNext;          // 本次選中的聯想內容
    std::vector<std::wstring> punctCandidates;
    int dictSize = 0;
    
    // 詞語庫資料（用於聯想字功能）
    // 格式：第一個字 -> 後續可能的字列表（按頻率排序）
    std::map<std::wstring, std::vector<std::wstring>> wordPhrases;
    std::map<std::wstring, std::map<std::wstring, double>> wordPhraseScores;
    int phraseDictSize = 0;  // 詞語庫大小

    // 字碼反查快取：候選顯示字碼時避免每次掃描整份 dict
    std::map<std::wstring, std::wstring> charToCode;
    
    // 聯想字功能設定
    int contextLearningPinnedBonus = 50;      // pinned 條目的額外分數加成
    int contextLearningMaxAutoEntries = 2000; // 自動學習條目的最大數量（全域上限）
    int contextLearningMinAutoCount = 0;      // 儲存門檻（已廢棄為雜訊過濾，現在次數>=1即存入）
    
    // 暫放視窗模式
    bool bufferMode = false;
    std::wstring bufferText = L"";
    int bufferCursorPos = 0;
    bool bufferShowCursor = true;
    DWORD bufferCursorBlinkTime = 0;
    bool bufferHasFocus = false;
    bool clipboardMode = false;  // 剪貼簿模式開關
    bool clipboardInputting = false;  // 剪貼簿模式：是否正在輸入中
    bool clipboardCopied = false;  // 剪貼簿模式：文字是否已複製到剪貼簿
    DWORD clipboardLastInputTime = 0;  // 最後輸入時間（用於判斷輸入結束）
    bool menuShowing = false;  // 選單是否正在顯示（用於防止TOPMOST衝突）
    bool imePaused = false;  // 輸入法是否暫停（鍵盤鉤子是否已釋放）
    bool enableWordPrediction = true;  // 是否啟用聯想字功能
    int  maxWordPredictions = 100;     // 聯想字視窗最多顯示的候選數（可在 interfaceconfig.ini 自定，預設 100）
    bool isPredictionMode = false;     // 是否處於聯想字模式（hPredWnd 顯示中）

    // 統一輸入模式（初始為空閒）
    InputMode currentMode = InputMode::IDLE;
    
    // 候選字選單是否顯示英文字碼（僅 interface_config.ini，預設關閉）
    bool showCandidateCode = false;
    // 候選字 hover 顏色（滑鼠移到候選列時的背景/文字色，可由 interface_config.ini 自訂）
    COLORREF candidateHoverBackgroundColor = RGB(0xFF, 0xA9, 0x30); // 預設與聯想字 hover 相同的橘色
    COLORREF candidateHoverTextColor = RGB(0x00, 0x00, 0x00);       // 預設黑色文字
    int hoverCandidateIndex = -1;  // 目前滑鼠懸停的候選索引

    // 聯想字視窗獨立顏色（可於 interfaceconfig.ini 自定）
    COLORREF predictionBackgroundColor  = RGB(0xFC, 0xFA, 0xED); // #fcfaed
    COLORREF predictionTextColor        = RGB(0x00, 0x00, 0x00); // #000000
    COLORREF predictionFirstItemBgColor = RGB(0xB0, 0xE1, 0xFF); // #b0e1ff（第一個聯想字底色）
    COLORREF predictionHoverBgColor     = RGB(0xFF, 0xA9, 0x30); // #ffa930
    COLORREF predictionHoverTextColor   = RGB(0x00, 0x00, 0x00); // #000000
    // 輸入框顯示筆劃符號或英文字母（true=筆劃符號一丨丿丶フ，false=uiojk）
    bool showStrokeSymbols = true;
    // 螢幕模式變更時是否彈出提示（預設關閉，靜默處理）
    bool showScreenModeNotification = false;
    // 迷你「劃／E」工具列（僅兩鍵＋右鍵選單；interfaceconfig.ini [WindowBehavior]，預設關閉）
    bool toolbarClassicModeBadges = false;
    
    // 萬用字元 * 觸發鍵（3+3 模式用，對應虛擬鍵碼，預設為 L 與 NumPad0）
    int wildcardKey1VK = 'L';
    int wildcardKey2VK = VK_NUMPAD0;

    // 自訂筆劃字母鍵（對應內部字根 u i o j k；關閉時行為同固定 U I O J K）
    // 開啟後僅 strokeKeyU~K 與下方小鍵盤筆劃鍵作為筆劃（與 useCustomNumpadStrokeKeys 分開）
    bool useCustomStrokeKeys = false;
    int strokeKeyUVK = 'U';
    int strokeKeyIVK = 'I';
    int strokeKeyOVK = 'O';
    int strokeKeyJVK = 'J';
    int strokeKeyKVK = 'K';

    // 自訂小鍵盤筆劃五鍵（僅 VK_NUMPAD0~9；關閉時為 7/8/9/4/5 對應 u i o j k）
    bool useCustomNumpadStrokeKeys = false;
    int numpadStrokeKeyUVK = VK_NUMPAD7;
    int numpadStrokeKeyIVK = VK_NUMPAD8;
    int numpadStrokeKeyOVK = VK_NUMPAD9;
    int numpadStrokeKeyJVK = VK_NUMPAD4;
    int numpadStrokeKeyKVK = VK_NUMPAD5;
	
	// 歷史記錄
	struct TextSnapshot {
    std::wstring text;
    int cursorPos;
};
std::vector<TextSnapshot> undoHistory;
std::vector<TextSnapshot> redoHistory;
int maxHistorySize = 50;  // 最多保存50步歷史
	
    // 視窗行為設定
    int topmostCheckInterval = 5000;  // 前置檢查間隔
    bool forceStayOnTop = true;       // 是否強制前置
    int refocusDelay = 50;            // 重新聚焦延遲	
    
    // 半透明設定
    bool enableTransparency = false;  // 是否啟用半透明顯示
    int transparencyAlpha = 220;      // 透明度值 (0-255, 255=完全不透明, 0=完全透明)
    
    // Shift鍵狀態
    bool shiftPressed = false;
    bool shiftUsedForCombo = false;
    DWORD shiftPressTime = 0;
    
    // 介面狀態
    bool isDragging = false;  // 保留向後兼容
    POINT dragStartPoint = {0};
    std::wstring statusInfo = L"就緒";
    
    // OptimizedUI狀態
    ToolbarElements toolbarElements;
    DragState dragState;
    UIColors uiColors;
    bool useOptimizedUI = true;  // 控制是否使用OptimizedUI風格
    
    // 按鈕狀態 (保留原有，向後兼容)
    RECT closeButtonRect = {0};
    RECT modeButtonRect = {0};
    RECT creditsButtonRect = {0};
    RECT refreshButtonRect = {0};
    RECT bufferButtonRect = {0};
    RECT sendButtonRect = {0};
    RECT clearButtonRect = {0};
    RECT saveButtonRect = {0};
    RECT clipboardModeButtonRect = {0};  // 剪貼簿模式開關按鈕
    
    bool closeButtonHover = false;
    bool modeButtonHover = false;
    bool creditsButtonHover = false;
    bool refreshButtonHover = false;
    bool bufferButtonHover = false;
    bool sendButtonHover = false;
    bool clearButtonHover = false;
    bool saveButtonHover = false;
    bool clipboardModeButtonHover = false;  // 剪貼簿模式按鈕懸停狀態
    
    // 顏色設定 (保留原有，向後兼容)
    COLORREF bgColor = RGB(240,240,240);
    COLORREF textColor = RGB(0,0,0);
    COLORREF selColor = RGB(0,120,215);
    COLORREF selBgColor = RGB(230,240,250);
    COLORREF errorColor = RGB(220,50,50);
    COLORREF closeButtonColor = RGB(220,50,50);
    COLORREF closeButtonHoverColor = RGB(255,70,70);
    COLORREF modeButtonColor = RGB(100,50,200);
    COLORREF modeButtonHoverColor = RGB(120,70,220);
    COLORREF creditsButtonColor = RGB(200,150,50);
    COLORREF creditsButtonHoverColor = RGB(220,170,70);
    COLORREF refreshButtonColor = RGB(50,150,50);
    COLORREF refreshButtonHoverColor = RGB(70,170,70);
    COLORREF candidateBackgroundColor = RGB(255,255,255);
    COLORREF candidateTextColor = RGB(0,0,0);
    COLORREF selectedCandidateBackgroundColor = RGB(230,240,250);
    COLORREF selectedCandidateTextColor = RGB(0,120,215);
    COLORREF bufferBackgroundColor = RGB(255,255,255);
    COLORREF bufferTextColor = RGB(0,0,0);
    COLORREF bufferCursorColor = RGB(0,0,0);
    
    // 字碼輸入視窗顏色設定 (MOVED FROM GLOBAL TO STRUCT)
    COLORREF inputBackgroundColor = RGB(255,255,255);
    COLORREF inputTextColor = RGB(0,0,0);
    COLORREF inputErrorTextColor = RGB(220,50,50);
    COLORREF inputHintTextColor = RGB(128,128,128);
    COLORREF inputBorderColor = RGB(128,128,128);
    
    // 字型設定
    int fontSize = 16;
    std::wstring fontName = L"Microsoft JhengHei";
    int candidateFontSize = 18;
    std::wstring candidateFontName = L"Microsoft JhengHei";
    int bufferFontSize = 14;
    std::wstring bufferFontName = L"Microsoft JhengHei";
    
    // 字碼輸入視窗字型設定 (MOVED FROM GLOBAL TO STRUCT)
    int inputFontSize = 16;
    std::wstring inputFontName = L"Microsoft JhengHei";
    
    // 視窗尺寸
    int windowWidth = 580;  // 非OptimizedUI模式使用
    int windowHeight = 70;
    int candidateWidth = 500;
    int candidateHeight = 320;
    
    // 字碼輸入視窗尺寸設定 (MOVED FROM GLOBAL TO STRUCT)
    int inputWindowWidth = 400;
    int inputWindowHeight = 30;
};

// 工具函數命名空間
namespace Utils {
    std::wstring utf8ToWstr(const std::string& str);
    std::string wstrToUtf8(const std::wstring& ws);
    void updateStatus(GlobalState& state, const std::wstring& msg);
    bool isPunctuation(const std::wstring& word);
    /// 句讀中斷（。?! 等）：應斷絕多字前綴與跨句聯想文脈
    bool isStrongContextBreakPunct(const std::wstring& word);
    COLORREF parseColorFromString(const std::string& colorStr);
    
    // OptimizedUI工具函數
    bool isPointInRect(int x, int y, const RECT& rect);
}

// 全域狀態變數宣告
extern GlobalState g_state;
extern HHOOK g_hKeyboardHook;

#endif // IME_CORE_H