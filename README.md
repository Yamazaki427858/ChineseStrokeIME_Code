# ChineseStrokeIME_發布頁面
免安裝、即開即用的中文筆劃輸入法
<br>
<br>
# 下載最新版本的ChineseStrokeIME和原始碼
### 2026年5月最新版本： 中文筆劃輸入法 V3.2.0 
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

## ✨ V3.3.0 重大更新

## 🖥 修復多螢幕相容問題

本版強化延伸桌面、鏡像模式與多螢幕熱插拔下的穩定性：

- **支援 3 螢幕以上**：即時偵測螢幕數量變更（例如 4→3、3→2），工具列若落在已移除的螢幕上，會自動移回安全位置 ✅
- **延伸 ↔ 同步切換**：從多螢幕延伸改為單螢幕同步時，工具列會自動移至主螢幕，避免視窗「消失」在無效座標 🔄
- **依螢幕定位**：候選字、字碼視窗等 UI 改以各螢幕的**工作區**計算位置，減少跨螢幕時偏移或超出可視範圍的情況
- **位置記憶分模式**：延伸模式與鏡像模式分別記住工具列位置，切換顯示配置後較易回到習慣的擺放處

> 若希望在螢幕模式變更時彈出提示，可在 `user\interfaceconfig.ini` 的 `[WindowBehavior]` 將 `showScreenModeNotification=1`。

---

## 🗑 刪除指定候選字

可自行整理**用戶字典**中不需要的候選字：

- 在**字碼候選模式**下，於候選字視窗對目標字**按右鍵**
- 選單顯示「用戶字典：〇〇」，點選 **「刪除此候選字」** 即可從 `user\user_dict.txt` 移除
- 僅對已加入用戶字典的字有效；非用戶自訂候選時，該選項會呈灰色無法使用
- 刪除後候選清單會立即更新，無需重啟程式

適合清除誤加入、重複或不再需要的個人候選，讓選字清單更乾淨。

---

## 😀 Emoji 輸入

新增 Emoji 選單，方便在一般輸入與暫放模式中插入表情符號：

- 按 **`P` 鍵**開啟標點／Emoji 選單，可切換至 **Emoji 分頁**
- 以**分類標籤**瀏覽（如表情、手勢、動物等），支援分頁與滑鼠點選
- 選取後直接插入目前輸入位置；選單保持開啟，可連續選多個，按 **ESC** 關閉
- Emoji 資料存放於 `user\emoji\`；首次使用若本地無資料，程式會嘗試從 GitHub 自動下載
- 亦可從托盤或工具列選單選 **「從 GitHub 更新 Emoji」** 手動更新

---

## 🔔 版本更新提醒：「不再提醒」

啟動約 3 秒後，程式會自動比對 GitHub 上的最新版本。若偵測到新版本，會在**螢幕中央**彈出通知，提供三個選項：

| 按鈕 | 說明 |
|------|------|
| **前往下載** | 開啟 GitHub 專案頁下載新版 |
| **稍後** | 關閉對話框；下次啟動仍會提醒 |
| **不再提醒** | 關閉對話框，之後啟動**不再**自動彈出版本通知 |

選「不再提醒」後，設定會寫入 `user\interfaceconfig.ini`：

```ini
[WindowBehavior]
; 0=仍提醒（預設），1=啟動時不再彈出版本更新通知
suppress_version_update_reminder=1
```

改回 `0` 並重啟輸入法，即可恢復自動提醒。（可能需要刪除舊interfaceconfig.ini才能生效。）

> **補充**：「關於」對話框內的**手動檢查更新**不受此設定影響，隨時可主動查詢是否有新版本。

---

---

# v3.2.0 功能介紹


---

## 📚 大型聯想詞語庫

輸入法內建**詞語聯想**功能：選完一個字後，可根據前文從大型詞語庫中推薦後續用字（例如「香港」→「貿易」「特區」等），與個人學習紀錄（`user\context_learning.txt`）互補，讓連續造句更順手。

**詞語庫檔案**

| 項目 | 說明 |
|------|------|
| 本地路徑 | `system\wordphrases.txt`（與 exe 同層的 `system` 資料夾） |
| 格式 | 每行一個詞語（約 2～10 字，UTF-8）；以 `#` 或 `;` 開頭為註解 |
| 可選權重 | 支援 `詞語<TAB>數字` 微調排序，例如 `愛你一萬年\t20` |
| 快取 | 首次載入或更新 txt 後會產生 `system\wordphrases.cache`（勿手動編輯） |



**下載與更新詞語庫**

- 官方詞庫目錄（GitHub）：  
  https://github.com/Yamazaki427858/ChineseStrokeIME/tree/ChineseStrokeIME/SourceCode/%E8%81%AF%E6%83%B3%E8%A9%9E%E5%BA%AB
- 請下載其中的 **`word_phrases.txt`**，覆蓋或另存為本地 **`system\wordphrases.txt`**
- 若本地尚無此檔且網路可用，程式啟動時也會嘗試從 GitHub 自動下載（檔名仍存為 `wordphrases.txt`）
- 更新 txt 後**重啟輸入法**即可；若快取與新檔不相容，程式會自動刪除舊 `wordphrases.cache` 並重建

---

## 🚀 詞語庫聯想加速（wordphrases.cache）

針對大型 `wordphrases.txt` 做啟動加速：

- **第一次啟動**：讀取 `system\wordphrases.txt`，建立索引並產生 `system\wordphrases.cache`
- **之後啟動**：優先讀取 `system\wordphrases.cache`，通常比每次重新解析 txt 更快 ⚡
- 快取內建版本辨識；若版本不相容或驗證失敗，會刪除舊快取並從 txt 重建（自我修復）🔧

> 多字前綴索引以「唯一 key 數量」作為安全上限，前綴長度 2～9 字，與連續前文窗口對齊。

---

## 🛡️ 快取更穩：原子寫入避免半截壞檔

重建 `wordphrases.cache` 時採用「原子寫入」：

- 先寫入 `wordphrases.cache.tmp`
- 完整寫入成功後，再一次性替換成正式的 `wordphrases.cache`

即使遇到當機、強制關閉、磁碟空間不足等狀況 💥，也不易留下寫到一半的快取 ✅

---

## 💞 多字前綴聯想

- 除「相鄰字」聯想外，詞語庫建立 **2～9 字前綴 → 剩餘片段** 的索引
- 以 **`lastContext`（最多 9 字滾動前文）** 作為查詢前綴，連續選字可命中較長片語的延伸（例如：愛你 → 一萬年）
- 排序區分「完整前文命中的詞庫接續」與「僅用最後一字的補位」，減少清單被不相干長串佔滿

---

## 🧠 個人學習與顯示（context_learning.txt）

- 同時以 **「完整前文」** 與 **「最後一字」** 查個人學習紀錄
- **第 1 次**出現的關聯即會寫入 `user\context_learning.txt`；次數用於排序權重
- 聯想候選的 **「聯想 / 詞語 / 常用」** 等標籤與排序邏輯已對齊
- 編輯或刪除 `context_learning.txt` 前請**關閉程式**，避免定時寫入覆蓋手動修改

---

## 🖱 聯想字右鍵：刪除／封鎖用對「前文 → 候選」

- 刪除、置頂、封鎖等操作以 **本輪聯想查詢的前文** 作為前字，避免選完字後刪錯關聯

---

## ↩ 選錯聯想可「反學習」

- 在筆劃或聯想選字後，於聯想視窗使用 **Backspace** 可扣回剛學的個人關聯、還原前文狀態（必要時刪上屏一個字）

---

## ⛔ 何時中斷「聯想前文」

以下情況會清空聯想前文（語境斷開）：

- **ESC**（取消輸入）
- **任何標點**
- **切換中／英輸入模式**
- **Enter**（含暫放送出等）
- **退格**在筆劃緩衝已空、且非走「聯想窗撤銷」邏輯時

---

## ⚙ 聯想候選數可在設定檔自訂（預設 100）

```ini
[WindowBehavior]
max_word_predictions=100
```

建議依螢幕與習慣在 **1～1000** 之間調整。

---

## ⌨ 筆劃符號與字碼顯示

可切換輸入框與候選字的顯示方式，方便對照字根或查碼：

**輸入框筆劃符號**（`showStrokeSymbols`，預設開啟）

- **開啟**：字碼視窗顯示 **一丨丿丶フ** 等筆劃符號
- **關閉**：改顯示英文字母 **uiojk**
- 可從工具列右鍵選單切換「筆劃符號：開／關」

**候選字英文字碼**（`showCandidateCode`，預設關閉）

- **開啟**：候選列附帶字根，例如 `3. 十[ui]`
- **關閉**：僅顯示候選字本身

```ini
[WindowBehavior]
showStrokeSymbols=1
showCandidateCode=0
```

---

## 🎹 自訂筆劃五鍵

若預設 **U I O J K**（或數字小鍵盤 **78945**）不符合鍵盤配置，可在設定檔自訂：

- **字母筆劃五鍵**：`enableCustomStrokeKeys=1` 後，以 `strokeKeyU`～`strokeKeyK` 指定（支援 A–Z）
- **數字小鍵盤筆劃五鍵**：`enableCustomNumpadStrokeKeys=1` 後，以 `numpadStrokeKeyU`～`numpadStrokeKeyK` 指定（僅 NumPad0–9）
- 兩組設定**分開生效**，可只改其中一組
- 工具列右鍵選單「自訂筆劃五鍵」可快速開關，變更會寫入 `interfaceconfig.ini`

```ini
[InputSettings]
enableCustomStrokeKeys=0
strokeKeyU=U
strokeKeyI=I
strokeKeyO=O
strokeKeyJ=J
strokeKeyK=K
enableCustomNumpadStrokeKeys=0
numpadStrokeKeyU=NumPad7
numpadStrokeKeyI=NumPad8
numpadStrokeKeyO=NumPad9
numpadStrokeKeyJ=NumPad4
numpadStrokeKeyK=NumPad5
```

---

## 📐 縮小顯示工具列

工具列可切換為**精簡模式**（`toolbarClassicModeBadges=1`）：

- 僅保留 **「劃」** 與 **「E」** 兩個按鈕，占用螢幕空間更小
- 在精簡列上**按右鍵**仍可開啟完整選單（設定、更新、關於等）
- 可從工具列右鍵選單切換「縮小顯示工作列」

適合希望工具列低調、不遮擋工作區的使用者。

---

<br>
<br>
