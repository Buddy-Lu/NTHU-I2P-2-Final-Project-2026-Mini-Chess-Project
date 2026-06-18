# MiniChess 引擎 — 學習筆記（中文）

以 function 為單位，逐一講解四個核心檔案做的所有功能：
`pvs.hpp`、`pvs.cpp`、`state.hpp`、`state.cpp`。

> 遊戲：MiniChess — 6×5 棋盤（`BOARD_H=6`、`BOARD_W=5`），勝負以**吃到對方國王**判定
> （不是將死）。兵只能升變成皇后。棋子代碼：`1=兵 2=城堡 3=騎士 4=主教 5=皇后 6=國王`。
> 玩家 `0` = 白方（往上走，朝 row 0），玩家 `1` = 黑方（往下走，朝 `BOARD_H-1`）。
>
> 分數採 **negamax（負極大值）** 慣例：每個節點都回傳「以目前該走的一方視角」的分數，
> 父節點對子節點取負（`-eval_ctx(...)`）。

---

## 第一部分 — `pvs.hpp`（介面宣告）

標頭檔宣告 `PVS` 類別並說明核心想法。

### PVS 的核心想法（註解區）
普通的 alpha-beta 對「每一步」都用完整的 `[alpha, beta]` 視窗搜尋。**PVS**
（主變例搜尋）假設排序後的**第一步**就是主變例（最好的一步），用完整視窗搜它；
之後的**每一步**先用 **null window（零寬視窗）** `[alpha, alpha+1]` 試探 —— 這只是個
便宜的「這步有沒有可能超過 alpha？」是非題，剪枝速度快很多。只有當試探**出乎意料**
（`alpha < score < beta`）時，才花成本用完整視窗重搜。因為好的排序讓大多數試探都
fail low，PVS 在相同深度下走訪的節點比 alpha-beta 少，而且結果完全相同。

### `static int eval_ctx(...)`
內部節點的遞迴 negamax 搜尋。參數：
- `state` — 要搜的盤面；`depth` — 剩餘層數；`history` — 重複盤面堆疊；
- `ply` — 距離根節點的層數（用於將軍分數與 killer 索引）；
- `ctx` — 節點計數 / 停止旗標 / 根節點更新回呼；
- `alpha, beta` — 搜尋視窗；
- `p` — `ABParams`，功能開關 + 數值旋鈕的集合；
- `null_ok` — 此處是否允許 null-move 剪枝（在 null 搜尋內設為 false，禁止連續兩次「跳過」）。

### `static SearchResult search(...)`
**根節點**進入點。執行一次固定深度的搜尋，回傳最佳步 + 分數 + 統計。
（疊代加深與 2 秒/步的時間控制在上一層 UBGI 協定程式碼裡，由它用遞增的 `depth` 反覆呼叫。）

### `static ParamMap default_params()` / `static std::vector<ParamDef> param_defs()`
向 UCI/UBGI 層宣告引擎的可調參數（可用 `setoption` 做 A/B 測試而不必重新編譯）。
預設值會「完全重現」內建引擎。

---

## 第二部分 — `pvs.cpp`（實作）

檔案結構：**(A)** 匿名 namespace 的搜尋基礎設施（轉置表、killer/history、輔助函式），
接著 **(B)** 三個 `PVS` 公開方法。

### A. 轉置表（Transposition Table, TT）

以盤面 Zobrist 雜湊為鍵的快取，避免重搜「不同走法順序到達同一盤面」（即 transposition）。
每筆還存下找到的最佳步，這是我們手上最強的排序訊號。

- **`enum TTFlag`** — `TT_NONE / TT_EXACT / TT_LOWER / TT_UPPER`。標示存的分數是精確值、
  下界（發生過 beta cutoff）或上界（沒有任何一步超過 alpha）。
- **`struct TTEntry`** — 一格：完整 64-bit `key`（偵測雜湊碰撞）、`score`、壓縮的 `move`、
  `depth`、`flag`，約 16 bytes。
- **`g_tt` / `g_tt_mask`** — 表格本體（`vector`）與索引遮罩。
- **`TT_BITS = 22`** — 2²² ≈ 400 萬筆（約 80 MB）。
- **`MATE_ZONE`** — 超過此值的分數屬「將軍距離分數」，需做 ply 修正。

#### `tt_init()`
首次使用時才配置 2²² 筆的表格，並設定索引遮罩（lazy allocation）。

#### `pack_move(m)` / `unpack_move(pm)`
把 `Move`（起點、終點）壓成 16-bit 整數（`(from<<8)|to`）以節省空間，並能解回。
每格編號為 `row*BOARD_W + col`。

#### `tt_score_to_store(score, ply)` / `tt_score_from_probe(score, ply)`
**將軍分數修正。**「N 步將死」的分數是相對於發現它的那個節點。若原樣存下、之後在
不同 ply 讀出，距離就錯了。所以存入時讓將軍分數與 ply 無關（我方將軍 `+ply`、對方 `-ply`），
讀出時再依目前 ply 重新錨定。非將軍分數原樣通過。

#### `tt_probe(key, depth, ply, alpha, beta, out, tt_move)`
查表。一定透過 `tt_move` 回傳存下的最佳步（供排序，即使不能剪枝）。
**只有**當該筆搜尋深度不小於目前深度、**且**界限對目前視窗可用時，才回傳 `true` 與可用的 `out`：
- `TT_EXACT` → 永遠可用；
- `TT_LOWER` 且 `score >= beta` → beta cutoff；
- `TT_UPPER` 且 `score <= alpha` → alpha cutoff。

較淺的筆數其分數會被拒絕，但它的 move 仍會被採用。

#### `tt_store(key, depth, ply, score, flag, move)`
以**深度優先取代**策略存結果：若既有筆是「同一盤面」且搜得「更深」，保留它（但若它沒有
最佳步則補上）。否則覆寫。不同盤面一律覆寫。

### B. 排序啟發式：killer + history

吃子用 MVV-LVA 排序，但棋局裡大多是**安靜步**（非吃子），MVV-LVA 對它們無話可說。
兩個便宜的啟發式來排序它們：

- **`g_killers[MAX_PLY][2]`** — 每一 ply 最近兩個造成 beta cutoff 的安靜步。這種步常常也能
  在同 ply 的兄弟節點造成剪枝。
- **`g_history[2][30][30]`** — 依 `(方, 起點, 終點)` 統計某安靜步造成 cutoff 的頻率，權重 `depth²`。
- **`g_prev_score` / `g_prev_depth`** — 上一輪完成的根節點分數/深度，用來在疊代加深迴圈間
  設定 aspiration（期望）視窗。

#### `clear_heuristics()`
每次根搜尋開始時，把 killer 與 history 表清零。

#### `is_capture(s, m)`
若該步終點有敵方棋子則為真。

#### `mvv_lva(s, m)`
**最有價值受害者 − 最低價值攻擊者。** 回傳 `victim*16 − attacker`，讓「以小吃大」排在前面
（例如「兵吃后」排在「后吃兵」之前）。

#### `order_moves(state, ply, tt_move)`
排序核心。為每個合法步評分後做穩定排序（由大到小）：
1. **TT 步** → `2,000,000`（最先試雜湊步）；
2. **吃子** → `1,000,000 + mvv_lva`；
3. **第一 killer** → `900,000`；**第二 killer** → `800,000`；
4. **安靜步** → 其 history 分數；
5. **兵升變**額外 `+500,000`（永遠值得早點試）。

#### `has_non_pawn_material(s)`
若該走的一方至少擁有一個城堡/騎士/主教/皇后則為真。用來**把關 null-move 剪枝** ——
在只剩兵/王的殘局裡，null-move 因為「逼著（zugzwang）」而不可靠。

#### `update_cutoff_heuristics(state, m, ply, depth)`
當安靜步造成 beta cutoff 後記錄它：推進此 ply 的 killer 欄位，並把 `depth²` 加到它的 history
分數。吃子略過（已由 MVV-LVA 處理）。

### C. `PVS::eval_ctx(...)` — 遞迴搜尋

依函式本體順序：

1. **記帳** —— 節點計數加一、更新 `seldepth`，若 `stop` 旗標被設（時間到）立刻返回。
2. **終局判定** —— 必要時產生合法步；勝局回傳 `P_MAX - ply`（越快將軍分數越高）、和局回傳 `0`。
3. **重複** —— `check_repetition` 若此盤面已出現足夠次數則回傳和局分。
4. **雜湊 + 推入 history** —— 計算 Zobrist 鍵並推入以追蹤重複。
5. **葉節點（`depth <= 0`）** —— 若啟用則呼叫靜默搜尋（`AlphaBeta::qsearch`），否則做靜態
   `evaluate()`。靜默搜尋會繼續搜吃子，避免在一次交換的中途停手（即「視野效應」）。
6. **TT 查表** —— 命中可用結果就立刻回傳快取分數。
7. **Null-move 剪枝** —— 若允許（`null_ok`、`depth>=3`、非將軍視窗、未被將、有非兵子力）：
   透過 `create_null_state()` 給對手一步**免費的**棋並以 `R=null_r` 減淺搜尋。若我方就算「跳過」
   後分數仍 `>= beta`，真實盤面幾乎必定剪枝 → 直接剪枝並回傳 `beta`。
8. **排序** —— 帶 TT 步呼叫 `order_moves`。
9. **主迴圈** —— 對每一步建立子盤面並遞迴。三種情況：
   - **`same_player_as_parent`**（同一方連走多步，遊戲特性）：視窗**不**取負；用 `[alpha, alpha+1]`
     做 PVS 試探，出乎意料才用完整視窗重搜。
   - **第一步（`move_index==0`）** —— 用完整視窗 `-eval_ctx(..., -beta, -alpha)` 建立主變例。
   - **之後各步** —— **後期步縮減（LMR）** + PVS 試探：
     - 後期的安靜步（`move_index >= lmr_min_move`、`depth >= lmr_min_depth`）以 `depth-1-reduction`
       搜尋（縮減 1，靠後且深時縮減 2）；
     - 用零寬視窗 `[-alpha-1, -alpha]` 試探；若一個被**縮減**的步竟超過 alpha，改以完整深度重搜；
     - 若它真的是新的主變例候選（`alpha < child < beta`），再用完整視窗重搜。
10. **更新最佳** —— 記錄最佳分數/步、抬高 alpha，當 `alpha >= beta` 時記錄 cutoff 啟發式並
    break（beta cutoff）。
11. **TT 存入** —— 把結果分類為 `UPPER`（無步超過原始 alpha）、`LOWER`（beta cutoff）或 `EXACT`，存入。
12. **彈出 history**，回傳 `best_score`。

### D. `PVS::search(...)` — 根節點

1. `ctx.reset()`，從參數表解析 `ABParams`，呼叫 `set_eval_params(tempo, pp_scale)` 把評估旋鈕
   送進 `State::evaluate`。
2. 對 TT 只查**雜湊步**（depth 0、忽略分數）來設定根排序；`clear_heuristics()`；在 ply 0 做 `order_moves`。
3. **Aspiration 視窗** —— 若有可信的前一輪分數（depth≥4、上一輪恰好淺一層、非將軍），就在窄視窗
   `[prev−delta, prev+delta]` 內搜尋，而非 `[-INF, INF]`。視窗越窄剪枝越多。
4. **根迴圈** —— 與 `eval_ctx` 相同的 PVS 結構（第一步完整視窗，其餘試探 + 重搜），並呼叫
   `ctx.on_root_update(...)` 讓 UI 隨搜尋進步即時顯示最佳路線。
5. **fail-low / fail-high 處理** —— 若分數落在 aspiration 界限上或之外，把那一側放寬到 `±INF` 重搜。
   這讓 aspiration「永遠不影響正確性」 —— 最多只多花時間，絕不改變答案。
6. 存 `g_prev_score/g_prev_depth`，把 EXACT 的根結果存入 TT（讓下一輪疊代加深最先搜這一步），
   填入時間/節點數，回傳。

### E. `PVS::default_params()` / `PVS::param_defs()`
定義 9 個布林開關（`UseKPEval, UseEvalMobility, ReportPartial, OrderMoves, UseQuiescence,
UseTT, UseNullMove, UseLMR, UseAspiration`）與 6 個數值旋鈕（`Tempo=6, PassedPawnScale=100,
NullR=2, LMRMinMove=3, LMRMinDepth=3, AspDelta=30`）及其 UI 範圍。預設值完全重現引擎。

---

## 第三部分 — `state.hpp`（介面宣告）

- **`void set_eval_params(int tempo, int passed_pawn_scale_pct)`** —— 自由函式，讓搜尋策略把
  執行期評估旋鈕推入 `evaluate()`，不必重新編譯。
- **`class Board`** —— 原始 `char[2][BOARD_H][BOARD_W]` 雙層棋盤（每位玩家一層），初始化即內建
  MiniChess 標準起始局面。
- **`class State : public BaseState`** —— 遊戲狀態。持有一個 `Board`、快取的 `score`，以及
  **延遲計算的 Zobrist 雜湊**（`zobrist_hash` + `zobrist_valid` 旗標）。宣告：`evaluate`、
  `next_state`、兩個走法產生器（`get_legal_actions_naive` / `_bitboard`）+ 派發器、序列化
  （`encode_*` / `decode_board`）、`check_repetition`、`create_null_state`，以及小型 inline 存取器
  （`piece_at`、`hash`、盤面尺寸、`game_name`）。

---

## 第四部分 — `state.cpp`（實作）

### A. 評估表與輔助函式

- **`kp_material[7]`** —— 10× 縮放的位置性子力價值（`兵 20 … 后 200、王 1000`），以求精細度。
- **`simple_material[7]`** —— 簡單評估模式用的純子力價值。
- **`pst[6][BOARD_H][BOARD_W]`** —— **棋子-格位表（PST）**（白方視角，黑方鏡射）。編碼位置偏好：
  兵想前進、騎士/主教想佔中心、王開局時想待在後方。
- **`tropism_w[7]`** —— **王向性（king-tropism）** 權重；每種棋子靠近**敵方國王**該獲得多少獎勵。

#### `king_tropism(piece_type, pr, pc, ekr, ekc)`
棋子到敵王的 Chebyshev 距離；在距離 2 以內回傳 `weight * (3 - distance)` —— 越近的攻擊者分越高。
這促使引擎圍攻敵王（在「吃王取勝」的遊戲裡非常重要）。

#### `set_eval_params(tempo, passed_pawn_scale_pct)`
寫入兩個檔案級靜態旋鈕 `g_eval_tempo` 與 `g_eval_pp_scale`，稍後在 `evaluate` 讀取。

- **`passed_pawn_bonus[BOARD_H] = {0,70,45,28,16,8}`** —— 依「距升變還差幾列」給獎勵；越接近升變
  越大，因為在這小棋盤上升變成后是決定性的。

#### `passed_pawns(self_b, enemy_b, owner)`
對 `owner` 的每個兵，檢查前方同列或相鄰列是否有敵兵（會阻擋/吃掉它）。若都沒有，就是**通路兵**，
依其距升變距離加上獎勵。兩個方向都處理（owner 0 朝 row 0 前進；owner 1 朝最後一列）。

#### `State::evaluate(use_kp_eval, use_mobility, history)`
以該走一方視角的靜態盤面分數。步驟：
1. 勝局捷徑 → `P_MAX`。
2. **KP 模式**（`use_kp_eval`）：找出雙方國王；對每個棋子加上 `子力 + PST`，若敵王在盤上再加
   `king_tropism`；接著加上縮放後的通路兵獎勵，以及給該走一方的小幅 `g_eval_tempo`。
3. **簡單模式**：只加總 `simple_material`。
4. **機動力**（`use_mobility`）：`+2 * (我方步數 − 對方步數)`，對方步數來自 `create_null_state()`。
   獎勵選擇較多的一方。
5. 回傳 `self_score − oppn_score + bonus`。

### B. Zobrist 雜湊

- **`zobrist_piece[2][7][BOARD_H][BOARD_W]`、`zobrist_side`** —— 隨機 64-bit 鍵，
  每個（玩家、棋種、格位）一個，外加一個代表「輪到誰走」。

#### `init_zobrist()`
用固定種子的快速 xorshift 亂數產生器把上述表格填一次（跨次執行可重現）。

#### `State::compute_hash_full()`
把盤上每個棋子的鍵 XOR 在一起，若輪到黑方再 XOR `zobrist_side` —— 即盤面完整的 64-bit 指紋。
在某狀態首次呼叫 `hash()` 時使用。

### C. 走子套用

#### `State::next_state(move)`
建立走一步之後的盤面，**並增量更新雜湊**（比整盤重算便宜得多）：
- 複製棋盤、讀取移動的棋子、把走到底線的兵升變成后；
- 切換 `zobrist_side`；把移動棋子從起點 XOR **出**；若有吃子，把受害者 XOR 出並清除；
  把移動後的棋子 XOR **入**終點；
- 寫入盤面變動，為對手建立子 `State`，並把預先算好的雜湊標為有效。

### D. 走法產生

- **`move_table_rook_bishop[8][7][2]`** —— 8 個滑行方向的射線位移。
- **`move_table_knight[8][2]`** / **`move_table_king[8][2]`** —— 騎士與國王的 8 個跳躍位移。

#### `State::get_legal_actions_naive()`
直觀的陣列式產生器。掃描棋盤，依棋種：
- **兵** —— 前方空則直進；斜向僅能吃子；方向依顏色。
- **城堡/主教/皇后** —— 沿對應的射線段滑行直到被擋。
- **騎士/國王** —— 8 個固定位移。
- **吃王短路**：任何一步能吃到敵王（`piece==6`）的瞬間，設 `game_state = WIN` 並立即返回 ——
  那就是制勝步，無須再看下去。

#### 位元棋盤產生器 —— `bb_*` 表、`bb_init()`、`get_legal_actions_bitboard()`
更快的產生器：30 格剛好放進 `uint32_t`，格位 `(r,c) → bit r*5+c`。
- `bb_init()` 為每格預算騎士、國王、兵的推進/吃子攻擊遮罩。
- `get_legal_actions_bitboard()` 建立佔位遮罩，用 `__builtin_ctz` 位元掃描走訪己方棋子；
  跳躍子把預算遮罩與 `~self_occ` 做 AND；滑行子沿射線走；同樣套用吃王短路。
  功能與 naive 版完全相同，但分支更少。

#### `State::get_legal_actions()`
派發器：在 `#ifdef USE_BITBOARD` 下選位元棋盤產生器，否則用 naive 版。

### E. 序列化、顯示、null 狀態、重複

- **`piece_table` / `encode_output()`** —— 用 Unicode 西洋棋字元渲染棋盤供 CLI 顯示。
- **`encode_state()`** —— 以數字輸出「該走的一方 + 雙方棋子層」（引擎 ↔ harness 協定）。
- **`create_null_state()`** —— 複製棋盤但**翻轉該走的一方**並重新產生合法步。同時驅動機動力評估
  與 null-move 剪枝。
- **`encode_board()` / `decode_board(s, side_to_move)`** —— 類 FEN 字串序列化（大寫=白、小寫=黑、
  `.`=空）及其逆運算。
- **`cell_display(row, col)`** —— 供協定 `d`（顯示）指令使用的單格 ASCII。
- **`check_repetition(history, out_score)`** —— 若目前雜湊在對局歷史中出現 **≥ 3** 次，回傳和局（`0`）
  （三次重複規則）。

---

## 整體如何串起來

```
UBGI 層（時間控制 + 疊代加深）
        │  以 depth = 1, 2, 3, ... 反覆呼叫
        ▼
PVS::search（根節點：aspiration 視窗 + PVS 根迴圈）
        │  遞迴
        ▼
PVS::eval_ctx  ──TT 查/存──►  轉置表
        │  ├─ null-move 剪枝 ──► State::create_null_state
        │  ├─ order_moves ──► killer / history / MVV-LVA / TT 步
        │  ├─ LMR + PVS 試探/重搜
        │  └─ 葉節點 ──► AlphaBeta::qsearch ──► State::evaluate
        ▼
State::next_state（增量 Zobrist）/ get_legal_actions
```

每個搜尋功能都能透過 `ABParams` 獨立開關，數值旋鈕也是執行期 UCI 選項 —— 因此任何單一技術都能
關閉或 A/B 測試而不必重新編譯，而預設值會重現滿血的引擎。
