# ChineseStrokeIME_發布頁面
免安裝、即開即用的中文筆劃輸入法
<br>
<br>
# 下載最新版本的ChineseStrokeIME和原始碼
### 2026年3月最新版本： 中文筆劃輸入法 V3.1.0
💾下載：https://github.com/Yamazaki427858/ChineseStrokeIME_Releases/releases
<br>
<br>
<br>
<br>
📖詳細介紹頁：https://github.com/Yamazaki427858/ChineseStrokeIME
<br>
📮發問題意見：https://github.com/Yamazaki427858/ChineseStrokeIME/issues
<br>
<br>
⌨️操作方式：鍵盤UIOJK 和 數字鍵盤78945 對應五種基本筆劃（一丨丿丶フ）
<br>
<br>
[![CodeQL](https://github.com/Yamazaki427858/ChineseStrokeIME_Releases/actions/workflows/github-code-scanning/codeql/badge.svg)](https://github.com/Yamazaki427858/ChineseStrokeIME_Releases/actions/workflows/github-code-scanning/codeql)
[![Latest Release](https://img.shields.io/github/v/release/Yamazaki427858/ChineseStrokeIME_Releases)](https://github.com/Yamazaki427858/ChineseStrokeIME_Releases/releases/latest)
[![Downloads](https://img.shields.io/github/downloads/Yamazaki427858/ChineseStrokeIME_Releases/total)](https://github.com/Yamazaki427858/ChineseStrokeIME_Releases/releases)
<br>
<br>

## ✨ V3.1.0 重大更新

## 🔮 聯想引擎強化
v3.1.0 主要強化了**智能聯想引擎**相關功能，讓「下一個字」的推薦更貼近你的使用習慣：

- **📈 更穩定的聯想排序**：結合字碼表內建的相鄰字對與個人使用記錄，候選順序更合理。
- **⚑⚐ 置頂與永遠置頂**：可對聯想條目設為「置頂 (pinned)」或「永遠置頂 (locked)」，常用搭配會固定在前方。
- **📝 右鍵管理**：在候選字／聯想字上按右鍵即可設定置頂、永遠置頂、刪除此聯想或永不再顯示，操作直覺。
- **💾 個人學習檔**：聯想記錄寫入 `user\context_learning.txt`，重啟後仍會保留你的設定與學習結果。

## 🔧 小問題優化與修復

- **📁 整齊的目錄結構**  
  程式以 exe 所在目錄為根目錄，自動使用並建立 **`system\`**（字碼表、詞語庫等）與 **`user\`**（使用者字典、聯想學習、介面設定、標點選單等）；建議將程式放在英文路徑下，避免中文或特殊字元造成讀寫異常。

- **一丨丿丶フ 筆劃符號開關**  
  可在設定或選單中切換「輸入框顯示筆劃符號（一丨丿丶フ）」或「英文字母（u i o j k）」，依個人習慣選擇。

- **🎯 自訂 3+3 萬用字元按鍵**  
  萬用字元 `*` 的觸發鍵可在 `user\interfaceconfig.ini` 的 `[InputSettings]` 中自訂（如 `wildcardKey1`、`wildcardKey2`），預設為 **L** 與 **NumPad0**，方便不同鍵盤配置。

- **⌨️ 按標點符號清除視窗**  
  輸入標點後會一併清空字碼與候選視窗，避免殘留上一輪的候選，畫面更乾淨。

- 修復了一些Bug

<br>
<br>
