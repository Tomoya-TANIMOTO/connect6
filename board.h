// header guard
#ifndef BOARD_H  // もし BOARD_H が未定義なら
#define BOARD_H  // BOARD_H を定義する

#include <vector>

struct MoveRec {
    char color;
    int x1, y1;
    int x2, y2;
};

class Board {
public:
    Board(int rows, int cols);
    ~Board();

    bool place(int x, int y, unsigned char stone);
    bool on_board(int x, int y) const;
    bool is_legal_single(int x, int y) const;
    int stones_required_this_turn() const;
    bool undo_last();
    std::vector<std::string> list_empty_coords() const;
    std::string pretty_board() const;

private:
    int size_;
    // 0: empty, 1: black, 2: white
    std::vector<unsigned char> cell_;
    bool black_to_move_;
    // 置石総数（初手判定に使う）
    uint32_t stones_placed_;

    std::vector<MoveRec> history_;
    bool pending_;
    char pending_color_;
    int pending_x_;
    int pending_y_;
};

#endif // BOARD_H