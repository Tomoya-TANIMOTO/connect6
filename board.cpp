#include <bits/stdc++.h>
using namespace std;

struct Board {
    int N = 19;
    // 0: empty, 1: black, 2: white
    vector<unsigned char> cell; // size N*N
    bool black_to_move = true;
    uint32_t stones_placed = 0; // 置石総数（初手判定に使う）
};

// ---- 直前手の履歴（1手＝1石 or 2石） -----------------------------------
struct MoveRec { char color; int x1, y1; int x2, y2; }; // 単点なら x2=y2=-1
static vector<MoveRec> g_history;

// ---- 未完了の「半手」（単点だけ置いた状態） ------------------------------
static bool g_pending = false;
static char g_pcolor = 'B';
static int  g_px = -1, g_py = -1;

// 内部ヘルパ
static inline int idx(const Board* b, int x, int y) { return y * b->N + x; }

static inline bool on_board(const Board* b, int x, int y) {
    return (0 <= x && x < b->N && 0 <= y && y < b->N);
}

static int letter_to_x(char c) {
    if ('a' <= c && c <= 'z') c = char(c - 'a' + 'A');
    if (c >= 'A' && c <= 'H') return c - 'A';
    if (c >= 'J' && c <= 'T') return c - 'A' - 1; // Iを飛ばす
    return -1;
}

static bool parse_coord(const string& s, int& x, int& y, int N) {
    // 例: "D10" / "Q3" / "pass"（今回は未対応）
    if (s == "pass" || s == "PASS") return false;
    if (s.size() < 2) return false;

    const int X = letter_to_x(s[0]);
    if (X < 0) return false;

    int num = 0;
    for (size_t i = 1; i < s.size(); ++i) {
        if (!isdigit(static_cast<unsigned char>(s[i]))) return false;
        num = num * 10 + (s[i] - '0');
    }
    if (num < 1 || num > N) return false;

    x = X;
    y = N - num; // 上をNとする囲碁風（上から下へ N, N-1, ..., 1）
    return true;
}

static bool place(Board* b, int x, int y, unsigned char stone) {
    if (!on_board(b, x, y)) return false;
    if (b->cell[idx(b, x, y)] != 0) return false;
    b->cell[idx(b, x, y)] = stone;
    b->stones_placed++; // 置けたらカウント
    return true;
}

// "+ または空白" で分割（GoGUIの %P=空白区切り にも対応）
static vector<string> split_tokens(const string& token) {
    vector<string> out; string cur;
    for (unsigned char uc : token) {
        char c = (char)uc;
        if (c == '+' || isspace((unsigned char)c)) {
            if (!cur.empty()) { out.push_back(cur); cur.clear(); }
        } else cur.push_back(c);
    }
    if (!cur.empty()) out.push_back(cur);
    return out;
}

// 生成/破棄
static inline void reset_pending() { g_pending = false; g_pcolor='B'; g_px=g_py=-1; }

Board* make_initial_board(int size) {
    Board* b = new Board();
    b->N = size;
    b->cell.assign(size * size, 0);
    b->black_to_move = true;
    b->stones_placed = 0;
    g_history.clear();
    reset_pending();
    return b;
}
void destroy_board(Board* b) { delete b; }

// 盤操作
void set_boardsize(Board* b, int n) {
    b->N = n;
    b->cell.assign(n * n, 0);
    b->black_to_move = true;
    b->stones_placed = 0;
    g_history.clear();
    reset_pending();
}
void clear_board(Board* b) {
    fill(b->cell.begin(), b->cell.end(), 0);
    b->black_to_move = true;
    b->stones_placed = 0;
    g_history.clear();
    reset_pending();
}

bool is_legal_single(const Board* b, int x, int y) {
    return on_board(b, x, y) && (b->cell[idx(b, x, y)] == 0);
}

// この手番で必要な石数（初手=1、pending中=残り1、それ以外=2）
int stones_required_this_turn(const Board* b) {
    (void)b; // 現実装はボード1つ想定（pendingはグローバル管理）
    if (g_pending) return 1;
    return (/*初手*/ (b->stones_placed == 0)) ? 1 : 2;
}

// ---- 探索・評価用の補助 --------------------------------------------------
unsigned char get_cell(const Board* b, int x, int y) {
    if (!on_board(b, x, y)) return 255; // 番兵
    return b->cell[idx(b, x, y)];
}
static inline unsigned char color_to_stone(char color) {
    return (toupper(color) == 'B') ? 1 : 2;
}

// 6連チェック（置いた石を起点に4方向）
static int count_dir(const Board* b, int x, int y, int dx, int dy, unsigned char s) {
    int c = 0, X = x + dx, Y = y + dy;
    while (on_board(b, X, Y) && b->cell[idx(b, X, Y)] == s) { c++; X += dx; Y += dy; }
    return c;
}
bool has_six_from(const Board* b, int x, int y, unsigned char s) {
    if (!on_board(b, x, y)) return false;
    if (b->cell[idx(b, x, y)] != s) return false;
    const int dirs[4][2] = {{1,0},{0,1},{1,1},{1,-1}};
    for (auto& d : dirs) {
        int total = 1 + count_dir(b,x,y, d[0],d[1], s)
                      + count_dir(b,x,y, -d[0],-d[1], s);
        if (total >= 6) return true;
    }
    return false;
}

// ---- 探索専用：仮置き/巻き戻し（履歴やpendingに影響を与えない） --------
// 単点は x2=y2=-1 を要求（need==1 のとき）
bool apply_pair(Board* b, char color, int x1, int y1, int x2, int y2) {
    const unsigned char s = (toupper(color) == 'B') ? 1 : 2;
    const int need = stones_required_this_turn(b);
    const bool single = (need == 1);

    if (single) {
        if (x2 != -1 || y2 != -1) return false;
    } else {
        if (x2 < 0 || y2 < 0) return false;
    }

    // 合法性
    if (!is_legal_single(b, x1, y1)) return false;
    if (!single) {
        if ((x1 == x2 && y1 == y2) || !is_legal_single(b, x2, y2)) return false;
    }

    // 置石
    if (!place(b, x1, y1, s)) return false;
    if (!single) {
        if (!place(b, x2, y2, s)) return false; // 理論上通る
    }

    // 手番反転（探索中の1手を完了）
    b->black_to_move = !b->black_to_move;
    return true;
}

// apply_pair の逆操作。手番を戻し、石を外し、stones_placed も戻す。
void undo_pair(Board* b, int x1, int y1, int x2, int y2) {
    // 手番を元に戻す
    b->black_to_move = !b->black_to_move;

    auto undo_one = [&](int x, int y) {
        if (x >= 0 && y >= 0) {
            if (on_board(b, x, y) && b->cell[idx(b, x, y)] != 0) {
                b->cell[idx(b, x, y)] = 0;
                if (b->stones_placed > 0) b->stones_placed--;
            }
        }
    };
    // 2点→1点の順で戻す（順はどちらでも可）
    undo_one(x2, y2);
    undo_one(x1, y1);
}

// ---- GTP: "D10" / "D10 K10" / "D10+K10" に対応 --------------------------
bool play_token(Board* b, char color, const string& token) {
    const unsigned char s = color_to_stone(color);
    const vector<string> parts = split_tokens(token);

    if (parts.empty() || parts.size() > 2) return false;

    // 2点まとめて（合成トークン or 空白2点）
    if (parts.size() == 2) {
        if (g_pending) return false; // 半手中に2点まとめは不可（運用上の整合）
        int x1,y1,x2,y2;
        if (!parse_coord(parts[0], x1, y1, b->N)) return false;
        if (!parse_coord(parts[1], x2, y2, b->N)) return false;
        if (!is_legal_single(b, x1, y1)) return false;
        if ((x1==x2 && y1==y2) || !is_legal_single(b, x2, y2)) return false;

        if (!place(b, x1, y1, s)) return false;
        if (!place(b, x2, y2, s)) return false;

        g_history.push_back(MoveRec{ (char)toupper(color), x1,y1, x2,y2 });
        b->black_to_move = !b->black_to_move;
        return true;
    }

    // 単点（1つだけ）
    int x,y;
    if (!parse_coord(parts[0], x, y, b->N)) return false;

    // 初手（必要石=1） or pending中（残り1）
    if (stones_required_this_turn(b) == 1) {
        // pending中なら色一致チェック
        if (g_pending && (toupper(color) != toupper(g_pcolor))) return false;
        if (!is_legal_single(b, x, y)) return false;

        if (g_pending) {
            // 2個目を置いて1手確定
            if ((x==g_px && y==g_py)) return false; // 同一点は不可
            if (!place(b, x, y, s)) return false;

            g_history.push_back(MoveRec{ (char)toupper(color), g_px,g_py, x,y });
            reset_pending();
            b->black_to_move = !b->black_to_move;
            return true;
        } else {
            // 初手1個（Connect6の最初だけ）を確定させる
            if (!is_legal_single(b, x, y)) return false;
            if (!place(b, x, y, s)) return false;

            g_history.push_back(MoveRec{ (char)toupper(color), x,y, -1,-1 });
            b->black_to_move = !b->black_to_move;
            return true;
        }
    }

    // ここからは通常手（必要石=2）で、単点＝「半手」として受け付ける
    if (g_pending) {
        // 別色が来たら不正（同一手番で同色2点がルール）
        if (toupper(color) != toupper(g_pcolor)) return false;
        // 2個目を置いて確定
        if ((x==g_px && y==g_py) || !is_legal_single(b, x, y)) return false;
        if (!place(b, x, y, s)) return false;

        g_history.push_back(MoveRec{ (char)toupper(color), g_px,g_py, x,y });
        reset_pending();
        b->black_to_move = !b->black_to_move;
        return true;
    } else {
        // 1個目を置いて pending にする（手番はまだ交代しない）
        if (!is_legal_single(b, x, y)) return false;
        if (!place(b, x, y, s)) return false;

        g_pending = true; g_pcolor = (char)toupper(color); g_px = x; g_py = y;
        // 手番維持（2個目待ち）
        return true;
    }
}

// 手番参照（GoGUI拡張用）
bool black_to_play(const Board* b){ return b->black_to_move; }

// 6連の検出（4方向）
static bool has_six_anywhere(const Board* b, unsigned char s){
    const int N=b->N;
    const int dirs[4][2]={{1,0},{0,1},{1,1},{1,-1}};
    auto at=[&](int x,int y){ return (x>=0&&y>=0&&x<N&&y<N)? b->cell[idx(b,x,y)] : 255; };
    for(int y=0;y<N;++y)for(int x=0;x<N;++x){
        if (at(x,y)!=s) continue;
        for(auto& d:dirs){
            int len=1, X=x+d[0], Y=y+d[1];
            while(at(X,Y)==s){ ++len; X+=d[0]; Y+=d[1]; }
            if (len>=6) return true;
        }
    }
    return false;
}

// GoGUI拡張：最終結果
bool detect_winner(const Board* b, char& winner){
    bool bw = has_six_anywhere(b, 1);
    bool ww = has_six_anywhere(b, 2);
    if (bw && !ww){ winner='B'; return true; }
    if (ww && !bw){ winner='W'; return true; }
    // 両立や未確定は unknown 扱い
    return false;
}

// 空点一覧（I列スキップで座標に戻す）
vector<string> list_empty_coords(const Board* b){
    vector<string> out;
    auto tocoord=[&](int x,int y){
        char col = (x<8)? char('A'+x) : char('A'+x+1); // I飛ばし
        int row = b->N - y;
        return string() + col + to_string(row);
    };
    for(int y=0;y<b->N;++y)for(int x=0;x<b->N;++x){
        if (b->cell[idx(b,x,y)]==0) out.push_back(tocoord(x,y));
    }
    return out;
}


// ---- GTP: undo -----------------------------------------------------------
bool undo_last(Board* b) {
    // まず pending 半手があればそれを取り消す（履歴未登録のため）
    if (g_pending) {
        if (g_px >= 0 && g_py >= 0) {
            if (on_board(b, g_px, g_py) && b->cell[idx(b, g_px, g_py)] != 0) {
                b->cell[idx(b, g_px, g_py)] = 0;
                if (b->stones_placed > 0) b->stones_placed--;
            }
        }
        reset_pending();
        // 手番は未交代のままなので、そのまま true
        return true;
    }

    if (g_history.empty()) return false;
    const MoveRec rec = g_history.back();
    g_history.pop_back();

    // 手番を元に戻す
    b->black_to_move = !b->black_to_move;

    auto undo_one = [&](int x, int y) {
        if (x >= 0 && y >= 0) {
            if (on_board(b, x, y) && b->cell[idx(b, x, y)] != 0) {
                b->cell[idx(b, x, y)] = 0;
                if (b->stones_placed > 0) b->stones_placed--;
            }
        }
    };
    // 2点→1点の順で戻す（順はどちらでも可）
    undo_one(rec.x2, rec.y2);
    undo_one(rec.x1, rec.y1);
    return true;
}

string pretty_board(const Board* b) {
    // 可視化（デバッグ用）
    stringstream ss;
    for (int y = 0; y < b->N; ++y) {
        ss << setw(2) << (b->N - y) << " ";
        for (int x = 0; x < b->N; ++x) {
            const unsigned char v = b->cell[idx(b, x, y)];
            const char c = (v == 0) ? '.' : (v == 1 ? 'X' : 'O');
            ss << c << ' ';
        }
        ss << '\n';
    }
    ss << "   ";
    for (int x = 0; x < b->N; ++x) {
        const char c = (x < 8) ? char('A' + x) : char('A' + x + 1); // I飛ばす
        ss << c << ' ';
    }
    return ss.str();
}

// alphabeta から盤サイズを参照するためのAPI
int board_size(const Board* b) { return b->N; }
