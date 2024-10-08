// 2019 Team AobaZero
// This source code is in the public domain.
// yss_dcnn.h
#ifndef INCLUDE_YSS_DCNN_H_GUARD	//[
#define INCLUDE_YSS_DCNN_H_GUARD

#include <vector>
#include "lock.h"
using namespace std;

const int SHOGI_MOVES_MAX = 593;
const float ILLEGAL_MOVE = -1000;
typedef struct child {
	int   move;			// 手の場所
	int   games;		// この手を探索した回数
	float value;		// 勝率(+1 で勝ち、0 で負け)
	float bias;			// policy
} CHILD;

typedef struct hash_shogi {
	lock_t entry_lock;			// SMPでのロック用(全体ロックでもentryごとでも差はなし mt=2で)
	uint64 hashcode64;			// ハッシュコード
	int deleted;	// 削除済み
	int games_sum;	// この局面に来た回数(子局面の回数の合計)
	int sort_done;	// 複雑な前処理でソート済みか？
	int used;		// 
	Color col;		// 手番
	int age;		// 何番目の思考時の登録か。ponderingでどのデータを消すか、のチェックに使う
	float net_value;		// value networkでの勝率
	int   has_net_value;

	int child_num;
	CHILD child[SHOGI_MOVES_MAX];
} HASH_SHOGI;


#define F11259  1	// 論文どおりに生成。0で存在しない手を削除
#if (F11259==0 ) 
const int MOVE_C_Y_X_ID_MAX = 3781;
#else
const int MOVE_C_Y_X_ID_MAX = 11259;	// 3781;
#endif

#define GCT_SELF 0	// GCTの棋譜 selfplay_gct-???.hcpe3.xz を使う場合。 https://tadaoyamaoka.hatenablog.com/entry/2021/05/06/223701
#define U8700 1		// 本番学習環境
#define TRAINED_NUM 0	// dlshogiのhcpe3用、学習された局面を覚える

typedef struct ZERO_DB {
	uint64 hash;	// 棋譜を示すハッシュ
	uint64 date;	// 棋譜の日付(新しいものから順に)
	int weight_n;	// weight番号
	int index;		// 通し番号
	int result;		// 結果。先手勝ち、後手勝ち、引き分け
	int result_type;// 投了、千日手、中断(513手)、宣言勝ち、連続王手の王逃げによる反則勝、
	int moves;		// 手数(棋譜のサイズと同じ)
	int handicap;	// 駒落ち
#ifdef FURIBISHA
	int furi_hope_bit[2];	// 希望の飛車の振る場所
	int furi_bit[2];		// 実際の振り場所
#endif
#if ( GCT_SELF==1)
	vector <unsigned char> v_init_pos;		// 開始局面＋手番 81+7*2+1、ハフマンで256bit(32byte)で表現できるので圧縮は可能
#endif
	vector <unsigned short> v_kif;			// 棋譜
	vector <unsigned short> v_playouts_sum;	// Rootの探索数。通常は800固定
	vector < vector<unsigned int> > vv_move_visit;	// (手+選択回数)のペア。上位16bitが手、下位16bitが回数
	vector <unsigned short> v_score_x10k;	// Rootの評価値。自分から見た勝率。1.0で勝ち。0.0で負け。1万倍されている。
	vector <unsigned short> v_rawscore_x10k;// Networkのその局面の勝率
	vector < vector<char> > vv_raw_policy;
#if ( TRAINED_NUM==1 )
	vector <unsigned short> v_trained_num;	// 学習された回数
#endif
} ZERO_DB;

extern ZERO_DB zdb_one;	// 棋譜読み込みで使用

enum { ZD_DRAW, ZD_S_WIN, ZD_G_WIN };
enum { RT_NONE, RT_TORYO, RT_KACHI, RT_SENNICHITE, RT_G_ILLEGAL_ACTION, RT_S_ILLEGAL_ACTION, RT_CHUDAN };
const int RT_MAX = 7;	// 7種類

const unsigned short NO_ROOT_SCORE = 10001;

void free_zero_db_struct(ZERO_DB *p);
void start_zero_train(int *, char ***);

int get_furi_bit_to_x(int bit);
float get_ave_furi_hope_bit(int bit, float array[], int t);

#endif	//]] INCLUDE__GUARD
