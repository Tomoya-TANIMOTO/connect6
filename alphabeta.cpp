#include <bits/stdc++.h>
using namespace std;

// board.cpp 側のAPI
struct Board;
bool is_legal_single(const Board* b, int x, int y);
int  board_size(const Board* b);
int  stones_required_this_turn(const Board* b);
unsigned char get_cell(const Board* b, int x, int y); // 0 empty, 1 black, 2 white
bool apply_pair(Board* b, char color, int x1,int y1, int x2,int y2);
void undo_pair (Board* b, int x1,int y1, int x2,int y2);
bool has_six_from(const Board* b, int x, int y, unsigned char s);

// ヘルパ
static inline unsigned char stone_of(char c){ return (toupper(c)=='B')?1:2; }
static inline char opp(char c){ return (c=='B')?'W':'B'; }

static string coord_to_string(int N, int x, int y) {
    const char col = (x < 8) ? char('A' + x) : char('A' + x + 1);
    const int row = N - y;
    return string(1, col) + to_string(row);
}
static inline uint64_t now_ms() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(high_resolution_clock::now().time_since_epoch()).count();
}

// 候補生成
static void collect_hotspots(const Board* b, vector<pair<int,int>>& out) {
    const int N = board_size(b);
    int stones = 0;
    for (int y=0;y<N;++y) for (int x=0;x<N;++x) if (get_cell(b,x,y)!=0) stones++;
    if (stones==0){ out.emplace_back(N/2,N/2); return; }

    vector<vector<char>> mark(N, vector<char>(N,0));
    auto put = [&](int x,int y){
        if (x>=0 && x<N && y>=0 && y<N && !mark[y][x] && is_legal_single(b,x,y)) {
            mark[y][x]=1; out.emplace_back(x,y);
        }
    };
    for (int y=0;y<N;++y) for (int x=0;x<N;++x) if (get_cell(b,x,y)!=0){
        for (int dy=-2; dy<=2; ++dy) for (int dx=-2; dx<=2; ++dx)
            if (abs(dx)+abs(dy)<=2) put(x+dx,y+dy);
    }
    if (out.empty()){
        for (int y=0;y<N;++y) for (int x=0;x<N;++x) if (is_legal_single(b,x,y)) out.emplace_back(x,y);
    }
}

static int point_score(const Board* b, int x, int y, char me) {
    const int N=board_size(b), cx=N/2, cy=N/2;
    int s = -(abs(x-cx)+abs(y-cy));
    for (int dy=-2; dy<=2; ++dy) for (int dx=-2; dx<=2; ++dx){
        int X=x+dx,Y=y+dy; if (X<0||Y<0||X>=N||Y>=N) continue;
        unsigned char v=get_cell(b,X,Y);
        if (v==stone_of(me)) s += (abs(dx)+abs(dy)<=1?3:1);
        else if (v!=0)       s += 1;
    }
    return s;
}
static vector<pair<int,int>> topK_points(const Board* b, char me, int K) {
    vector<pair<int,int>> cand; collect_hotspots(b, cand);
    vector<pair<int,pair<int,int>>> scored;
    for (auto p : cand) scored.push_back({point_score(b,p.first,p.second,me), p});
    sort(scored.begin(), scored.end(), [](auto& A, auto& B){ return A.first>B.first; });
    vector<pair<int,int>> out;
    for (int i=0;i<(int)scored.size() && (int)out.size()<K; ++i) out.push_back(scored[i].second);
    return out;
}
static vector<pair<pair<int,int>,pair<int,int>>> topM_pairs(
        const Board* b, char me, const vector<pair<int,int>>& pts, int need, int M) {
    vector<pair<pair<int,int>,pair<int,int>>> out;
    if (need==1){ for (auto p:pts) out.push_back({p,{-1,-1}}); return out; }

    vector<tuple<int,int,int>> ord; // (score,i,j)
    const int n=(int)pts.size();
    for (int i=0;i<n;++i) for (int j=i+1;j<n;++j){
        int s = point_score(b,pts[i].first,pts[i].second,me)
              + point_score(b,pts[j].first,pts[j].second,me)
              - (abs(pts[i].first-pts[j].first)+abs(pts[i].second-pts[j].second));
        ord.emplace_back(s,i,j);
    }
    sort(ord.begin(), ord.end(), [](auto& A, auto& B){ return get<0>(A)>get<0>(B); });
    for (int k=0;k<(int)ord.size() && (int)out.size()<M; ++k){
        auto [_,i,j]=ord[k]; out.push_back({pts[i],pts[j]});
    }
    return out;
}

// 評価関数 （連の長さと空間）
struct LineStat{ int my_o5=0,my_s5=0,my_o4=0,my_s4=0,my_o3=0,my_s3=0;
                 int op_o5=0,op_s5=0,op_o4=0,op_s4=0,op_o3=0,op_s3=0; };

static void accumulate_line_stats(const Board* b, char me, LineStat& st){
    const int N=board_size(b);
    auto add = [&](int len, bool openL, bool openR, unsigned char who){
        bool o = openL && openR, s = (openL ^ openR);
        if (who==stone_of(me)){
            if (len>=5){ if(o) st.my_o5++; else if(s) st.my_s5++; }
            else if (len==4){ if(o) st.my_o4++; else if(s) st.my_s4++; }
            else if (len==3){ if(o) st.my_o3++; else if(s) st.my_s3++; }
        }else{
            if (len>=5){ if(o) st.op_o5++; else if(s) st.op_s5++; }
            else if (len==4){ if(o) st.op_o4++; else if(s) st.op_s4++; }
            else if (len==3){ if(o) st.op_o3++; else if(s) st.op_s3++; }
        }
    };
    const int dirs[4][2]={{1,0},{0,1},{1,1},{1,-1}};
    for (auto& d:dirs){
        int dx=d[0], dy=d[1];
        auto start = [&](int x,int y){
            int px=x-dx, py=y-dy;
            return !(px>=0&&py>=0&&px<N&&py<N && get_cell(b,px,py)==get_cell(b,x,y));
        };
        for (int y=0;y<N;++y) for (int x=0;x<N;++x){
            unsigned char v=get_cell(b,x,y); if (v==0) continue;
            if (!start(x,y)) continue;
            int len=1, X=x+dx, Y=y+dy;
            while (X>=0&&Y>=0&&X<N&&Y<N && get_cell(b,X,Y)==v){ len++; X+=dx; Y+=dy; }
            bool openL=false, openR=false;
            int lx=x-dx, ly=y-dy, rx=X, ry=Y;
            if (lx>=0&&ly>=0&&lx<N&&ly<N && get_cell(b,lx,ly)==0) openL=true;
            if (rx>=0&&ry>=0&&rx<N&&ry<N && get_cell(b,rx,ry)==0) openR=true;
            add(len, openL, openR, v);
        }
    }
}
static int evaluate(const Board* b, char me){
    LineStat st; accumulate_line_stats(b, me, st);
    long long sc = 0;
    sc += 800000LL*st.my_o5 + 400000LL*st.my_s5;
    sc += 12000LL*st.my_o4 + 6000LL*st.my_s4;
    sc += 600LL*st.my_o3 + 250LL*st.my_s3;

    sc -= 800000LL*st.op_o5 + 400000LL*st.op_s5;
    sc -= 12000LL*st.op_o4 + 6000LL*st.op_s4;
    sc -= 600LL*st.op_o3 + 250LL*st.op_s3;

    const int N=board_size(b), cx=N/2, cy=N/2;
    for (int y=0;y<N;++y) for (int x=0;x<N;++x){
        unsigned char v=get_cell(b,x,y); if (!v) continue;
        int d=-(abs(x-cx)+abs(y-cy));
        sc += (v==stone_of(me)? d : -d);
    }
    if (sc>INT_MAX) sc=INT_MAX; if (sc<INT_MIN) sc=INT_MIN;
    return (int)sc;
}

// αβ探索
static int alphabeta(Board* wb, int depth, int alpha, int beta,
                     char to_move, uint64_t hard_deadline_ms,
                     pair<int,int>& pv_a, pair<int,int>& pv_b){
    if (hard_deadline_ms && now_ms() >= hard_deadline_ms) return 0;
    const int need = stones_required_this_turn(wb);
    if (depth==0) return evaluate(wb, opp(to_move));

    auto pts   = topK_points(wb, to_move, 32);
    auto moves = topM_pairs(wb, to_move, pts, need, 96);
    if (moves.empty()) return evaluate(wb, opp(to_move));

    int best = numeric_limits<int>::min()/2;
    pair<int,int> bestA{-1,-1}, bestB{-1,-1};

    for (auto mv : moves){
        auto [a,b] = mv;
        if (!apply_pair(wb, to_move, a.first,a.second, b.first,b.second)) continue;

        unsigned char s = stone_of(to_move);
        bool win = has_six_from(wb, a.first,a.second,s) ||
                  (b.first>=0 && has_six_from(wb, b.first,b.second,s));
        if (win){
            undo_pair(wb, a.first,a.second, b.first,b.second);
            pv_a=a; pv_b=b;
            return 100000000;
        }

        pair<int,int> childA{-1,-1}, childB{-1,-1};
        int val = -alphabeta(wb, depth-1, -beta, -alpha, opp(to_move), hard_deadline_ms, childA, childB);
        undo_pair(wb, a.first,a.second, b.first,b.second);

        if (val > best){ best = val; bestA=a; bestB=b; }
        if (best > alpha) alpha = best;
        if (alpha >= beta) break;
        if (hard_deadline_ms && now_ms() >= hard_deadline_ms) break;
    }
    pv_a=bestA; pv_b=bestB;
    return best;
}

// 反復深化 genmove
string genmove_random(const Board* b, char color, uint64_t deadline_ms) {
    Board* wb = const_cast<Board*>(b);
    const int N    = board_size(wb);
    const int need = stones_required_this_turn(wb);

    auto pts   = topK_points(wb, color, 32);
    auto moves = topM_pairs(wb, color, pts, need, 96);
    if (moves.empty()) return "";

    uint64_t now = now_ms();
    uint64_t budget = (deadline_ms ? (deadline_ms>now ? deadline_ms-now : 0) : 1500);
    if (budget < 50) budget = 50;
    uint64_t hard = now + (budget>20 ? budget-10 : budget);

    pair<int,int> bestA = moves[0].first, bestB = moves[0].second;
    int bestVal = numeric_limits<int>::min()/2;

    // 即勝ち
    for (auto mv : moves){
        auto [a,bp]=mv;
        if (!apply_pair(wb, color, a.first,a.second, bp.first,bp.second)) continue;
        unsigned char s=stone_of(color);
        bool win = has_six_from(wb, a.first,a.second,s) ||
                  (bp.first>=0 && has_six_from(wb, bp.first,bp.second,s));
        undo_pair(wb, a.first,a.second, bp.first,bp.second);
        if (win){ bestA=a; bestB=bp; bestVal=100000000; goto end_search; }
    }

    for (int depth=1; ; ++depth){
        if (now_ms() >= hard) break;
        int alpha = numeric_limits<int>::min()/2, beta = numeric_limits<int>::max()/2;
        pair<int,int> pvA{-1,-1}, pvB{-1,-1};
        int val = alphabeta(wb, depth, alpha, beta, color, hard, pvA, pvB);
        if (now_ms() >= hard) break;
        if (val > bestVal && pvA.first!=-1){
            bestVal = val; bestA = pvA; bestB = pvB;
        }
    }

end_search:
    return coord_to_string(N, bestA.first, bestA.second);
}
