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
        display = L"標點符號選單";
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
    if (Utils::isPunctuation(word)) return;
    if (word.empty()) return;
    
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
    
    // 更新最後選字
    state.lastSelected = word;
}

void loadMainDict(GlobalState& state) {
    state.dict.clear();
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
                    // 過濾掉極小次數的雜訊紀錄（例如只出現 1 次）
                    if (nextPair.second > state.contextLearningMinAutoCount) {
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
        learnWord(state, selected);
        // 延遲保存用戶字典（使用定時器，避免頻繁寫入文件）
        if (state.hWnd) {
            KillTimer(state.hWnd, 995);  // 先清除舊的定時器（使用995避免衝突）
            SetTimer(state.hWnd, 995, 2000, NULL);  // 2秒後保存
        }
    }
    
    // 如果是標點符號選單，直接結束
    if (state.showPunctMenu) {
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

// 載入詞語庫文件
void loadWordPhrases(GlobalState& state) {
    state.wordPhrases.clear();
    state.phraseDictSize = 0;
    
    std::wstring path = state.systemDir + L"wordphrases.txt";
    std::string pathNarrow = Utils::wstrToUtf8(path);
    std::ifstream fin(pathNarrow, std::ios::in | std::ios::binary);
    if (!fin.is_open()) {
        // 文件不存在，嘗試從GitHub自動下載（靜默下載，不顯示提示）
        const char* downloadUrl = 
            "https://raw.githubusercontent.com/Yamazaki427858/ChineseStrokeIME/ChineseStrokeIME/SourceCode/%E8%81%AF%E6%83%B3%E8%A9%9E%E5%BA%AB/word_phrases.txt";
        
        DictUpdater::DownloadResult downloadResult = DictUpdater::downloadFromGitHub(
            downloadUrl, 
            pathNarrow.c_str(),
            30  // 30秒超時
        );
        
        if (downloadResult.status == DictUpdater::DownloadStatus::Success) {
            fin.close();
            fin.open(pathNarrow, std::ios::in | std::ios::binary);
            if (!fin.is_open()) {
                // 下載成功但無法打開文件（不應該發生）
                return;
            }
        } else {
            // 下載失敗，靜默返回（不顯示錯誤）
            return;
        }
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
        if (line.empty() || line[0] == '#' || line[0] == ';') continue;
        
        // 轉換為寬字符
        try {
            std::wstring phrase = Utils::utf8ToWstr(line);
            // 支持2字以上的詞語（不限制最大長度，但建議不超過10字以保持性能）
            if (phrase.length() >= 2 && phrase.length() <= 10) {
                // 為詞語中的每個字（除了最後一個）建立到下一個字的映射
                // 例如「電腦系統管理」會建立：
                // 「電」 → 「腦」
                // 「腦」 → 「系」
                // 「系」 → 「統」
                // 「統」 → 「管」
                // 「管」 → 「理」
                // 這樣可以支持連續聯想：電→腦→系→統→管→理
                for (size_t i = 0; i < phrase.length() - 1; i++) {
                    std::wstring currentChar = phrase.substr(i, 1);
                    std::wstring nextChar = phrase.substr(i + 1, 1);
                    
                    // 檢查是否已存在
                    bool exists = false;
                    if (state.wordPhrases.find(currentChar) != state.wordPhrases.end()) {
                        const auto& existing = state.wordPhrases[currentChar];
                        if (std::find(existing.begin(), existing.end(), nextChar) != existing.end()) {
                            exists = true;
                        }
                    }
                    if (!exists) {
                        state.wordPhrases[currentChar].push_back(nextChar);
                        count++;
                    }
                }
                // 支援至少兩字後續的聯想選擇：詞語長度≥3 時，將「首字→後續 2 字以上」加入候選
                // 例如「高登仔」→ 高→「登仔」、「電腦系統」→ 電→「腦系統」
                if (phrase.length() >= 3) {
                    std::wstring firstChar = phrase.substr(0, 1);
                    std::wstring continuation = phrase.substr(1);  // 長度 ≥ 2
                    bool exists = false;
                    if (state.wordPhrases.find(firstChar) != state.wordPhrases.end()) {
                        const auto& existing = state.wordPhrases[firstChar];
                        if (std::find(existing.begin(), existing.end(), continuation) != existing.end()) {
                            exists = true;
                        }
                    }
                    if (!exists) {
                        state.wordPhrases[firstChar].push_back(continuation);
                        count++;
                    }
                }
            }
        } catch (...) {
            // 轉換失敗，跳過這行
            continue;
        }
    }
    
    state.phraseDictSize = count;
    if (count > 0) {
        Utils::updateStatus(state, L"載入詞語庫：" + std::to_wstring(count) + L" 個詞語組合");
    }
}

// 獲取聯想字候選列表（智能聯想引擎優化版）
void getWordPredictions(GlobalState& state, const std::wstring& word) {
    state.candidates.clear();
    state.candidateCodes.clear();
    
    if (word.empty()) return;
    
    // 多字詞時：個人聯想用完整前文 word，詞語庫/共現表用最後一字查詢
    const std::wstring wordKey = (word.length() > 1) ? word.substr(word.length() - 1, 1) : word;
    
    // 候選字及其分數（使用 map 自動去重並合併分數）
    // key: 候選字/詞, value: 分數
    std::map<std::wstring, double> candidateScores;
    std::unordered_set<std::wstring> seenCandidates;  // 用於快速去重檢查
    
    // 輔助函數：查找字碼（支援單字和多字詞）
    auto findCode = [&state](const std::wstring& w) -> std::wstring {
        // 如果是單字，直接查找
        if (w.length() == 1) {
            for (const auto& pair : state.dict) {
                for (const auto& dictWord : pair.second) {
                    if (dictWord == w) {
                        return pair.first;
                    }
                }
            }
        } else {
            // 如果是多字詞，查找第一個字的字碼
            std::wstring firstChar = w.substr(0, 1);
            for (const auto& pair : state.dict) {
                for (const auto& dictWord : pair.second) {
                    if (dictWord == firstChar) {
                        return pair.first;
                    }
                }
            }
        }
        return L"";
    };
    
    // ========== 來源一：contextLearning（個人上下文，優先級最高） ==========
    if (state.contextLearning.find(word) != state.contextLearning.end()) {
        const auto& contextMap = state.contextLearning.at(word);
        for (const auto& pair : contextMap) {
            const std::wstring& nextWord = pair.first;
            int count = pair.second;
            std::pair<std::wstring, std::wstring> keyPair(word, nextWord);
            
            // blocked 條目：無論來源如何都直接略過
            if (state.blockedContext.find(keyPair) != state.blockedContext.end()) {
                continue;
            }
            
            double score = 0.0;
            // locked → score = ∞（使用非常大的數值）
            if (state.lockedContext.find(keyPair) != state.lockedContext.end()) {
                score = 1000000.0;  // 永遠置頂
            }
            // pinned → 置頂+加分，分數須恆高於自動學習
            else if (state.pinnedContext.find(keyPair) != state.pinnedContext.end()) {
                score = 10000.0 + count;  // 確保高於任何自動學習分數
            }
            // 自動學習 → score = count * 3.0 + baseBonus
            else {
                const double baseBonus = 20.0;
                score = count * 3.0 + baseBonus;
            }
            
            // 去重規則：合併分數（取最大值）
            if (candidateScores.find(nextWord) == candidateScores.end()) {
                candidateScores[nextWord] = score;
                seenCandidates.insert(nextWord);
            } else {
                candidateScores[nextWord] = std::max(candidateScores[nextWord], score);
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
    
    // 2.2 從 wordPhrases 查詢詞語庫（多字前文時用最後一字）
    if (state.phraseDictSize > 0 && state.wordPhrases.find(wordKey) != state.wordPhrases.end()) {
        const auto& phrases = state.wordPhrases.at(wordKey);
        for (const auto& phraseChar : phrases) {
            std::pair<std::wstring, std::wstring> keyPair(word, phraseChar);
            if (state.blockedContext.find(keyPair) != state.blockedContext.end()) {
                continue;
            }
            // 靜態共現權重（低於個人學習分數）
            double score = 5.0;  // 詞語庫基礎分數
            
            if (seenCandidates.find(phraseChar) == seenCandidates.end()) {
                candidateScores[phraseChar] = score;
                seenCandidates.insert(phraseChar);
            } else {
                candidateScores[phraseChar] = std::max(candidateScores[phraseChar], score);
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
    
    // 優先顯示 context_learning.txt（個人聯想）的候選：來自個人聯想的排在最前，其餘按分數
    std::unordered_set<std::wstring> fromContextLearning;
    if (state.contextLearning.find(word) != state.contextLearning.end()) {
        for (const auto& p : state.contextLearning.at(word)) {
            fromContextLearning.insert(p.first);
        }
    }
    std::sort(sortedCandidates.begin(), sortedCandidates.end(),
        [&fromContextLearning](const std::pair<std::wstring, double>& a, const std::pair<std::wstring, double>& b) {
            bool aFrom = (fromContextLearning.find(a.first) != fromContextLearning.end());
            bool bFrom = (fromContextLearning.find(b.first) != fromContextLearning.end());
            if (aFrom && !bFrom) return true;
            if (!aFrom && bFrom) return false;
            return a.second > b.second;
        });
    
    // 限制聯想字數量（最多20個）
    size_t maxPredictions = 20;
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
        } else if (state.contextLearning.find(word) != state.contextLearning.end() &&
                   state.contextLearning.at(word).find(candidate) != state.contextLearning.at(word).end()) {
            code = L"聯想";
        } else if (state.wordPhrases.find(wordKey) != state.wordPhrases.end()) {
            const auto& phrases = state.wordPhrases.at(wordKey);
            if (std::find(phrases.begin(), phrases.end(), candidate) != phrases.end()) {
                code = L"詞語";
            } else {
                code = L"常用";
            }
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
    
    // 獲取聯想字（多字詞時 getWordPredictions 內部會以最後一字查詞語庫/共現，前文仍用完整詞供個人聯想）
    getWordPredictions(state, selected);
    
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