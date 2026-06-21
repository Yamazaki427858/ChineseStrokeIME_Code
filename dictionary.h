// dictionary.h - 字典與候選字管理（修復版）
#ifndef DICTIONARY_H
#define DICTIONARY_H

#include "ime_core.h"

namespace Dictionary {
    // 字典載入函數
    void loadMainDict(GlobalState& state);
    void loadPunctuator(GlobalState& state);
    void loadPunctMenu(GlobalState& state);
    void loadEmojiGroups(GlobalState& state);
    bool loadEmojiGroupAt(GlobalState& state, int groupIndex);
    void applyEmojiGroupDisplay(GlobalState& state, int groupIndex);
    void selectEmoji(GlobalState& state, const std::wstring& emoji);
    bool updateEmojiFromGitHub(GlobalState& state, bool showProgress = true);
    void loadUserDict(GlobalState& state);
    void saveUserDict(const GlobalState& state);
    bool removeFromUserDict(GlobalState& state, const std::wstring& word);
    
    // 智能聯想引擎持久化
    void loadContextLearning(GlobalState& state);
    void saveContextLearning(GlobalState& state);
    // 聯想條目模式設定（locked 永遠置頂 / pinned 置頂+加分），供日後 UI 或腳本呼叫
    void setContextLocked(GlobalState& state, const std::wstring& prevWord, const std::wstring& nextWord, bool locked);
    void setContextPinned(GlobalState& state, const std::wstring& prevWord, const std::wstring& nextWord, bool pinned);
    
    // 字典更新函數（從GitHub下載）
    bool updateDictFromGitHub(GlobalState& state, bool showProgress = true);
    
    // 候選字處理
    void updateCandidates(GlobalState& state);
    void selectCandidate(GlobalState& state, int index);
    void changePage(GlobalState& state, int direction);
    void sortCandidatesBySmartScore(GlobalState& state);
    
    // 學習功能
    void learnWord(GlobalState& state, const std::wstring& word);
    /// 聯想字選錯後按 Backspace：扣回剛學的 contextLearning 並還原前文狀態
    void unlearnFromLastPrediction(GlobalState& state);
    double getWordScore(const GlobalState& state, const std::wstring& word, const std::wstring& code);
    double calculateTimeWeight(time_t lastUsed);
    
    // 輸入驗證和處理
    bool validateInput(const std::wstring& input);
    bool enhancedValidateInput(const std::wstring& input);
    std::wstring filterValidChars(const std::wstring& input);
    
    // ★ 新增：輸入顯示處理（包含3+3提示）
    std::wstring getInputDisplay(const GlobalState& state);
    
    // 萬用字元匹配
    bool wildcardMatch(const std::wstring& pattern, const std::wstring& text);
    
    // 3+3模式
    void autoApply3Plus3Mode(GlobalState& state);
    void suggest3Plus3Mode(const GlobalState& state);
    
    // 聯想字功能
    void getWordPredictions(GlobalState& state, const std::wstring& word);
    void showPredictionsAfterSelection(GlobalState& state, const std::wstring& selected);
    
    // 詞語庫功能
    void loadWordPhrases(GlobalState& state);
}

#endif // DICTIONARY_H