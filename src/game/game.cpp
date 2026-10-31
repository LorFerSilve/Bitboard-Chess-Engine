#include "chess/game.hpp"
#include <bit>
#include <algorithm>
#include <array>
#include <cctype>
#include <sstream>
#include <stdexcept>
#include <cstdlib>
#include <utility>

namespace chess::game {
namespace {
constexpr int file(int s) { return s&7; }
constexpr int rank(int s) { return s>>3; }
constexpr bool inside(int f,int r) { return f>=0&&f<8&&r>=0&&r<8; }
constexpr Bitboard bit(int sq) { return 1ULL<<sq; }
constexpr int index(std::int8_t p) { return p>0?0:1; }
constexpr int type(std::int8_t p) { return p<0?-static_cast<int>(p):static_cast<int>(p); }
constexpr std::array<std::pair<int,int>,8> kingDirs{{{1,0},{-1,0},{0,1},{0,-1},{1,1},{1,-1},{-1,1},{-1,-1}}};
constexpr std::array<std::pair<int,int>,8> knightDirs{{{1,2},{2,1},{-1,2},{-2,1},{1,-2},{2,-1},{-1,-2},{-2,-1}}};
constexpr std::array<std::pair<int,int>,4> rookDirs{{{1,0},{-1,0},{0,1},{0,-1}}};
constexpr std::array<std::pair<int,int>,4> bishopDirs{{{1,1},{1,-1},{-1,1},{-1,-1}}};

std::array<Bitboard,64> attacks(const std::array<std::pair<int,int>,8>& dirs) {
    std::array<Bitboard,64> result{};
    for(int sq=0;sq<64;++sq) for(auto [dx,dy]:dirs) {
        int f=file(sq)+dx,r=rank(sq)+dy;
        if(inside(f,r)) result[sq]|=bit(r*8+f);
    }
    return result;
}
const auto knightAttacks=attacks(knightDirs);
const auto kingAttacks=attacks(kingDirs);

std::uint64_t splitmix(std::uint64_t& state) {
    std::uint64_t z=(state+=0x9e3779b97f4a7c15ULL);
    z=(z^(z>>30))*0xbf58476d1ce4e5b9ULL;
    z=(z^(z>>27))*0x94d049bb133111ebULL;
    return z^(z>>31);
}
struct Zobrist {
    std::uint64_t piece[2][6][64]{}, castle[16]{}, epFile[8]{}, side=0;
    Zobrist() {
        std::uint64_t seed=0xBAD5EED123456789ULL;
        for(auto& side:piece)for(auto& kind:side)for(auto& square:kind)square=splitmix(seed);
        for(auto& v:castle)v=splitmix(seed);
        for(auto& v:epFile)v=splitmix(seed);
        side=splitmix(seed);
    }
};
const Zobrist zobrist;
char letter(int p) {
    constexpr char names[]=" PNBRQK";
    char c=names[type(static_cast<std::int8_t>(p))];
    return p>0?c:static_cast<char>(std::tolower(c));
}
std::int8_t parsePiece(char c) {
    constexpr std::string_view names=" PNBRQK";
    const auto p=names.find(static_cast<char>(std::toupper(static_cast<unsigned char>(c))));
    if(p==std::string_view::npos||p==0) throw std::invalid_argument("Invalid FEN piece");
    return static_cast<std::int8_t>(std::isupper(static_cast<unsigned char>(c))?p:-static_cast<int>(p));
}
std::string squareName(int sq) { return {static_cast<char>('a'+file(sq)),static_cast<char>('1'+rank(sq))}; }
int squareIndex(std::string_view str) {
    if(str.size()!=2||str[0]<'a'||str[0]>'h'||str[1]<'1'||str[1]>'8') return -1;
    return (str[1]-'1')*8+(str[0]-'a');
}
}

void MoveList::push(Move m) {
    if(size>=MAX_MOVES) throw std::runtime_error("Move buffer exceeded");
    data[size++]=m;
}
std::string Move::uci() const {
    std::string value=squareName(from)+squareName(to);
    if(promotion) value+=static_cast<char>(std::tolower(letter(promotion)));
    return value;
}
void Position::put(int square,std::int8_t p) {
    if(p==0) return;
    board[square]=p;
    bitboards[index(p)][type(p)-1]|=bit(square);
    occupied[index(p)]|=bit(square);
    hash^=zobrist.piece[index(p)][type(p)-1][square];
}
std::int8_t Position::remove(int square) {
    auto p=board[square];
    if(p==0) return 0;
    board[square]=0;
    bitboards[index(p)][type(p)-1]&=~bit(square);
    occupied[index(p)]&=~bit(square);
    hash^=zobrist.piece[index(p)][type(p)-1][square];
    return p;
}
bool Position::epHashable() const {
    if(ep<0) return false;
    int r=rank(ep)+(whiteToMove?-1:1);
    for(int df:{-1,1}) {
        int f=file(ep)+df;
        if(inside(f,r)&&board[r*8+f]==(whiteToMove?Pawn:-Pawn))return true;
    }
    return false;
}
void Position::rehash() {
    hash=0;
    for(int sq=0;sq<64;++sq)if(board[sq]) {
        auto p=board[sq];hash^=zobrist.piece[index(p)][type(p)-1][sq];
    }
    hash^=zobrist.castle[castling];
    if(epHashable())hash^=zobrist.epFile[file(ep)];
    if(!whiteToMove)hash^=zobrist.side;
}
Position Position::start() { return fromFen("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1"); }
Position Position::fromFen(std::string_view fenText) {
    std::istringstream in{std::string(fenText)};
    std::string pieces,side,rights,epField,extra;
    int half=0,full=1;
    if(!(in>>pieces>>side>>rights>>epField>>half>>full)||(in>>extra))throw std::invalid_argument("Invalid FEN fields");
    if(side!="w"&&side!="b")throw std::invalid_argument("Invalid active color");
    if(half<0||half>65535||full<1||full>65535)throw std::invalid_argument("Invalid FEN counters");
    Position p;
    p.whiteToMove=(side=="w");p.castling=0;p.ep=-1;
    int sq=56,files=0,rows=0;
    for(char c:pieces) {
        if(c=='/') {
            if(files!=8||rows>=7)throw std::invalid_argument("Invalid FEN rank");
            sq-=16;files=0;++rows;
        } else if(c>='1'&&c<='8') {
            sq+=c-'0';files+=c-'0';
        } else {
            if(files>=8||sq<0||sq>=64)throw std::invalid_argument("Invalid FEN board");
            auto piece=parsePiece(c);
            if(type(piece)==Pawn&&(rank(sq)==0||rank(sq)==7))throw std::invalid_argument("Pawn on first/last rank");
            p.put(sq++,piece);++files;
        }
        if(files>8)throw std::invalid_argument("Invalid FEN rank length");
    }
    if(rows!=7||files!=8||std::popcount(p.bitboards[0][5])!=1||std::popcount(p.bitboards[1][5])!=1)
        throw std::invalid_argument("Invalid FEN kings or board");
    if(rights!="-")for(char c:rights) {
        auto pos=std::string_view("KQkq").find(c);
        if(pos==std::string_view::npos||(p.castling&(1<<pos)))throw std::invalid_argument("Invalid castling rights");
        p.castling|=1<<pos;
    }
    if(epField!="-") {
        int e=squareIndex(epField);
        if(e<0||(rank(e)!=(p.whiteToMove?5:2))||p.board[e]!=0)
            throw std::invalid_argument("Invalid en passant square");
        p.ep=static_cast<std::int8_t>(e);
    }
    p.halfmove=static_cast<std::uint16_t>(half);
    p.fullmove=static_cast<std::uint16_t>(full);
    p.rehash();
    return p;
}
std::string Position::fen() const {
    std::string out;
    for(int r=7;r>=0;--r) {
        int empty=0;
        for(int f=0;f<8;++f) {
            auto p=board[r*8+f];
            if(p==0){++empty;continue;}
            if(empty){out+=static_cast<char>('0'+empty);empty=0;}
            out+=letter(p);
        }
        if(empty)out+=static_cast<char>('0'+empty);
        if(r)out+='/';
    }
    out+=whiteToMove?" w ":" b ";
    if(!castling)out+='-';
    else for(int i=0;i<4;++i)if(castling&(1<<i))out+="KQkq"[i];
    out+=' ';
    out+=ep>=0?squareName(ep):"-";
    out+=' '+std::to_string(halfmove)+' '+std::to_string(fullmove);
    return out;
}
bool Position::attacked(int sq,bool white) const {
    auto own=white?0:1;
    // Pawn attacks are reversed from the target square.
    int pawnRank=rank(sq)+(white?-1:1);
    if(pawnRank>=0&&pawnRank<8)for(int df:{-1,1}) {
        int f=file(sq)+df;
        if(inside(f,pawnRank)&&board[pawnRank*8+f]==(white?Pawn:-Pawn))return true;
    }
    if(knightAttacks[sq]&bitboards[own][Knight-1])return true;
    if(kingAttacks[sq]&bitboards[own][King-1])return true;
    auto scan=[&](const auto& dirs, int required) {
        for(auto [df,dr]:dirs)for(int f=file(sq)+df,r=rank(sq)+dr;inside(f,r);f+=df,r+=dr) {
            auto piece=board[r*8+f];
            if(!piece)continue;
            if((piece>0)==white&&(type(piece)==required||type(piece)==Queen))return true;
            break;
        }
        return false;
    };
    return scan(rookDirs,Rook)||scan(bishopDirs,Bishop);
}
bool Position::inCheck(bool white) const {
    auto kings=bitboards[white?0:1][King-1];
    return kings==0||attacked(std::countr_zero(kings),!white);
}
void Position::pseudoMoves(MoveList& out) const {
    out.size=0;
    const bool w=whiteToMove;
    const int side=w?0:1,dir=w?8:-8,enemy=w?-1:1;
    auto add=[&](int from,int to,std::uint8_t flag=Normal){
        if(board[to] && type(board[to])==King) return; // Never capture kings.
        if(type(board[from])==Pawn && (rank(to)==0||rank(to)==7)) {
            for(int promotion:{Queen,Rook,Bishop,Knight})out.push({static_cast<std::uint8_t>(from),static_cast<std::uint8_t>(to),static_cast<std::uint8_t>(promotion),flag});
        } else out.push({static_cast<std::uint8_t>(from),static_cast<std::uint8_t>(to),0,flag});
    };
    Bitboard pawns=bitboards[side][Pawn-1];
    while(pawns) {
        int from=std::countr_zero(pawns);pawns&=pawns-1;
        int to=from+dir;
        if(to>=0&&to<64&&board[to]==0) {
            add(from,to);
            int two=to+dir;
            if(rank(from)==(w?1:6)&&board[two]==0)add(from,two,DoublePush);
        }
        for(int df:{-1,1}) {
            if(!inside(file(from)+df,rank(from)+(w?1:-1)))continue;
            int capture=from+dir+df;
            if(board[capture]&&((board[capture]>0)==(enemy>0)))add(from,capture);
            else if(capture==ep && board[capture]==0 && board[capture-dir]==(w?-Pawn:Pawn))add(from,capture,EnPassant);
        }
    }
    for(int piece:{Knight,Bishop,Rook,Queen,King}) {
        Bitboard bb=bitboards[side][piece-1];
        while(bb) {
            int from=std::countr_zero(bb);bb&=bb-1;
            if(piece==Knight||piece==King) {
                Bitboard a=(piece==Knight?knightAttacks[from]:kingAttacks[from])&~occupied[side];
                while(a){int to=std::countr_zero(a);a&=a-1;add(from,to);}
            } else {
                auto scan=[&](const auto& dirs) {
                    for(auto [df,dr]:dirs)for(int f=file(from)+df,r=rank(from)+dr;inside(f,r);f+=df,r+=dr) {
                        int to=r*8+f;
                        if(board[to]&&((board[to]>0)==w))break;
                        add(from,to);
                        if(board[to])break;
                    }
                };
                if(piece==Bishop||piece==Queen)scan(bishopDirs);
                if(piece==Rook||piece==Queen)scan(rookDirs);
            }
        }
    }
    const int kingSq=w?4:60;
    if(board[kingSq]==(w?King:-King)&&!inCheck(w)) {
        if(castling&(w?1:4)) {
            int rookSq=w?7:63;
            if(board[rookSq]==(w?Rook:-Rook)&&board[kingSq+1]==0&&board[kingSq+2]==0&&
               !attacked(kingSq+1,!w)&&!attacked(kingSq+2,!w))add(kingSq,kingSq+2,Castle);
        }
        if(castling&(w?2:8)) {
            int rookSq=w?0:56;
            if(board[rookSq]==(w?Rook:-Rook)&&board[kingSq-1]==0&&board[kingSq-2]==0&&board[kingSq-3]==0&&
               !attacked(kingSq-1,!w)&&!attacked(kingSq-2,!w))add(kingSq,kingSq-2,Castle);
        }
    }
}
Undo Position::make(Move m) {
    Undo u{board[m.from],board[m.to],ep,castling,halfmove,fullmove,hash};
    hash^=zobrist.castle[castling];
    if(epHashable())hash^=zobrist.epFile[file(ep)];
    std::int8_t moved=remove(m.from);
    if(board[m.to])remove(m.to);
    if(m.flag==EnPassant)u.captured=remove(m.to+(whiteToMove?-8:8));
    if(m.flag==Castle) {
        int rookFrom=m.to>m.from?m.from+3:m.from-4;
        int rookTo=m.to>m.from?m.from+1:m.from-1;
        auto rook=remove(rookFrom);put(rookTo,rook);
    }
    put(m.to,m.promotion?static_cast<std::int8_t>((whiteToMove?1:-1)*m.promotion):moved);
    if(type(moved)==King)castling&=whiteToMove?~3:~12;
    if(m.from==0||m.to==0)castling&=~2;
    if(m.from==7||m.to==7)castling&=~1;
    if(m.from==56||m.to==56)castling&=~8;
    if(m.from==63||m.to==63)castling&=~4;
    ep=m.flag==DoublePush?static_cast<std::int8_t>((m.from+m.to)/2):-1;
    halfmove=(type(moved)==Pawn||u.captured)?0:static_cast<std::uint16_t>(halfmove+1);
    if(!whiteToMove)++fullmove;
    whiteToMove=!whiteToMove;
    hash^=zobrist.castle[castling];
    if(epHashable())hash^=zobrist.epFile[file(ep)];
    hash^=zobrist.side;
    return u;
}
void Position::unmake(Move m,Undo u) {
    whiteToMove=!whiteToMove;
    remove(m.to);
    if(m.flag==Castle) {
        int rookFrom=m.to>m.from?m.from+3:m.from-4;
        int rookTo=m.to>m.from?m.from+1:m.from-1;
        auto rook=remove(rookTo);put(rookFrom,rook);
    }
    put(m.from,u.moved);
    if(u.captured)put(m.flag==EnPassant?m.to+(whiteToMove?-8:8):m.to,u.captured);
    castling=u.castling;ep=u.ep;halfmove=u.halfmove;fullmove=u.fullmove;hash=u.hash;
}
void Position::legalMoves(MoveList& out) {
    MoveList pseudo;pseudoMoves(pseudo);out.size=0;
    bool mover=whiteToMove;
    for(int i=0;i<pseudo.size;++i) {
        Move m=pseudo[i];auto u=make(m);
        bool ok=!inCheck(mover);
        unmake(m,u);
        if(ok)out.push(m);
    }
}
bool Position::playUci(std::string_view str) {
    MoveList legal;legalMoves(legal);
    for(int i=0;i<legal.size;++i)if(legal[i].uci()==str){make(legal[i]);return true;}
    return false;
}
std::uint64_t Position::perft(int depth) {
    if(depth<0)throw std::invalid_argument("Negative perft depth");
    if(depth==0)return 1;
    MoveList legal;legalMoves(legal);
    if(depth==1)return legal.size;
    std::uint64_t total=0;
    for(int i=0;i<legal.size;++i){auto m=legal[i];auto u=make(m);total+=perft(depth-1);unmake(m,u);}
    return total;
}
bool Position::insufficientMaterial() const {
    if(bitboards[0][Pawn-1]||bitboards[1][Pawn-1]||bitboards[0][Rook-1]||bitboards[1][Rook-1]||bitboards[0][Queen-1]||bitboards[1][Queen-1])return false;
    int minors=std::popcount(bitboards[0][Knight-1]|bitboards[0][Bishop-1]|bitboards[1][Knight-1]|bitboards[1][Bishop-1]);
    if(minors<=1)return true;
    // Only bishops all on the same square color cannot force mate.
    if(bitboards[0][Knight-1]||bitboards[1][Knight-1])return false;
    Bitboard bishops=bitboards[0][Bishop-1]|bitboards[1][Bishop-1];
    bool light=false,dark=false;
    while(bishops){int sq=std::countr_zero(bishops);bishops&=bishops-1;((file(sq)+rank(sq))&1?dark:light)=true;}
    return !(light&&dark);
}
} // namespace chess::game
