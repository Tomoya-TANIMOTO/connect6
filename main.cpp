#include <bits/stdc++.h>
using namespace std;

// board.cpp 提供API
struct Board;

Board*  make_initial_board(int size);
void    destroy_board(Board* b);
void    set_boardsize(Board* b, int n);
void    clear_board(Board* b);
bool    play_token(Board* b, char color, const string& token); // "D10" / "D10+K10"
string  pretty_board(const Board* b);
bool    undo_last(Board* b);

int     board_size(const Board* b);
bool    detect_winner(const Board* b, char& winner); // winner='B'|'W'
vector<string> list_empty_coords(const Board* b);

// pending/手番参照（board.cpp に追加済みの想定）
bool    black_to_play(const Board* b);

// 探索（簡易）
string  genmove_random(const Board* b, char color, uint64_t deadline_ms);

// ==== GTP ヘルパ
static inline void ok(const string& msg=""){ cout << "= " << msg << "\n\n"; cout.flush(); }
static inline void ng(const string& msg){ cout << "? " << msg << "\n\n"; cout.flush(); }

static vector<string> tokenize(const string& s){
    vector<string> t; string cur; istringstream iss(s);
    while (iss >> cur) t.push_back(cur);
    return t;
}

// 複数行応答（list_commands 用）
static void ok_lines(const vector<string>& lines){
    cout << "= \n";
    for (auto& l : lines) cout << l << "\n";
    cout << "\n";
    cout.flush();
}

static const vector<string>& supported_commands(){
    static const vector<string> cmds = {
        "protocol_version",
        "name",
        "version",
        "known_command",
        "list_commands",
        "boardsize",
        "clear_board",
        "komi",
        "play",
        "genmove",
        "undo",
        "quit",
        // GoGUI rules extension
        "gogui-rules_game_id",
        "gogui-rules_board_size",
        "gogui-rules_board",
        "gogui-rules_legal_moves",
        "gogui-rules_side_to_move",
        "gogui-rules_final_result",
        // debug
        "showboard",
    };
    return cmds;
}

static bool is_supported(const string& cmd){
    const auto& cmds = supported_commands();
    return find(cmds.begin(), cmds.end(), cmd) != cmds.end();
}

int main(){
    ios::sync_with_stdio(false);
    cin.tie(nullptr);

    Board* board = make_initial_board(19);

    string line;
    while (getline(cin, line)){
        if(line.empty()) continue;
        auto tok = tokenize(line);
        if (tok.empty()) continue;
        const string cmd = tok[0];

        // ---- 基本GTP ----
        if (cmd == "protocol_version"){ ok("2"); continue; }
        if (cmd == "name"){ ok("cpp-connect6"); continue; }
        if (cmd == "version"){ ok("0.2"); continue; }

        if (cmd == "known_command"){
            if (tok.size()!=2){ ng("syntax: known_command <cmd>"); continue; }
            ok(is_supported(tok[1]) ? "true" : "false"); continue;
        }
        if (cmd == "list_commands"){ ok_lines(supported_commands()); continue; }

        if (cmd == "quit"){ ok(); break; }

        if (cmd == "boardsize"){
            if (tok.size()!=2){ ng("syntax: boardsize <int>"); continue; }
            int n;
            try { n = stoi(tok[1]); } catch (...) { ng("syntax: boardsize <int>"); continue; }
            if (n<3 || n>25){ ng("range 3..25"); continue; }
            set_boardsize(board, n);
            ok(); continue;
        }

        if (cmd == "clear_board"){ clear_board(board); ok(); continue; }
        if (cmd == "komi"){ ok(); continue; } // Connect6では未使用

        if (cmd == "undo"){
            if (!undo_last(board)) ng("cannot undo"); else ok();
            continue;
        }

        // ---- 対局操作 ----
        if (cmd == "play"){ // play <B|W> <c1> [c2]
            if (tok.size()!=3 && tok.size()!=4){ ng("syntax: play <B|W> <coord> [coord]"); continue; }

            // ★ 既に終局していたら人間入力も拒否
            char wres='?';
            if (detect_winner(board, wres)){ ng("game over"); continue; }

            char color = toupper(tok[1][0]);
            if (color!='B' && color!='W'){ ng("color must be B or W"); continue; }

            string token = tok[2];
            if (tok.size()==4) token += "+" + tok[3]; // "H13 L12" → "H13+L12"

            if (!play_token(board, color, token)) ng("illegal move");
            else ok();
            continue;
        }

        if (cmd == "genmove"){ // genmove <B|W> （1回につき1座標を返す）
            if (tok.size()!=2){ ng("syntax: genmove <B|W>"); continue; }
            char color = toupper(tok[1][0]);
            if (color!='B' && color!='W'){ ng("color must be B or W"); continue; }

            // ★ 勝敗チェック：勝っていれば対局終了扱い（合法手を返さない）
            char wres='?';
            if (detect_winner(board, wres)){
                ok("resign");          // GTP的にはこれで終局に入る
                continue;
            }

            string mv = genmove_random(board, color, /*deadline_ms=*/0);
            if (mv.empty()){
                ok("resign");
                continue;
            }
            // 自己反映（GUIがplayを返さない場合に備える）。失敗しても落とさない。
            if (!play_token(board, color, mv)){
                cerr << "[warn] play_token failed after genmove: " << mv << "\n";
            }
            ok(mv);
            continue;
        }

        // ---- GoGUI拡張（Connect6向け）----
        if (cmd == "gogui-rules_game_id"){ ok("Connect6"); continue; }
        if (cmd == "gogui-rules_board_size"){ ok(to_string(board_size(board))); continue; }
        if (cmd == "gogui-rules_side_to_move"){
            ok(black_to_play(board) ? "black" : "white");
            continue;
        }
        if (cmd == "gogui-rules_board"){
            ok("\n" + pretty_board(board));
            continue;
        }
        if (cmd == "gogui-rules_legal_moves"){
            // ★ 勝敗が出ていれば合法手は空（終局）
            char wres='?';
            if (detect_winner(board, wres)){ ok(); continue; }

            auto ls = list_empty_coords(board);
            if (ls.empty()) { ok(); }
            else {
                string s; for (size_t i=0;i<ls.size();++i){ if(i) s+=' '; s+=ls[i]; }
                ok(s);
            }
            continue;
        }
        if (cmd == "gogui-rules_final_result"){
            char wres='?';
            if (detect_winner(board, wres)) ok((wres=='B') ? "black wins." : "white wins.");
            else ok("unknown");
            continue;
        }

        // ---- デバッグ ----
        if (cmd == "showboard"){ cerr << pretty_board(board) << "\n"; ok(); continue; }

        ng("unknown command");
    }

    destroy_board(board);
    return 0;
}
