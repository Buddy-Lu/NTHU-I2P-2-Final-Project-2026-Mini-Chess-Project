# PVS Cheat Sheet — 逐區塊深度詳解

> 本檔把 `src/policy/pvs.hpp` 與 `src/policy/pvs.cpp` **一字不漏**地重現，
> 並依「運算區塊」切開，每塊前面附上**非常詳細的中文演算法講解**，後面接該塊原始碼
> （每行也都有行內註解）。原始程式碼檔案未被更動，這只是對照學習用速查表。
>
> **棋規前提**：MiniChess 為 6×5 棋盤（`BOARD_H=6`、`BOARD_W=5`），勝負以**吃到對方國王**判定
> （非將死）。兵只升變皇后。棋子碼 `1=兵 2=城堡 3=騎士 4=主教 5=皇后 6=國王`。
> 玩家 `0`=白（往 row 0 走），`1`=黑（往 row 5 走）。
>
> **分數慣例（negamax / 負極大值）**：每個節點都回傳「以目前該走一方視角」的分數，
> 父節點對子節點取負（`-eval_ctx(...)`）。所以「我的好 = 你的壞」，整棵樹只要一套
> max 邏輯就能處理雙方。`alpha` = 我方目前已保證的最低分（下界），`beta` = 對手會
> 容許的最高分（上界）；一旦某步使分數 `≥ beta`，對手根本不會讓我們走到這裡 →
> 直接剪枝（beta cutoff）。

---

# 一、`pvs.hpp` — 介面宣告

## 🔹 演算法總覽：PVS 是什麼、為什麼比 alpha-beta 快

**Alpha-beta** 的核心是：搜尋時維持一個 `[alpha, beta]` 視窗，若某分支不可能改善結果就剪掉。
但它對「每一步」都用完整視窗去搜，成本仍高。

**PVS（Principal Variation Search，主變例搜尋）** 多做一個假設：
> 「如果走法排序夠好，**第一步**極可能就是最佳步（主變例 PV）。」

於是：
1. **第一步**：用完整視窗 `[alpha, beta]` 認真搜，得到一個可靠的 PV 分數，把 `alpha` 抬上來。
2. **之後每一步**：先用 **零寬視窗（null window）** `[alpha, alpha+1]` 做一次「是非題」試探 ——
   只問「這步**有沒有可能**超過目前的 `alpha`？」。零寬視窗讓 `alpha` 幾乎等於 `beta`，
   絕大多數分支會立刻 fail（剪枝），所以**試探非常便宜**。
3. 只有當試探**出乎意料**（`alpha < 分數 < beta`，代表這步可能比 PV 還好）時，才付出代價用
   完整視窗**重搜**一次。

因為好的排序讓大多數試探都 fail low（不必重搜），PVS 在**相同深度下走訪的節點比 alpha-beta 少**，
而且**回傳的結果完全相同**（不犧牲正確性）。本實作的葉節點（靜默搜尋）與吃子排序（MVV-LVA）
都重用 alpha-beta 的程式碼（`ABParams`）。

```cpp
#pragma once                              // 標頭只引入一次（避免重複定義）
#include "search_types.hpp"               // 引入 State / SearchResult / SearchContext / ParamMap 等型別
#include "game_history.hpp"               // 引入 GameHistory（重複盤面追蹤）
#include "alphabeta.hpp"   // reuses ABParams, AlphaBeta::qsearch, order_moves
                                          //   ↑ 重用 ABParams 結構、靜默搜尋與 MVV-LVA 排序

/*============================================================
 * PVS — Principal Variation Search
 *
 * Alpha-beta's refinement: assume the first (best-ordered) move
 * is the principal variation and search it with the full window.
 * Every later move is first probed with a *null window*
 * [alpha, alpha+1] — just a yes/no "can this beat alpha?" test,
 * which prunes much faster. Only if that probe surprises us
 * (alpha < score < beta) do we re-search it with the full window.
 *
 * With good move ordering most probes fail low (no re-search), so
 * PVS searches fewer nodes than plain alpha-beta at the same
 * depth, returning the identical result. Reuses the alpha-beta
 * quiescence leaf and MVV-LVA ordering (ABParams).
 *============================================================*/
                                          // ↑ 說明 PVS：第一步用完整視窗，其餘先用零寬視窗試探，
                                          //   只有出乎意料時才重搜；排序好時節點數少於 alpha-beta，結果相同
class PVS{                                // PVS 策略類別（全靜態方法）
public:                                   // 公開介面
    static int eval_ctx(                  // 內部節點遞迴搜尋：回傳該局面分數
        State *state,                     // 要搜尋的盤面
        int depth,                        // 剩餘搜尋層數
        GameHistory& history,             // 重複盤面堆疊
        int ply,                          // 距離根節點的層數（將軍分數 / killer 索引）
        SearchContext& ctx,               // 節點計數 / 停止旗標 / 回呼
        int alpha,                        // 搜尋視窗下界
        int beta,                         // 搜尋視窗上界
        const ABParams& p,                // 功能開關 + 數值旋鈕
        bool null_ok = true               // 此處是否允許 null-move（預設允許）
    );                                    // eval_ctx 宣告結束
    static SearchResult search(           // 根節點進入點：跑一次固定深度搜尋
        State *state,                     // 根盤面
        int depth,                        // 本次搜尋深度
        GameHistory& history,             // 重複盤面堆疊
        SearchContext& ctx                // 搜尋環境
    );                                    // search 宣告結束

    static ParamMap default_params();     // 回傳預設參數表（重現內建引擎）
    static std::vector<ParamDef> param_defs(); // 回傳參數定義（供 UCI 宣告選項）
};                                        // PVS 類別結束
```

---

# 二、`pvs.cpp` — 實作

## 🔹 區塊 0：引入標頭

沒有演算法，只是把後面會用到的標準函式庫與專案標頭拉進來。`config.hpp` 提供盤面尺寸、
`P_MAX`（將軍分數量級）、`PIECE_VALUES`（子力價值）等常數。

```cpp
#include <utility>                        // std::pair（Move = pair<Point,Point>）
#include <chrono>                         // 高解析度計時器（量測搜尋耗時）
#include <vector>                         // std::vector（轉置表、排序暫存）
#include <cstdint>                        // 固定寬度整數型別（uint64_t 等）
#include <algorithm>                      // std::stable_sort、std::max
#include "state.hpp"                      // State 與 evaluate()
#include "config.hpp"                     // BOARD_H / BOARD_W / P_MAX / PIECE_VALUES 等常數
#include "pvs.hpp"                        // 本策略的宣告
```

---

## 🔹 演算法：轉置表（Transposition Table, TT）

### 為什麼需要它
搜尋樹裡，**不同的走法順序常會到達同一個盤面**（這叫 *transposition*）。例如先走 A 再走 B，
跟先走 B 再走 A，可能得到一模一樣的局面。如果每次都重搜，等於做了大量重複工。

TT 是一張**雜湊表（快取）**：以盤面的 **Zobrist 雜湊**為鍵，把「這個盤面搜過的結果」存起來，
下次遇到同盤面直接取用。除了分數，**每筆還存下當時找到的最佳步** —— 這是整個搜尋裡
**最強的排序訊號**（下次先試這步，往往立刻 cutoff）。

### 三種分數型別（flag）很關鍵
搜尋因 alpha-beta 剪枝，存下的分數**不一定是精確值**，可能只是個界限：
- `TT_EXACT`：在 `(alpha, beta)` 內正常搜完 → 分數是**精確值**，永遠能用。
- `TT_LOWER`：發生 beta cutoff → 真實分數**至少**這麼大（**下界**）。只有當它 `≥ beta` 時可用來剪枝。
- `TT_UPPER`：所有步都沒超過 alpha → 真實分數**最多**這麼大（**上界**）。只有當它 `≤ alpha` 時可用。

### 取代策略：深度優先
表格有限會滿，發生衝突時要決定保留誰。本實作用**深度優先（depth-preferred）**：
同一盤面就保留搜得**更深**那筆（更深 = 更可信）；不同盤面則直接覆寫。

### 為什麼存完整 64-bit 鍵
索引只用雜湊低 22 位，會有不同盤面落到同格（碰撞）。所以每筆額外存**完整 64-bit 鍵**，
讀取時比對全鍵，**偵測**碰撞並丟棄，不會用錯盤面的結果。

```cpp
/*============================================================
 * Transposition table
 *
 * Caches the result of searching a position (keyed by its Zobrist
 * hash) so transpositions — different move orders reaching the
 * same position — are not re-searched. Each entry also stores the
 * best move found, which is tried first next time: by far the
 * strongest move-ordering signal there is.
 *
 * Replacement: depth-preferred (keep the deeper search). The full
 * 64-bit key is stored so hash collisions are detected, not used.
 *============================================================*/
                                          // ↑ 轉置表：以 Zobrist 雜湊為鍵快取搜尋結果，避免重搜；
                                          //   存最佳步（最強排序訊號）；深度優先取代；存完整 64-bit 鍵防碰撞
namespace {                               // 匿名 namespace：以下符號只在本檔可見

enum TTFlag : uint8_t { TT_NONE = 0, TT_EXACT, TT_LOWER, TT_UPPER };
                                          // ↑ 旗標：無 / 精確值 / 下界(beta cutoff) / 上界(沒步超過 alpha)

struct TTEntry {                          // 轉置表一格
    uint64_t key   = 0;     // full Zobrist key (collision check)
                                          //   ↑ 完整雜湊鍵，用來偵測碰撞
    int32_t  score = 0;                   // 存下的分數
    uint16_t move  = 0;     // packed from/to (0 = none)
                                          //   ↑ 壓縮後的最佳步（0 表示無）
    int16_t  depth = -1;                  // 此結果的搜尋深度
    uint8_t  flag  = TT_NONE;             // 分數型別（見 TTFlag）
};                                        // TTEntry 結束

std::vector<TTEntry> g_tt;                // 轉置表本體
uint64_t g_tt_mask = 0;                   // 索引遮罩（key & mask 取格號）

constexpr int TT_BITS = 22;             // 2^22 entries (~80 MB), well under 4 GB
                                          //   ↑ 表大小 2^22 ≈ 400 萬筆（約 80 MB）
constexpr int MATE_ZONE = P_MAX - 1000; // scores above this are mate-distance
                                          //   ↑ 超過此值的分數視為「將軍距離分數」，需 ply 修正

void tt_init(){                           // 延遲配置轉置表
    if(g_tt.empty()){                     // 尚未配置時才做
        g_tt.assign(static_cast<size_t>(1) << TT_BITS, TTEntry{});
                                          //   ↑ 配置 2^22 筆並清空
        g_tt_mask = (static_cast<uint64_t>(1) << TT_BITS) - 1;
                                          //   ↑ 遮罩 = 2^22 - 1
    }                                     // if 結束
}                                         // tt_init 結束
```

---

## 🔹 演算法：把一步壓成 16-bit（`pack_move` / `unpack_move`）

TT 每筆都要存最佳步，但 `Move`（兩個 `Point`）很佔空間。由於整盤只有 30 格，
**一格可用 `row*5+col`（0..29）一個 byte 表示**，一步只需「起點 byte + 終點 byte」共 16-bit。
`pack` 把起點放高 8 位、終點放低 8 位；`unpack` 反向用整除/取餘還原成 `(row, col)`。
（本檔 `unpack_move` 雖未被外部呼叫，但保留為對稱工具。）

```cpp
inline uint16_t pack_move(const Move& m){ // 把一步壓成 16-bit
    int from = static_cast<int>(m.first.first)  * BOARD_W + static_cast<int>(m.first.second);
                                          //   ↑ 起點格號 = row*寬 + col
    int to   = static_cast<int>(m.second.first) * BOARD_W + static_cast<int>(m.second.second);
                                          //   ↑ 終點格號
    return static_cast<uint16_t>((from << 8) | to);
                                          //   ↑ 高 8 位放起點、低 8 位放終點
}                                         // pack_move 結束

inline Move unpack_move(uint16_t pm){     // 把 16-bit 解回一步
    int from = pm >> 8, to = pm & 0xFF;   // 取出起點 / 終點格號
    return Move(Point(from / BOARD_W, from % BOARD_W),
                Point(to   / BOARD_W, to   % BOARD_W));
                                          //   ↑ 格號還原成 (row, col)
}                                         // unpack_move 結束
```

---

## 🔹 演算法：將軍分數的 ply 修正（`tt_score_to_store` / `tt_score_from_probe`）

### 問題
將軍分數寫成 `P_MAX - ply`（越快將死分越高）。但這個 `ply` 是**相對於發現它的那個節點**。
如果原樣存進 TT，之後在**不同 ply** 的節點讀出來，「還要幾步將死」的距離就錯了 ——
可能把「5 步後將死」誤當成「立刻將死」。

### 解法
存入時把它轉成**與 ply 無關**的形式：我方將軍 `+ply`、對方將軍 `-ply`，抵銷掉當下節點的 ply 偏移；
讀出時再依**目前**節點的 ply 反向加回去，重新錨定。`MATE_ZONE` 用來判斷「這分數是不是將軍級別」
（一般評估分遠小於它，原樣通過不動）。

```cpp
/* Mate scores are stored relative to the node (ply-independent) so a
 * cached mate is still correct when probed at a different ply. */
                                          // ↑ 將軍分數以「與 ply 無關」方式儲存，跨 ply 讀取才正確
inline int tt_score_to_store(int score, int ply){ // 存入時的將軍分數修正
    if(score >=  MATE_ZONE) return score + ply;    // 我方將軍：加上 ply
    if(score <= -MATE_ZONE) return score - ply;    // 對方將軍：減去 ply
    return score;                                  // 一般分數原樣
}                                         // tt_score_to_store 結束
inline int tt_score_from_probe(int score, int ply){ // 讀出時的反向修正
    if(score >=  MATE_ZONE) return score - ply;     // 我方將軍：減回 ply
    if(score <= -MATE_ZONE) return score + ply;     // 對方將軍：加回 ply
    return score;                                   // 一般分數原樣
}                                         // tt_score_from_probe 結束
```

---

## 🔹 演算法：查表（`tt_probe`）

回傳兩種東西：
1. **最佳步 `tt_move`**：只要鍵相符就回傳，**即使分數不能用來剪枝**也要 —— 因為它是排序黃金訊號。
2. **是否可剪枝（回傳值 + `out`）**：必須同時滿足兩個條件才回 `true`：
   - **深度足夠**：存的那筆搜得**不比現在淺**（`e.depth >= depth`）。較淺的結果可信度不足，
     此時**只用步、不用分數**。
   - **界限對得上視窗**：`EXACT` 永遠可用；`LOWER` 要 `s ≥ beta`（能造成 beta cutoff）；
     `UPPER` 要 `s ≤ alpha`（確認沒救、直接回上界）。

```cpp
/* Probe: returns true and sets `out` if a usable cutoff exists for
 * this depth/window. Always sets `tt_move` (0 if none) for ordering. */
                                          // ↑ 查表：可用時回傳 true 並設定 out；一定回傳 tt_move 供排序
bool tt_probe(uint64_t key, int depth, int ply, int alpha, int beta,
              int& out, uint16_t& tt_move){
    tt_move = 0;                          // 預設無雜湊步
    const TTEntry& e = g_tt[key & g_tt_mask]; // 以遮罩取出對應格
    if(e.key != key || e.flag == TT_NONE){    // 鍵不符（碰撞）或空格
        return false;                     //   ↑ 不可用
    }                                     // if 結束
    tt_move = e.move;                     // 即使不能剪枝，也回傳最佳步供排序
    if(e.depth < depth){                  // 此筆搜得比我們淺
        return false;                 /* shallower: use move, not the score */
                                          //   ↑ 只用步、不用分數
    }                                     // if 結束
    int s = tt_score_from_probe(e.score, ply); // 還原將軍分數
    if(e.flag == TT_EXACT){ out = s; return true; }          // 精確值：直接可用
    if(e.flag == TT_LOWER && s >= beta){ out = s; return true; } // 下界且 ≥ beta：beta cutoff
    if(e.flag == TT_UPPER && s <= alpha){ out = s; return true; }// 上界且 ≤ alpha：alpha cutoff
    return false;                         // 否則不可用作截斷
}                                         // tt_probe 結束
```

---

## 🔹 演算法：存表（`tt_store`）

實作**深度優先取代**：若目標格已是**同一盤面**且既有筆**更深**，就保留它（不覆寫）；
但若舊筆缺最佳步而新結果有，就**補上那一步**（深度資訊保留、順手強化排序）。其餘情況
（空格、不同盤面、或新筆更深）一律覆寫。寫入時分數先經 `tt_score_to_store` 做 ply 修正。

```cpp
void tt_store(uint64_t key, int depth, int ply, int score, uint8_t flag, uint16_t move){
                                          // 存入一筆結果
    TTEntry& e = g_tt[key & g_tt_mask];   // 取出目標格（參考）
    /* depth-preferred replacement; always replace a different position */
                                          // ↑ 深度優先取代；不同盤面一律覆寫
    if(e.key == key && e.depth > depth && e.flag != TT_NONE){
                                          // 同盤面且既有更深
        if(move && e.move == 0) e.move = move;  /* keep deeper, refresh move */
                                          //   ↑ 保留深的，但補上缺的最佳步
        return;                           // 不覆寫
    }                                     // if 結束
    e.key   = key;                        // 寫入鍵
    e.score = tt_score_to_store(score, ply); // 寫入（修正過的）分數
    e.move  = move ? move : e.move;       // 有新步就更新，否則保留舊步
    e.depth = static_cast<int16_t>(depth); // 寫入深度
    e.flag  = flag;                       // 寫入分數型別
}                                         // tt_store 結束
```

---

## 🔹 演算法：走法排序 — killer + history（為什麼排序決定一切）

### 排序對 alpha-beta / PVS 是「命脈」
alpha-beta 的剪枝量**極度依賴排序**：若最佳步排第一，第一步就把 `alpha` 抬到很高，
後面全部秒剪；若最佳步排最後，幾乎什麼都剪不掉、退化成完整搜尋。理論上理想排序能把
分支因子從 b 降到 √b（深度等於翻倍）。

### 吃子好排、安靜步難排
吃子可用 **MVV-LVA**（見下）排序，但棋局中**大多數是安靜步（非吃子）**，MVV-LVA 對它們沒輒。
兩個便宜又強的啟發式補上：

- **Killer moves（殺手步）**：某個**安靜步**在某 ply 造成了 beta cutoff，那麼在**同 ply 的
  兄弟節點**它也很可能再次 cutoff（局面通常相似）。所以每個 ply 記住最近**兩個**殺手步，優先試。
- **History heuristic（歷史啟發）**：一張 `(方, 起點, 終點)` 的全域統計表，記錄某安靜步
  「造成 cutoff」的累計次數，權重 `depth²`（越深的 cutoff 越有價值）。安靜步就按這個分數排序。

`g_prev_score / g_prev_depth` 則是給根節點 **aspiration 視窗**用的：記住上一輪疊代加深的根分數/深度。

```cpp
/*============================================================
 * Move-ordering heuristics: killers + history
 *
 * Captures are ordered by MVV-LVA, but most moves in a chess
 * search are *quiet* (non-captures) and MVV-LVA says nothing about
 * them. Two cheap, powerful heuristics fix that:
 *
 *  - Killer moves: a quiet move that caused a beta-cutoff at a
 *    given ply is likely to cut off sibling nodes at the same ply
 *    too, so we try the last two such moves early.
 *  - History heuristic: a per (side, from, to) table of how often
 *    a quiet move caused a cutoff (weighted by depth^2). Quiet
 *    moves are then ordered by this score.
 *============================================================*/
                                          // ↑ killer：同 ply 造成 cutoff 的安靜步先試；
                                          //   history：依 (方,起,終) 統計 cutoff 次數(權重 depth^2) 排序安靜步
constexpr int MAX_PLY = 128;              // 支援的最大層數
uint16_t g_killers[MAX_PLY][2];           // 每 ply 兩個 killer 步
int      g_history[2][30][30];            // [方][起點格][終點格] 的歷史分數

/* Previous iteration's root score/depth, for aspiration windows
 * across the UBGI iterative-deepening loop. */
                                          // ↑ 上一輪根節點分數/深度，供 aspiration 視窗使用
int g_prev_score = 0;                     // 上一輪根分數
int g_prev_depth = 0;                     // 上一輪根深度

void clear_heuristics(){                  // 清空 killer 與 history
    for(int i = 0; i < MAX_PLY; i++){     // 逐 ply
        g_killers[i][0] = g_killers[i][1] = 0; // 兩個 killer 清零
    }                                     // for 結束
    for(int s = 0; s < 2; s++){           // 雙方
        for(int f = 0; f < 30; f++){      // 起點 0..29
            for(int t = 0; t < 30; t++){  // 終點 0..29
                g_history[s][f][t] = 0;   // 清零
            }                             // for t
        }                                 // for f
    }                                     // for s
}                                         // clear_heuristics 結束
```

---

## 🔹 演算法：吃子判定與 MVV-LVA（`is_capture` / `mvv_lva`）

- **`is_capture`**：看這步**終點格**在對手那一層是否有子（`> 0`）。有 = 吃子。
- **MVV-LVA**（Most Valuable Victim − Least Valuable Attacker，最有價值受害者 − 最低價值攻擊者）：
  吃子的好壞直覺上是「**吃到大子、用小子去吃**」最賺。公式 `受害者價值 × 16 − 攻擊者價值`：
  - `× 16` 確保「受害者大小」是主排序鍵（先比吃到多大的子）。
  - 減去攻擊者價值當次要鍵：同樣吃皇后，用兵吃排在用城堡吃之前（風險更低、更可能是好棋）。
  - 例：兵吃后 vs 后吃兵 → 前者分數遠高，先搜。

```cpp
inline bool is_capture(const State* s, const Move& m){ // 是否為吃子
    return s->piece_at(1 - s->player,                  // 看對手方
                       (int)m.second.first, (int)m.second.second) > 0;
                                          //   ↑ 終點若有敵子即為吃子
}                                         // is_capture 結束

inline int mvv_lva(const State* s, const Move& m){ // MVV-LVA 評分
    int victim   = s->piece_at(1 - s->player, (int)m.second.first, (int)m.second.second);
                                          //   ↑ 受害者（被吃的敵子）
    int attacker = s->piece_at(s->player,     (int)m.first.first,  (int)m.first.second);
                                          //   ↑ 攻擊者（移動的己子）
    return PIECE_VALUES[victim] * 16 - PIECE_VALUES[attacker];
                                          //   ↑ 大受害者×16 − 小攻擊者：以小吃大排前面
}                                         // mvv_lva 結束
```

---

## 🔹 演算法：完整走法排序（`order_moves`）

把上述所有訊號合成一個**排序鍵**，再做**穩定排序**（由大到小）。鍵的分層（用巨大常數隔開
每層，確保上層永遠壓過下層）：

| 優先序 | 類別 | 鍵值 |
|--------|------|------|
| 1（最高）| **TT 步**（雜湊表建議的最佳步）| `2,000,000` |
| 2 | **吃子** | `1,000,000 + mvv_lva` |
| 3 | **第一殺手步** | `900,000` |
| 4 | **第二殺手步** | `800,000` |
| 5（最低）| **其餘安靜步** | `history 分數`（通常 0～數千）|
| 額外 | **兵升變** | 在以上之上再 `+500,000` |

少於 2 步直接跳過（不必排）。用 `stable_sort` 是為了同分時保持原順序（行為可重現）。
兵升變因為直接變皇后、在小盤上幾乎是決勝手，所以**不論它原本屬哪層都額外加大權重**先試。

```cpp
/* Order this node's moves: TT move, then captures (MVV-LVA), then
 * killers, then quiet moves by history score. */
                                          // ↑ 排序順序：TT步 → 吃子 → killer → 安靜步(history)
void order_moves(State* state, int ply, uint16_t tt_move){
    auto& moves = state->legal_actions;   // 取得合法步參考
    const size_t n = moves.size();        // 步數
    if(n < 2){                            // 0 或 1 步
        return;                           //   ↑ 無需排序
    }                                     // if 結束
    int self = state->player;             // 目前該走的一方
    std::vector<std::pair<int, Move>> scored; // (分數, 步) 暫存
    scored.reserve(n);                    // 預留空間
    for(const auto& m : moves){           // 逐步評分
        uint16_t pm = pack_move(m);       // 壓成 16-bit 以比對
        int from = (int)m.first.first  * BOARD_W + (int)m.first.second;  // 起點格號
        int to   = (int)m.second.first * BOARD_W + (int)m.second.second; // 終點格號
        int key;                          // 排序鍵
        if(pm == tt_move){                // 是 TT 步
            key = 2000000;                //   ↑ 最高優先
        }else if(is_capture(state, m)){   // 是吃子
            key = 1000000 + mvv_lva(state, m); //   ↑ 100 萬 + MVV-LVA
        }else if(ply < MAX_PLY && pm == g_killers[ply][0]){ // 第一 killer
            key = 900000;                 //   ↑ 90 萬
        }else if(ply < MAX_PLY && pm == g_killers[ply][1]){ // 第二 killer
            key = 800000;                 //   ↑ 80 萬
        }else{                            // 其餘安靜步
            key = g_history[self][from][to];   /* quiet: history score */
                                          //   ↑ 用 history 分數
        }                                 // if-else 結束
        /* pawn promotion (to back rank) is always worth trying early */
                                          // ↑ 兵升變永遠值得早試
        int attacker = state->piece_at(self, (int)m.first.first, (int)m.first.second);
                                          //   ↑ 移動的棋子種類
        if(attacker == 1 && ((int)m.second.first == 0 || (int)m.second.first == BOARD_H - 1)){
                                          // 是兵且走到底線
            key += 500000;                //   ↑ 額外 +50 萬
        }                                 // if 結束
        scored.emplace_back(key, m);      // 收集 (分數, 步)
    }                                     // for 結束
    std::stable_sort(scored.begin(), scored.end(), // 穩定排序（保持同分原序）
        [](const std::pair<int, Move>& a, const std::pair<int, Move>& b){
            return a.first > b.first;     //   ↑ 分數由大到小
        });                               // 排序結束
    for(size_t i = 0; i < n; i++){        // 寫回排序後的步
        moves[i] = scored[i].second;      //   ↑ 覆蓋 legal_actions
    }                                     // for 結束
}                                         // order_moves 結束
```

---

## 🔹 演算法：null-move 的安全守門（`has_non_pawn_material`）

null-move 剪枝（下面會講）在「**逼著（zugzwang）**」局面是**不正確**的 —— 那種局面裡
「被迫走子反而變差」，於是「假裝跳過一步」的假設崩潰。逼著幾乎只發生在**只剩兵和王**的殘局。
所以這個函式檢查該走方是否還有**非兵子力**（城堡/騎士/主教/皇后，碼 2..5）；有才允許 null-move。

```cpp
/* Side to move has at least one rook/knight/bishop/queen — used to
 * gate null-move pruning, which is unsound in pawn/king-only
 * endgames (zugzwang). */
                                          // ↑ 該走方是否有非兵子力；用來把關 null-move（殘局逼著時不可靠）
inline bool has_non_pawn_material(const State* s){
    for(int r = 0; r < BOARD_H; r++){     // 逐列
        for(int c = 0; c < BOARD_W; c++){ // 逐行
            int pc = s->piece_at(s->player, r, c); // 該格己子
            if(pc >= 2 && pc <= 5){       // 城堡/騎士/主教/皇后
                return true;              //   ↑ 有非兵子力
            }                             // if 結束
        }                                 // for c
    }                                     // for r
    return false;                         // 只剩兵/王
}                                         // has_non_pawn_material 結束
```

---

## 🔹 演算法：cutoff 後更新啟發式（`update_cutoff_heuristics`）

當某步造成 beta cutoff（證明它很強），就回頭「獎勵」它，讓未來先試：
- 只對**安靜步**做（吃子已由 MVV-LVA 處理，跳過）。
- **Killer 更新**：把它推進此 ply 的第一殺手位，舊的第一退為第二（保留最近兩個；去重避免重複）。
- **History 更新**：對應 `(方, 起, 終)` 累加 `depth²` —— 越深層的 cutoff 越值錢，權重越大。

```cpp
/* Record a quiet move that caused a beta cutoff. */
                                          // ↑ 記錄造成 beta cutoff 的安靜步
void update_cutoff_heuristics(const State* state, const Move& m, int ply, int depth){
    if(is_capture(state, m)){             // 吃子不在此處理
        return;                          /* captures handled by MVV-LVA */
                                          //   ↑ 吃子由 MVV-LVA 負責
    }                                     // if 結束
    uint16_t pm = pack_move(m);           // 壓成 16-bit
    if(ply < MAX_PLY && g_killers[ply][0] != pm){ // 尚非第一 killer
        g_killers[ply][1] = g_killers[ply][0]; // 舊的退為第二
        g_killers[ply][0] = pm;           // 新的成為第一
    }                                     // if 結束
    int from = (int)m.first.first  * BOARD_W + (int)m.first.second;  // 起點格號
    int to   = (int)m.second.first * BOARD_W + (int)m.second.second; // 終點格號
    g_history[state->player][from][to] += depth * depth; // history 加上 depth^2
}                                         // update_cutoff_heuristics 結束

} // namespace                            // 匿名 namespace 結束
```

---

# 三、`PVS::eval_ctx` — 內部節點的遞迴搜尋

這是整個引擎的心臟。以下把它依**運算階段**逐段拆解。

## 🔹 階段 1：記帳與終局前置（prologue）

進入節點先做四件事，順序有意義：
1. **記帳**：節點計數 +1、更新 `seldepth`（追蹤實際搜到多深，含靜默延伸）；若 `stop` 旗標被設
   （時間用完）立刻返回，不再深入。
2. **終局判定**：必要時產生合法步；**勝局**（剛被吃王）回 `P_MAX - ply` —— 減 ply 是讓
   「越快將死分越高」，引擎才會選最快的殺著、避免拖延。**和局**回 0。
3. **重複判定**：三次重複等規則觸發就回和局分（避免在已和的局面繞圈）。
4. **雜湊 + 推入 history**：算出 Zobrist 鍵，推入歷史堆疊以追蹤重複（離開時會 `pop`）。

```cpp
/*============================================================
 * PVS — eval_ctx
 *
 * Negamax with principal-variation (null-window) search on top
 * of alpha-beta. Prologue (terminals, repetition, quiescence
 * leaf) matches AlphaBeta::eval_ctx; the difference is the loop:
 * full window for the first move, null-window probe + optional
 * re-search for the rest.
 *============================================================*/
                                          // ↑ 在 alpha-beta 上加 PVS：前置(終局/重複/葉)同 AlphaBeta，
                                          //   差別在主迴圈：第一步完整視窗，其餘零寬試探 + 視情況重搜
int PVS::eval_ctx(                        // 內部節點搜尋實作
    State *state,                         // 盤面
    int depth,                            // 剩餘層數
    GameHistory& history,                 // 重複堆疊
    int ply,                              // 距根層數
    SearchContext& ctx,                   // 搜尋環境
    int alpha,                            // 視窗下界
    int beta,                             // 視窗上界
    const ABParams& p,                    // 參數
    bool null_ok                          // 是否允許 null-move
){                                        // 函式主體開始
    ctx.nodes++;                          // 節點計數 +1
    if(ply > ctx.seldepth){               // 追蹤最深抵達層
        ctx.seldepth = ply;               //   ↑ 更新 seldepth
    }                                     // if 結束
    if(ctx.stop){                         // 時間到 / 被要求停止
        return 0;                         //   ↑ 立刻返回
    }                                     // if 結束

    if(state->legal_actions.empty() && state->game_state == UNKNOWN){
                                          // 尚未產生合法步且狀態未知
        state->get_legal_actions();       //   ↑ 產生合法步
    }                                     // if 結束

    if(state->game_state == WIN){         // 此盤面為（剛被吃王的）勝局
        return P_MAX - ply;               //   ↑ 越快將軍分數越高
    }                                     // if 結束
    if(state->game_state == DRAW){        // 和局
        return 0;                         //   ↑ 0 分
    }                                     // if 結束

    int rep_score;                        // 重複分數承接變數
    if(state->check_repetition(history, rep_score)){ // 觸發重複規則
        return rep_score;                 //   ↑ 回傳（和局）分
    }                                     // if 結束

    uint64_t key = state->hash();         // 取得此盤面 Zobrist 鍵
    history.push(key);                    // 推入歷史（追蹤重複）
```

---

## 🔹 階段 2：葉節點與靜默搜尋（quiescence）

當 `depth <= 0` 抵達葉節點，**不能**直接回傳靜態評估 —— 因為可能正停在一次吃子交換的中途
（例如「我吃了你的后」之後就停，沒看到「你下一步吃回我的后」）。這種因深度截斷造成的誤判叫
**視野效應（horizon effect）**。

解法是**靜默搜尋（quiescence search）**：在葉節點繼續只搜「吃子等不安靜的步」，直到局面
「安靜」（沒有立即吃子）才做靜態評估，確保不會在交換到一半時誤判。本實作重用
`AlphaBeta::qsearch`。若關閉靜默，就直接 `evaluate()`。

```cpp
    if(depth <= 0){                       // 抵達葉節點
        int score = p.use_quiescence      //   ↑ 是否啟用靜默搜尋
            ? AlphaBeta::qsearch(state, history, ply, ctx, alpha, beta, p) // 是：搜吃子直到安靜
            : state->evaluate(p.use_kp_eval, p.use_eval_mobility, &history); // 否：靜態評估
        history.pop(key);                 // 彈出歷史
        return score;                     // 回傳葉分數
    }                                     // if 結束
```

---

## 🔹 階段 3：轉置表查表

進主迴圈前先查 TT。命中**可剪枝**的結果就直接回傳（省下整棵子樹）。`alpha_orig` 記下進來時的
原始 `alpha`，留待最後決定要把結果存成 `EXACT / LOWER / UPPER` 哪種旗標。即使不能剪枝，
`tt_move` 也已被設好，等下排序會先試它。

```cpp
    /* === Transposition-table probe === */
                                          // ↑ 轉置表查表
    int alpha_orig = alpha;               // 記住原始 alpha（決定存入旗標用）
    uint16_t tt_move = 0;                 // 雜湊步（供排序）
    if(p.use_tt){                         // 啟用 TT
        int tt_val;                       // 承接 TT 分數
        if(tt_probe(key, depth, ply, alpha, beta, tt_val, tt_move)){ // 命中可用
            history.pop(key);             //   ↑ 先彈出歷史
            return tt_val;                //   ↑ 直接回傳快取分數
        }                                 // if 結束
    }                                     // if 結束
```

---

## 🔹 演算法：null-move 剪枝（最強的前向剪枝之一）

### 直覺
「**如果我這裡強到——就算白白送對手免費一步、讓他連走兩手——我都還是好得超過 beta，
那這個局面實際上幾乎一定是 cutoff。**」既然如此，不必認真展開，直接剪掉回傳 `beta`。

### 做法
透過 `create_null_state()` 造一個「**換對手走、但盤面不動**」的局面（等於我方 pass），
再以**減淺**深度 `depth-1-R`（`R=null_r`，預設 2）、**零寬視窗** `[-beta, -beta+1]` 去搜它
（取負是 negamax）。傳 `null_ok=false` 禁止連續兩次 null（不可無限互相 pass）。
若回來的分數仍 `≥ beta`，剪枝。

### 三道安全守門（缺一不可）
- **`depth >= 3`**：太淺剪了不划算、風險高。
- **非將軍視窗**（`|beta| < MATE_ZONE`）：將軍分附近不適用此啟發式。
- **未被將**：用 `null_st->game_state != WIN` 判斷 —— 若換對手走後他能**立刻吃我王**，
  代表我**正被將軍**，此時 pass 不合法、剪枝不安全。
- **有非兵子力**（`has_non_pawn_material`）：避開逼著殘局。

```cpp
    /* === Null-move pruning ===
     * Give the opponent a free move ("pass"). If our position is still
     * good enough to beat beta even after that, the real position is
     * almost certainly a cutoff — so prune without searching it fully.
     * Guards: depth left, non-mate window, not in check, and we hold
     * some non-pawn material (zugzwang safety). */
                                          // ↑ Null-move：給對手一步免費棋；若我方仍 ≥ beta 就剪枝。
                                          //   守門：深度足夠、非將軍視窗、未被將、有非兵子力
    if(p.use_null && null_ok && depth >= 3 // 啟用且允許且深度 ≥ 3
       && beta < MATE_ZONE && beta > -MATE_ZONE // 非將軍視窗
       && has_non_pawn_material(state)){  // 有非兵子力
        BaseState* null_bs = state->create_null_state(); // 造「換對手走」的盤面
        State* null_st = static_cast<State*>(null_bs);   // 轉回 State*
        if(null_st->game_state != WIN){          /* not in check */
                                          //   ↑ 對手無法立即吃我王 = 我方未被將
            const int R = p.null_r;              /* reduction */
                                          //     ↑ 減淺層數 R
            int null_score = -eval_ctx(null_st, depth - 1 - R, history,
                                       ply + 1, ctx, -beta, -beta + 1, p, false);
                                          //     ↑ 減淺、零寬、禁止再 null 的搜尋（取負）
            delete null_bs;               //     釋放臨時盤面
            if(null_score >= beta){       //     仍能達到 beta
                history.pop(key);         //       ↑ 彈出歷史
                return beta;                     /* fail-high: prune */
                                          //       ↑ 直接剪枝回傳 beta
            }                             //     if 結束
        }else{                            //   被將的情況
            delete null_bs;               //     釋放臨時盤面
        }                                 //   if-else 結束
    }                                     // null-move 區塊結束
```

---

## 🔹 階段 4：排序並初始化迴圈狀態

剪枝沒發生才會走到這。先用 `order_moves` 把這節點的走法排好（帶入 `tt_move`），再初始化：
`best_score` 設為極小值 `M_MAX`（即 `-∞`，等著被任何子分數刷新）、`best_move` 暫取第一步、
`move_index` 從 0 起算（用來區分「第一步 vs 之後各步」與 LMR 的後段判定）。

```cpp
    if(p.order_moves){                    // 啟用排序
        order_moves(state, ply, tt_move); //   ↑ 依 TT/吃子/killer/history 排序
    }                                     // if 結束

    int best_score = M_MAX;               // 目前最佳分（初始為極小值 M_MAX）
    Move best_move = state->legal_actions[0]; // 目前最佳步（暫取第一步）
    int move_index = 0;                   // 第幾步（0 起算）
```

---

## 🔹 演算法：主迴圈 — PVS + LMR（本節點最核心）

對排序後的每一步，先 `next_state` 生出子盤面，再依**三種情境**遞迴。
`same = same_player_as_parent()` 表示「子盤面是否仍由**同一方**走」（本遊戲允許某些情況同方
連走；此時視窗**不翻面、不取負**，也不套 LMR）。

### 情境 A：同方續走（`same`）
不換手，所以用「不取負」的視窗。第一步完整視窗；之後各步先零寬試探，出乎意料才完整重搜
（這就是 PVS 的精神，只是因不換手而不取負）。

### 情境 B：換手 + 第一步（`move_index == 0`）
這是**主變例 PV**。用**完整視窗** `-eval_ctx(..., -beta, -alpha)` 認真搜，建立可靠基準分。

### 情境 C：換手 + 之後各步 → LMR + PVS 三段式
1. **LMR（Late Move Reductions，後期步縮減）判定**：排序好時，**排在後面的安靜步**極可能不是
   最佳步，沒必要花全深去搜。若這步是安靜步（非吃子、非升變）、且夠後段（`move_index ≥ lmr_min_move`）
   夠深（`depth ≥ lmr_min_depth`），就**減淺**：縮減 1 層；若更後段更深（`move_index≥6 且 depth≥6`）
   再多縮 1（共 2）。
2. **減淺 + 零寬「斥候（scout）」**：用 `depth-1-reduction`、視窗 `[-alpha-1, -alpha]` 試探。
   絕大多數會 fail low（確認它不重要）→ 超便宜。
3. **兩段重搜（只在必要時）**：
   - 若一個**被縮減**的步竟然超過 alpha（`reduction>0 && child>alpha`），表示「減淺可能看走眼」，
     先用**全深、仍零寬**驗證一次。
   - 若它確實是**新的 PV 候選**（`alpha < child < beta`），才用**完整視窗**重搜拿到精確分。

> 三段式的妙處：99% 的後段步在第 2 步就被零寬秒剪；只有真正可能更好的少數步，才付出重搜成本。
> 這是 PVS 引擎能搜很深的關鍵。

```cpp
    for(auto& action : state->legal_actions){ // 逐步搜尋
        State* next = static_cast<State*>(state->next_state(action)); // 套用該步得子盤面
        bool same = next->same_player_as_parent(); // 子盤面是否仍同一方走

        int child;                        // 子節點分數
        if(same){                         // 同一方連走（遊戲特性）
            /* same player keeps moving: window not flipped (no LMR here) */
                                          //   ↑ 視窗不翻面、此分支不做 LMR
            if(move_index == 0){          // 第一步
                child = eval_ctx(next, depth - 1, history, ply + 1, ctx, alpha, beta, p);
                                          //     ↑ 完整視窗（不取負）
            }else{                        // 之後各步
                child = eval_ctx(next, depth - 1, history, ply + 1, ctx, alpha, alpha + 1, p);
                                          //     ↑ 零寬試探
                if(child > alpha && child < beta){ // 出乎意料
                    child = eval_ctx(next, depth - 1, history, ply + 1, ctx, alpha, beta, p);
                                          //       ↑ 完整視窗重搜
                }                         //     if 結束
            }                             //   if-else 結束
        }else if(move_index == 0){        // 換手且為第一步
            /* first move: full window establishes the PV */
                                          //   ↑ 第一步以完整視窗建立主變例
            child = -eval_ctx(next, depth - 1, history, ply + 1, ctx, -beta, -alpha, p);
                                          //     ↑ negamax 取負、視窗翻面
        }else{                            // 換手且為之後各步
            /* === Late Move Reductions ===
             * Late, quiet moves are unlikely to be best after good
             * ordering, so scout them at reduced depth; only re-search
             * at full depth if the scout beats alpha. */
                                          //   ↑ LMR：後段安靜步先減淺試探，超過 alpha 才全深重搜
            int reduction = 0;            // 縮減層數
            int attacker = state->piece_at(state->player,
                                           (int)action.first.first, (int)action.first.second);
                                          //   ↑ 移動的棋種
            bool promo = (attacker == 1 &&
                          ((int)action.second.first == 0 ||
                           (int)action.second.first == BOARD_H - 1));
                                          //   ↑ 是否為兵升變
            bool quiet = !is_capture(state, action) && !promo; // 非吃子且非升變 = 安靜步
            if(p.use_lmr && quiet && move_index >= p.lmr_min_move && depth >= p.lmr_min_depth){
                                          //   ↑ 啟用 LMR 且安靜且夠後段夠深
                reduction = 1 + ((move_index >= 6 && depth >= 6) ? 1 : 0);
                                          //     ↑ 縮減 1；更後段更深則縮減 2
            }                             //   if 結束

            /* reduced-depth null-window scout */
                                          //   ↑ 減淺 + 零寬 試探
            child = -eval_ctx(next, depth - 1 - reduction, history, ply + 1, ctx,
                              -alpha - 1, -alpha, p);
                                          //     ↑ 取負、視窗 [-alpha-1,-alpha]
            /* if a reduced move beat alpha, re-search at full depth */
                                          //   ↑ 被縮減的步若超過 alpha，全深重搜
            if(reduction > 0 && child > alpha){ // 有縮減且超過 alpha
                child = -eval_ctx(next, depth - 1, history, ply + 1, ctx,
                                  -alpha - 1, -alpha, p);
                                          //     ↑ 全深、仍零寬
            }                             //   if 結束
            /* full-window PVS re-search if it's a new PV candidate */
                                          //   ↑ 真的是新主變例候選才全視窗重搜
            if(child > alpha && child < beta){ // alpha < child < beta
                child = -eval_ctx(next, depth - 1, history, ply + 1, ctx, -beta, -alpha, p);
                                          //     ↑ 完整視窗重搜
            }                             //   if 結束
        }                                 // if-else 鏈結束
```

---

## 🔹 階段 5：更新最佳值與 beta cutoff

每搜完一步：刷新 `best_score / best_move`；若超過 `alpha` 就抬高 `alpha`（收窄視窗、後面更好剪）；
一旦 `alpha >= beta`，代表這步已經好到「對手根本不會讓我走到這個節點」——
呼叫 `update_cutoff_heuristics` 獎勵這步（餵養 killer/history），然後 **break** 剪掉其餘兄弟步。
記得 `delete next` 釋放子盤面，避免記憶體洩漏。

```cpp
        delete next;                      // 釋放子盤面

        if(child > best_score){           // 找到更好分數
            best_score = child;           //   ↑ 更新最佳分
            best_move = action;           //   ↑ 更新最佳步
        }                                 // if 結束
        if(best_score > alpha){           // 抬高下界
            alpha = best_score;           //   ↑ alpha 提升
        }                                 // if 結束
        if(alpha >= beta){                // 越界 → beta cutoff
            update_cutoff_heuristics(state, best_move, ply, depth); // 記錄 killer/history
            break;                  /* beta cutoff */
                                          //   ↑ 剪掉其餘兄弟步
        }                                 // if 結束
        move_index++;                     // 換下一步
    }                                     // for 結束
```

---

## 🔹 階段 6：把結果存進 TT 並返回

依結果與視窗關係決定旗標（這對下次查表能否剪枝至關重要）：
- `best_score <= alpha_orig`：沒有任何步超過進來時的 alpha → 結果是**上界** `TT_UPPER`。
- `best_score >= beta`：發生過 cutoff → 結果是**下界** `TT_LOWER`。
- 介於之間：在視窗內正常搜完 → **精確值** `TT_EXACT`。

存完 `pop` 掉 history、回傳 `best_score`。

```cpp
    /* === Transposition-table store === */
                                          // ↑ 把結果寫入轉置表
    if(p.use_tt){                         // 啟用 TT
        uint8_t flag = (best_score <= alpha_orig) ? TT_UPPER  // 沒步超過原 alpha → 上界
                     : (best_score >= beta)       ? TT_LOWER  // 發生 cutoff → 下界
                                                  : TT_EXACT; // 否則 → 精確值
        tt_store(key, depth, ply, best_score, flag, pack_move(best_move)); // 寫入
    }                                     // if 結束

    history.pop(key);                     // 彈出歷史
    return best_score;                    // 回傳最佳分
}                                         // eval_ctx 結束
```

---

# 四、`PVS::search` — 根節點

根節點不只是「呼叫一次 eval_ctx」，它額外負責：解析參數、播種排序、套用 **aspiration 視窗**、
逐根步搜尋、處理 fail、回報進度、回填統計。（**疊代加深**與 **2 秒/步時間控制**在更上層的
UBGI 程式，會以 `depth = 1, 2, 3, ...` 反覆呼叫本函式，逐層加深直到時間用完。）

## 🔹 階段 1：前置 — 參數、評估旋鈕、計時、TT 播種、排序

- `ABParams::from_map` 把 UCI 參數解析成開關與旋鈕；`set_eval_params` 把 `tempo`、通路兵縮放
  推進到 `evaluate()`（讓評估也能被調參）。
- 對 TT 做一次**只取雜湊步**的查（`depth 0`、忽略分數），用它替根節點排序播種 ——
  上一輪疊代加深存下的最佳步會排第一，命中率極高。
- `clear_heuristics()` 清空 killer/history，再 `order_moves` 排根節點。

```cpp
/*============================================================
 * PVS — search (root)
 *
 * The first root move establishes the PV with a full window;
 * later root moves are scouted with a null window and only
 * re-searched (full window) if they beat the current best.
 *============================================================*/
                                          // ↑ 根節點：第一步完整視窗建立 PV，其餘零寬試探、超過最佳才重搜
SearchResult PVS::search(                 // 根節點搜尋實作
    State *state,                         // 根盤面
    int depth,                            // 本次深度
    GameHistory& history,                 // 重複堆疊
    SearchContext& ctx                    // 搜尋環境
){                                        // 主體開始
    ctx.reset();                          // 重設節點數 / 旗標
    ABParams p = ABParams::from_map(ctx.params); // 從參數表解析開關與旋鈕
    set_eval_params(p.tempo, p.pp_scale);   /* propagate eval knobs to evaluate() */
                                          //   ↑ 把 tempo / 通路兵縮放推進 evaluate()
    SearchResult result;                  // 結果結構
    result.depth = depth;                 // 記錄本次深度

    auto t_start = std::chrono::high_resolution_clock::now(); // 計時起點

    if(!state->legal_actions.size()){     // 尚無合法步
        state->get_legal_actions();       //   ↑ 產生
    }                                     // if 結束

    const int INF = 1000000;              // 視窗無限大代表值
    int total_moves = (int)state->legal_actions.size(); // 根節點步數（供進度回呼）

    uint64_t key = state->hash();         // 根盤面雜湊
    uint16_t tt_move = 0;                 // 根雜湊步
    if(p.use_tt){                         // 啟用 TT
        tt_init();                        //   ↑ 確保表已配置
        int tt_val;                       //   ↑ 承接（忽略）的分數
        /* probe for the hash move only (depth 0, return ignored → no cutoff) */
                                          //   ↑ 只為取雜湊步而查（depth 0，不會截斷）
        tt_probe(key, 0, 0, -INF, INF, tt_val, tt_move); // 取得 tt_move
    }                                     // if 結束
    clear_heuristics();                   // 清空 killer / history

    if(p.order_moves){                    // 啟用排序
        order_moves(state, 0, tt_move);   //   ↑ 根節點排序（ply 0）
    }                                     // if 結束
```

---

## 🔹 演算法：aspiration window（期望視窗）

### 直覺
疊代加深時，**深度 d 的分數通常和深度 d−1 很接近**。既然如此，與其用 `[-∞, +∞]` 全開搜，
不如賭它落在「上一輪分數附近」的**窄窗** `[prev−delta, prev+delta]`。窗越窄 → alpha-beta
剪枝越兇 → 搜得越快。

### 安全性
萬一真實分數**落在窄窗外**，會被偵測為 fail low / fail high（見下一段），此時把該側放寬重搜。
所以 aspiration **永遠不會搜錯**，最壞情況只是「多搜一次」的時間成本。啟用條件：`depth ≥ 4`、
上一輪恰好淺一層（分數才可比）、且上一輪非將軍分。

```cpp
    /* === Aspiration window ===
     * Re-search the root inside a narrow window around the previous
     * iteration's score. A tighter window prunes more; if the true
     * score falls outside it, we detect the fail-low/high and widen
     * (so it is always correctness-safe — only ever a time cost). */
                                          // ↑ Aspiration：以上一輪分數為中心開窄視窗；落外則放寬重搜，永不影響正確性
    int best_score;                       // 本輪最佳分
    int asp_lo = -INF, asp_hi = INF;      // 視窗上下界（預設無限）
    bool use_asp = p.use_asp && depth >= 4 // 啟用且深度 ≥ 4
                && g_prev_depth == depth - 1 // 上一輪恰好淺一層
                && g_prev_score < MATE_ZONE && g_prev_score > -MATE_ZONE; // 上一輪非將軍
    if(use_asp){                          // 採用 aspiration
        asp_lo = g_prev_score - p.asp_delta; // 下界 = 前分 − delta
        asp_hi = g_prev_score + p.asp_delta; // 上界 = 前分 + delta
    }                                     // if 結束
```

---

## 🔹 演算法：根迴圈（PVS 結構 + 即時回報）

外層 `for(;;)` 是**重搜迴圈**（aspiration fail 時放寬後再來一輪）。內層逐根步，結構與 `eval_ctx`
完全相同：第一步完整視窗、之後零寬試探 + 出乎意料才重搜，並依 `same` 決定是否翻面取負。

特別處：每當刷新最佳步，若開啟 `report_partial` 就呼叫 `ctx.on_root_update(...)`，把「目前最佳步、
分數、進度（第幾步/共幾步）」即時回報給上層（GUI 才能邊想邊顯示主變例）。

```cpp
    for(;;){                              // 重搜迴圈（fail 時放寬後再來）
        int alpha = asp_lo, beta = asp_hi; // 本輪視窗
        best_score = -INF;                // 重設最佳分
        int move_index = 0;               // 第幾步
        Move iter_best = state->legal_actions[0]; // 本輪最佳步（暫取首步）

        for(auto& action : state->legal_actions){ // 逐根步
            State* next = static_cast<State*>(state->next_state(action)); // 子盤面
            bool same = next->same_player_as_parent(); // 是否同一方續走

            int child;                    // 子分數
            if(move_index == 0){          // 第一根步
                child = same              //   ↑ 是否換手
                    ? eval_ctx(next, depth - 1, history, 1, ctx, alpha, beta, p)        // 不換手：完整視窗
                    : -eval_ctx(next, depth - 1, history, 1, ctx, -beta, -alpha, p);    // 換手：取負完整視窗
            }else{                        // 之後各根步
                if(same){                 // 不換手
                    child = eval_ctx(next, depth - 1, history, 1, ctx, alpha, alpha + 1, p); // 零寬試探
                    if(child > alpha && child < beta){ // 出乎意料
                        child = eval_ctx(next, depth - 1, history, 1, ctx, alpha, beta, p); // 完整重搜
                    }                     //   if 結束
                }else{                    // 換手
                    child = -eval_ctx(next, depth - 1, history, 1, ctx, -alpha - 1, -alpha, p); // 取負零寬試探
                    if(child > alpha && child < beta){ // 出乎意料
                        child = -eval_ctx(next, depth - 1, history, 1, ctx, -beta, -alpha, p); // 取負完整重搜
                    }                     //   if 結束
                }                         // if-else 結束
            }                             // if-else 結束

            delete next;                  // 釋放子盤面

            if(move_index == 0){          // 第一步
                iter_best = action;       //   ↑ 先記為最佳
            }                             // if 結束
            if(child > best_score){       // 更好分數
                best_score = child;       //   ↑ 更新最佳分
                iter_best = action;       //   ↑ 更新最佳步
                if(best_score > alpha){   //   ↑ 抬高下界
                    alpha = best_score;   //     alpha 提升
                }                         //   if 結束
                if(p.report_partial && ctx.on_root_update){ // 啟用進度回呼
                    ctx.on_root_update({iter_best, best_score, depth, move_index + 1, total_moves});
                                          //     ↑ 即時回報目前最佳線
                }                         //   if 結束
            }                             // if 結束
            move_index++;                 // 下一步
        }                                 // for 結束

        result.best_move = iter_best;     // 記錄本輪最佳步
```

---

## 🔹 演算法：aspiration 的 fail 處理

搜完一輪後檢查分數是否撞到（或穿出）窄窗：
- **fail low**（`best_score ≤ asp_lo`）：真實分數比預期低很多 → 把**下界**放寬到 `-INF`、`continue` 重搜。
- **fail high**（`best_score ≥ asp_hi`）：真實分數比預期高很多 → 把**上界**放寬到 `+INF`、`continue` 重搜。
- 都沒撞到 → 分數落在窗內、可信 → `break` 接受。

> 注意只放寬撞到的那一側，另一側仍維持窄窗，盡量保留剪枝效益。

```cpp
        /* fail low/high → widen and re-search; otherwise accept */
                                          // ↑ 觸界則放寬重搜，否則接受
        if(best_score <= asp_lo && asp_lo > -INF){ // fail low
            asp_lo = -INF;                //   ↑ 下界放寬
            continue;                     //   ↑ 重搜
        }                                 // if 結束
        if(best_score >= asp_hi && asp_hi < INF){ // fail high
            asp_hi = INF;                 //   ↑ 上界放寬
            continue;                     //   ↑ 重搜
        }                                 // if 結束
        break;                            // 在窗內 → 接受
    }                                     // for(;;) 結束
```

---

## 🔹 階段 5：保存、存 TT、回填統計

- 把本輪分數/深度存進 `g_prev_score / g_prev_depth`，供**下一輪**疊代加深的 aspiration 用。
- 把根結果以 **`TT_EXACT`** 存進 TT（只在未中途停止時），這樣下一輪會**最先搜這步**，
  形成「上一輪最佳步 → 這輪首先試 → 命中即抬高 alpha → 全樹剪枝更兇」的正向循環。
- 回填耗時、節點數、seldepth、主變例（此處 `pv` 僅放最佳步），回傳 `result`。

```cpp
    g_prev_score = best_score;            // 保存本輪分數（給下一輪 aspiration）
    g_prev_depth = depth;                 // 保存本輪深度

    /* Store the root result (EXACT — full window) so the next
     * iterative-deepening pass searches this move first. */
                                          // ↑ 以 EXACT 存根結果，讓下一輪疊代加深最先搜這步
    if(p.use_tt && !ctx.stop){            // 啟用 TT 且未中途停止
        tt_store(key, depth, 0, best_score, TT_EXACT, pack_move(result.best_move)); // 寫入
    }                                     // if 結束

    auto t_end = std::chrono::high_resolution_clock::now(); // 計時終點
    result.score = best_score;            // 填入分數
    result.nodes = ctx.nodes;             // 填入節點數
    result.seldepth = ctx.seldepth;       // 填入最大深度
    result.time_ms = std::chrono::duration<double, std::milli>(t_end - t_start).count(); // 填入耗時(ms)
    result.pv = {result.best_move};       // 主變例（此處僅放最佳步）

    return result;                        // 回傳結果
}                                         // search 結束
```

---

# 五、參數宣告（`default_params` / `param_defs`）

## 🔹 為什麼參數化
所有功能開關（`UseTT`、`UseNullMove`、`UseLMR`…）與數值旋鈕（`Tempo`、`NullR`、`LMRMinMove`…）
都做成**執行期 UCI 選項**。好處：可以**不重新編譯**就關掉某技術或 A/B 調參（自我對弈實驗）。
`default_params` 給出「重現滿血引擎」的預設值；`param_defs` 額外提供型別（CHECK 布林 / SPIN 數值）
與合法範圍，供 UCI 介面宣告選項。

```cpp
/*============================================================
 * PVS — default_params / param_defs (same knobs as AlphaBeta)
 *============================================================*/
                                          // ↑ 預設參數 / 參數定義（旋鈕同 AlphaBeta）
ParamMap PVS::default_params(){           // 預設參數表
    return {                              // 回傳鍵值對
        {"UseKPEval", "true"},            // 啟用 KP 評估（材料+PST+向性）
        {"UseEvalMobility", "true"},      // 啟用機動力加分
        {"ReportPartial", "true"},        // 回報根節點即時最佳
        {"OrderMoves", "true"},           // 啟用走法排序
        {"UseQuiescence", "true"},        // 啟用靜默搜尋
        {"UseTT", "true"},                // 啟用轉置表
        {"UseNullMove", "true"},          // 啟用 null-move 剪枝
        {"UseLMR", "true"},               // 啟用後期步縮減
        {"UseAspiration", "true"},        // 啟用 aspiration 視窗
        {"Tempo", "6"},                   // 該走方先手加分
        {"PassedPawnScale", "100"},       // 通路兵獎勵縮放(%)
        {"NullR", "2"},                   // null-move 減淺層數
        {"LMRMinMove", "3"},              // LMR 起始步序
        {"LMRMinDepth", "3"},             // LMR 最低深度
        {"AspDelta", "30"},               // aspiration 視窗半寬
    };                                    // map 結束
}                                         // default_params 結束

std::vector<ParamDef> PVS::param_defs(){  // 參數定義（型別/範圍，供 UCI 宣告）
    return {                              // 回傳定義清單
        {"UseKPEval", ParamDef::CHECK, "true"},       // 布林：KP 評估
        {"UseEvalMobility", ParamDef::CHECK, "true"}, // 布林：機動力
        {"ReportPartial", ParamDef::CHECK, "true"},   // 布林：即時回報
        {"OrderMoves", ParamDef::CHECK, "true"},      // 布林：排序
        {"UseQuiescence", ParamDef::CHECK, "true"},   // 布林：靜默搜尋
        {"UseTT", ParamDef::CHECK, "true"},           // 布林：轉置表
        {"UseNullMove", ParamDef::CHECK, "true"},     // 布林：null-move
        {"UseLMR", ParamDef::CHECK, "true"},          // 布林：LMR
        {"UseAspiration", ParamDef::CHECK, "true"},   // 布林：aspiration
        {"Tempo", ParamDef::SPIN, "6", 0, 50},        // 數值：先手加分 0..50
        {"PassedPawnScale", ParamDef::SPIN, "100", 0, 400}, // 數值：通路兵縮放 0..400
        {"NullR", ParamDef::SPIN, "2", 1, 4},         // 數值：null 減淺 1..4
        {"LMRMinMove", ParamDef::SPIN, "3", 1, 12},   // 數值：LMR 起始步序 1..12
        {"LMRMinDepth", ParamDef::SPIN, "3", 1, 12},  // 數值：LMR 最低深度 1..12
        {"AspDelta", ParamDef::SPIN, "30", 5, 200},   // 數值：aspiration 半寬 5..200
    };                                    // 清單結束
}                                         // param_defs 結束
```

---

# 六、速記重點對照表

| 技術 | 函式 / 位置 | 一句話 | 為什麼有效 |
|------|------------|--------|-----------|
| 主變例搜尋 (PVS) | `eval_ctx` / `search` 主迴圈 | 第一步全視窗，其餘零寬試探 + 視情況重搜 | 排序好時大多試探秒剪，節點少於 alpha-beta 但結果相同 |
| 轉置表 (TT) | `tt_probe` / `tt_store` | 以雜湊快取結果、存最佳步、深度優先取代 | 避免重搜 transposition；最佳步是最強排序訊號 |
| 將軍分數修正 | `tt_score_to_store/from_probe` | 讓快取的將軍分跨 ply 仍正確 | 將軍分相對節點，跨 ply 讀取需重新錨定距離 |
| 走法排序 | `order_moves` | TT步 → 吃子(MVV-LVA) → killer → 安靜步(history) | alpha-beta 剪枝量正比於排序品質 |
| MVV-LVA | `mvv_lva` | 大受害者×16 − 小攻擊者 | 「以小吃大」通常最賺、最該先搜 |
| killer / history | `update_cutoff_heuristics` | 記錄造成 cutoff 的安靜步以便先試 | 同 ply 兄弟局面相似，殺手步常重複奏效 |
| Null-move 剪枝 | `eval_ctx` null 區塊 | 給對手免費一步仍 ≥ beta 就剪枝 | 強局面禁得起「送一手」；四道守門避開誤剪 |
| 靜默搜尋 | 葉節點 `qsearch` | 葉節點繼續搜吃子直到安靜 | 消除「交換到一半就評估」的視野效應 |
| 後期步縮減 (LMR) | `eval_ctx` else 分支 | 後段安靜步減淺搜，超過 alpha 才全深 | 排序後尾段步幾乎不會是最佳，省深度 |
| Aspiration 視窗 | `search` for(;;) | 以前分開窄窗，觸界放寬重搜 | 深度間分數接近，窄窗剪更多；fail 才放寬，不失正確性 |
| negamax | 各處 `-eval_ctx(...)` | 子節點分數取負、視窗翻面 | 用一套 max 邏輯處理雙方對抗 |
