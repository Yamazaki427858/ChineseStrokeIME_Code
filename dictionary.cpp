// dictionary.cpp - 字典管理實作（修正字碼表持續顯示和3+3提示）
#include "dictionary.h"
#include "dict_updater.h"
#include "buffer_manager.h"
#include "input_handler.h"
#include "window_manager.h"
#include "ime_manager.h"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <ctime>
#include <cstdint>

namespace Dictionary {

// 新增：增強型輸入驗證（參考OptimizedChineseStrokeIME.cpp）
bool enhancedValidateInput(const std::wstring& input) {
    if (input.empty()) return true;
    if (input.length() > 30) return false;  // 防止過長輸入
    
    int validCharCount = 0;
    for (wchar_t ch : input) {
        if (ch == L'u' || ch == L'i' || ch == L'o' || ch == L'j' || ch == L'k' || ch == L'*') {
            validCharCount++;
        }
    }
    
    return validCharCount > 0;
}

// 新增：過濾有效字符（參考OptimizedChineseStrokeIME.cpp）
std::wstring filterValidChars(const std::wstring& input) {
    std::wstring filtered;
    for (wchar_t ch : input) {
        if (ch == L'u' || ch == L'i' || ch == L'o' || ch == L'j' || ch == L'k' || ch == L'*') {
            filtered += ch;
        }
    }
    return filtered;
}

// 將 u i o j k 轉為筆劃符號一丨丿丶フ，* 不轉換
static std::wstring convertToStrokeSymbols(const std::wstring& input) {
    std::wstring result;
    for (wchar_t ch : input) {
        switch (ch) {
            case L'u': result += L'一'; break;
            case L'i': result += L'丨'; break;
            case L'o': result += L'丿'; break;
            case L'j': result += L'丶'; break;
            case L'k': result += L'フ'; break;
            default:   result += ch;    break;
        }
    }
    return result;
}

// 新增：獲取輸入顯示內容（包含3+3提示）
std::wstring getInputDisplay(const GlobalState& state) {
    std::wstring display = state.input;
    
    if (state.showPunctMenu) {
        if (state.punctMenuMode == PunctMenuMode::EMOJI &&
            state.emojiGroupIndex >= 0 &&
            state.emojiGroupIndex < (int)state.emojiGroups.size()) {
            display = L"Emoji：" + state.emojiGroups[state.emojiGroupIndex].label;
        } else {
            display = L"標點符號選單";
        }
    } else if (!state.input.empty()) {
        std::wstring filtered = filterValidChars(state.input);
        std::wstring displayable = state.showStrokeSymbols ? convertToStrokeSymbols(filtered) : filtered;
        display = displayable;
        
        // 如果原輸入包含無效字符，顯示過濾結果
        if (filtered != state.input) {
            display += L" [已過濾: " + displayable + L"]";
        }
        
        // 3+3模式提示（第七個字碼開始提示）
        if (filtered.length() >= 7) {
            std::wstring first3 = filtered.substr(0, 3);
            std::wstring last3 = filtered.substr(filtered.length() - 3);
            std::wstring dFirst3 = state.showStrokeSymbols ? convertToStrokeSymbols(first3) : first3;
            std::wstring dLast3 = state.showStrokeSymbols ? convertToStrokeSymbols(last3) : last3;
            display += L" (建議: " + dFirst3 + L"*" + dLast3 + L")";
        } else if (filtered.length() > 6) {
            display += L" (可用*號導出)";
        } else if (filtered.length() > 3) {
            display += L" (可用*號搜尋)";
        }
    }
    
    return display;
}

double calculateTimeWeight(time_t lastUsed) {
    time_t now = time(nullptr);
    double daysDiff = difftime(now, lastUsed) / (24 * 3600);
    if (daysDiff <= 1) return 1.0;
    if (daysDiff <= 7) return 0.8;
    if (daysDiff <= 30) return 0.6;
    if (daysDiff <= 90) return 0.4;
    return 0.2;
}

double getWordScore(const GlobalState& state, const std::wstring& word, const std::wstring& code) {
    double score = (10.0 - code.length()) * 2.0;
    if (state.wordFreq.find(word) != state.wordFreq.end()) {
        const WordInfo& info = state.wordFreq.at(word);
        double freqScore = info.frequency * 1.0;
        double timeWeight = calculateTimeWeight(info.lastUsed);
        double permanentBonus = info.isPermanent ? 5.0 : 0.0;
        score += (freqScore * timeWeight) + permanentBonus;
    }
    // 智能聯想引擎：支援新的 contextLearning 結構
    if (!state.lastSelected.empty() && state.contextLearning.find(state.lastSelected) != state.contextLearning.end()) {
        const auto& context = state.contextLearning.at(state.lastSelected);
        if (context.find(word) != context.end()) {
            std::pair<std::wstring, std::wstring> keyPair(state.lastSelected, word);
            int count = context.at(word);
            
            // locked → score = ∞（使用非常大的數值）
            if (state.lockedContext.find(keyPair) != state.lockedContext.end()) {
                score += 1000000.0;  // 永遠置頂
            }
            // pinned → 置頂+加分，分數須恆高於自動學習（自動學習最高約 count*3+20）
            else if (state.pinnedContext.find(keyPair) != state.pinnedContext.end()) {
                score += 10000.0 + count;  // 確保高於任何自動學習分數
            }
            // 自動學習 → score = count * 3.0 + baseBonus
            else {
                const double baseBonus = 20.0;
                score += count * 3.0 + baseBonus;
            }
        }
    }
    return score;
}

void learnWord(GlobalState& state, const std::wstring& word) {
    if (word.empty()) return;

    if (Utils::isPunctuation(word)) {
        // 任何標點都完全中斷聯想前文（逗號、句號等均視為語境分隔）
        state.lastContext.clear();
        state.lastSelected.clear();
        state.lastBigram.clear();
        state.pendingUnlearnFromPrediction = false;
        return;
    }
    
    time_t now = time(nullptr);
    // 更新用戶詞庫（user_dict）
    if (state.wordFreq.find(word) == state.wordFreq.end()) {
        state.wordFreq[word] = {1, now, 1, false};
        Utils::updateStatus(state, L"學習新詞：" + word + L"（暫存）");
    } else {
        WordInfo& info = state.wordFreq[word];
        info.frequency++;
        info.lastUsed = now;
        if (!info.isPermanent) {
            info.tempCount++;
            if (info.tempCount >= 3) {
                info.isPermanent = true;
                Utils::updateStatus(state, L"詞語加入永久詞庫：" + word);
            } else {
                Utils::updateStatus(state, L"詞語學習中：" + word + L"（" + std::to_wstring(info.tempCount) + L"/3）");
            }
        }
    }
    
    // 智能聯想引擎：更新 contextLearning（支援多字詞與片語）
    const std::wstring prevWord = state.lastSelected;  // 上一個選字（單字或詞）
    const std::wstring prevBigram = state.lastBigram;  // 上一個片語（例如「電腦」）
    
    if (!prevWord.empty() && prevWord != word) {
        // 單字/詞 → 下一個字/詞
        state.contextLearning[prevWord][word]++;
    }
    if (!prevBigram.empty() && prevBigram != word) {
        // 片語 → 下一個字/詞，例如「電腦」→「系」
        state.contextLearning[prevBigram][word]++;
    }
    
    // 更新 lastBigram：由前一個選字與本次選字組成片語
    if (!prevWord.empty()) {
        state.lastBigram = prevWord + word;
    } else {
        state.lastBigram.clear();
    }

    // 更新 lastContext：滾動窗口，最多保留 4 字，用於多字前綴聯想查詢
    // 例：依序選「香」「港」「理」→ lastContext 變化：「香」→「香港」→「香港理」
    {
        const size_t MAX_CONTEXT_LEN = 9;
        if (!state.lastSelected.empty()) {
            state.lastContext += word;
            if (state.lastContext.length() > MAX_CONTEXT_LEN) {
                state.lastContext = state.lastContext.substr(
                    state.lastContext.length() - MAX_CONTEXT_LEN);
            }
        } else {
            state.lastContext = word;  // 前文中斷後重新起頭
        }
    }

    // 更新最後選字
    state.lastSelected = word;
}

static void decContextLearningEdge(GlobalState& state, const std::wstring& prev,
                                   const std::wstring& next) {
    if (prev.empty() || next.empty() || prev == next) return;
    auto itMap = state.contextLearning.find(prev);
    if (itMap == state.contextLearning.end()) return;
    auto itCnt = itMap->second.find(next);
    if (itCnt == itMap->second.end()) return;
    if (itCnt->second > 1) {
        itMap->second[next] = itCnt->second - 1;
    } else {
        itMap->second.erase(itCnt);
        if (itMap->second.empty()) {
            state.contextLearning.erase(itMap);
        }
    }
}

void unlearnFromLastPrediction(GlobalState& state) {
    if (!state.pendingUnlearnFromPrediction) return;
    const std::wstring& next = state.pendingUnlearnNext;
    if (!state.pendingUnlearnPrev.empty() && !next.empty() && state.pendingUnlearnPrev != next) {
        decContextLearningEdge(state, state.pendingUnlearnPrev, next);
    }
    if (!state.pendingUnlearnBigramSnap.empty() && state.pendingUnlearnBigramSnap != next) {
        decContextLearningEdge(state, state.pendingUnlearnBigramSnap, next);
    }
    state.lastContext = state.pendingUnlearnContextSnap;
    state.lastBigram = state.pendingUnlearnBigramSnap;
    state.lastSelected = state.pendingUnlearnPrev;
    state.pendingUnlearnFromPrediction = false;
    state.pendingUnlearnContextSnap.clear();
    state.pendingUnlearnBigramSnap.clear();
    state.pendingUnlearnPrev.clear();
    state.pendingUnlearnNext.clear();
    if (state.hWnd) {
        KillTimer(state.hWnd, 995);
        SetTimer(state.hWnd, 995, 2000, NULL);
    }
}

void loadMainDict(GlobalState& state) {
    state.dict.clear();
    state.charToCode.clear();
    std::wstring dictPath = state.systemDir + L"Zi-Ma-Biao.txt";
    std::string dictPathNarrow = Utils::wstrToUtf8(dictPath);
    std::ifstream fin(dictPathNarrow);
    if (!fin.is_open()) {
        // 文件不存在，尝试从GitHub自动下载
        Utils::updateStatus(state, L"字碼表檔案不存在，嘗試從GitHub下載...");
        if (state.hWnd) {
            InvalidateRect(state.hWnd, nullptr, TRUE);
            UpdateWindow(state.hWnd);
        }
        DictUpdater::DownloadResult result = DictUpdater::updateDictionarySafely(nullptr, dictPathNarrow.c_str());
        
        if (result.status == DictUpdater::DownloadStatus::Success) {
            fin.close();
            fin.open(dictPathNarrow);
            if (fin.is_open()) {
                Utils::updateStatus(state, L"✓ 成功從GitHub下載字碼表，正在載入...");
                if (state.hWnd) {
                    InvalidateRect(state.hWnd, nullptr, TRUE);
                    UpdateWindow(state.hWnd);
                }
                // 继续下面的加载逻辑
            } else {
                // 下载成功但无法打开文件（不应该发生）
                std::wstring errorMsg = L"下載成功但無法打開檔案";
                Utils::updateStatus(state, L"✗ " + errorMsg);
                if (state.hWnd) {
                    MessageBoxW(state.hWnd, 
                        (L"警告：字碼表檔案異常\n\n" + errorMsg + 
                         L"\n\n建議手動下載 Zi-Ma-Biao.txt 文件。").c_str(),
                        L"字碼表載入失敗", MB_OK | MB_ICONWARNING);
                    InvalidateRect(state.hWnd, nullptr, TRUE);
                    UpdateWindow(state.hWnd);
                }
                state.dict[L"u"] = {L"一"};
                state.dict[L"i"] = {L"丨"};
                state.dict[L"o"] = {L"丿"};
                state.dict[L"j"] = {L"丶"};
                state.dict[L"k"] = {L"乙"};
                state.charToCode[L"一"] = L"u";
                state.charToCode[L"丨"] = L"i";
                state.charToCode[L"丿"] = L"o";
                state.charToCode[L"丶"] = L"j";
                state.charToCode[L"乙"] = L"k";
                state.dictSize = 5;
                return;
            }
        } else {
            // 下载失败
            std::wstring errorMsg = DictUpdater::getStatusMessage(result);
            std::wstring fullErrorMsg = L"✗ 無法下載字碼表：" + errorMsg;
            Utils::updateStatus(state, fullErrorMsg);
            
            // 显示弹窗警告
            if (state.hWnd) {
                std::wstring msgBoxText = L"警告：字碼表檔案缺失且下載失敗\n\n";
                msgBoxText += L"錯誤原因：" + errorMsg + L"\n\n";
                msgBoxText += L"建議：\n";
                msgBoxText += L"1. 檢查網路連接\n";
                msgBoxText += L"2. 手動從GitHub下載 Zi-Ma-Biao.txt\n";
                msgBoxText += L"3. 將文件放在 system 目錄下\n\n";
                msgBoxText += L"GitHub地址：\n";
                msgBoxText += L"https://github.com/Yamazaki427858/ChineseStrokeIME";
                
                MessageBoxW(state.hWnd, msgBoxText.c_str(), 
                    L"字碼表缺失警告", MB_OK | MB_ICONWARNING);
                InvalidateRect(state.hWnd, nullptr, TRUE);
                UpdateWindow(state.hWnd);
            }
            
            state.dict[L"u"] = {L"一"};
            state.dict[L"i"] = {L"丨"};
            state.dict[L"o"] = {L"丿"};
            state.dict[L"j"] = {L"丶"};
            state.dict[L"k"] = {L"乙"};
            state.charToCode[L"一"] = L"u";
            state.charToCode[L"丨"] = L"i";
            state.charToCode[L"丿"] = L"o";
            state.charToCode[L"丶"] = L"j";
            state.charToCode[L"乙"] = L"k";
            state.dictSize = 5;
            return;
        }
    }
    
    std::string line;
    int count = 0;
    while (std::getline(fin, line)) {
        if (line.empty() || line[0] == '#') continue;
        size_t tab = line.find('\t');
        if (tab == std::string::npos) continue;
        std::wstring key = Utils::utf8ToWstr(line.substr(tab+1));
        std::wstring val = Utils::utf8ToWstr(line.substr(0, tab));
        if (!key.empty() && !val.empty()) {
            state.dict[key].push_back(val);
            if (val.length() == 1 &&
                (state.charToCode.find(val) == state.charToCode.end() ||
                 key.length() < state.charToCode[val].length())) {
                state.charToCode[val] = key;
            }
            count++;
        }
    }
    fin.close();
    state.dictSize = count;
    
    // 建立 bigramIndex（相鄰字對索引）
    state.bigramIndex.clear();
    for (const auto& entry : state.dict) {
        for (const auto& word : entry.second) {
            // 掃描所有長度 ≥ 2 的詞條，建立相鄰字對索引
            for (size_t i = 0; i + 1 < word.size(); i++) {
                state.bigramIndex[word[i]][word[i+1]]++;
            }
        }
    }
    
    Utils::updateStatus(state, L"重新載入中文字典：" + std::to_wstring(count) + L" 個字，已建立相鄰字對索引");
}

void loadPunctuator(GlobalState& state) {
    state.punct[L","] = {L"，", L","};
    state.punct[L"."] = {L"。", L"."};
    state.punct[L"?"] = {L"？", L"?"};
    state.punct[L"!"] = {L"！", L"!"};
    state.punct[L":"] = {L"：", L":"};
    state.punct[L";"] = {L"；", L";"};
    state.punct[L"("] = {L"（", L"("};
    state.punct[L")"] = {L"）", L")"};
    state.punct[L"["] = {L"「", L"「", L"［", L"["};
    state.punct[L"]"] = {L"」", L"」", L"］", L"]"};
    state.punct[L"{"] = {L"『", L"{"};
    state.punct[L"}"] = {L"』", L"}"};
    state.punct[L" "] = {L" "};
    state.punct[L"<"] = {L"《", L"<"};
    state.punct[L">"] = {L"》", L">"};
    state.punct[L"/"] = {L"／", L"/"};
    state.punct[L"'"] = {L"、", L"'"};
    state.punct[L"-"] = {L"－", L"-"};
    state.punct[L"_"] = {L"＿", L"_"};
    state.punct[L"="] = {L"＝", L"="};
    state.punct[L"\\"] = {L"＼", L"\\"};
    state.punct[L"|"] = {L"｜", L"|"}; 
    state.punct[L"~"] = {L"～", L"~"}; 
    state.punct[L"`"] = {L"`", L"`"};
    state.punct[L"^"] = {L"⌃", L"^"};
    state.punct[L"&"] = {L"＆", L"&"}; 
    state.punct[L"*"] = {L"＊", L"*"}; 
    state.punct[L"+"] = {L"＋", L"+"};
    state.punct[L"#"] = {L"＃", L"#"};
    state.punct[L"@"] = {L"＠", L"@"};   
    state.punct[L"$"] = {L"＄", L"$"}; 
    state.punct[L"%"] = {L"％", L"%"};
    state.punct[L"\""] = {L"＂", L"\""};
	
	
}

void loadPunctMenu(GlobalState& state) {
    state.punctCandidates.clear();
    bool fileDidNotExist = true;
    
    std::wstring menuPath = state.userDir + L"punctmenu.txt";
    std::ifstream fin(Utils::wstrToUtf8(menuPath), std::ios::in | std::ios::binary);
    if (!fin.is_open()) {
        Utils::updateStatus(state, L"無法開啟 punctmenu.txt，使用內建標點選單");
    } else {
        fileDidNotExist = false;
        std::string content((std::istreambuf_iterator<char>(fin)), std::istreambuf_iterator<char>());
        fin.close();
        
        // 處理 UTF-8 BOM
        if (content.length() >= 3 && 
            content[0] == static_cast<char>(0xEF) &&
            content[1] == static_cast<char>(0xBB) &&
            content[2] == static_cast<char>(0xBF)) {
            content = content.substr(3);
        }
        
        // 按行分割處理
        std::stringstream ss(content);
        std::string line;
        int count = 0;
        
        while (std::getline(ss, line)) {
            // 移除行尾的 \r（Windows 換行符）
            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }
            
            // 移除前後空格
            line.erase(0, line.find_first_not_of(" \t"));
            line.erase(line.find_last_not_of(" \t") + 1);
            
            // 跳過空行和註解行
            if (line.empty() || line[0] == '#') continue;
            
            // 轉換為寬字符
            try {
                std::wstring punct = Utils::utf8ToWstr(line);
                if (!punct.empty()) {
                    state.punctCandidates.push_back(punct);
                    count++;
                }
            } catch (...) {
                // 轉換失敗，跳過這行
                continue;
            }
        }
        
        if (count >= 5) {
            Utils::updateStatus(state, L"載入標點符號選單：" + std::to_wstring(count) + L" 個符號");
            return; // 成功載入檔案，直接返回
        } else {
            Utils::updateStatus(state, L"標點選單檔案內容過少，使用內建選單");
        }
    }
    
    // 如果檔案載入失敗或內容不足，使用內建選單
    state.punctCandidates = { 
        // 特殊符號
        L"※", L"✓", L"★", L"☆", L"●", L"○",
        
        // 中文標點符號
        L"，", L"。", L"？", L"！", L"：", L"；", 
        
        // 引號和括號
        L"（", L"）", L"「", L"」", L"『", L"』", L"《", L"》", 
        L"〈", L"〉",
        
        // 其他符號
        L"　", L"·", L"－", L"—", L"……", L""", L""", L"'", L"'", 
        L"｜", L"＼", L"／", L"～", L"＿", L"￥", L"％", L"＃", L"＠", 
        L"［", L"］",
        
        // 撲克牌符號
        L"♠", L"♥", L"♣", L"♦"
    };
    
    // 僅在檔案不存在時建立預設 punctmenu.txt（user/ 永不覆蓋既有檔案）
    if (fileDidNotExist) {
        std::ofstream fout(Utils::wstrToUtf8(state.userDir + L"punctmenu.txt"), std::ios::out | std::ios::binary);
        if (fout.is_open()) {
            for (const auto& punct : state.punctCandidates) {
                fout << Utils::wstrToUtf8(punct) << "\n";
            }
            fout.close();
        }
    }
    
    Utils::updateStatus(state, L"使用內建標點符號選單：" + std::to_wstring(state.punctCandidates.size()) + L" 個符號");
}

namespace {

std::string readFileUtf8(const std::wstring& path) {
    std::ifstream fin(Utils::wstrToUtf8(path), std::ios::in | std::ios::binary);
    if (!fin.is_open()) return "";
    return std::string((std::istreambuf_iterator<char>(fin)), std::istreambuf_iterator<char>());
}

void stripUtf8Bom(std::string& content) {
    if (content.size() >= 3 &&
        static_cast<unsigned char>(content[0]) == 0xEF &&
        static_cast<unsigned char>(content[1]) == 0xBB &&
        static_cast<unsigned char>(content[2]) == 0xBF) {
        content = content.substr(3);
    }
}

std::wstring jsonStringAfter(const std::string& json, const char* key, size_t fromPos) {
    std::string pattern = std::string("\"") + key + "\"";
    size_t pos = json.find(pattern, fromPos);
    if (pos == std::string::npos) return L"";
    pos = json.find(':', pos);
    if (pos == std::string::npos) return L"";
    pos = json.find('"', pos + 1);
    if (pos == std::string::npos) return L"";
    size_t end = pos + 1;
    while (end < json.size()) {
        if (json[end] == '"' && json[end - 1] != '\\') break;
        ++end;
    }
    try {
        return Utils::utf8ToWstr(json.substr(pos + 1, end - pos - 1));
    } catch (...) {
        return L"";
    }
}

int jsonIntAfter(const std::string& json, const char* key, size_t fromPos, int defaultVal) {
    std::string pattern = std::string("\"") + key + "\"";
    size_t pos = json.find(pattern, fromPos);
    if (pos == std::string::npos) return defaultVal;
    pos = json.find(':', pos);
    if (pos == std::string::npos) return defaultVal;
    ++pos;
    while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t')) ++pos;
    size_t end = pos;
    while (end < json.size() && (json[end] == '-' || (json[end] >= '0' && json[end] <= '9'))) ++end;
    if (end == pos) return defaultVal;
    try {
        return std::stoi(json.substr(pos, end - pos));
    } catch (...) {
        return defaultVal;
    }
}

std::vector<std::wstring> parseEmojiJsonEntries(const std::string& json) {
    std::vector<std::wstring> result;
    size_t pos = 0;
    while ((pos = json.find("\"e\"", pos)) != std::string::npos) {
        pos = json.find(':', pos);
        if (pos == std::string::npos) break;
        pos = json.find('"', pos + 1);
        if (pos == std::string::npos) break;
        size_t end = pos + 1;
        while (end < json.size()) {
            if (json[end] == '"' && json[end - 1] != '\\') break;
            ++end;
        }
        try {
            std::wstring e = Utils::utf8ToWstr(json.substr(pos + 1, end - pos - 1));
            if (!e.empty()) result.push_back(e);
        } catch (...) {}
        pos = end + 1;
    }
    return result;
}

bool parseEmojiManifest(const std::string& json, GlobalState& state) {
    if (json.find("\"format\"") == std::string::npos) return false;
    state.emojiDataVersion = jsonStringAfter(json, "version", 0);
    state.emojiGridCols = jsonIntAfter(json, "cols", json.find("\"grid\""), EMOJI_GRID_COLS);
    state.emojiGridRows = jsonIntAfter(json, "rows", json.find("\"grid\""), EMOJI_GRID_ROWS);
    if (state.emojiGridCols < 4) state.emojiGridCols = EMOJI_GRID_COLS;
    if (state.emojiGridRows < 3) state.emojiGridRows = EMOJI_GRID_ROWS;

    state.emojiGroups.clear();
    size_t pos = 0;
    while ((pos = json.find("\"slug\"", pos)) != std::string::npos) {
        EmojiGroup g;
        g.slug = jsonStringAfter(json, "slug", pos);
        g.label = jsonStringAfter(json, "label_zh", pos);
        g.icon = jsonStringAfter(json, "icon", pos);
        g.file = jsonStringAfter(json, "file", pos);
        g.count = jsonIntAfter(json, "count", pos, 0);
        if (!g.slug.empty() && !g.file.empty()) {
            state.emojiGroups.push_back(g);
        }
        pos += 6;
    }
    return !state.emojiGroups.empty();
}

bool emojiDataComplete(const GlobalState& state) {
    std::wstring emojiDir = state.userDir + L"emoji\\";
    std::string content = readFileUtf8(emojiDir + L"manifest.json");
    if (content.empty()) return false;
    stripUtf8Bom(content);
    GlobalState probe;
    probe.userDir = state.userDir;
    if (!parseEmojiManifest(content, probe)) return false;
    for (const EmojiGroup& g : probe.emojiGroups) {
        std::ifstream fin(Utils::wstrToUtf8(emojiDir + g.file));
        if (!fin.is_open()) return false;
    }
    return true;
}

void loadEmojiGroupsFromDisk(GlobalState& state) {
    std::wstring emojiDir = state.userDir + L"emoji\\";
    CreateDirectoryW(emojiDir.c_str(), NULL);

    state.emojiGroups.clear();
    std::string content = readFileUtf8(emojiDir + L"manifest.json");
    if (content.empty()) return;
    stripUtf8Bom(content);
    if (!parseEmojiManifest(content, state)) return;

    int groupIndex = state.emojiGroupIndex;
    if (groupIndex < 0 || groupIndex >= (int)state.emojiGroups.size()) {
        groupIndex = 0;
    }
    if (!state.emojiGroups.empty()) {
        applyEmojiGroupDisplay(state, groupIndex);
    }
}

} // namespace

bool loadEmojiGroupAt(GlobalState& state, int groupIndex) {
    if (groupIndex < 0 || groupIndex >= (int)state.emojiGroups.size()) return false;
    EmojiGroup& g = state.emojiGroups[groupIndex];
    if (g.loaded) return true;

    std::wstring path = state.userDir + L"emoji\\" + g.file;
    std::string content = readFileUtf8(path);
    if (content.empty()) {
        if (updateEmojiFromGitHub(state, false)) {
            g.loaded = false;
            return loadEmojiGroupAt(state, groupIndex);
        }
        return false;
    }
    stripUtf8Bom(content);
    g.emojis = parseEmojiJsonEntries(content);
    if (g.emojis.empty()) return false;
    g.loaded = true;
    g.count = (int)g.emojis.size();
    return true;
}

void applyEmojiGroupDisplay(GlobalState& state, int groupIndex) {
    if (!loadEmojiGroupAt(state, groupIndex)) return;
    state.emojiGroupIndex = groupIndex;
    state.candidates = state.emojiGroups[groupIndex].emojis;
    state.candidateCodes.clear();
    state.selected = 0;
    state.currentPage = 0;
    state.hoverCandidateIndex = -1;
    state.emojiHoverCell = -1;
    int perPage = state.emojiGridCols * state.emojiGridRows;
    state.totalPages = perPage > 0
        ? (int)((state.candidates.size() + perPage - 1) / perPage)
        : 0;
    if (state.totalPages < 1) state.totalPages = 1;
}

void loadEmojiGroups(GlobalState& state) {
    std::wstring emojiDir = state.userDir + L"emoji\\";
    CreateDirectoryW(emojiDir.c_str(), NULL);

    if (!emojiDataComplete(state)) {
        Utils::updateStatus(state, L"Emoji 資料不存在或不完整，嘗試從 GitHub 下載…");
        if (state.hWnd) {
            InvalidateRect(state.hWnd, nullptr, TRUE);
            UpdateWindow(state.hWnd);
        }
        if (!updateEmojiFromGitHub(state, true)) {
            Utils::updateStatus(state, L"無法載入 Emoji 資料（請檢查網路，或從托盤選單手動更新）");
        }
        return;
    }

    loadEmojiGroupsFromDisk(state);
    if (!state.emojiGroups.empty()) {
        Utils::updateStatus(state, L"已載入 Emoji 資料：" + std::to_wstring(state.emojiGroups.size()) + L" 類");
    }
}

namespace {

DWORD g_lastEmojiStatusPaintTick = 0;
const DWORD kEmojiStatusPaintIntervalMs = 400;

void refreshEmojiInsertStatus(GlobalState& state, bool bufferMode) {
    const std::wstring msg = bufferMode
        ? L"已插入 Emoji（選單仍開啟，ESC 關閉）"
        : L"已插入 Emoji（ESC 關閉選單）";
    Utils::setStatusMessage(state, msg);

    DWORD now = GetTickCount();
    if (now - g_lastEmojiStatusPaintTick >= kEmojiStatusPaintIntervalMs) {
        g_lastEmojiStatusPaintTick = now;
        if (state.hWnd) InvalidateRect(state.hWnd, nullptr, FALSE);
    }
}

} // namespace

void selectEmoji(GlobalState& state, const std::wstring& emoji) {
    if (emoji.empty()) return;
    if (state.bufferMode) {
        BufferManager::insertTextAtCursor(state, emoji);
    } else if (state.showPunctMenu) {
        InputHandler::queueTextDirectUnicode(emoji);
    } else {
        InputHandler::sendTextDirectUnicode(emoji);
    }
    refreshEmojiInsertStatus(state, state.bufferMode);
}

bool updateEmojiFromGitHub(GlobalState& state, bool showProgress) {
    if (showProgress) {
        Utils::updateStatus(state, L"正在從 GitHub 下載 Emoji 資料…");
        if (state.hWnd) {
            InvalidateRect(state.hWnd, nullptr, TRUE);
            UpdateWindow(state.hWnd);
        }
    }

    const char* baseUrl =
        "https://raw.githubusercontent.com/Yamazaki427858/ChineseStrokeIME/ChineseStrokeIME/SourceCode/emoji/";
    std::wstring emojiDir = state.userDir + L"emoji\\";
    CreateDirectoryW(emojiDir.c_str(), NULL);

    std::string manifestLocal = Utils::wstrToUtf8(emojiDir + L"manifest.json");
    std::string manifestTmp = manifestLocal + ".tmp";
    std::string manifestUrl = std::string(baseUrl) + "manifest.json";

    DictUpdater::DownloadResult r = DictUpdater::downloadFromGitHub(manifestUrl.c_str(), manifestTmp.c_str());
    if (r.status != DictUpdater::DownloadStatus::Success) {
        Utils::updateStatus(state, L"✗ Emoji manifest 下載失敗");
        return false;
    }

    std::string manifestContent;
    {
        std::ifstream fin(manifestTmp, std::ios::in | std::ios::binary);
        if (!fin.is_open()) {
            Utils::updateStatus(state, L"✗ 無法讀取下載的 manifest");
            return false;
        }
        manifestContent.assign((std::istreambuf_iterator<char>(fin)), std::istreambuf_iterator<char>());
    }

    GlobalState tempState = state;
    stripUtf8Bom(manifestContent);
    if (!parseEmojiManifest(manifestContent, tempState)) {
        DeleteFileA(manifestTmp.c_str());
        Utils::updateStatus(state, L"✗ Emoji manifest 格式無效");
        return false;
    }

    if (!MoveFileExA(manifestTmp.c_str(), manifestLocal.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        DeleteFileA(manifestTmp.c_str());
        Utils::updateStatus(state, L"✗ 無法寫入 manifest.json");
        return false;
    }

    for (const EmojiGroup& g : tempState.emojiGroups) {
        std::string url = std::string(baseUrl) + Utils::wstrToUtf8(g.file);
        std::string local = Utils::wstrToUtf8(emojiDir + g.file);
        std::string tmp = local + ".tmp";
        r = DictUpdater::downloadFromGitHub(url.c_str(), tmp.c_str());
        if (r.status != DictUpdater::DownloadStatus::Success) {
            Utils::updateStatus(state, L"✗ 下載失敗：" + g.file);
            return false;
        }
        if (!MoveFileExA(tmp.c_str(), local.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
            DeleteFileA(tmp.c_str());
            Utils::updateStatus(state, L"✗ 無法寫入：" + g.file);
            return false;
        }
    }

    loadEmojiGroupsFromDisk(state);
    if (!state.emojiGroups.empty()) {
        Utils::updateStatus(state, L"✓ Emoji 資料已更新（" + state.emojiDataVersion + L"，" +
                          std::to_wstring(state.emojiGroups.size()) + L" 類）");
    } else {
        Utils::updateStatus(state, L"✓ Emoji 資料已下載，但載入失敗");
    }
    if (state.hWnd) InvalidateRect(state.hWnd, nullptr, TRUE);
    return true;
}

void loadUserDict(GlobalState& state) {
    state.wordFreq.clear();
    std::ifstream fin(Utils::wstrToUtf8(state.userDir + L"user_dict.txt"));
    if (!fin.is_open()) {
        Utils::updateStatus(state, L"首次使用，將建立用戶字典");
        return;
    }
    
    std::string line;
    int count = 0;
    time_t now = time(nullptr);
    try {
        while (std::getline(fin, line)) {
            if (line.empty() || line[0] == '#') continue;
            std::vector<std::string> parts;
            std::stringstream ss(line);
            std::string part;
            while (std::getline(ss, part, '\t')) {
                parts.push_back(part);
            }
            if (parts.size() >= 2) {
                std::wstring character = Utils::utf8ToWstr(parts[0]);
                int freq = (parts.size() >= 3) ? std::stoi(parts[2]) : 1;
                if (!character.empty()) {
                    state.wordFreq[character] = {freq, now, std::max(3, freq), freq >= 3};
                    count++;
                }
            }
        }
    } catch (...) {}
    fin.close();
    Utils::updateStatus(state, L"重新載入用戶字典：" + std::to_wstring(count) + L" 個記錄");
}

void saveUserDict(const GlobalState& state) {
    try {
        std::ofstream fout(Utils::wstrToUtf8(state.userDir + L"user_dict.txt"));
        if (!fout.is_open()) return;
        fout << "# 用戶字典 - 自動生成（已過濾標點符號）" << std::endl;
        fout << "# 格式：詞語<TAB><TAB>使用頻率<TAB>狀態" << std::endl;
        fout << "# 可自行添加修改" << std::endl;
        
        std::vector<std::pair<std::wstring, WordInfo>> freqList;
        for (const auto& pair : state.wordFreq) {
            freqList.push_back(std::make_pair(pair.first, pair.second));
        }
        
        std::sort(freqList.begin(), freqList.end(), 
            [](const std::pair<std::wstring, WordInfo>& a, const std::pair<std::wstring, WordInfo>& b) {
            double scoreA = a.second.frequency * calculateTimeWeight(a.second.lastUsed);
            double scoreB = b.second.frequency * calculateTimeWeight(b.second.lastUsed);
            return scoreA > scoreB;
        });
        
        int maxEntries = std::min(2000, (int)freqList.size());
        for (int i = 0; i < maxEntries; i++) {
            const auto& item = freqList[i];
            std::string status = item.second.isPermanent ? "permanent" : "temp";
            fout << Utils::wstrToUtf8(item.first) << "\t\t" << item.second.frequency << "\t" << status << std::endl;
        }
        fout.close();
    } catch (...) {}
}

bool removeFromUserDict(GlobalState& state, const std::wstring& word) {
    if (word.empty()) return false;
    auto it = state.wordFreq.find(word);
    if (it == state.wordFreq.end()) return false;
    state.wordFreq.erase(it);
    saveUserDict(state);
    Utils::updateStatus(state, L"已從用戶字典刪除：" + word + L"（再次選字可重新學習）");
    return true;
}

// 載入智能聯想引擎的個人上下文學習記錄
void loadContextLearning(GlobalState& state) {
    state.contextLearning.clear();
    state.lockedContext.clear();
    state.pinnedContext.clear();
    state.blockedContext.clear();
    
    std::ifstream fin(Utils::wstrToUtf8(state.userDir + L"context_learning.txt"), std::ios::in | std::ios::binary);
    if (!fin.is_open()) {
        Utils::updateStatus(state, L"首次使用，將建立個人聯想記錄");
        return;
    }
    
    std::string content((std::istreambuf_iterator<char>(fin)), std::istreambuf_iterator<char>());
    fin.close();
    
    // 處理 UTF-8 BOM
    if (content.length() >= 3 && 
        content[0] == static_cast<char>(0xEF) &&
        content[1] == static_cast<char>(0xBB) &&
        content[2] == static_cast<char>(0xBF)) {
        content = content.substr(3);
    }
    
    std::stringstream ss(content);
    std::string line;
    int entryCount = 0;
    
    while (std::getline(ss, line)) {
        // 移除行尾的 \r（Windows 換行符）
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        
        // 移除前後空格
        line.erase(0, line.find_first_not_of(" \t"));
        line.erase(line.find_last_not_of(" \t") + 1);
        
        // 跳過空行和註解行
        if (line.empty() || line[0] == '#') continue;
        
        // 解析格式：支援兩種
        // 四欄：前字[TAB]後字[TAB]次數[TAB]模式
        // 三欄：前字[TAB]後字[TAB]次數  或  前字[TAB]後字[TAB]模式（無次數時模式即第三欄）
        std::vector<std::string> parts;
        std::stringstream lineStream(line);
        std::string part;
        while (std::getline(lineStream, part, '\t')) {
            parts.push_back(part);
        }
        
        if (parts.size() >= 2) {
            std::string p0 = parts[0], p1 = parts[1];
            p0.erase(0, p0.find_first_not_of(" \t")); if (!p0.empty()) p0.erase(p0.find_last_not_of(" \t") + 1);
            p1.erase(0, p1.find_first_not_of(" \t")); if (!p1.empty()) p1.erase(p1.find_last_not_of(" \t") + 1);
            std::wstring prevWord = Utils::utf8ToWstr(p0);
            std::wstring nextWord = Utils::utf8ToWstr(p1);
            int useCount = 1;
            std::string mode;
            if (parts.size() >= 4) {
                if (!parts[2].empty()) {
                    try {
                        useCount = std::stoi(parts[2]);
                        if (useCount < 1) useCount = 1;
                    } catch (...) {
                        useCount = 1;
                    }
                }
                mode = parts[3];
            } else if (parts.size() == 3 && !parts[2].empty()) {
                std::string third = parts[2];
                third.erase(0, third.find_first_not_of(" \t"));
                if (!third.empty()) third.erase(third.find_last_not_of(" \t") + 1);
                if (third == "locked" || third == "pinned") {
                    mode = third;
                    useCount = 1;
                } else {
                    try {
                        useCount = std::stoi(parts[2]);
                        if (useCount < 1) useCount = 1;
                    } catch (...) {
                        useCount = 1;
                    }
                }
            }
            mode.erase(0, mode.find_first_not_of(" \t"));
            if (!mode.empty()) mode.erase(mode.find_last_not_of(" \t") + 1);
            
            if (!prevWord.empty() && !nextWord.empty()) {
                std::pair<std::wstring, std::wstring> keyPair(prevWord, nextWord);
                if (mode == "blocked") {
                    // 屏蔽條目：不加入 contextLearning，只記錄到 blockedContext
                    state.blockedContext.insert(keyPair);
                } else {
                    auto& cnt = state.contextLearning[prevWord][nextWord];
                    if (useCount > cnt) cnt = useCount;
                    
                    if (mode == "locked") {
                        state.lockedContext.insert(keyPair);
                        state.pinnedContext.erase(keyPair);  // 同一條目只能為 locked 或 pinned 其一
                    } else if (mode == "pinned") {
                        state.pinnedContext.insert(keyPair);
                        state.lockedContext.erase(keyPair);
                    }
                }
                entryCount++;
            }
        }
    }
    
    if (entryCount > 0) {
        Utils::updateStatus(state, L"載入個人聯想記錄：" + std::to_wstring(entryCount) + L" 個條目");
    }
}

// 儲存智能聯想引擎的個人上下文學習記錄
// 重要：直接以記憶體中的 contextLearning / lockedContext / pinnedContext / blockedContext 為準，
// 不再重讀舊檔案，以避免覆蓋剛剛對 pinned/locked/blocked 所做的修改。
void saveContextLearning(GlobalState& state) {
    try {
        std::string pathNarrow = Utils::wstrToUtf8(state.userDir + L"context_learning.txt");
        std::ofstream fout(pathNarrow, std::ios::out | std::ios::binary);
        if (!fout.is_open()) return;
        
        fout << static_cast<char>(0xEF) << static_cast<char>(0xBB) << static_cast<char>(0xBF);
        fout << "# 個人聯想記錄 - 智能聯想引擎自動生成" << std::endl;
        fout << "# 格式：前字[TAB]後字[TAB]次數[TAB]模式（三欄時可為 前[TAB]後[TAB]模式）" << std::endl;
        fout << "# 模式：locked（永遠置頂）、pinned（置頂+加分）、無標記（自動學習）" << std::endl;
        fout << "# 儲存順序：locked → pinned → 自動學習" << std::endl;
        fout << std::endl;
        
        std::vector<std::tuple<std::wstring, std::wstring, int>> lockedEntries;
        for (const auto& pair : state.lockedContext) {
            const std::wstring& prevWord = pair.first;
            const std::wstring& nextWord = pair.second;
            if (state.contextLearning.find(prevWord) != state.contextLearning.end() &&
                state.contextLearning.at(prevWord).find(nextWord) != state.contextLearning.at(prevWord).end()) {
                int count = state.contextLearning.at(prevWord).at(nextWord);
                lockedEntries.push_back(std::make_tuple(prevWord, nextWord, count));
            }
        }
        std::sort(lockedEntries.begin(), lockedEntries.end(),
            [](const std::tuple<std::wstring, std::wstring, int>& a,
               const std::tuple<std::wstring, std::wstring, int>& b) {
                return std::get<2>(a) > std::get<2>(b);
            });
        
        for (const auto& entry : lockedEntries) {
            fout << Utils::wstrToUtf8(std::get<0>(entry)) << "\t"
                 << Utils::wstrToUtf8(std::get<1>(entry)) << "\t"
                 << std::get<2>(entry) << "\tlocked" << std::endl;
        }
        
        // 2. 再寫入所有 pinned 條目（按次數降序）
        std::vector<std::tuple<std::wstring, std::wstring, int>> pinnedEntries;
        for (const auto& pair : state.pinnedContext) {
            const std::wstring& prevWord = pair.first;
            const std::wstring& nextWord = pair.second;
            if (state.contextLearning.find(prevWord) != state.contextLearning.end() &&
                state.contextLearning.at(prevWord).find(nextWord) != state.contextLearning.at(prevWord).end()) {
                int count = state.contextLearning.at(prevWord).at(nextWord);
                pinnedEntries.push_back(std::make_tuple(prevWord, nextWord, count));
            }
        }
        std::sort(pinnedEntries.begin(), pinnedEntries.end(),
            [](const std::tuple<std::wstring, std::wstring, int>& a,
               const std::tuple<std::wstring, std::wstring, int>& b) {
                return std::get<2>(a) > std::get<2>(b);
            });
        
        for (const auto& entry : pinnedEntries) {
            fout << Utils::wstrToUtf8(std::get<0>(entry)) << "\t"
                 << Utils::wstrToUtf8(std::get<1>(entry)) << "\t"
                 << std::get<2>(entry) << "\tpinned" << std::endl;
        }
        
        // 3. 最後寫入自動學習記錄（按次數降序，上限可設定）
        std::vector<std::tuple<std::wstring, std::wstring, int>> autoEntries;
        for (const auto& prevPair : state.contextLearning) {
            for (const auto& nextPair : prevPair.second) {
                std::pair<std::wstring, std::wstring> keyPair(prevPair.first, nextPair.first);
                // 排除 locked 和 pinned 條目
                if (state.lockedContext.find(keyPair) == state.lockedContext.end() &&
                    state.pinnedContext.find(keyPair) == state.pinnedContext.end()) {
                    // 所有出現過的聯想都存入（次數決定排序優先級，不決定是否存檔）
                    if (nextPair.second >= 1) {
                        autoEntries.push_back(std::make_tuple(prevPair.first, nextPair.first, nextPair.second));
                    }
                }
            }
        }
        std::sort(autoEntries.begin(), autoEntries.end(),
            [](const std::tuple<std::wstring, std::wstring, int>& a,
               const std::tuple<std::wstring, std::wstring, int>& b) {
                return std::get<2>(a) > std::get<2>(b);
            });
        
        int maxAutoEntries = state.contextLearningMaxAutoEntries;
        int autoCount = std::min(maxAutoEntries, (int)autoEntries.size());
        for (int i = 0; i < autoCount; i++) {
            const auto& entry = autoEntries[i];
            fout << Utils::wstrToUtf8(std::get<0>(entry)) << "\t"
                 << Utils::wstrToUtf8(std::get<1>(entry)) << "\t"
                 << std::get<2>(entry) << std::endl;
        }
        
        // 4. 寫入 blocked 條目（最後，避免影響正常排序）
        for (const auto& pair : state.blockedContext) {
            fout << Utils::wstrToUtf8(pair.first) << "\t"
                 << Utils::wstrToUtf8(pair.second) << "\t0\tblocked" << std::endl;
        }
        
        fout.close();
    } catch (...) {}
}

void setContextLocked(GlobalState& state, const std::wstring& prevWord, const std::wstring& nextWord, bool locked) {
    if (prevWord.empty() || nextWord.empty()) return;
    std::pair<std::wstring, std::wstring> keyPair(prevWord, nextWord);
    if (state.contextLearning.find(prevWord) == state.contextLearning.end() ||
        state.contextLearning[prevWord].find(nextWord) == state.contextLearning[prevWord].end()) {
        state.contextLearning[prevWord][nextWord] = 1;
    }
    if (locked) {
        state.lockedContext.insert(keyPair);
        state.pinnedContext.erase(keyPair);
    } else {
        state.lockedContext.erase(keyPair);
    }
}

void setContextPinned(GlobalState& state, const std::wstring& prevWord, const std::wstring& nextWord, bool pinned) {
    if (prevWord.empty() || nextWord.empty()) return;
    std::pair<std::wstring, std::wstring> keyPair(prevWord, nextWord);
    if (state.contextLearning.find(prevWord) == state.contextLearning.end() ||
        state.contextLearning[prevWord].find(nextWord) == state.contextLearning[prevWord].end()) {
        state.contextLearning[prevWord][nextWord] = 1;
    }
    if (pinned) {
        state.pinnedContext.insert(keyPair);
        state.lockedContext.erase(keyPair);
    } else {
        state.pinnedContext.erase(keyPair);
    }
}

bool validateInput(const std::wstring& input) {
    if (input.empty()) return true;
    for (wchar_t ch : input) {
        if (ch != L'u' && ch != L'i' && ch != L'o' && ch != L'j' && ch != L'k' && ch != L'*') {
            return false;
        }
    }
    return true;
}

bool wildcardMatch(const std::wstring& pattern, const std::wstring& text) {
    int pLen = pattern.length();
    int tLen = text.length();
    
    std::vector<std::vector<bool>> dp(tLen + 1, std::vector<bool>(pLen + 1, false));
    
    dp[0][0] = true;
    
    for (int j = 1; j <= pLen; j++) {
        if (pattern[j-1] == L'*') {
            dp[0][j] = dp[0][j-1];
        }
    }
    
    for (int i = 1; i <= tLen; i++) {
        for (int j = 1; j <= pLen; j++) {
            if (pattern[j-1] == L'*') {
                dp[i][j] = dp[i-1][j] || dp[i][j-1];
            } else if (pattern[j-1] == text[i-1]) {
                dp[i][j] = dp[i-1][j-1];
            }
        }
    }
    
    return dp[tLen][pLen];
}

void sortCandidatesBySmartScore(GlobalState& state) {
    std::vector<std::pair<std::wstring, std::wstring>> candidatePairs;
    for (size_t i = 0; i < state.candidates.size(); i++) {
        candidatePairs.push_back(std::make_pair(state.candidates[i], state.candidateCodes[i]));
    }
    
    std::sort(candidatePairs.begin(), candidatePairs.end(), 
        [&state](const std::pair<std::wstring, std::wstring>& a, const std::pair<std::wstring, std::wstring>& b) {
        double scoreA = getWordScore(state, a.first, a.second);
        double scoreB = getWordScore(state, b.first, b.second);
        return scoreA > scoreB;
    });
    
    state.candidates.clear();
    state.candidateCodes.clear();
    for (const auto& pair : candidatePairs) {
        state.candidates.push_back(pair.first);
        state.candidateCodes.push_back(pair.second);
    }
}


// 改進的候選字更新函數
void updateCandidates(GlobalState& state) {
    state.candidates.clear();
    state.candidateCodes.clear();
    state.selected = 0;
    state.currentPage = 0;
    state.inputError = false;
    state.hoverCandidateIndex = -1;
    // 進入字碼查字模式時，一律離開聯想模式
    state.isPredictionMode = false;
    state.hoverCandidateIndex = -1;
    
    // 顯示字碼候選字時，隱藏聯想字視窗（改由模式切換統一處理）
    if (state.hPredWnd) ShowWindow(state.hPredWnd, SW_HIDE);
    
    if (state.input.empty()) { 
        state.showCand = false;
        state.isInputting = false;
        // 重置為空閒模式並隱藏所有輸入相關視窗
        WindowManager::switchMode(state, InputMode::IDLE);
        std::wstring modeText = state.chineseMode ? L"中文筆劃+全形" : L"英文直接+半形";
        Utils::updateStatus(state, modeText + L"模式" + (state.bufferMode ? L" [暫放模式]" : L""));
        // 修復：重繪工具列以即時更新狀態指示燈（使用 FALSE 只重繪無效區域，減少閃爍）
        if (state.hWnd) InvalidateRect(state.hWnd, nullptr, FALSE);
        return; 
    }
    
    // 使用增強型驗證
    if (!enhancedValidateInput(state.input)) {
        state.inputError = true;
        state.showCand = false;
        // ★ 關鍵修改：保持輸入狀態，不設為false
        state.isInputting = true;  
        
        // 保持字碼輸入視窗顯示，候選/聯想均隱藏（視為暫時離開候選模式）
        if (state.hInputWnd) {
            ShowWindow(state.hInputWnd, SW_SHOW);
            InvalidateRect(state.hInputWnd, nullptr, TRUE);
        }
        if (state.hCandWnd) {
            ShowWindow(state.hCandWnd, SW_HIDE);
        }
        if (state.hPredWnd) {
            ShowWindow(state.hPredWnd, SW_HIDE);
        }
        
        // ★ 新增：強制重新定位字碼視窗
        WindowManager::positionInputWindow(state);
        
        Utils::updateStatus(state, L"字碼過長：建議使用(3+3)搜尋或清除重新輸入");
        if (state.hWnd) InvalidateRect(state.hWnd, nullptr, TRUE);
        return;
    }
    
    std::wstring filteredInput = filterValidChars(state.input);
    
    if (filteredInput.empty()) {
        state.inputError = true;
        state.showCand = false;
        // ★ 關鍵修改：保持輸入狀態
        state.isInputting = true;
        
        if (state.hInputWnd) {
            ShowWindow(state.hInputWnd, SW_SHOW);
            InvalidateRect(state.hInputWnd, nullptr, TRUE);
        }
        
        if (state.hCandWnd) {
            ShowWindow(state.hCandWnd, SW_HIDE);
        }
        if (state.hPredWnd) {
            ShowWindow(state.hPredWnd, SW_HIDE);
        }
        
        // ★ 新增：強制重新定位字碼視窗
        WindowManager::positionInputWindow(state);
        
        Utils::updateStatus(state, L"請輸入有效字碼：uiojk或*");
        if (state.hWnd) InvalidateRect(state.hWnd, nullptr, TRUE);
        return;
    }
    
    // 候選字查找邏輯（保持原有）
    bool hasWildcard = filteredInput.find(L'*') != std::wstring::npos;
    if (hasWildcard) {
        for (const auto& pair : state.dict) {
            if (wildcardMatch(filteredInput, pair.first)) {
                for (const auto& character : pair.second) {
                    state.candidates.push_back(character);
                    state.candidateCodes.push_back(pair.first);
                }
            }
        }
    } else {
        if (state.dict.count(filteredInput)) {
            for (const auto& character : state.dict[filteredInput]) {
                state.candidates.push_back(character);
                state.candidateCodes.push_back(filteredInput);
            }
        }
        
        // 前綴匹配
        int prefixMatchCount = 0;
        const int MAX_PREFIX_MATCHES = 50;
        for (const auto& pair : state.dict) {
            if (prefixMatchCount >= MAX_PREFIX_MATCHES) break;
            if (pair.first.length() > filteredInput.length() && 
                pair.first.substr(0, filteredInput.length()) == filteredInput) {
                for (const auto& character : pair.second) {
                    if (std::find(state.candidates.begin(), state.candidates.end(), character) == state.candidates.end()) {
                        state.candidates.push_back(character);
                        state.candidateCodes.push_back(pair.first);
                        prefixMatchCount++;
                        if (prefixMatchCount >= MAX_PREFIX_MATCHES) break;
                    }
                }
            }
        }
        
        // 自動(3+3)搜尋
        if (filteredInput.length() > 8 && state.candidates.empty()) {
            std::wstring first3 = filteredInput.substr(0, std::min(3, (int)filteredInput.length()));
            std::wstring last3;
            if (filteredInput.length() >= 6) {
                last3 = filteredInput.substr(filteredInput.length() - 3);
            } else if (filteredInput.length() > 3) {
                last3 = filteredInput.substr(3);
            }
            std::wstring searchPattern = first3 + L"*" + last3;
            for (const auto& pair : state.dict) {
                if (wildcardMatch(searchPattern, pair.first)) {
                    for (const auto& character : pair.second) {
                        state.candidates.push_back(character);
                        state.candidateCodes.push_back(pair.first);
                    }
                }
            }
        }
    }
    
    sortCandidatesBySmartScore(state);
    state.totalPages = (state.candidates.size() + CANDIDATES_PER_PAGE - 1) / CANDIDATES_PER_PAGE;
    state.showCand = !state.candidates.empty();
    // ★ 關鍵修改：無論是否有候選字都保持輸入狀態
    state.isInputting = true;
    
    // ★ 修改：統一使用 WindowManager 來處理視窗定位
    if (state.showCand) {
        // 有候選字時，進入候選模式並定位候選字視窗與字碼視窗
        WindowManager::switchMode(state, InputMode::CAND_MODE);
        WindowManager::positionWindowsOptimized(state);
    } else {
        // 沒有候選字時，只定位字碼視窗並維持非候選模式
        WindowManager::switchMode(state, InputMode::IDLE);
        WindowManager::positionInputWindow(state);
        if (state.hCandWnd) ShowWindow(state.hCandWnd, SW_HIDE);
    }
    
    std::wstring statusMsg;
    if (state.showCand) {
        std::wstring searchType = hasWildcard ? L"(3+3)模式搜尋" : L"智慧排序搜尋";
        statusMsg = searchType + L"：找到 " + std::to_wstring(state.candidates.size()) + L" 個候選字";
    } else {
        statusMsg = L"輸入中：" + filteredInput + L"（無候選字）";
    }
    
    // 3+3模式建議
    if (filteredInput.length() > 6 && !hasWildcard && state.candidates.empty()) {
        std::wstring first3 = filteredInput.substr(0, 3);
        std::wstring last3;
        if (filteredInput.length() >= 6) {
            last3 = filteredInput.substr(filteredInput.length() - 3);
        } else if (filteredInput.length() > 3) {
            last3 = filteredInput.substr(3);
        }
        if (!last3.empty()) {
            statusMsg += L" | 建議(3+3)：" + first3 + L"*" + last3;
        }
    }
    
    if (state.bufferMode) {
        statusMsg = L"[暫放模式] " + statusMsg;
    }
    
    Utils::updateStatus(state, statusMsg);
    
    if (state.hCandWnd) InvalidateRect(state.hCandWnd, nullptr, TRUE);
    if (state.hWnd) InvalidateRect(state.hWnd, nullptr, TRUE);
}


void selectCandidate(GlobalState& state, int idx) {
    int actualIndex = state.currentPage * CANDIDATES_PER_PAGE + idx;
    if (actualIndex < 0 || actualIndex >= (int)state.candidates.size()) return;
    std::wstring selected = state.candidates[actualIndex];
    
    // 判斷是否為聯想字模式（候選字碼為 ⚑⚐聯想／常用／詞語 皆繼續聯想）
    bool isPredictionMode = (actualIndex < (int)state.candidateCodes.size() && 
                            (state.candidateCodes[actualIndex] == L"⚑" ||
                             state.candidateCodes[actualIndex] == L"⚐" ||
                             state.candidateCodes[actualIndex] == L"聯想" || 
                             state.candidateCodes[actualIndex] == L"常用" ||
                             state.candidateCodes[actualIndex] == L"詞語"));
    
    // 輸出文字
    if (state.bufferMode) {
        // 暫放模式下：所有選擇的文字（包括標點符號）都插入暫放區
        BufferManager::insertTextAtCursor(state, selected);
        if (state.showPunctMenu) {
            Utils::updateStatus(state, L"已加入標點符號：" + selected + L" (共" + std::to_wstring(state.bufferText.length()) + L"字)");
        } else {
            Utils::updateStatus(state, L"已加入暫放區：" + selected + L" (共" + std::to_wstring(state.bufferText.length()) + L"字)");
        }
    } else {
        // 非暫放模式：直接發送到目標應用程式
        InputHandler::sendTextDirectUnicode(selected);
    }
    
    // ✅ learnWord 必須在此處呼叫，不受 enableWordPrediction 影響
    // 確保關閉聯想字時，contextLearning 和 lastSelected 仍然持續更新
    if (!state.showPunctMenu) {
        const std::wstring snapCtx = state.lastContext;
        const std::wstring snapBg = state.lastBigram;
        const std::wstring snapPrev = state.lastSelected;
        learnWord(state, selected);
        // 無論是筆劃選字還是聯想選字，只要前文存在就設可撤銷標記。
        // 這樣用戶打了「香」再打（筆劃）「隹」後按 Backspace，仍可反學習「香→隹」。
        if (!Utils::isPunctuation(selected) && !snapPrev.empty()) {
            state.pendingUnlearnFromPrediction = true;
            state.pendingUnlearnContextSnap = snapCtx;
            state.pendingUnlearnBigramSnap = snapBg;
            state.pendingUnlearnPrev = snapPrev;
            state.pendingUnlearnNext = selected;
        } else {
            state.pendingUnlearnFromPrediction = false;
        }
        // 延遲保存用戶字典（使用定時器，避免頻繁寫入文件）
        if (state.hWnd) {
            KillTimer(state.hWnd, 995);  // 先清除舊的定時器（使用995避免衝突）
            SetTimer(state.hWnd, 995, 2000, NULL);  // 2秒後保存
        }
    }
    
    // 標點符號選單（非 Emoji）：選取後關閉；Emoji 模式走 selectEmoji，不在此處理
    if (state.showPunctMenu && state.punctMenuMode == PunctMenuMode::PUNCT) {
        state.input.clear();
        state.candidates.clear();
        state.candidateCodes.clear();
        state.showCand = false;
        state.isInputting = false;
        state.inputError = false;
        state.showPunctMenu = false;
        // 標點選單完成後回到空閒模式
        WindowManager::switchMode(state, InputMode::IDLE);
        IMEManager::restoreWindowsIME();
        // 修復：重繪工具列以即時更新狀態指示燈（使用 FALSE 只重繪無效區域，減少閃爍）
        if (state.hWnd) InvalidateRect(state.hWnd, nullptr, FALSE);
        return;
    }
    
    // 如果是聯想字模式，選擇後繼續顯示新的聯想字
    if (isPredictionMode && state.enableWordPrediction) {
        // 清空輸入，準備顯示新的聯想字
        state.input.clear();
        state.inputError = false;
        
        // 顯示該字的聯想字
        showPredictionsAfterSelection(state, selected);
        return;
    }
    
    // 正常模式：選擇字後，如果啟用聯想字功能，顯示聯想字
    if (state.enableWordPrediction && !Utils::isPunctuation(selected)) {
        state.input.clear();
        state.inputError = false;
        
        // 顯示該字的聯想字
        showPredictionsAfterSelection(state, selected);
        return;
    }
    
    // 不啟用聯想字或選擇標點符號：正常結束輸入
    state.input.clear();
    state.candidates.clear();
    state.candidateCodes.clear();
    state.showCand = false;
    state.isInputting = false;
    state.inputError = false;
    state.showPunctMenu = false;
    // 正常結束輸入：切換到空閒模式並關閉所有輸入相關視窗
    WindowManager::switchMode(state, InputMode::IDLE);
    
    // 🔥 恢復 Windows 輸入法狀態（輸入完成後）
    IMEManager::restoreWindowsIME();
    
    // 修復：輸入完成後重繪工具列以即時更新狀態指示燈（使用 FALSE 只重繪無效區域，減少閃爍）
    if (state.hWnd) InvalidateRect(state.hWnd, nullptr, FALSE);	
}

void changePage(GlobalState& state, int direction) {
    if (!state.showCand || state.totalPages <= 1) return;
    if (direction > 0 && state.currentPage < state.totalPages - 1) {
        state.currentPage++;
        state.selected = 0;
    } else if (direction < 0 && state.currentPage > 0) {
        state.currentPage--;
        state.selected = 0;
    }
    // 翻頁優化：只更新狀態列文字，不觸發工具列重繪（頁碼已在候選/聯想視窗內顯示）
    state.statusInfo = L"第" + std::to_wstring(state.currentPage + 1) + L"/" +
                      std::to_wstring(state.totalPages) + L"頁 共" +
                      std::to_wstring(state.candidates.size()) + L"個候選字";
    // 只重繪目前顯示的列表視窗，並立即更新以減少遲緩感
    HWND listWnd = (state.currentMode == InputMode::CAND_MODE) ? state.hCandWnd : state.hPredWnd;
    if (listWnd) {
        InvalidateRect(listWnd, nullptr, TRUE);
        UpdateWindow(listWnd);
    }
}

void autoApply3Plus3Mode(GlobalState& state) {
    if (state.input.length() > 12) {
        std::wstring first3 = state.input.substr(0, 3);
        std::wstring last3 = state.input.substr(state.input.length() - 3);
        state.input = first3 + L"*" + last3;
        
        Utils::updateStatus(state, L"自動轉換為(3+3)模式：" + state.input);
        updateCandidates(state);
    }
}

void suggest3Plus3Mode(const GlobalState& state) {
    if (state.input.length() > 8) {
        std::wstring first3 = state.input.substr(0, 3);
        std::wstring last3 = state.input.substr(state.input.length() - 3);
        std::wstring suggestion = first3 + L"*" + last3;
        
        Utils::updateStatus(const_cast<GlobalState&>(state), 
                           L"建議(3+3)模式：" + suggestion + L"（可節省輸入時間）");
    }  
}

// 從GitHub手動更新字典（直接下載，不檢查更新）
bool updateDictFromGitHub(GlobalState& state, bool showProgress) {
    if (showProgress) {
        Utils::updateStatus(state, L"正在從GitHub下載字碼表...");
        if (state.hWnd) {
            InvalidateRect(state.hWnd, nullptr, TRUE);
            UpdateWindow(state.hWnd);
        }
    }
    
    std::string dictPathNarrow = Utils::wstrToUtf8(state.systemDir + L"Zi-Ma-Biao.txt");
    DictUpdater::DownloadResult downloadResult = DictUpdater::updateDictionarySafely(nullptr, dictPathNarrow.c_str());
    
    if (downloadResult.status == DictUpdater::DownloadStatus::Success) {
        std::ifstream testFile(dictPathNarrow);
        if (testFile.is_open()) {
            testFile.close();
            loadMainDict(state);
            Utils::updateStatus(state, L"✓ 字碼表已更新：" + 
                              std::to_wstring(downloadResult.fileSize) + L" 字節");
            if (state.hWnd) {
                InvalidateRect(state.hWnd, nullptr, TRUE);
                UpdateWindow(state.hWnd);
            }
            return true;
        } else {
            // 文件不存在（不应该发生，因为下载成功了）
            Utils::updateStatus(state, L"✗ 更新成功但檔案未找到");
            if (state.hWnd) {
                MessageBoxW(state.hWnd, 
                    L"錯誤：字碼表更新成功但檔案未找到。\n請重新啟動程序或手動檢查。", 
                    L"更新異常", MB_OK | MB_ICONWARNING);
                InvalidateRect(state.hWnd, nullptr, TRUE);
                UpdateWindow(state.hWnd);
            }
            return false;
        }
    } else {
        // 下載失败，显示详细错误信息
        std::wstring errorMsg = DictUpdater::getStatusMessage(downloadResult);
        Utils::updateStatus(state, L"✗ 下載失敗：" + errorMsg);
        
        if (showProgress && state.hWnd) {
            std::wstring msgBoxText = L"字碼表下載失敗\n\n";
            msgBoxText += L"錯誤原因：" + errorMsg + L"\n\n";
            msgBoxText += L"建議：\n";
            msgBoxText += L"1. 檢查網路連接\n";
            msgBoxText += L"2. 稍後重試\n";
            
            MessageBoxW(state.hWnd, msgBoxText.c_str(), 
                L"下載失敗", MB_OK | MB_ICONWARNING);
            InvalidateRect(state.hWnd, nullptr, TRUE);
            UpdateWindow(state.hWnd);
        }
        return false;
    }
}

// ==================== 詞語庫進階優化（cache + 多字前綴 + 評分） ====================
//
// 設計原則：
//   - 全域記憶體與 cache 大小皆設硬上限，避免大型詞庫撐爆啟動。
//   - cache 採用 tmp + MoveFileExW 原子替換；任何欄位驗證失敗即刪除 cache
//     檔，下次啟動會自動 fallback 到 txt 重建。
//   - magic 升到 CStrokeWPC7；舊版 cache 即使存在也會被視為失效並刪除。
//   - 多字前綴只額外建立「兩字 → 後綴整段」（length>=4），避免無上限膨脹。

struct WordPhraseCacheMetadata {
    uint64_t fileSize;
    DWORD lastWriteLow;
    DWORD lastWriteHigh;
};

struct WordPhraseBuilderEntry {
    std::vector<std::wstring> items;
    std::unordered_set<std::wstring> seen;
    std::map<std::wstring, double> scores;
};

static const size_t   MAX_WORD_PHRASE_PREFIX_LEN         = 9;    // 最多建立幾字的前綴索引（對應 lastContext 窗口大小）
static const size_t   MAX_WORD_PHRASE_CANDIDATES_PER_KEY = 300;
static const size_t   MAX_WORD_PHRASE_TOTAL_ENTRIES      = 10000000; // 僅作日誌統計用，不作截止限制
static const uint64_t MAX_WORD_PHRASE_CACHE_BYTES        = 300ULL * 1024 * 1024; // 快取上限 300 MB
static const uint32_t MAX_WORD_PHRASE_CACHE_KEY_COUNT    = 500000;  // 唯一 key 數上限（真正的記憶體守門）
static const uint32_t MAX_WORD_PHRASE_CACHE_ITEMS_PER_KEY = 1000;
static const size_t   MAX_WORD_PHRASE_CACHE_STRING_LEN   = 64;

static void trimAsciiInPlace(std::string& value) {
    size_t first = value.find_first_not_of(" \t");
    if (first == std::string::npos) {
        value.clear();
        return;
    }
    size_t last = value.find_last_not_of(" \t");
    value = value.substr(first, last - first + 1);
}

static bool getWordPhraseFileMetadata(const std::wstring& path, WordPhraseCacheMetadata& metadata) {
    WIN32_FILE_ATTRIBUTE_DATA data;
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &data)) return false;
    if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) return false;

    metadata.fileSize = (static_cast<uint64_t>(data.nFileSizeHigh) << 32) | data.nFileSizeLow;
    metadata.lastWriteLow = data.ftLastWriteTime.dwLowDateTime;
    metadata.lastWriteHigh = data.ftLastWriteTime.dwHighDateTime;
    return true;
}

template <typename T>
static bool writeBinaryValue(std::ofstream& out, const T& value) {
    out.write(reinterpret_cast<const char*>(&value), sizeof(T));
    return out.good();
}

template <typename T>
static bool readBinaryValue(std::ifstream& in, T& value) {
    in.read(reinterpret_cast<char*>(&value), sizeof(T));
    return in.good();
}

static bool readBinaryWString(std::ifstream& in, std::wstring& value) {
    uint32_t length = 0;
    if (!readBinaryValue(in, length)) return false;
    if (length > MAX_WORD_PHRASE_CACHE_STRING_LEN) return false;

    value.assign(length, L'\0');
    if (length > 0) {
        in.read(reinterpret_cast<char*>(&value[0]), length * sizeof(wchar_t));
    }
    return in.good();
}

static void addWordPhraseCandidate(
    std::map<std::wstring, WordPhraseBuilderEntry>& builder,
    size_t& totalEntries,
    const std::wstring& key,
    const std::wstring& candidate,
    double score,
    int& count
) {
    if (key.empty() || candidate.empty()) return;
    if (key.size() > MAX_WORD_PHRASE_CACHE_STRING_LEN) return;
    if (candidate.size() > MAX_WORD_PHRASE_CACHE_STRING_LEN) return;

    // 以唯一 key 數作記憶體守門：新 key 才受上限限制，現有 key 可繼續加候選
    auto it = builder.find(key);
    if (it == builder.end()) {
        if (builder.size() >= MAX_WORD_PHRASE_CACHE_KEY_COUNT) return;
        it = builder.emplace(key, WordPhraseBuilderEntry{}).first;
    }
    WordPhraseBuilderEntry& entry = it->second;

    if (entry.seen.insert(candidate).second) {
        if (entry.items.size() >= MAX_WORD_PHRASE_CANDIDATES_PER_KEY) {
            entry.seen.erase(candidate);
            return;
        }
        entry.items.push_back(candidate);
        ++totalEntries;
        ++count;
    }
    auto scoreIt = entry.scores.find(candidate);
    if (scoreIt == entry.scores.end() || score > scoreIt->second) {
        entry.scores[candidate] = score;
    }
}

static void commitWordPhraseBuilder(
    GlobalState& state,
    const std::map<std::wstring, WordPhraseBuilderEntry>& builder,
    int count
) {
    state.wordPhrases.clear();
    state.wordPhraseScores.clear();
    for (const auto& pair : builder) {
        state.wordPhrases[pair.first] = pair.second.items;
        state.wordPhraseScores[pair.first] = pair.second.scores;
    }
    state.phraseDictSize = count;
}

// 讀取 cache：任何驗證失敗都會刪除 cache 並回傳 false，迫使外層 fallback。
static bool loadWordPhrasesCache(
    GlobalState& state,
    const std::wstring& cachePath,
    const WordPhraseCacheMetadata& sourceMetadata
) {
    auto invalidate = [&cachePath]() {
        DeleteFileW(cachePath.c_str());
        return false;
    };

    std::ifstream cache(Utils::wstrToUtf8(cachePath), std::ios::in | std::ios::binary);
    if (!cache.is_open()) return false;

    cache.seekg(0, std::ios::end);
    std::streampos cacheSize = cache.tellg();
    cache.seekg(0, std::ios::beg);
    if (cacheSize <= 0 || static_cast<uint64_t>(cacheSize) > MAX_WORD_PHRASE_CACHE_BYTES) {
        cache.close();
        return invalidate();
    }

    const char expectedMagic[] = "CStrokeWPC8";
    char magic[sizeof(expectedMagic) - 1] = {0};
    cache.read(magic, sizeof(magic));
    if (!cache.good() || std::string(magic, sizeof(magic)) != std::string(expectedMagic, sizeof(expectedMagic) - 1)) {
        cache.close();
        return invalidate();
    }

    WordPhraseCacheMetadata cachedMetadata = {};
    if (!readBinaryValue(cache, cachedMetadata.fileSize) ||
        !readBinaryValue(cache, cachedMetadata.lastWriteLow) ||
        !readBinaryValue(cache, cachedMetadata.lastWriteHigh)) {
        cache.close();
        return invalidate();
    }
    if (cachedMetadata.fileSize != sourceMetadata.fileSize ||
        cachedMetadata.lastWriteLow != sourceMetadata.lastWriteLow ||
        cachedMetadata.lastWriteHigh != sourceMetadata.lastWriteHigh) {
        cache.close();
        return invalidate();
    }

    uint32_t entryCount = 0;
    if (!readBinaryValue(cache, entryCount) || entryCount > MAX_WORD_PHRASE_CACHE_KEY_COUNT) {
        cache.close();
        return invalidate();
    }

    std::map<std::wstring, std::vector<std::wstring>> loadedPhrases;
    std::map<std::wstring, std::map<std::wstring, double>> loadedScores;
    int phraseCount = 0;
    size_t totalEntries = 0;

    for (uint32_t i = 0; i < entryCount; ++i) {
        std::wstring key;
        if (!readBinaryWString(cache, key) || key.empty()) {
            cache.close();
            return invalidate();
        }

        uint32_t itemCount = 0;
        if (!readBinaryValue(cache, itemCount) || itemCount > MAX_WORD_PHRASE_CACHE_ITEMS_PER_KEY) {
            cache.close();
            return invalidate();
        }

        std::vector<std::wstring> items;
        items.reserve(itemCount);
        std::map<std::wstring, double> scores;

        for (uint32_t j = 0; j < itemCount; ++j) {
            std::wstring item;
            if (!readBinaryWString(cache, item) || item.empty()) {
                cache.close();
                return invalidate();
            }
            double score = 0.0;
            if (!readBinaryValue(cache, score)) {
                cache.close();
                return invalidate();
            }
            items.push_back(item);
            scores[item] = score;
            if (++totalEntries > MAX_WORD_PHRASE_TOTAL_ENTRIES) {
                cache.close();
                return invalidate();
            }
        }

        phraseCount += static_cast<int>(items.size());
        loadedPhrases.emplace(key, std::move(items));
        loadedScores.emplace(key, std::move(scores));
    }

    cache.close();

    state.wordPhrases.swap(loadedPhrases);
    state.wordPhraseScores.swap(loadedScores);
    state.phraseDictSize = phraseCount;
    return true;
}

// 寫入 cache：先寫到 .tmp，全部成功且大小在預算內才用 MoveFileExW 替換正本。
static void saveWordPhrasesCache(
    const GlobalState& state,
    const std::wstring& cachePath,
    const WordPhraseCacheMetadata& sourceMetadata
) {
    std::wstring tmpPath = cachePath + L".tmp";
    DeleteFileW(tmpPath.c_str());

    auto abortAndCleanup = [&tmpPath]() {
        DeleteFileW(tmpPath.c_str());
    };

    std::ofstream cache(Utils::wstrToUtf8(tmpPath), std::ios::out | std::ios::binary | std::ios::trunc);
    if (!cache.is_open()) return;

    uint64_t bytesWritten = 0;
    auto checkBudget = [&bytesWritten]() {
        return bytesWritten <= MAX_WORD_PHRASE_CACHE_BYTES;
    };
    auto writeRaw = [&](const void* data, size_t size) {
        cache.write(reinterpret_cast<const char*>(data), size);
        bytesWritten += size;
        return cache.good() && checkBudget();
    };

    const char expectedMagic[] = "CStrokeWPC8";
    if (!writeRaw(expectedMagic, sizeof(expectedMagic) - 1)) {
        cache.close(); abortAndCleanup(); return;
    }
    if (!writeRaw(&sourceMetadata.fileSize, sizeof(sourceMetadata.fileSize)) ||
        !writeRaw(&sourceMetadata.lastWriteLow, sizeof(sourceMetadata.lastWriteLow)) ||
        !writeRaw(&sourceMetadata.lastWriteHigh, sizeof(sourceMetadata.lastWriteHigh))) {
        cache.close(); abortAndCleanup(); return;
    }

    uint32_t entryCount = static_cast<uint32_t>(state.wordPhrases.size());
    if (entryCount > MAX_WORD_PHRASE_CACHE_KEY_COUNT) {
        cache.close(); abortAndCleanup(); return;
    }
    if (!writeRaw(&entryCount, sizeof(entryCount))) {
        cache.close(); abortAndCleanup(); return;
    }

    for (const auto& pair : state.wordPhrases) {
        const std::wstring& key = pair.first;
        if (key.empty() || key.size() > MAX_WORD_PHRASE_CACHE_STRING_LEN) {
            cache.close(); abortAndCleanup(); return;
        }
        uint32_t keyLen = static_cast<uint32_t>(key.size());
        if (!writeRaw(&keyLen, sizeof(keyLen)) ||
            !writeRaw(key.data(), keyLen * sizeof(wchar_t))) {
            cache.close(); abortAndCleanup(); return;
        }

        uint32_t itemCount = static_cast<uint32_t>(pair.second.size());
        if (itemCount > MAX_WORD_PHRASE_CACHE_ITEMS_PER_KEY) {
            cache.close(); abortAndCleanup(); return;
        }
        if (!writeRaw(&itemCount, sizeof(itemCount))) {
            cache.close(); abortAndCleanup(); return;
        }

        auto keyScoreIt = state.wordPhraseScores.find(key);
        for (const auto& item : pair.second) {
            if (item.empty() || item.size() > MAX_WORD_PHRASE_CACHE_STRING_LEN) {
                cache.close(); abortAndCleanup(); return;
            }
            uint32_t itemLen = static_cast<uint32_t>(item.size());
            if (!writeRaw(&itemLen, sizeof(itemLen)) ||
                !writeRaw(item.data(), itemLen * sizeof(wchar_t))) {
                cache.close(); abortAndCleanup(); return;
            }
            double score = 5.0;
            if (keyScoreIt != state.wordPhraseScores.end()) {
                auto itemScoreIt = keyScoreIt->second.find(item);
                if (itemScoreIt != keyScoreIt->second.end()) {
                    score = itemScoreIt->second;
                }
            }
            if (!writeRaw(&score, sizeof(score))) {
                cache.close(); abortAndCleanup(); return;
            }
        }
    }

    cache.flush();
    if (!cache.good()) {
        cache.close(); abortAndCleanup(); return;
    }
    cache.close();

    if (!MoveFileExW(tmpPath.c_str(), cachePath.c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        abortAndCleanup();
    }
}

// 載入詞語庫：優先讀 cache，失敗則從 txt 重建並產生新 cache。
void loadWordPhrases(GlobalState& state) {
    state.wordPhrases.clear();
    state.wordPhraseScores.clear();
    state.phraseDictSize = 0;

    std::wstring path = state.systemDir + L"wordphrases.txt";
    std::wstring cachePath = state.systemDir + L"wordphrases.cache";
    std::wstring cacheTmpPath = cachePath + L".tmp";
    DeleteFileW(cacheTmpPath.c_str()); // 清掉上次寫到一半的暫存檔

    std::string pathNarrow = Utils::wstrToUtf8(path);
    std::ifstream fin(pathNarrow, std::ios::in | std::ios::binary);
    if (!fin.is_open()) {
        const char* downloadUrl =
            "https://raw.githubusercontent.com/Yamazaki427858/ChineseStrokeIME/ChineseStrokeIME/SourceCode/%E8%81%AF%E6%83%B3%E8%A9%9E%E5%BA%AB/word_phrases.txt";

        DictUpdater::DownloadResult downloadResult = DictUpdater::downloadFromGitHub(
            downloadUrl,
            pathNarrow.c_str(),
            30
        );

        if (downloadResult.status == DictUpdater::DownloadStatus::Success) {
            fin.close();
            fin.open(pathNarrow, std::ios::in | std::ios::binary);
            if (!fin.is_open()) return;
        } else {
            return;
        }
    }

    WordPhraseCacheMetadata sourceMetadata = {};
    bool haveMetadata = getWordPhraseFileMetadata(path, sourceMetadata);
    if (haveMetadata && loadWordPhrasesCache(state, cachePath, sourceMetadata)) {
        if (state.phraseDictSize > 0) {
            Utils::updateStatus(state, L"載入詞語庫快取：" + std::to_wstring(state.phraseDictSize) + L" 個詞語組合");
        }
        return;
    }

    std::map<std::wstring, WordPhraseBuilderEntry> builder;
    size_t totalEntries = 0;
    int count = 0;
    int phraseLineIndex = 0;
    bool firstLine = true;
    std::string line;

    while (std::getline(fin, line)) {
        // 不再用 totalEntries 截斷迴圈；由 addWordPhraseCandidate 內部的 key 數上限守門

        if (!line.empty() && line.back() == '\r') line.pop_back();

        if (firstLine && line.length() >= 3 &&
            line[0] == static_cast<char>(0xEF) &&
            line[1] == static_cast<char>(0xBB) &&
            line[2] == static_cast<char>(0xBF)) {
            line = line.substr(3);
        }
        firstLine = false;

        trimAsciiInPlace(line);
        if (line.empty() || line[0] == '#' || line[0] == ';') continue;

        try {
            // 支援「詞語<TAB>權重」格式：例如「愛你一萬年\t20」可微調排序權重。
            std::string phraseText = line;
            std::string weightText;
            size_t tabPos = line.find('\t');
            if (tabPos != std::string::npos) {
                phraseText = line.substr(0, tabPos);
                weightText = line.substr(tabPos + 1);
                trimAsciiInPlace(phraseText);
                trimAsciiInPlace(weightText);
            }

            double explicitWeight = 0.0;
            if (!weightText.empty()) {
                try { explicitWeight = std::stod(weightText); }
                catch (...) { explicitWeight = 0.0; }
            }

            std::wstring phrase = Utils::utf8ToWstr(phraseText);
            if (phrase.length() < 2 || phrase.length() > 10) continue;

            // 行序加分：越前面的詞分數略高（1500 行內衰減一半）
            double orderBonus = 3.0 / (1.0 + phraseLineIndex * 0.0007);
            double lengthBonus = std::min(1.0, static_cast<double>(phrase.length()) * 0.1);
            double phraseScore = 5.0 + orderBonus + lengthBonus + explicitWeight;

            // 1) 相鄰兩字：char[i] → char[i+1]（各字指向下一字，用於單字聯想）
            for (size_t i = 0; i + 1 < phrase.length(); ++i) {
                addWordPhraseCandidate(builder, totalEntries,
                    phrase.substr(i, 1), phrase.substr(i + 1, 1),
                    phraseScore, count);
            }

            // 2) 多字前綴 → 後綴整段（前綴長度 2 到 MAX_WORD_PHRASE_PREFIX_LEN）
            // pfxLen=1 與鄰接對(1)重複（A→B），從 2 開始可節省大量索引空間。
            // 例：「香港理工大學」建立：
            //   「香港」   → 「理工大學」  （2字前綴）
            //   「香港理」 → 「工大學」    （3字前綴）
            //   「香港理工」→ 「大學」     （4字前綴）
            if (phrase.length() >= 3) {
                size_t maxPfx = std::min(MAX_WORD_PHRASE_PREFIX_LEN, phrase.length() - 1);
                for (size_t pfxLen = 2; pfxLen <= maxPfx; ++pfxLen) {
                    addWordPhraseCandidate(builder, totalEntries,
                        phrase.substr(0, pfxLen), phrase.substr(pfxLen),
                        phraseScore + static_cast<double>(pfxLen) * 0.5, count);
                }
            }

            ++phraseLineIndex;
        } catch (...) {
            continue;
        }
    }
    fin.close();

    commitWordPhraseBuilder(state, builder, count);

    if (count > 0 && haveMetadata) {
        saveWordPhrasesCache(state, cachePath, sourceMetadata);
    }

    if (count > 0) {
        Utils::updateStatus(state, L"載入詞語庫：" + std::to_wstring(count) + L" 個詞語組合");
    }
}

// 獲取聯想字候選列表（智能聯想引擎優化版）
void getWordPredictions(GlobalState& state, const std::wstring& word) {
    state.candidates.clear();
    state.candidateCodes.clear();
    // 記錄此次查詢的前文，供右鍵選單刪除/封鎖聯想時作為 prev key
    state.predictionQueryWord = word;
    
    if (word.empty()) return;
    
    // 多字詞時：詞語庫優先用完整前文查詢，再 fallback 到最後一字
    const std::wstring wordKey = (word.length() > 1) ? word.substr(word.length() - 1, 1) : word;
    
    // 候選字及其分數（使用 map 自動去重並合併分數）
    // key: 候選字/詞, value: 分數
    std::map<std::wstring, double> candidateScores;
    std::unordered_set<std::wstring> seenCandidates;  // 用於快速去重檢查
    std::unordered_set<std::wstring> fromWordPhrases;
    
    // 輔助函數：查找字碼（支援單字和多字詞，使用載入字典時建立的反查快取）
    auto findCode = [&state](const std::wstring& w) -> std::wstring {
        std::wstring key = (w.length() > 1) ? w.substr(0, 1) : w;
        auto it = state.charToCode.find(key);
        if (it != state.charToCode.end()) return it->second;
        return L"";
    };
    
    // ========== 來源一：contextLearning（個人上下文，優先級最高） ==========
    // 同時查詢「完整前文」與「最後一字」兩個 key，避免使用兩字前綴查詢時遺失單字級個人聯想。
    {
        std::vector<std::wstring> contextKeys;
        contextKeys.push_back(word);
        if (wordKey != word) contextKeys.push_back(wordKey);

        for (const auto& contextKey : contextKeys) {
            auto it = state.contextLearning.find(contextKey);
            if (it == state.contextLearning.end()) continue;
            const auto& contextMap = it->second;
            for (const auto& pair : contextMap) {
                const std::wstring& nextWord = pair.first;
                int count = pair.second;
                // blocked / locked / pinned 鍵仍以「實際查詢前文」為準，避免影響使用者既有設定。
                std::pair<std::wstring, std::wstring> keyPair(contextKey, nextWord);

                if (state.blockedContext.find(keyPair) != state.blockedContext.end()) {
                    continue;
                }

                double score = 0.0;
                if (state.lockedContext.find(keyPair) != state.lockedContext.end()) {
                    score = 1000000.0;
                } else if (state.pinnedContext.find(keyPair) != state.pinnedContext.end()) {
                    score = 10000.0 + count;
                } else {
                    const double baseBonus = 20.0;
                    score = count * 3.0 + baseBonus;
                }

                // 完整前文（contextKey == word）較精準，分數略高於最後一字 fallback。
                if (contextKey == word && contextKey != wordKey) {
                    score += 1.0;
                }

                if (candidateScores.find(nextWord) == candidateScores.end()) {
                    candidateScores[nextWord] = score;
                    seenCandidates.insert(nextWord);
                } else {
                    candidateScores[nextWord] = std::max(candidateScores[nextWord], score);
                }
            }
        }
    }
    
    // ========== 來源二：bigramIndex + wordPhrases（詞語庫，補充通用詞） ==========
    // 2.1 從 bigramIndex 查詢（O(1) 查表，多字前文時用最後一字）
    if (!wordKey.empty() && state.bigramIndex.find(wordKey[0]) != state.bigramIndex.end()) {
        const auto& bigramMap = state.bigramIndex.at(wordKey[0]);
        for (const auto& pair : bigramMap) {
            wchar_t nextChar = pair.first;
            int cooccurCount = pair.second;
            std::wstring nextWord(1, nextChar);
            std::pair<std::wstring, std::wstring> keyPair(word, nextWord);
            
            // blocked 條目：無論來源如何都直接略過
            if (state.blockedContext.find(keyPair) != state.blockedContext.end()) {
                continue;
            }
            
            // 靜態共現權重（低於個人學習分數）
            double score = cooccurCount * 0.5;  // 降低權重，避免覆蓋個人學習
            
            if (seenCandidates.find(nextWord) == seenCandidates.end()) {
                candidateScores[nextWord] = score;
                seenCandidates.insert(nextWord);
            } else {
                candidateScores[nextWord] = std::max(candidateScores[nextWord], score);
            }
        }
    }
    
    // 2.2 從 wordPhrases 查詢詞語庫；完整前文優先，沒有命中才用最後一字補充
    // fromFullPrefixMatch：來自「完整前文 key」命中的候選（排序時優先於 fallback 候選）
    std::unordered_set<std::wstring> fromFullPrefixMatch;
    if (state.phraseDictSize > 0) {
        std::vector<std::wstring> phraseKeys;
        phraseKeys.push_back(word);
        if (wordKey != word) {
            phraseKeys.push_back(wordKey);
        }

        for (const auto& phraseKey : phraseKeys) {
            auto phraseIt = state.wordPhrases.find(phraseKey);
            if (phraseIt == state.wordPhrases.end()) continue;

            const auto& phrases = phraseIt->second;
            for (const auto& phraseChar : phrases) {
                std::pair<std::wstring, std::wstring> keyPair(word, phraseChar);
                if (state.blockedContext.find(keyPair) != state.blockedContext.end()) {
                    continue;
                }

                double score = 5.0;
                auto scoreKeyIt = state.wordPhraseScores.find(phraseKey);
                if (scoreKeyIt != state.wordPhraseScores.end()) {
                    auto scoreIt = scoreKeyIt->second.find(phraseChar);
                    if (scoreIt != scoreKeyIt->second.end()) {
                        score = scoreIt->second;
                    }
                }
                if (phraseKey == word) {
                    score += 2.0;  // 完整前文命中較準
                    fromFullPrefixMatch.insert(phraseChar);  // 標記為全前綴命中
                }

                fromWordPhrases.insert(phraseChar);
                if (seenCandidates.find(phraseChar) == seenCandidates.end()) {
                    candidateScores[phraseChar] = score;
                    seenCandidates.insert(phraseChar);
                } else {
                    candidateScores[phraseChar] = std::max(candidateScores[phraseChar], score);
                }
            }
        }
    }
    
    // ========== 來源三：wordFreq（兜底，確保候選字不為空） ==========
    if (candidateScores.size() < 5) {
        // 從 wordFreq 補入高頻單字
        std::vector<std::pair<std::wstring, int>> freqWords;
        for (const auto& pair : state.wordFreq) {
            // 只考慮單字，且未被加入候選列表
            if (pair.first.length() == 1 && seenCandidates.find(pair.first) == seenCandidates.end()) {
                std::pair<std::wstring, std::wstring> keyPair(word, pair.first);
                if (state.blockedContext.find(keyPair) != state.blockedContext.end()) {
                    continue;
                }
                freqWords.push_back(std::make_pair(pair.first, pair.second.frequency));
            }
        }
        std::sort(freqWords.begin(), freqWords.end(),
            [](const std::pair<std::wstring, int>& a, const std::pair<std::wstring, int>& b) {
                return a.second > b.second;
            });
        
        // 補入最多 5 個高頻字
        int maxSupplement = 5 - (int)candidateScores.size();
        for (int i = 0; i < maxSupplement && i < (int)freqWords.size(); i++) {
            const std::wstring& freqWord = freqWords[i].first;
            double score = freqWords[i].second * 0.1;  // 兜底分數較低
            candidateScores[freqWord] = score;
            seenCandidates.insert(freqWord);
        }
    }
    
    // ========== 按分數排序並生成候選列表 ==========
    std::vector<std::pair<std::wstring, double>> sortedCandidates;
    for (const auto& pair : candidateScores) {
        sortedCandidates.push_back(std::make_pair(pair.first, pair.second));
    }

    // 個人學習（contextLearning）候選集合，用於排序優先判斷
    std::unordered_set<std::wstring> fromContextLearning;
    if (state.contextLearning.find(word) != state.contextLearning.end()) {
        for (const auto& p : state.contextLearning.at(word)) {
            fromContextLearning.insert(p.first);
        }
    }
    if (wordKey != word && state.contextLearning.find(wordKey) != state.contextLearning.end()) {
        for (const auto& p : state.contextLearning.at(wordKey)) {
            fromContextLearning.insert(p.first);
        }
    }

    // ========== 排序規則（優先順序由高到低） ==========
    // 1. locked⚑ / pinned⚐（score≥10000）：按分數，永遠最前
    // 2. 個人學習（contextLearning）：按分數
    // 3. 全前綴命中（fromFullPrefixMatch）：用完整前文 key 查到的詞語庫候選
    //    → 以「等效字數」升序：若有多筆命中，字數>5 的接續加微小懲罰，避免幾條超長後綴占滿欄
    //    → 僅 1 筆全前綴命中時不懲罰（長詞仍可顯示）
    //    → 等效字數相同再按分數
    // 4. 其他候選（bigramIndex / fallback wordPhrases / wordFreq）：
    //    → 與前文字數相同或更長的排前面，比前文短的沉到最後；字數相同按分數
    {
        size_t prefixLen = word.length();
        const size_t fullPrefixN = fromFullPrefixMatch.size();
        // 字長 >5 時每多一字增加 0.12 的「等效長度」，僅在 fullPrefixN>1 時啟用
        auto effFullKey = [fullPrefixN](size_t len) -> double {
            if (fullPrefixN <= 1) return static_cast<double>(len);
            if (len <= 5) return static_cast<double>(len);
            return static_cast<double>(len) + 0.12 * static_cast<double>(len - 5);
        };
        auto lengthKey = [prefixLen](size_t candLen) -> int {
            if (candLen >= prefixLen) return static_cast<int>(candLen);
            return 10000 + static_cast<int>(prefixLen - candLen);
        };

        std::sort(sortedCandidates.begin(), sortedCandidates.end(),
            [&fromContextLearning, &fromFullPrefixMatch, &lengthKey, &effFullKey]
            (const std::pair<std::wstring, double>& a,
             const std::pair<std::wstring, double>& b) {
                double sa = a.second, sb = b.second;

                // 第一層：locked / pinned
                bool aHigh = (sa >= 10000.0), bHigh = (sb >= 10000.0);
                if (aHigh != bHigh) return aHigh > bHigh;
                if (aHigh && bHigh) return sa > sb;

                // 第二層：個人學習
                bool aCtx = (fromContextLearning.count(a.first) > 0);
                bool bCtx = (fromContextLearning.count(b.first) > 0);
                if (aCtx != bCtx) return aCtx > bCtx;
                if (aCtx && bCtx) return sa > sb;

                // 第三層：全前綴命中詞語庫
                bool aFull = (fromFullPrefixMatch.count(a.first) > 0);
                bool bFull = (fromFullPrefixMatch.count(b.first) > 0);
                if (aFull != bFull) return aFull > bFull;
                if (aFull && bFull) {
                    const size_t la = a.first.length(), lb = b.first.length();
                    const double ea = effFullKey(la);
                    const double eb = effFullKey(lb);
                    if (ea < eb) return true;
                    if (ea > eb) return false;
                    return sa > sb;
                }

                // 第四層：其他候選（按前文字數對齊）
                int ka = lengthKey(a.first.length());
                int kb = lengthKey(b.first.length());
                if (ka != kb) return ka < kb;
                return sa > sb;
            });
    }
    
    // 限制聯想字數量（可由 interfaceconfig.ini 的 max_word_predictions 自訂，預設 100）
    int configuredMax = state.maxWordPredictions;
    if (configuredMax < 1) configuredMax = 1;
    if (configuredMax > 1000) configuredMax = 1000;
    size_t maxPredictions = static_cast<size_t>(configuredMax);
    for (size_t i = 0; i < sortedCandidates.size() && i < maxPredictions; i++) {
        const std::wstring& candidate = sortedCandidates[i].first;
        state.candidates.push_back(candidate);
        
        // 聯想字清單優先顯示狀態符號（⚑/⚐/聯想/詞語/常用），否則才顯示筆劃字碼
        double score = sortedCandidates[i].second;
        std::wstring code;
        if (score >= 1000000.0) {
            code = L"⚑";   // locked 永遠置頂
        } else if (score >= 10000.0) {
            code = L"⚐";   // pinned 置頂+加分
        } else if ((state.contextLearning.find(word) != state.contextLearning.end() &&
                    state.contextLearning.at(word).find(candidate) != state.contextLearning.at(word).end()) ||
                   (wordKey != word &&
                    state.contextLearning.find(wordKey) != state.contextLearning.end() &&
                    state.contextLearning.at(wordKey).find(candidate) != state.contextLearning.at(wordKey).end())) {
            code = L"聯想";
        } else if (fromWordPhrases.find(candidate) != fromWordPhrases.end()) {
            code = L"詞語";
        } else {
            code = L"常用";
        }
        // 若無特殊標記且可查得筆劃字碼，則顯示字碼（僅在 showCandidateCode 時由視窗決定是否顯示）
        if (code == L"常用") {
            std::wstring strokeCode = findCode(candidate);
            if (!strokeCode.empty()) code = strokeCode;
        }
        state.candidateCodes.push_back(code);
    }
}

// 選擇字後顯示聯想字
void showPredictionsAfterSelection(GlobalState& state, const std::wstring& selected) {
    if (!state.enableWordPrediction) return;
    if (selected.empty()) return;
    if (Utils::isPunctuation(selected)) return;  // 標點符號不觸發聯想

    // 以 lastContext（最多4字的連續前文）作為查詢前綴，讓詞庫多字索引（如「香港理」→「工大學」）命中。
    // lastContext 由 learnWord 維護，末尾一定是剛選的字（selected）。
    // getWordPredictions 內部仍會 fallback 到最後一字，個人聯想/共現不會遺失。
    std::wstring queryWord = selected;
    if (selected.length() == 1 &&
        state.lastContext.length() >= 2 &&
        !state.lastContext.empty() &&
        state.lastContext.back() == selected[0] &&
        !Utils::isPunctuation(state.lastContext.substr(0, 1))) {
        queryWord = state.lastContext;
    }

    getWordPredictions(state, queryWord);
    
    if (state.candidates.empty()) {
        // 沒有聯想字：隱藏聯想字視窗即可（字碼候選由 updateCandidates 管理）
        if (state.hPredWnd) ShowWindow(state.hPredWnd, SW_HIDE);
        // 沒有聯想時回到一般候選或空閒狀態，由外層邏輯決定
        state.isPredictionMode = false;
        return;
    }
    
    // 有聯想字，顯示聯想視窗（hPredWnd）
    state.selected = 0;
    state.currentPage = 0;
    state.totalPages = (state.candidates.size() + CANDIDATES_PER_PAGE - 1) / CANDIDATES_PER_PAGE;
    state.showCand = true;
    state.isInputting = true;  // 保持輸入狀態，以便繼續選擇聯想字
    state.isPredictionMode = true;
    // 預設不啟用任何候選字的 hover，等滑鼠移入時再設定
    state.hoverCandidateIndex = -1;
    
    // 聯想字視窗定位與顯示：切換到聯想模式，由模式控制視窗顯示
    WindowManager::switchMode(state, InputMode::PRED_MODE);
    WindowManager::positionWindowsOptimized(state);
    if (state.hPredWnd) {
        InvalidateRect(state.hPredWnd, nullptr, FALSE);
    }
}

} // namespace Dictionary