﻿// 2019 Team AobaZero
// This source code is in the public domain.
#include "../config.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <time.h>
#include <math.h>
#if !defined(_MSC_VER)
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>
#endif

#include <string>
#include <vector>
#include <random>
#include <thread>
#include <mutex>
#include <atomic>

#include "shogi.h"
#include "dfpn.h"

#include "lock.h"
#include "yss_var.h"
#include "yss_dcnn.h"
#include "process_batch.h"

#include "../GTP.h"
#include "../Utils.h"

int NOT_USE_NN = 0;

// 棋譜と探索木を含めた局面図
//min_posi_t record_plus_ply_min_posi[REP_HIST_LEN];

int nVisitCount = 0;	//30;	// この手数まで最大でなく、回数分布で選ぶ
int nVisitCountSafe = 0;
int fAddNoise = 0;				// always add dirichlet noise on root node.
int fUSIMoveCount;	// USIで上位ｎ手の訪問回数も返す
int fPrtNetworkRawPath = 0;
int fVerbose = 1;
int fClearHashAlways = 0;
int fUsiInfo = 0;
bool fLCB = true;
double MinimumKLDGainPerNode = 0;	//0.000002;	0で無効, lc0は 0.000005
int    KLDGainAverageInterval = 100;
bool fResetRootVisit = false;
bool fDiffRootVisit = false;
bool fSkipOneReply  = true;		// 王手を逃げる手が1手の局面は評価せずに木を降りる
bool fSkipKingCheck = false;	// 王手がかかってる局面では評価せずに木を降りる
bool fRawValuePolicy = true;
bool fFuriShitei = false;
 
int nLimitUctLoop = 100;
double dLimitSec = 0;
int nDrawMove = MAX_DRAW_MOVES;	// 引き分けになる手数。0でなし。floodgateは256, 選手権は321。学習中も探索内では引き分けに
int time_left_msec[2];

const float FIXED_RESIGN_WINRATE = 0.10;	// 自己対戦でこの勝率以下なら投了。0で投了しない。0.10 で勝率10%。 0 <= x <= +1.0
float resign_winrate = 0;

int    fAutoResign = 0;				// 過渡的なフラグ。投了の自動調整ありで、go visit で勝率も返す
double dAutoResignWinrate = 0;

double dSelectRandom = 0;			// この確率で乱数で選んだ手を指す 0 <= x <= 1.0。0 でなし無効
int nHandicapRate[HANDICAP_TYPE];
const int TEMP_RATE_MAX = 1400;		// このレート差まではsoftmaxの温度で調整
char engine_name[SIZE_CMDLINE];
float average_winrate = 0;
int balanced_opening_move[PLY_MAX];
int usi_newgames;
int nFuriHandicapRate[ROOK_HANDICAP_NUM];
int nFuriPos[18];


std::vector <HASH_SHOGI> hash_shogi_table;
const int HASH_SHOGI_TABLE_SIZE_MIN = 1024*4*4 *2;	// 先手、後手分けたので念のため2倍に
int Hash_Shogi_Table_Size = HASH_SHOGI_TABLE_SIZE_MIN;
int Hash_Shogi_Mask;
int hash_shogi_use = 0;
int hash_shogi_sort_num = 0;
int thinking_age = 0;

const int REHASH_MAX = (2048*1);
const int REHASH_SHOGI = (REHASH_MAX-1);

int rehash[REHASH_MAX-1];	// バケットが衝突した際の再ハッシュ用の場所を求めるランダムデータ
int rehash_flag[REHASH_MAX];	// 最初に作成するために

// 81*81*2 + (81*7) = 13122 + 567 = 13689 * 512 = 7008768.  7MB * 8 = 56MB
//uint64_t sequence_hash_from_to[SEQUENCE_HASH_SIZE][81][81][2];	// [from][to][promote]
//uint64_t sequence_hash_drop[SEQUENCE_HASH_SIZE][81][7];
uint64_t (*sequence_hash_from_to)[81][81][2];
uint64_t (*sequence_hash_drop)[81][7];

int usi_go_count = 0;		// bestmoveを送った直後にstopが来るのを防ぐため
int usi_bestmove_count = 0;

void PRT_sub(const char *fmt, va_list arg)
{
	va_list arg2;
	va_copy(arg2, arg);
	vfprintf(stderr, fmt, arg);

	if ( 0 ) {
		FILE *fp = fopen("aoba_log.txt","a");
		if ( fp ) {
			vfprintf( fp, fmt, arg2);
			fclose(fp);
		}
	}
	va_end(arg2);
}

void PRT(const char *fmt, ...)
{
	if ( fVerbose == 0 ) return;
	va_list arg;
	va_start( arg, fmt );
	PRT_sub(fmt, arg);
	va_end( arg );
}

const int TMP_BUF_LEN = 256*2;
static char debug_str[TMP_BUF_LEN];

void debug_set(const char *file, int line)
{
	char str[TMP_BUF_LEN];
	strncpy(str, file, TMP_BUF_LEN-1);
	const char *p = strrchr(str, '\\');
	if ( p == NULL ) p = file;
	else p++;
	sprintf(debug_str,"%s Line %d\n\n",p,line);
}

void debug_print(const char *fmt, ... )
{
	va_list ap;
	static char text[TMP_BUF_LEN];
	va_start(ap, fmt);
#if defined(_MSC_VER)
	_vsnprintf( text, TMP_BUF_LEN-1, fmt, ap );
#else
	 vsnprintf( text, TMP_BUF_LEN-1, fmt, ap );
#endif
	va_end(ap);
	static char text_out[TMP_BUF_LEN*2];
	sprintf(text_out,"%s%s",debug_str,text);
	PRT("%s\n",text_out);
	debug();
}

#if defined(_MSC_VER)
#include <process.h>
int getpid_YSS() { return _getpid(); }
#else 
int getpid_YSS() { return getpid(); }
#endif

const int CLOCKS_PER_SEC_MS = 1000;	// CLOCKS_PER_SEC を統一。linuxではより小さい.
int get_clock()
{
#if defined(_MSC_VER)
	if ( CLOCKS_PER_SEC_MS != CLOCKS_PER_SEC ) { PRT("CLOCKS_PER_SEC=%d Err. not Windows OS?\n"); debug(); }
	return clock();
#else
	struct timeval  val;
	struct timezone zone;
	if ( gettimeofday( &val, &zone ) == -1 ) { PRT("time err\n"); debug(); }
	return val.tv_sec*1000 + (val.tv_usec / 1000);
#endif
}
double get_diff_sec(int diff_ct)
{
	return (double)diff_ct / CLOCKS_PER_SEC_MS;
}
double get_spend_time(int ct1)
{
	return get_diff_sec(get_clock()+1 - ct1);	// +1をつけて0にならないように。
}

std::mt19937 get_mt_rand;

void init_rnd521(unsigned long u_seed)
{
	get_mt_rand.seed(u_seed);
//	PRT("u_seed=%d, mt()=%d\n",u_seed,get_mt_rand());
}
unsigned long rand_m521()
{
	return get_mt_rand();
}


float f_rnd()
{
//	double f = (double)rand_m521() / (0xffffffffUL + 1.0);	// 0 <= rnd() <  1, ULONG_MAX は gcc 64bitで違う
	double f = (double)rand_m521() / (0xffffffffUL + 0.0);	// 0 <= rnd() <= 1
//	PRT("f_rnd()=%f\n",f);
	return (float)f;
}

// from gen_legal_moves()
int generate_all_move(tree_t * restrict ptree, int turn, int ply)
{
	unsigned int * restrict pmove = ptree->move_last[0];
	ptree->move_last[1] = GenCaptures( turn, pmove );
	ptree->move_last[1] = GenNoCaptures( turn, ptree->move_last[1] );
	ptree->move_last[1] = GenCapNoProEx2( turn, ptree->move_last[1] );
	ptree->move_last[1] = GenNoCapNoProEx2( turn, ptree->move_last[1] );
	ptree->move_last[1] = GenDrop( turn, ptree->move_last[1] );
	int num_move = (int)( ptree->move_last[1] - pmove );
	int i;
	for (i = 0; i < num_move; i++) {
		MakeMove( turn, pmove[i], ply );
		if ( InCheck(turn) ) {
			UnMakeMove( turn, pmove[i], ply );
			pmove[i] = 0;
			continue;
		}
		UnMakeMove( turn, pmove[i], ply );
	}
	int num_legal = 0;
	for (i = 0; i < num_move; i++) {
		if ( pmove[i]==0 ) continue;
		pmove[num_legal++] = pmove[i];
	}
//	PRT("num_legal=%d/%d,ply=%d\n",num_legal,num_move,ply);
	if ( num_legal > SHOGI_MOVES_MAX ) { PRT("num_legal=%d Err\n",num_legal); debug(); }
	return num_legal;
}

int is_drop_pawn_mate(tree_t * restrict ptree, int turn, int ply)
{
	int move_num = generate_all_move( ptree, turn, ply );
	unsigned int * restrict pmove = ptree->move_last[0];
	int i;
	for ( i = 0; i < move_num; i++ ) {
		int move = pmove[i];
		int tt = root_turn;
		if ( ! is_move_valid( ptree, move, tt ) ) {
			PRT("illegal move?=%08x\n",move);
		}
		int not_mate = 0;
		MakeMove( tt, move, ply );
		if ( InCheck(tt) ) {
			PRT("illegal. check\n");
		} else {
			not_mate = 1;
		}
		UnMakeMove( tt, move, ply );
		if ( not_mate ) return 0;
	}
	return 1;
}

const int USI_BESTMOVE_LEN = MAX_LEGAL_MOVES*(8+1+5)+10+10;

int YssZero_com_turn_start( tree_t * restrict ptree )
{
	if ( 0 ) {
		dLimitSec = 1.7;
		if ( ptree->nrep < 144 ) {
			if ( root_turn == 1 ) {	// denryu-sen, sente 300-2f, gote 600-2f, first 4 moves is dummy
				dLimitSec = 10;
			} else {
				dLimitSec = 6;
			}
		}
	}
        
	if ( 0 ) {
		int ct1 = get_clock();
		int i;
		for(i=0;i<10000;i++) {
			generate_all_move( ptree, root_turn, 1 );
//			make_root_move_list( ptree );	// 100回で12秒, root は探索してる
		}
		PRT("%.2f sec\n",get_spend_time(ct1));
	}

	int ply = 1;	// 1 から始まる
/*
	int move_num = generate_all_move( ptree, root_turn );
	PRT("move_num=%d,root_turn=%d,nrep=%d\n",move_num,root_turn,ptree->nrep);

	unsigned int * restrict pmove = ptree->move_last[0];
	int i;
	for ( i = 0; i < move_num; i++ ) {
//		int move = root_move_list[i].move;
		int move = pmove[i];
  
		int tt = root_turn;
		if ( ! is_move_valid( ptree, move, tt ) ) {
			PRT("illegal move?=%08x\n",move); debug();
		}
		int from = (int)I2From(move);
		int to   = (int)I2To(move);
		int cap  = (int)UToCap(move);
		int drop = (int)From2Drop(from);
		int piece_m	= (int)I2PieceMove(move);
		int is_promote = (int)I2IsPromote(move);
		PRT("%3d:%s(%d), from=%2d,to=%2d,cap=%2d,drop=%3d,is_promote=%d,peice_move=%d\n",i,str_CSA_move(move),tt,from,to,cap,drop,is_promote,piece_m);
		
		MakeMove( tt, move, ply );
		if ( InCheck(tt) ) PRT("illegal. check\n");
		if ( InCheck(Flip(tt)) ) {
			PRT("check\n");
//			if ( drop == pawn && is_drop_pawn_mate( ptree, Flip(tt), ply+1 ) ) {	// 打歩詰？
//				PRT("drop pawn check_mate!\n");	// BonaはGenDropで認識して生成しない？ -> しないね。かしこい。
//			}
		}
//		tt = Flip(tt);
		UnMakeMove( tt, move, ply );
	}
*/
	char buf_move_count[USI_BESTMOVE_LEN];
	int m = uct_search_start( ptree, root_turn, ply, buf_move_count );

	char buf[7];
	if ( m == 0 ) {
		sprintf(buf,"%s","resign");
	} else {
		csa2usi( ptree, str_CSA_move(m), buf );
	}
	char str_best[USI_BESTMOVE_LEN+11+7];
	if ( fUSIMoveCount ) {
		sprintf( str_best,"bestmove %s,%s\n",buf,buf_move_count );
	} else {
		sprintf( str_best,"bestmove %s\n",   buf );
	}
	if ( fUsiInfo && is_declare_win_root(ptree, root_turn) ) {
		sprintf( str_best,"bestmove win\n");
	}

	set_latest_bestmove(str_best);

	if ( 0 && m ) {	// test fClearHashAlways
		char buf_tmp[USI_BESTMOVE_LEN];
		make_move_root( ptree, m, 0 );
		uct_search_start( ptree, root_turn, ply, buf_tmp );
	}

	send_latest_bestmove();
	return 1;
}

char latest_bestmove[USI_BESTMOVE_LEN] = "bestmove resign\n";
void set_latest_bestmove(char *str)
{
	strcpy(latest_bestmove,str);
}
void send_latest_bestmove()
{
	usi_bestmove_count++;
	USIOut( "%s", latest_bestmove);
}

void init_seqence_hash()
{
	static int fDone = 0;
	if ( fDone ) return;
	fDone = 1;

	sequence_hash_from_to = (uint64_t(*)[81][81][2])malloc( SEQUENCE_HASH_SIZE*81*81*2 * sizeof(uint64_t) );
	sequence_hash_drop    = (uint64_t(*)[81][7])    malloc( SEQUENCE_HASH_SIZE*81*7    * sizeof(uint64_t) );
	if ( sequence_hash_from_to == NULL || sequence_hash_drop == NULL ) { PRT("Fail sequence_hash malloc()\n"); debug(); }

	int m,i,j,k;
	for (m=0;m<SEQUENCE_HASH_SIZE;m++) {
		for (i=0;i<81;i++) {
			for (j=0;j<81;j++) {
				for (k=0;k<2;k++) {
					sequence_hash_from_to[m][i][j][k] = ((uint64)(rand_m521()) << 32) | rand_m521();
//					if ( i==0 && j==0 ) { PRT("%016" PRIx64 ",",sequence_hash_from_to[m][i][j][k]); if ( k==1 ) PRT("\n"); }
				}
			}
			for (j=0;j<7;j++) {
				sequence_hash_drop[m][i][j] = ((uint64)(rand_m521()) << 32) | rand_m521();
//				PRT("%016" PRIx64 ",",sequence_hash_drop[m][i][j]);
			}
		}
	}
}

uint64_t get_sequence_hash_from_to(int moves, int from, int to, int promote)
{
	uint64_t ret = 0;
	if ( moves < 0 || moves >= SEQUENCE_HASH_SIZE || from < 0 || from >= 81 || to < 0 || to >= 81 || promote < 0 || promote >= 2 ) { PRT("Err. sequence move\n"); debug(); }
	if ( is_process_batch() ) {
		ret = get_process_mem(moves * (81*81*2) + from * (81*2) + to * (2) + promote);
	} else {
		ret = sequence_hash_from_to[moves][from][to][promote];
	}
//	PRT("moves=%3d(%3d),from=%2d,to=%2d,prom=%d,%016" PRIx64 "\n",moves,moves & (SEQUENCE_HASH_SIZE-1),from,to,promote,ret);
	return ret;
}
uint64_t get_sequence_hash_drop(int moves, int to, int piece)
{
	if ( moves < 0 || moves >= SEQUENCE_HASH_SIZE || to < 0 || to >= 81 || piece < 0 || piece >= 7 ) { PRT("Err. sequence drop\n"); debug(); }
	if ( is_process_batch() ) {
		return get_process_mem(moves * (81*7) + to * (7) + piece + (SEQUENCE_HASH_SIZE*81*81*2));
	} else {
		return sequence_hash_drop[moves][to][piece];
	}
}


void set_Hash_Shogi_Table_Size(int playouts)
{
	int n = playouts * 3 * 2;	// 先手、後手別にしたので念のため2倍確保
	
	Hash_Shogi_Table_Size = HASH_SHOGI_TABLE_SIZE_MIN;
	for (;;) {
		if ( Hash_Shogi_Table_Size > n ) break;
		Hash_Shogi_Table_Size *= 2;
	}
}

void hash_shogi_table_reset()
{
	for (int i=0;i<Hash_Shogi_Table_Size;i++) {
		HASH_SHOGI *pt = &hash_shogi_table[i];
		pt->deleted = 1;
		LockInit(pt->entry_lock);
//		pt->lock = false;
#ifdef CHILD_VEC
		std::vector<CHILD>().swap(pt->child);	// memory free hack for vector. 
#endif
	}
	hash_shogi_use = 0;
}

void hash_shogi_table_clear()
{
	Hash_Shogi_Mask       = Hash_Shogi_Table_Size - 1;
	Hash_Shogi_Mask       &= Hash_Shogi_Mask - 1;	// 最下位bitでRoot先手番とRoot後手番に分ける
	HASH_ALLOC_SIZE size = sizeof(HASH_SHOGI) * Hash_Shogi_Table_Size;
	hash_shogi_table.resize(Hash_Shogi_Table_Size);	// reserve()だと全要素のコンストラクタが走らないのでダメ
	PRT("HashShogi=%7d(%3dMB),sizeof(HASH_SHOGI)=%d,Hash_SHOGI_Mask=%d(%08x)\n",Hash_Shogi_Table_Size,(int)(size/(1024*1024)),sizeof(HASH_SHOGI),Hash_Shogi_Mask,Hash_Shogi_Mask);
	hash_shogi_table_reset();
}

void inti_rehash()
{
	int i = 0;
	rehash_flag[0] = 1;	// 0 は使わない
	for ( ;; ) {
		int k = (rand_m521() >> 8) & (REHASH_MAX-1);
		if ( rehash_flag[k] ) continue;	// 既に登録済み
		rehash[i] = k;
		rehash_flag[k] = 1;
		i++;
		if ( i == REHASH_MAX-1 ) break;
	}
//	for (i=0;i<REHASH_MAX-1;i++) PRT("%08x,",rehash[i]);
}

int IsHashFull()
{
///	if ( (uint64)hash_shogi_use >= (uint64)Hash_Shogi_Table_Size*90/100 ) {
	if ( (uint64)hash_shogi_use >= (uint64)Hash_Shogi_Table_Size*45/100 ) {
		PRT("hash full! hash_shogi_use=%d,Hash_Shogi_Table_Size=%d\n",hash_shogi_use,Hash_Shogi_Table_Size);
		return 1;
	}
	return 0; 
}
void all_hash_go_unlock()
{
	for (int i=0;i<Hash_Shogi_Table_Size;i++) UnLock(hash_shogi_table[i].entry_lock);
}

uint64 get_marge_hash(tree_t * restrict ptree, int sideToMove)
{
	uint64 key = HASH_KEY ^ HAND_B;
	if ( ! sideToMove ) key = ~key;
	return key;
};

void hash_half_del(tree_t * restrict ptree, int sideToMove)
{
	uint64 hash64pos  = get_marge_hash(ptree, sideToMove);
	uint64 hashcode64 = ptree->sequence_hash;

	int i,sum = 0;
	for (i=0;i<Hash_Shogi_Table_Size;i++) if ( hash_shogi_table[i].deleted==0 ) sum++;
	if ( sum != hash_shogi_use ) PRT("warning! sum=%d,hash_shogi_use=%d\n",sum,hash_shogi_use);
	hash_shogi_use = sum;	// hash_shogi_useはロックしてないので12スレッドだと頻繁にずれる

	int max_sum = 0;
	int del_games = max_sum * 5 / 10000;	// 0.05%以上。5%程度残る。メモリを最大限まで使い切ってる場合のみ。age_minus = 2 に。

//	const double limit_occupy = 50;		// 50%以上空くまで削除
	const double limit_occupy = 25;		// 25%以上空くまで削除
	const int    limit_use    = (int)(limit_occupy*Hash_Shogi_Table_Size / 100);
	int del_sum=0,age_minus = 4;
	for (;age_minus>=0;age_minus--) {
		for (i=0;i<Hash_Shogi_Table_Size;i++) {
			HASH_SHOGI *pt = &hash_shogi_table[i];
			int del = 0;
			if ( pt->deleted == 0 && hashcode64 == pt->hashcode64 && hash64pos == pt->hash64pos ) {
//				PRT("root node, del hash\n");
//				del = 1;
			}
			if ( pt->deleted == 0 && (pt->age <= thinking_age - age_minus || pt->games_sum < del_games) ) {
				del = 1;
			}
			if ( del ) {
#ifdef CHILD_VEC
				std::vector<CHILD>().swap(pt->child);	// memory free hack for vector. 
#else
//				memset(pt,0,sizeof(HASH_SHOGI));
#endif
				pt->deleted = 1;
				hash_shogi_use--;
				del_sum++;
			}
//			if ( hash_go_use < limit_use ) break;	// いきなり10分予測読みして埋めてしまっても全部消さないように --> 前半ばっかり消して再ハッシュでエラーになる。
		}
		double occupy = hash_shogi_use*100.0/Hash_Shogi_Table_Size;
		PRT("hash del=%d,age=%d,minus=%d, %.0f%%(%d/%d)\n",del_sum,thinking_age,age_minus,occupy,hash_shogi_use,Hash_Shogi_Table_Size);
		if ( hash_shogi_use < limit_use ) break;
		if ( age_minus==0 ) { PRT("age_minus=0\n"); debug(); }
	}
}



HASH_SHOGI* HashShogiReadLock(tree_t * restrict ptree, int sideToMove)
{
research_empty_block:
	int n,first_n,loop = 0;

	uint64 hash64pos  = get_marge_hash(ptree, sideToMove);
	uint64 hashcode64 = ptree->sequence_hash;
//	PRT("ReadLock hash=%016" PRIx64 "\n",hashcode64);

	n = (int)hashcode64 & Hash_Shogi_Mask;
	n |= root_turn;	// Root先手番とRoot後手番でハッシュ表を分離

	first_n = n;
	const int TRY_MAX = 8;

	HASH_SHOGI *pt_first = NULL;

	for (;;) {
		HASH_SHOGI *pt = &hash_shogi_table[n];
		Lock(pt->entry_lock);		// Lockをかけっぱなしにするように
		if ( pt->deleted == 0 ) {
			if ( hashcode64 == pt->hashcode64 && hash64pos == pt->hash64pos ) {
				return pt;
			}
		} else {
			if ( pt_first == NULL ) pt_first = pt;
		}

		UnLock(pt->entry_lock);
		// 違う局面だった
		if ( loop == REHASH_SHOGI ) break;	// 見つからず
		if ( loop >= TRY_MAX && pt_first ) break;	// 妥協。TRY_MAX回探してなければ未登録扱い。
		n = (rehash[loop++] + first_n ) & Hash_Shogi_Mask;
		n |= root_turn;
	}
//	{ static int count, loop_sum; count++; loop_sum+=loop; PRT("%d,",loop); if ( (count%100)==0 ) PRT("loop_ave=%.1f\n",(float)loop_sum/count); }
	if ( pt_first ) {
		// 検索中に既にpt_firstが使われてしまっていることもありうる。もしくは同時に同じ場所を選んでしまうケースも。
		Lock(pt_first->entry_lock);
		if ( pt_first->deleted == 0 ) {	// 先に使われてしまった！
			UnLock(pt_first->entry_lock);
			goto research_empty_block;
		}
		return pt_first;	// 最初にみつけた削除済みの場所を利用
	}
	int sum = 0;
	for (int i=0;i<Hash_Shogi_Table_Size;i++) { sum += hash_shogi_table[i].deleted; PRT("%d",hash_shogi_table[i].deleted); }
	PRT("\nno child hash Err loop=%d,hash_shogi_use=%d,first_n=%d,del_sum=%d(%.1f%%)\n",loop,hash_shogi_use,first_n,sum, 100.0*sum/Hash_Shogi_Table_Size); debug(); return NULL;
}



std::vector <HASH_SHOGI> opening_hash;
int Opening_Hash_Size;
int Opening_Hash_Mask;
int Opening_Hash_Stop_Ply;
int opening_hash_use = 0;

void reset_opening_hash()
{
	for (int i=0;i<Opening_Hash_Size;i++) {
		HASH_SHOGI *pt = &opening_hash[i];
		pt->deleted = 1;
#ifdef CHILD_VEC
		std::vector<CHILD>().swap(pt->child);	// memory free hack for vector. 
#endif
	}
	opening_hash_use = 0;
}

void clear_opening_hash()
{
	static int fDone = 0;
	int memGB = GetSystemMemoryMB() / 1024;
	if ( fDone == 0 ) {
		Opening_Hash_Size = 1024*16;
		Opening_Hash_Stop_Ply = 3;		// 3手以上の出現局面は登録しない。3手以上では2回出現で徐々に登録される
		if ( memGB >= 7 ) {				// そもそも35000棋譜で1重み終了。140並列だと200棋譜程度しか1プロセスで作らない。
//			Opening_Hash_Size = 1024*128;	// 平均31手で1個が(5*4*31)+(10*4)=660byte。1024*128=131072 で83MB, 65個並列だと5.2GB。colabは12GB。
//			Opening_Hash_Stop_Ply = 8;		// 8手以上の出現局面は登録しない。
		}
		Opening_Hash_Mask = Opening_Hash_Size - 1;
		opening_hash.resize(Opening_Hash_Size);
		fDone = 1;
	}
	PRT("memGB=%d,OpeningHash=%d,StopPly=%d,sizeof(HASH_SHOGI)=%d,Opening_Hash_Mask=%d\n",memGB,Opening_Hash_Size,Opening_Hash_Stop_Ply,sizeof(HASH_SHOGI),Opening_Hash_Mask);
	reset_opening_hash();
}

int IsOpeningHashFull()
{
	if ( (float)opening_hash_use >= Opening_Hash_Size*0.9f ) {
		static int count; if ( count++ < 1 ) PRT("opening hash full! opening_hash_use=%d\n",opening_hash_use);
		return 1;
	}
	return 0; 
}

HASH_SHOGI* ReadOpeningHash(tree_t * restrict ptree, int sideToMove)
{
research_block:
	int n,first_n,loop = 0;

	uint64 hash64pos  = get_marge_hash(ptree, sideToMove);
	uint64 hashcode64 = ptree->sequence_hash;
//	PRT("ReadLock hash=%016" PRIx64 "\n",hashcode64);

	n = (int)hashcode64 & Opening_Hash_Mask;
	first_n = n;
	const int TRY_MAX = REHASH_SHOGI;

	HASH_SHOGI *pt_first = NULL;

	for (;;) {
		HASH_SHOGI *pt = &opening_hash[n];
		if ( pt->deleted == 0 ) {
			if ( hashcode64 == pt->hashcode64 && hash64pos == pt->hash64pos ) {
				return pt;
			}
		} else {
			if ( pt_first == NULL ) pt_first = pt;
		}

		// 違う局面だった
		if ( loop == REHASH_SHOGI ) break;	// 見つからず
		if ( loop >= TRY_MAX && pt_first ) break;	// 妥協。TRY_MAX回探してなければ未登録扱い。
		n = (rehash[loop++] + first_n ) & Opening_Hash_Mask;
	}
	if ( pt_first ) {
		// 検索中に既にpt_firstが使われてしまっていることもありうる。もしくは同時に同じ場所を選んでしまうケースも。
		if ( pt_first->deleted == 0 ) {	// 先に使われてしまった！
			goto research_block;
		}
		return pt_first;	// 最初にみつけた削除済みの場所を利用
	}
	int sum = 0;
	for (int i=0;i<Opening_Hash_Size;i++) { sum += opening_hash[i].deleted; PRT("%d",opening_hash[i].deleted); }
	PRT("\nno child hash Err loop=%d,opening_hash_use=%d,first_n=%d,del_sum=%d(%.1f%%)\n",loop,opening_hash_use,first_n,sum, 100.0*sum/Opening_Hash_Size); debug(); return NULL;
}

void count_OpeningHash() {
	int n = 0, sum = 0;
	for (int i=0;i<Opening_Hash_Size;i++) {
		if ( opening_hash[i].deleted ) continue;
		n++;
		sum += opening_hash[i].child_num;
	}
	PRT("n=%d, sum=%d,%d\n",n,sum,Opening_Hash_Size);
}



const int PV_CSA = 0;
const int PV_USI = 1;

char *prt_pv_from_hash(tree_t * restrict ptree, int ply, int sideToMove, int fusi_str)
{
	static char str[TMP_BUF_LEN];
	if ( ply==1 ) str[0] = 0;
	HASH_SHOGI *phg = HashShogiReadLock(ptree, sideToMove);
	UnLock(phg->entry_lock);
	if ( phg->deleted ) return str;
//	if ( phg->hashcode64 != get_marge_hash(ptree, sideToMove) ) return str;
	if ( phg->hashcode64 != ptree->sequence_hash || phg->hash64pos != get_marge_hash(ptree, sideToMove) ) return str;
	if ( ply > 30 ) return str;

	int max_i = -1;
	int max_games = 0;
	int i;
	for (i=0;i<phg->child_num;i++) {
		CHILD *pc = &phg->child[i];
		if ( pc->games > max_games ) {
			max_games = pc->games;
			max_i = i;
		}
	}
	if ( max_i >= 0 ) {
		CHILD *pc = &phg->child[max_i];
		if ( ply > 1 ) strcat(str," ");

		if ( fusi_str ) {
			char buf[7];
			csa2usi( ptree, str_CSA_move(pc->move), buf );
			strcat(str,buf);
		} else {
			const char *sg[2] = { "-", "+" };
			strcat(str,sg[(root_turn + ply) & 1]);
			strcat(str,str_CSA_move(pc->move));
		}
		MakeMove( sideToMove, pc->move, ply );

		prt_pv_from_hash(ptree, ply+1, Flip(sideToMove), fusi_str);
		UnMakeMove( sideToMove, pc->move, ply );
	}
	return str;
}


int search_start_ct;
int stop_search_flag = 0;

void set_stop_search()
{
	stop_search_flag = 1;
}
int is_stop_search()
{
	return stop_search_flag;
}

std::mutex g_mtx;

std::atomic<int> uct_count(0);

int inc_uct_count()
{
	int count = uct_count.fetch_add(+1) + 1;
	if ( count >= nLimitUctLoop + 1 - (int)cfg_num_threads ) set_stop_search();
	return count;
}

int is_main_thread(tree_t * restrict ptree)
{
	return ( ptree == &tlp_atree_work[0] );
}
int get_thread_id(tree_t * restrict ptree)
{
	int i;
	for (i=0; i<(int)cfg_num_threads; i++) {
		if ( ptree == &tlp_atree_work[i] ) return i;
	}
	DEBUG_PRT("Err. get_thread_id()\n");
	return -1;
}
int is_limit_sec()
{
	double st = get_spend_time(search_start_ct);
	if ( time_left_msec[root_turn] && st > (double)time_left_msec[root_turn]/1000.0 - 10.0 ) return 1;
	if ( dLimitSec == 0 ) return 0;
	if ( st >= dLimitSec ) return 1;
	return 0;
}
int is_limit_sec_or_stop_input() {
	if ( is_limit_sec() ) set_stop_search();
	if ( check_stop_input() ) set_stop_search();
	return is_stop_search();
}

bool is_do_mate3() { return true; }
bool is_use_exact() { return true; }	// 有効だと駒落ちで最後の1手詰を見つけたら高確率でその1手だけを探索した、と扱われる

void uct_tree_loop(tree_t * restrict ptree, int sideToMove, int ply)
{
	ptree->sum_reached_ply = 0;
	ptree->max_reached_ply = 0;
	ptree->tlp_id = get_thread_id(ptree);	// for dfpn
	for (;;) {
		ptree->reached_ply = 0;
		int exact_value = EX_NONE;
		uct_tree(ptree, sideToMove, ply, &exact_value);
		ptree->sum_reached_ply += ptree->reached_ply;
		if ( ptree->reached_ply > ptree->max_reached_ply ) ptree->max_reached_ply = ptree->reached_ply;
		int count = inc_uct_count();
		if ( is_use_exact() && (exact_value == EX_WIN || exact_value == EX_LOSS) ) set_stop_search();
		if ( is_main_thread(ptree) ) {
			if ( IsHashFull() ) set_stop_search();
			is_limit_sec_or_stop_input();
			if ( isKLDGainSmall(ptree, sideToMove) ) set_stop_search();
			if ( is_send_usi_info() || is_stop_search() ) {
				send_usi_info(ptree, sideToMove, ply, count, (int)(count/get_spend_time(search_start_ct)));
			}
		}
		if ( is_stop_search() ) break;
	}
}

int uct_search_start(tree_t * restrict ptree, int sideToMove, int ply, char *buf_move_count)
{
	if ( fClearHashAlways ) {
		hash_shogi_table_clear();
	} else {
		thinking_age = (thinking_age + 1) & 0x7ffffff;
		if ( thinking_age == 0 ) thinking_age = 1;
		if ( thinking_age == 1 ) {
			hash_shogi_table_clear();
		} else {
			hash_half_del(ptree, sideToMove);
		}
	}

//	{ make_balanced_opening(ptree, sideToMove, ply); return 0; }
	int nFuri = getFuriPos(ptree, sideToMove, ply);

	HASH_SHOGI *phg = HashShogiReadLock(ptree, sideToMove);
	create_node(ptree, sideToMove, ply, phg);
	UnLock(phg->entry_lock);
//	int keep_root_games[MAX_LEGAL_MOVES];
	if ( fDiffRootVisit ) {
//		for (int i=0; i<phg->child_num; i++) keep_root_games[i] = phg->child[i].games;
	}
	if ( fResetRootVisit ) {
		for (int i=0; i<phg->child_num; i++) phg->child[i].games = 0;
	}
	const bool fPolicyRealization = false;
	float keep_root_policy[MAX_LEGAL_MOVES];
	for (int i=0; i<phg->child_num; i++) keep_root_policy[i] = phg->child[i].bias;

	const float epsilon = 0.25f;	// epsilon = 0.25
	const float alpha   = 0.15f;	// alpha ... Chess = 0.3, Shogi = 0.15, Go = 0.03
	if ( fAddNoise ) add_dirichlet_noise(epsilon, alpha, phg);
//{ void test_dirichlet_noise(float epsilon, float alpha);  test_dirichlet_noise(0.25f, 0.03f); }
	PRT("root phg->sequence_hash=%" PRIx64 ",pos=%" PRIx64 ", child_num=%d\n",phg->hashcode64,phg->hash64pos,phg->child_num);

	init_KLDGain_prev_dist_visits_total(phg->games_sum);

	search_start_ct = get_clock();

	int book_move = get_book_move(ptree,phg);
	if ( book_move ) {
		return book_move;
	}

	int thread_max = cfg_num_threads;
	std::vector<std::thread> ths(thread_max);
	uct_count = 0;
	stop_search_flag = 0;
	int i;
	for (i=1;i<thread_max;i++) {
		init_state( &tlp_atree_work[0], &tlp_atree_work[i]);
//		tlp_atree_work[i] = tlp_atree_work[0];	// lock_init()してるものがあるのでダメ
	}
	for (i=0;i<thread_max;i++) {
		ths[i] = std::thread(uct_tree_loop, &tlp_atree_work[i], sideToMove, ply);
	}

	for (std::thread& th : ths) {
		th.join();
	}

	int sum_r_ply = 0;
	int max_r_ply = 0;
	for (i=0;i<thread_max;i++) {
		tree_t * restrict ptree = &tlp_atree_work[i];
		sum_r_ply += ptree->sum_reached_ply;
		if ( ptree->max_reached_ply > max_r_ply ) max_r_ply = ptree->max_reached_ply;
	}

	int playouts = uct_count.load();
	double ave_reached_ply = (double)sum_r_ply / (playouts + (playouts==0));
	double ct = get_spend_time(search_start_ct);

	// select best
	int best_move = 0;
	int max_i = -1;
	int max_games = 0;
	const int SORT_MAX = MAX_LEGAL_MOVES;	// 593
	int sort_n = 0;
	bool found_mate = false;
	const float LARGE_NEGATIVE_VALUE = -1e4f;
	float max_lcb = LARGE_NEGATIVE_VALUE;
	typedef struct SORT_LCB {	// LCBを使わなくてもいったん代入
		int   move;
		int   games;
		float lcb;
		int   index;
	} SORT_LCB;
	SORT_LCB sort_lcb[SORT_MAX];	// 勝率、LCB、ゲーム数、着手、元のindexを保存

	float exact_v = ILLEGAL_MOVE;
	int loss_moves = 0;
	if ( is_use_exact() ) for (i=0;i<phg->child_num;i++) {
		CHILD *pc = &phg->child[i];
		if ( pc->exact_value == EX_LOSS || pc->value == ILLEGAL_MOVE ) loss_moves++;
		if ( pc->exact_value != EX_WIN ) continue;
		if ( pc->games == 0 ) DEBUG_PRT("");
		max_games = pc->games;
		max_i = i;

		SORT_LCB *p = &sort_lcb[sort_n];
		p->move  = pc->move;
		p->games = pc->games;
		p->lcb   = 0;
		p->index = i;
		sort_n++;
		exact_v = +1.0f;
		found_mate = true;
		PRT("MATE:%3d(%3d)%7s,%5d,%6.3f,bias=%.10f\n",i,sort_n,str_CSA_move(pc->move),pc->games,pc->value,pc->bias);
		break;
	}
	if ( phg->child_num == loss_moves ) {
		exact_v = 0.0f;
		PRT("LOSS\n");
	}

	if ( !found_mate ) {
		for (i=0;i<phg->child_num;i++) {
			CHILD *pc = &phg->child[i];
			if ( pc->games > max_games ) {
				max_games = pc->games;
				max_i = i;
			}
		}

		for (i=0;i<phg->child_num;i++) {
			CHILD *pc = &phg->child[i];
			float lcb = 0;
#ifdef USE_LCB
			if ( fLCB ) {	// Lower confidence bound of winrate.
				int visits = pc->games;
				float mean = pc->value;	// AobaZ(-1<x<1), LZ (0<x<1)
				lcb = LARGE_NEGATIVE_VALUE + visits + mean/4.0;	// large negative value if not enough visits.
				if (visits >= 2) {
//					float eval_variance = visits > 1 ? pc->squared_eval_diff / (visits - 1) : 1.0f;
//					auto stddev = std::sqrt(eval_variance / visits);
//					auto z = Utils::cached_t_quantile(visits - 1);
//					lcb = mean - 2.0f * z * stddev;
//					lcb = mean - 1.6f * sqrt( log((double)(phg->games_sum+1)) / visits );
					lcb = (pc->value*visits + -1.0*(max_games - visits)) / max_games;	// 残り全敗と仮定
				}
//				if ( lcb > max_lcb && visits > (float)max_games/1.5f ) {	// ある程度の回数を必要に
				if ( lcb > max_lcb ) {
					max_lcb = lcb;
					max_i = i;
				}
			}
#endif
			if ( pc->games ) {
				if ( sort_n >= SORT_MAX ) DEBUG_PRT("");
				SORT_LCB *p = &sort_lcb[sort_n];
				p->move  = pc->move;
				p->games = pc->games;
				p->lcb   = lcb;
				p->index = i;
				sort_n++;
				float v = pc->value;
				PRT("%3d(%3d)%7s,%5d,%6.3f,bias=%.10f,V=%6.2f%%,LCB=%6.2f%%\n",i,sort_n,str_CSA_move(pc->move),pc->games,pc->value,pc->bias,100.0*(v+1.0)/2.0,100.0*(lcb+1.0)/2.0);
			}
		}
	}
	if ( max_i >= 0 ) {
		CHILD *pc = &phg->child[max_i];
		best_move = pc->move;
		double v = 100.0 * (pc->value + 1.0) / 2.0;
		PRT("best:%s,%3d,%6.2f%%(%6.3f),bias=%6.3f\n",str_CSA_move(pc->move),pc->games,v,pc->value,pc->bias);
//		if ( v < 5 ) { PRT("resign threshold. 5%%\n"); best_move = 0; }
		char *pv_str = prt_pv_from_hash(ptree, ply, sideToMove, PV_CSA); PRT("%s\n",pv_str);
	}

	if ( fDiffRootVisit ) {
//		for (int i=0; i<phg->child_num; i++) {
//			sort_lcb[i].games -= keep_root_games[i];	// 1対1の対応でない
//			if ( sort_lcb[i].games < 0 ) DEBUG_PRT("");
//		}
	}
	int sum_games = 0;
	for (i=0; i<sort_n; i++) sum_games += sort_lcb[i].games;

	for (i=0; i<sort_n-1; i++) {
		int max_i = i;
		int max_g = sort_lcb[i].games;
		int j;
		for (j=i+1; j<sort_n; j++) {
			SORT_LCB *p = &sort_lcb[j];
			if ( p->games <= max_g ) continue;
			max_g = p->games;
			max_i = j;
		}
		if ( max_i == i ) continue;
		SORT_LCB dummy;
		dummy           = sort_lcb[    i];
		sort_lcb[    i] = sort_lcb[max_i];
		sort_lcb[max_i] = dummy;
	}
	if ( fLCB ) {	// 再度ソート。ただしgamesは変更しない。
		for (i=0; i<sort_n-1; i++) {
			int   max_i   = i;
			float max_lcb = sort_lcb[i].lcb;
			int j;
			for (j=i+1; j<sort_n; j++) {
				SORT_LCB *p = &sort_lcb[j];
				if ( p->lcb <= max_lcb ) continue;
				max_lcb = p->lcb;
				max_i   = j;
			}
			if ( max_i == i ) continue;
			SORT_LCB dummy;
			dummy           = sort_lcb[    i];
			sort_lcb[    i] = sort_lcb[max_i];
			sort_lcb[max_i] = dummy;
			int dum_g             = sort_lcb[    i].games;	// 再度swapでgamesはそのまま
			sort_lcb[    i].games = sort_lcb[max_i].games;
			sort_lcb[max_i].games = dum_g;
		}
	}

	buf_move_count[0] = 0;

	double best_v01 = 0;
	if ( fAutoResign ) {
		if ( max_i >= 0 ) {
			CHILD *pbest = &phg->child[max_i];
			best_v01 = (pbest->value + 1.0) / 2.0;	// -1 <= x <= +1  -->  0 <= x <= +1
		}
		if ( exact_v != ILLEGAL_MOVE ) best_v01 = exact_v;
		if ( fRawValuePolicy ) {
			sprintf(buf_move_count,"v=%.04f,r=%.04f,%d",best_v01,(phg->net_value + 1.0) / 2.0,sum_games);
		} else {
			sprintf(buf_move_count,"v=%.04f,%d",best_v01,sum_games);
		}
	} else {
		sprintf(buf_move_count,"%d",sum_games);
	}
	for (i=0;i<sort_n;i++) {
		SORT_LCB *p = &sort_lcb[i];
		char buf[7];
		csa2usi( ptree, str_CSA_move(p->move), buf );
		if ( 0 ) strcpy(buf,str_CSA_move(p->move));
//		PRT("%s,%d,",str_CSA_move(p->move),p->games);
		char str[TMP_BUF_LEN];
		if ( fRawValuePolicy ) {
//			static int p_count[301],p_sum = 0;
			int j;
			for (j=0;j<phg->child_num;j++) {
				if ( p->move != phg->child[j].move ) continue;
				float f = keep_root_policy[j];
				int min_c = -1;
				float min_diff = 10.0;
				int c;
				for (c=0;c<52;c++) {
					double k = 1.144;	// 52段階で0.001から0.99 までに分布するように
					double min = 0.001;
					double p = pow(k,c) * min;
					if ( fabs(p - f) < min_diff ) { min_c = c; min_diff = fabs(p - f); }
//					PRT("%d:p=%f\n",c,p);
				}
				if ( min_c < 0 ) DEBUG_PRT("");
				c = 'A' + min_c + (min_c >= 26)*6;	// A=0.001,Z=0.028,a=0.033,z=0.954
//				sprintf(str,",%s,%d,%.3f",buf,p->games,f);
				sprintf(str,",%s,%d%c",buf,p->games,c);
/*
				if ( f >= 0.01 ) p_count[(int)(f*100)]++;
				else if ( f >= 0.001 ) p_count[(int)(100+f*1000)]++;
				else if ( f >= 0.0001 ) p_count[(int)(200+f*10000)]++;
				else p_count[300]++;
				p_sum++;
*/
				break;
			}
			if ( j == phg->child_num ) DEBUG_PRT("");
/*
			if ( (p_sum%10)==0 && p_sum>0 ) {
				PRT("\np_sum=%d\n",p_sum);
				for (int j=0;j<301;j++) {
					PRT("%d,",p_count[j]);
				}
				PRT("\n");
				for (int j=0;j<301;j++) {
					PRT("%.4f,",(float)p_count[j]/p_sum);
				}
			}
*/
		} else {
			sprintf(str,",%s,%d",buf,p->games);
		}
		strcat(buf_move_count,str);
//		PRT("%s",str);
	}
//	PRT("\n");

	
	// 30手までは通常の温度で選び、31手以上はハンデのレートで弱くする。
	int is_opening_random = (ptree->nrep < nVisitCount || ptree->nrep < nVisitCountSafe);
	double select_rand_prob = dSelectRandom;
	double softmax_temp = cfg_random_temp;
//	int rate = nHandicapRate[nHandicap];
	int rate = 0;
	if ( nFuri >= 0 ) rate = nFuriHandicapRate[nFuri];
	int is_weaken = 0;

	// 1400点差まではsoftmaxの温度で。1400+1157 = 2557差までは合法手ランダムで。
	if ( (rate > 0 && sideToMove == black) ||	// レート差「＋」は先手を弱く、「－」は後手を弱く
		 (rate < 0 && sideToMove == white) ) {
		double t = 0;
		double s = 0;
		int abs_rate = abs(rate);
		if ( abs_rate <= TEMP_RATE_MAX ) {
			t = get_sigmoid_temperature_from_rate(abs_rate);
		} else {
			t = 6.068;
			s = get_sel_rand_prob_from_rate(abs_rate - TEMP_RATE_MAX);
		}
		if ( is_opening_random == 0 || (is_opening_random && t > softmax_temp) ) {	// 31手以上、または30手以下で温度1を超えてたら適用
			is_weaken = 1;
			softmax_temp     = t;
			select_rand_prob = s;
		}
	}
	PRT("nFuri=%2d,rate=%d,is_weaken=%d,softmax_temp=%lf,select_rand_prob=%lf\n",nFuri,rate,is_weaken,softmax_temp,select_rand_prob);

	// selects moves proportionally to their visit count
	if ( (is_opening_random || is_weaken) && sum_games > 0 && sort_n > 0 ) {
//		static std::mt19937_64 mt64;
		static std::uniform_real_distribution<> dist(0, 1);
		double indicator = dist(get_mt_rand);

		double inv_temperature = 1.0 / softmax_temp;
		double wheel[MAX_LEGAL_MOVES];
		double w_sum = 0.0;
		for (int i = 0; i < sort_n; i++) {
			double d = static_cast<double>(sort_lcb[i].games);
			wheel[i] = pow(d, inv_temperature);
			w_sum += wheel[i];
		}
		double factor = 1.0 / w_sum;

		int select_index = -1;
		double sum = 0.0;
		for (i = 0; i < sort_n; i++) {
			sum += factor * wheel[i];
			if (sum <= indicator && i + 1 < sort_n) continue;	// 誤差が出た場合は最後の手を必ず選ぶ
			select_index = i;
			break;
		}
		int r = (int)(indicator * sum_games);

		if ( select_index < 0 || i==sort_n ) DEBUG_PRT("Err. nVisitCount not found.\n");

		int org_index = sort_lcb[select_index].index;	// LCB適用前の手
		CHILD *pc  = &phg->child[org_index];
		bool fSwap = true;
		if ( nVisitCountSafe && max_i >= 0 ) {	// 勝率がそれほど下がらず、ある程度の回数試した手だけを選ぶ
			CHILD *pbest = &phg->child[max_i];
			fSwap = false;
			if ( fabs(pbest->value - pc->value) < 0.04 && pc->games*5 > pbest->games ) fSwap = true;	// 0.04 で勝率2%。1%だとelmo相手に同棋譜が800局で15局、2%で4局。
		}

		if ( average_winrate ) {
			for (i=0;i<sort_n;i++) {
				SORT_LCB *p = &sort_lcb[i];
				if ( p->move == balanced_opening_move[ptree->nrep+1] && p->games > 0 ) {
					PRT("average_winrate PLAY:%d->%d,move=%08x(%s)\n",select_index,i,p->move,string_CSA_move(p->move).c_str());
					select_index = i;
				}
			}
		}

		if ( fSwap ) {
			best_move = sort_lcb[select_index].move;
			PRT("rand select:%s,%3d,%6.3f,bias=%6.3f,r=%d/%d,softmax_temp=%.3f(rate=%d),select_rand_prob=%.3f\n",str_CSA_move(pc->move),pc->games,pc->value,pc->bias,r,sum_games,softmax_temp,rate,select_rand_prob);
		}
		if ( 0 && fPolicyRealization && ptree->nrep < 30 ) {
			static int prev_nrep = +999;
			static double realization_prob = 1;	// 単純に掛けると非常に小さい数になる。FLT_MIN = 1.175494e-38, DBL_MIN = 2.225074e-308
			static double realization_log  = 0;
			static double realization_log_sum = 0;
			if ( ptree->nrep < prev_nrep ) {
				realization_log_sum += realization_log;
				realization_prob = 1;
				realization_log  = 0;
			}
			prev_nrep = ptree->nrep;

			double b = keep_root_policy[org_index];
			realization_prob *= b;
			realization_log  += log(b);	// logを取って足す
			FILE *fp = fopen("policy_dist.log","a");
			if ( fp ) {
				fprintf(fp,"%7d:%4d:%2d,%7s,b=%10.7f(%10.7f),prob=%12g,log=%12f(%12f)\n",getpid_YSS(),usi_newgames,ptree->nrep,str_CSA_move(best_move),b,phg->child[org_index].bias, realization_prob, realization_log,(float)realization_log_sum/(usi_newgames-1+(usi_newgames==1)));
				fclose(fp);
			}
		}
	}
	if ( fPolicyRealization && phg->games_sum ) {
		FILE *fp = fopen("v_change.log","a");
		if ( fp ) {
			fprintf(fp,"%7d:%4d:%3d,%7s,%7.4f -> %7.4f(%7.4f):(%3d)",getpid_YSS(),usi_newgames,ptree->nrep,str_CSA_move(best_move),phg->net_value, best_v01*2.0-1, phg->win_sum / phg->games_sum,phg->child_num);
			for (int i=0;i<5;i++) {
				double b = keep_root_policy[i];
				double v = phg->child[i].value;
				if ( i>=phg->child_num ) { b = 0, v = 0; };
				fprintf(fp,",%6.4f(%7.4f)",b,v);
			}
			fprintf(fp,"\n");
			fclose(fp);
		}
	}
	if ( select_rand_prob > 0 && phg->child_num > 0 ) {
		double r = f_rnd();
		if ( r < select_rand_prob ) {
			int ri = rand_m521() % phg->child_num;
			best_move = phg->child[ri].move;
			PRT("ri=%d,r=%.3f,select_rand_prob=%.3f,best=%s\n",ri,r,select_rand_prob,str_CSA_move(best_move));
		}
	}

	if ( 0 && is_selfplay() && resign_winrate == 0 ) {
		int id = get_nnet_id();
		int pid = getpid_YSS();
		static int count, games;
		char str[TMP_BUF_LEN];
		if ( ptree->nrep==0 ) {
			if ( ++games > 15 ) { games = 1; count++; }
		}
		sprintf(str,"res%03d_%05d_%05d.csa",id,pid,count);
		FILE *fp = fopen(str,"a");
		if ( fp==NULL ) { PRT("fail res open.\n"); debug(); }

		float best_v = -1;
		if ( max_i >= 0 ) {
			CHILD *pbest = &phg->child[max_i];
			best_v = pbest->value;
		}
		if ( ptree->nrep==0 ) fprintf(fp,"/\nPI\n+\n");
		if ( best_move==0 ) {
			fprintf(fp,"%%TORYO,'%7.4f\n",best_v);
		} else {
			char sg[2] = { '+','-' };
			fprintf(fp,"%c%s,'%7.4f,%s\n",sg[ptree->nrep & 1],str_CSA_move(best_move),best_v,buf_move_count);
		}
		fclose(fp);
	}
	if ( 0 ) {
		static int search_sum = 0;
		static int playouts_sum = 0;
		const int M = 81;
		static int playouts_dist[M] = { 0 };
		search_sum++;
		playouts_sum += playouts;
		int m = playouts / KLDGainAverageInterval;
		if ( m > M-1 ) m = M-1;
		playouts_dist[m]++;
		FILE *fp = fopen("playouts_dist.log","a");
		if ( fp ) {
			fprintf(fp,"%7d:%4d:search_sum=%5d,playouts_ave=%7.2f:",getpid_YSS(),usi_newgames,search_sum, (float)playouts_sum/search_sum );
			for (i=0;i<M;i++) fprintf(fp,"%d,",playouts_dist[i]);
			fprintf(fp,"\n");
			fclose(fp);
		}
	}

	if ( fAutoResign == 0 && is_selfplay() && resign_winrate > 0 && max_i >= 0 ) {
		CHILD *pbest = &phg->child[max_i];
		double v = (pbest->value + 1.0) / 2.0;	// -1 <= x <= +1  -->  0 <= x <= +1
		if ( v < resign_winrate ) {
			best_move = 0;
			if ( 0 ) {
				FILE *fp = fopen("restmp.log","a");
				if ( fp ) {
					fprintf(fp,"%3d:%5d:%3d:v=%7.4f(%7.4f), resign_winrate=%7.4f\n",get_nnet_id(), getpid_YSS(), ptree->nrep, v, pbest->value, resign_winrate);
					fclose(fp);
				}
			}
		}
	}
	if ( fAutoResign && dAutoResignWinrate > 0 && max_i >= 0 ) {
		CHILD *pbest = &phg->child[max_i];
		double v = (pbest->value + 1.0) / 2.0;	// -1 <= x <= +1  -->  0 <= x <= +1
		if ( v < dAutoResignWinrate ) {
			best_move = 0;
		}
	}


	PRT("%.2f sec, c=%d,net_v=%6.2f%%(%.6f),h_use=%d,po=%d,%.0f/s,ave_ply=%.1f/%d (%d/%d),Noise=%d,g=%d,mt=%d,b=%d\n",
		ct,phg->child_num,(phg->net_value+1.0)*50.0,phg->net_value,hash_shogi_use,playouts,(double)playouts/ct,ave_reached_ply,max_r_ply,ptree->nrep,nVisitCount,fAddNoise,default_gpus.size(),thread_max,cfg_batch_size );

	return best_move;
}

void create_node(tree_t * restrict ptree, int sideToMove, int ply, HASH_SHOGI *phg, bool fOpeningHash)
{
	if ( phg->deleted == 0 ) {
		PRT("already created? ply=%d,sideToMove=%d,games_sum=%d,child_num=%d\n",ply,sideToMove,phg->games_sum,phg->child_num);
		return;
	}

if (0) {
	print_board(ptree);
	int i;
	for (i=0;i<81;i++) {
		int k = BOARD[i];
		PRT("%3d",k);
		if ( (i+1)%9==0 ) PRT("\n");
	}
	PRT("===kiki_count===\n");
	int kiki_count[2][81] = { 0 };
	kiki_count_indirect(ptree, kiki_count, NULL, false);
	for (int c=0;c<2;c++) for (i=0;i<81;i++) {
		PRT("%2d",kiki_count[c][i]);
		if ( (i+1)%9==0 ) PRT("\n");
		if ( i==80 ) PRT("\n");
	}

	int kiki_bit[14][2][81] = { 0 };
	kiki_count_indirect(ptree, NULL, kiki_bit, true);
	for (int c=0;c<2;c++) for (int k=0;k<14;k++) for (i=0;i<81;i++) {
		if ( i==0 ) PRT("k=%d,c=%d\n",k,c);
		PRT("%2d",kiki_bit[k][c][i]);
		if ( (i+1)%9==0 ) PRT("\n");
	}


	PRT("===\n");
	for (i=0;i<81;i++) {
		int k = count_square_attack(ptree, black, i);
		PRT("%2d",k);
		if ( (i+1)%9==0 ) PRT("\n");
	}
	PRT("---\n");
	for (i=0;i<81;i++) {
		int k = count_square_attack(ptree, white, i);
		PRT("%2d",k);
		if ( (i+1)%9==0 ) PRT("\n");
	}
	PRT("===\n");
}

	int move_num = generate_all_move( ptree, sideToMove, ply );

#ifdef CHILD_VEC
	phg->child.reserve(move_num);
#endif

	unsigned int * restrict pmove = ptree->move_last[0];
	int i;
	for ( i = 0; i < move_num; i++ ) {
		int move = pmove[i];
		CHILD *pc = &phg->child[i];
		pc->move = move;
		pc->bias = 0;
		if ( NOT_USE_NN ) pc->bias = f_rnd()*2.0f - 1.0f;	// -1 <= x <= +1
		pc->games = 0;
		pc->value = 0;
		pc->exact_value = EX_NONE;
		pc->mate_bit = MATE_CLEAR;
#ifdef USE_LCB
		pc->squared_eval_diff = 1e-4f;	// Initialized to small non-zero value to avoid accidental zero variances at low visits.
		pc->acc_virtual_loss = 0;
#endif
	}
	phg->child_num      = move_num;

	if ( fSkipOneReply ) {
//		static int count, all; all++;
//		if ( move_num==1 ) PRT("move_num=1,ply=%d,%d/%d\n",ply,++count,all);
	}


	// 静止探索で空き王手、飛角の移動で好手になりうる手を
	if (0) {
//		static tree_t copy_tree;
//		copy_tree = *ptree;
		int alpha = -30000, beta = +30000;
//		ptree->move_last[ply] = ptree->move_last[0];
		for (i=2;i<=ply;i++) ptree->move_last[i] = ptree->move_last[1];
		for (i=0; i<move_num; i++) {
			CHILD *pc = &phg->child[i];
			MakeMove( sideToMove, pc->move, ply );
			MOVE_CURR = pc->move;
//			ptree->path[ply] = pc->move;
			if ( InCheck(sideToMove) ) DEBUG_PRT("escape check err. %2d:%8s(%2d/%3d):selt=%3d\n",ply,str_CSA_move(pc->move),pc->games,phg->games_sum,i);
			int now_in_check = InCheck(Flip(sideToMove));
			if ( now_in_check ) {
				ptree->nsuc_check[ply+1] = (unsigned char)( ptree->nsuc_check[ply-1] + 1U );
			} else {
				ptree->nsuc_check[ply+1] = 0;
			}
//		    int v = -search_quies( ptree, -beta, -alpha, Flip(sideToMove), 2, 1 );
//			int v = -search_quies_aoba( ptree, -INT_MAX, +INT_MAX, Flip(sideToMove), ply+1, 1 );
		    int v = -search_quies_aoba( ptree, -beta, -alpha, Flip(sideToMove), ply+1, 1 );
//			print_board(ptree);
			PRT("root %3d,ply=%2d:%8s,v=%5d(%4d),%d\n",i,ply,str_CSA_move(pc->move),v,(int)ptree->node_searched,now_in_check);
			UnMakeMove( sideToMove, pc->move, ply );
//			out_pv( ptree, int value, int turn, unsigned int time );
		}
//		int *p = reinterpret_cast<int *>(ptree);
//		int *q = reinterpret_cast<int *>(&copy_tree);
//		int sz = sizeof(tree_t);
//		PRT("sz=%d\n",sz);
//		for (i=0;i<170000;i++) if ( *(p+i) != *(q+i) ) PRT("%d,",i);
//		*ptree = copy_tree;

//		print_board(ptree);
	}

	if ( NOT_USE_NN ) {
		// softmax
		const float temperature = 1.0f;
		float max = -10000000.0f;
		for (i=0; i<move_num; i++) {
			CHILD *pc = &phg->child[i];
			if ( max < pc->bias ) max = pc->bias;
		}
		float sum = 0;
		for (i=0; i<move_num; i++) {
			CHILD *pc = &phg->child[i];
			pc->bias = (float)exp((pc->bias - max)/temperature);
			sum += pc->bias;
		}
		for(i=0; i<move_num; i++){
			CHILD *pc = &phg->child[i];
			pc->bias /= sum;
//			PRT("%2d:bias=%10f, sum=%f,ply=%d\n",i,pc->bias,sum,ply);
		}
	}

	float v = 0;
	if ( NOT_USE_NN ) {
		float f = f_rnd()*2.0f - 1.0f;
		v = std::tanh(f);		// -1 <= x <= +1   -->  -0.76 <= x <= +0.76
//		{ static double va[2]; static int count[2]; va[sideToMove] += v; count[sideToMove]++; PRT("va[]=%10f,%10f\n",va[0]/(count[0]+1),va[1]/(count[1]+1)); }
//		PRT("f=%10f,tanh()=%10f\n",f,v);
	} else {
		if ( fSkipOneReply && move_num == 1 ) {
			v = 0;
			CHILD *pc = &phg->child[0];
			pc->bias = 1.0;
		} else if ( move_num == 0 ) {
			// get_network_policy_value() は常に先手が勝で(+1)、先手が負けで(-1)を返す。sideToMove は無関係
			v = -1;
			if ( sideToMove==white ) v = +1;	// 後手番で可能手がないなら先手の勝
//		} else if ( fSkipKingCheck && InCheck(sideToMove) ) {	// Policyは必要か
//			v = 0;
		} else {
			v = get_network_policy_value(ptree, sideToMove, ply, phg);

			// Valueの出力の形(-1 < v < 1)を変えてみる
			if (0) {
				double a = 4.0;	// 2.0でほぼ一致
				double vv = 2.0 / (1.0 + exp( -a*(v+0.0f))) - 1.0;
				PRT("v=%8.5f -> %8.5f\n",v,vv);
				v = vv;
			}

		}
//		{ PRT("ply=%2d,sideToMove=%d(white=%d),move_num=%3d,v=%.5f,eval=%d\n",ply,sideToMove,white,move_num,v,evaluate(ptree,ply,sideToMove)); print_board(ptree); }
	}
	if ( sideToMove==white ) v = -v;

	if ( 0 ) {	// 王手での素抜き、空き王手を無理やり補正
		unsigned int amove[MAX_NMOVE];
		unsigned int *plast = GenCheck( sideToMove, amove );
		int s = -sideToMove*2 + 1;	// 0,1 -> +1, -1

		float max_bias = -10000000.0f;
		for (i = 0; i < phg->child_num; i++) {
			CHILD *pc = &phg->child[i];
			if ( max_bias < pc->bias ) max_bias = pc->bias;
		}

		for (i = 0; i < phg->child_num; i++) {
			CHILD *pc = &phg->child[i];
			unsigned int move = pc->move;
			int is_promote = (int)I2IsPromote(move);
			int ipiece     = (int)I2PieceMove(move);
			int ifrom      = (int)I2From(move);
			int ito        = (int)I2To(move);

			if ( 1 && is_promote==0 ) {	// 無意味な不成を指さない。打ち歩詰打開でまれに有効だけど。dfpn、3手詰では不成は生成してない。打歩詰で不成が有効な13手詰では実際に不成を指す9手目まで進めないとp800では気づけない
				if ( ipiece==6 && sideToMove==black && (ifrom<A6 || ito<A6) ) pc->value = ILLEGAL_MOVE;
				if ( ipiece==6 && sideToMove==white && (ifrom>I4 || ito>I4) ) pc->value = ILLEGAL_MOVE;
				if ( ipiece==7 && sideToMove==black && (ifrom<A6 || ito<A6) ) pc->value = ILLEGAL_MOVE;
				if ( ipiece==7 && sideToMove==white && (ifrom>I4 || ito>I4) ) pc->value = ILLEGAL_MOVE;
				if ( ipiece==2 && sideToMove==black && (            ito<A7) ) pc->value = ILLEGAL_MOVE;
				if ( ipiece==2 && sideToMove==white && (            ito>I3) ) pc->value = ILLEGAL_MOVE;
				if ( ipiece==1 && sideToMove==black && (            ito<A6) ) pc->value = ILLEGAL_MOVE;
				if ( ipiece==1 && sideToMove==white && (            ito>I4) ) pc->value = ILLEGAL_MOVE;
			}
//continue;
//			if ( (ipiece&0x07)!=6 ) continue;
//			if ( ipiece==6 && sideToMove==black && (ifrom<A6 || ito<A6) && is_promote==0 ) continue;
//			if ( ipiece==6 && sideToMove==white && (ifrom>I4 || ito>I4) && is_promote==0 ) continue;
			if ( ifrom > I1 ) continue;	// 駒打ち
			int iking = (sideToMove==black) ? SQ_WKING : SQ_BKING;

			unsigned int *p;
			for ( p = amove; p != plast; p++ ) if ( *p == move ) break;
			if ( p == plast ) continue;

			// 移動元の8方向に自分の飛角がいて、その反対側に敵王がいれば空き王手
			//                                              敵の駒がいれば素抜き(移動した駒で王手)
			// 空き王手で移動後の駒で他の駒を取る場合。動いた駒で王手をかけて同～で素抜く場合。
			int d[8] = {+1,-1,+9,-9,+10,-10,+8,-8};
			int j,ka[2];
			for (j=0;j<8;j++) {
				ka[(j&1)] = 0;
				int dz = d[j];
				int prev,z;
				for (prev=ifrom; ; ) {
					z = prev + dz;
					if ( z > 80 || z < 0 ) break;
					if ( (prev%9)==8 && (z%9)==0 ) break;	// 盤外
					if ( (prev%9)==0 && (z%9)==8 ) break;
					int k = BOARD[z];
					if ( k ) {
//						za[(j&1)] = z;
						ka[(j&1)] = k;
						break;
					}
					prev = z;
				}
				if ( (j&1)==0 ) continue;
				if ( ka[0]==0 || ka[1]==0 ) continue;
				if ( ka[0]*ka[1] >= 0 ) continue;
				if ( abs(ka[0])==1 || abs(ka[1])==1 ) continue;

				int ok = 0;
				if ( j< 4 && (ipiece&0x07) != 7 && (abs(ka[0])&0x07)==7 && ka[0]*s > 0 ) ok =1;	// 手番と同じ飛車(龍)で反対側には相手の駒
				if ( j< 4 && (ipiece&0x07) != 7 && (abs(ka[1])&0x07)==7 && ka[1]*s > 0 ) ok =1;
				if ( j>=4 && (ipiece&0x07) != 6 && (abs(ka[0])&0x07)==6 && ka[0]*s > 0 ) ok =1;
				if ( j>=4 && (ipiece&0x07) != 6 && (abs(ka[1])&0x07)==6 && ka[1]*s > 0 ) ok =1;
				if ( (ipiece&0x07) != 6 && (ipiece&0x07) != 7 && abs((iking%9)-(ito%9))<=1 && abs(iking/9-ito/9)<=1 ) ok = 0;
				if ( (ipiece&0x07) >= 6 && abs((ifrom%9)-(ito%9))<=1 && abs(ifrom/9-ito/9)<=1 ) ok = 0;	// 2マス以上動く手
				if ( ok==0 ) continue;
//				PRT("ply=%2d,col=%d:%3d:%s %2d -> %2d(%2d,%d),k0=%3d,k1=%2d,OU=%2d,bias=%.5f\n",ply,sideToMove,i, string_CSA_move(move).c_str(),ifrom,ito,ipiece,(is_promote!=0),ka[0],ka[1],iking,pc->bias);
//				print_board(ptree);
				float b = max_bias / 3.0;
				if ( pc->bias < b ) pc->bias = b;
			}
		}
		
		if ( 0 ) {
//			print_board(ptree);
			unsigned int best_usi = get_best_move_alphabeta_usi( ptree, sideToMove, ply);
			for (i = 0; i < phg->child_num; i++) {
				CHILD *pc = &phg->child[i];
				if ( pc->move != (int)best_usi ) continue;
				PRT("found best_usi:ply=%2d,col=%d:%3d:%s,bias=%.5f,max_bias=%.5f\n",ply,sideToMove,i, string_CSA_move(best_usi).c_str(),pc->bias,max_bias);
				float b = max_bias / 3.0;
				if ( pc->bias < b ) pc->bias = b;
				break;
			}
		}

	}

	if ( 0 ) {	// 勝率補正。valueは楽観的？。初期valueは800playoutした後(最大回数の手の勝率)、0に近づく傾向にある
		float add[40] = {
			 0.004, 0.012, 0.016, 0.060, 0.081, 0.106, 0.145, 0.157, 0.152, 0.140,
			 0.127, 0.110, 0.100, 0.089, 0.078, 0.064, 0.055, 0.038, 0.031, 0.022,
			 0.012, 0.002,-0.004,-0.018,-0.031,-0.045,-0.062,-0.074,-0.094,-0.102,
			-0.123,-0.141,-0.149,-0.162,-0.131,-0.112,-0.052,-0.021, 0.001,-0.001
		};
		float w = v;
		int m = (int)((w+1.0)*20.0);
		if ( m <  0 ) m = 0;
		if ( m > 39 ) m = 39;
		w += add[m]*1.00;
		PRT("%d:%2d:m=%2d:v=%9.6f -> %9.6f(%6.3f)\n",sideToMove,ply,m,v,w,add[i]);
		v = w;
	}


	if ( fOpeningHash == false ) {	// policy softmax
		const float temperature = 1.8f;	// 1.0 より 1.4 - 1.8 の方が100-800playoutでは+50 ELO強い
//		float temperature = 2.3f - 0.004*(ptree->nrep + ply - 1);
//		if ( temperature < 1.3 ) temperature = 1.3;
//		PRT("%2d:%f\n",ptree->nrep + ply - 1,temperature);
		double inv_temperature = 1.0 / temperature;
		double wheel[MAX_LEGAL_MOVES];
		double w_sum = 0.0;
		for (int i = 0; i < phg->child_num; i++) {
			double d = phg->child[i].bias;
			wheel[i] = pow(d, inv_temperature);
			w_sum += wheel[i];
		}
		double factor = 1.0 / w_sum;
//		double h = 0;
		for (i = 0; i < phg->child_num; i++) {
//			double d = phg->child[i].bias;
//			h += d*log(d);
//			PRT("%2d:bias=%10f -> %10f, ply=%d,%f,%f\n",i,phg->child[i].bias,factor * wheel[i],ply, -d*log(d),-h );
			phg->child[i].bias = factor * wheel[i];
		}
	}

	phg->hashcode64     = ptree->sequence_hash;
	phg->hash64pos      = get_marge_hash(ptree, sideToMove);
	phg->mate_bit       = 0;
	phg->win_sum        = 0;
	phg->squared_eval   = 0;
	phg->games_sum      = 0;	// この局面に来た回数(子局面の回数の合計)
	phg->col            = sideToMove;
	phg->age            = thinking_age;
	phg->net_value      = v;
	phg->deleted        = 0;

//	if ( ! is_main_thread(ptree) && ply==3 ) { PRT("create_node(),ply=%2d,c=%3d,v=%.5f,seqhash=%" PRIx64 "\n",ply,move_num,v,ptree->sequence_hash); print_board(ptree); }
//PRT("create_node done...ply=%d,sideToMove=%d,games_sum=%d,child_num=%d,slot=%d,v=%5.2f\n",ply,sideToMove,phg->games_sum,phg->child_num, ptree->tlp_slot,v);

	if ( fOpeningHash ) {
	} else {
		std::lock_guard<std::mutex> guard(g_mtx); 
 		hash_shogi_use++;
	}
}

double uct_tree(tree_t * restrict ptree, int sideToMove, int ply, int *pExactValue)
{
	ptree->reached_ply = ply;
	HASH_SHOGI *phg = HashShogiReadLock(ptree, sideToMove);	// phgに触る場合は必ずロック！

	if ( phg->deleted ) {
		if ( ply<=1 ) PRT("not created? ply=%2d,col=%d\n",ply,sideToMove);
		if ( fClearHashAlways ) { DEBUG_PRT("not created Err\n"); }
		create_node(ptree, sideToMove, ply, phg);
	}

	if ( phg->col != sideToMove ) { DEBUG_PRT("hash col Err. phg->col=%d,col=%d,age=%d(%d),ply=%d,nrep=%d,child_num=%d,games_sum=%d,phg->hash=%" PRIx64 "\n",phg->col,sideToMove,phg->age,thinking_age,ply,ptree->nrep,phg->child_num,phg->games_sum,phg->hashcode64); }

	int child_num = phg->child_num;
	int select = -1;
	int loop;
	double max_value = -10000;

	double init_v = -1.0;	// 基本の勝率初期値は「負け」fpu (first play urgency)とも。
/*
	// 展開したい候補手の数。Progressive Windeningぽい感じにしてみたい
	double upper = log((phg->games_sum+1)/100.0) / log(2.0);	// 800で3, 8000で6
	if ( ply==1 && upper < 2 ) upper = 2;	// Rootは必ず2手は展開
	int expand = 0;
	for (loop=0; loop<child_num; loop++) {
		CHILD *pc  = &phg->child[loop];
		expand += (pc->games != 0);
	}
	if ( expand < (int)upper ) init_v = +1.0;	// 必ず展開
*/
	if ( phg->games_sum > 0 ) init_v = (phg->win_sum / phg->games_sum)/2.0f - 0.65f;	// 親ノードの勝率を使う
/*	if ( phg->games_sum > 100 ) {
		if ( phg->games_sum > 800 ) {
			init_v = -1.0;
		} else {	// 直線で近似
			init_v = ((-1.0 -init_v) / 700.0) * phg->games_sum  -1.0  -((-1.0 -init_v) / 700.0) * 800.0;
		}
	}
	PRT("init_v=%9f:w_sum=%7.2f,g_sum=%5d,ply=%2d\n",init_v,phg->win_sum,phg->games_sum,ply);
*/
	bool do_dfpn = false;
//	if ( ptree->tlp_id == 0 ) {	// スレッド固定だと特定の手にまったく来ないことがある？頓死するのでとりあえず削除。
//		if ( ply==1                   && (phg->mate_bit & MATE_3)     ==0 ) { phg->mate_bit |= MATE_3;      do_dfpn = true; }
		if ( phg->games_sum >=     10 && (phg->mate_bit & MATE_DFPN_0)==0 ) { phg->mate_bit |= MATE_DFPN_0; do_dfpn = true; }
		if ( phg->games_sum >=    100 && (phg->mate_bit & MATE_DFPN_1)==0 ) { phg->mate_bit |= MATE_DFPN_1; do_dfpn = true; }
		if ( phg->games_sum >=   1000 && (phg->mate_bit & MATE_DFPN_2)==0 ) { phg->mate_bit |= MATE_DFPN_2; do_dfpn = true; }
		if ( phg->games_sum >=  10000 && (phg->mate_bit & MATE_DFPN_3)==0 ) { phg->mate_bit |= MATE_DFPN_3; do_dfpn = true; }
		if ( phg->games_sum >= 100000 && (phg->mate_bit & MATE_DFPN_4)==0 ) { phg->mate_bit |= MATE_DFPN_4; do_dfpn = true; }
//	}

	if ( is_do_mate3() && do_dfpn ) for (;;) {
		if ( 0 ) {
			if ( ! is_mate_in3ply(ptree, sideToMove, ply) ) break;
		} else {
			UnLock(phg->entry_lock);
			unsigned int move;
			dfpn(ptree, sideToMove, ply, &move, phg->games_sum*1000);
			Lock(phg->entry_lock);
//			if ( ply == 2 ) { static int count; PRT("%2d:%6s:dfpn=%3d,games=%d(%d)\n",ply,string_CSA_move(ptree->path[ply-1]).c_str(),count++,phg->games_sum*1000,phg->games_sum); }
			if ( move == MOVE_NA ) break;
			MOVE_CURR = move;
		}
		if ( ply <= 4 ) PRT("dfpn mate: ply=%2d,col=%d,%-6s:move=%s:games=%d,%016" PRIx64 "\n",ply,sideToMove, string_CSA_move(ptree->path[ply-1]).c_str(), string_CSA_move(MOVE_CURR).c_str(),phg->games_sum, ptree->sequence_hash);

		if ( ! is_move_valid( ptree, MOVE_CURR, sideToMove ) ) break;

		// 1手詰があるのに3手詰を選び、連続王手の千日手になるのを避ける
		MakeMove( sideToMove, MOVE_CURR, ply );
		const int np = ptree->nrep + ply - 1;
		int i;
		for (i=np-1; i>=0; i-=2) {
			if ( ptree->rep_board_list[i] == HASH_KEY && ptree->rep_hand_list[i] == HAND_B ) break;
		}
		UnMakeMove( sideToMove, MOVE_CURR, ply );
		if ( i>=0 ) {
		 	PRT("repetition? ignore mate. ply=%d\n",ply);
		 	break;
		}

		for (loop=0; loop<child_num; loop++) {
			CHILD *pc  = &phg->child[loop];
			if ( (pc->move & MATE3_MASK) != (MOVE_CURR & MATE3_MASK) ) continue;
			pc->value = +1;
			pc->games++;
			pc->exact_value = EX_WIN;
			phg->games_sum++;
			break;
		}
		if ( loop==child_num ) {
			// position sfen l2g1R3/2ss1+P3/2ng3p1/1+Pp1p4/k3P4/NLPG1P3/g1B3P2/2KS4r/1N7 b 4P2LNSB5p 1
			// この局面で move=00015725(8685NY),  香の空き王手が成香になり、is_move_valid() はこれを認識しない。保木さんの古いバグ？一応修正
			DEBUG_PRT("Err. no mate move. ply=%d\n",ply);
		}
		UnLock(phg->entry_lock);
		*pExactValue = EX_LOSS;
		return +1;
	}

	double cINIT = 1.25;
	const bool fDynamicPUCT = true;
	if ( fDynamicPUCT ) {	// Dynamic Variance-Scaled cPUCT  https://github.com/lightvector/KataGo/blob/master/docs/KataGoMethods.md#dynamic-variance-scaled-cpuct
		int visits = phg->games_sum;
		if (visits >= 2) {
//			static double stddev_sum = 0;
//			static double stddev_min = 1;
//			static double stddev_max = 0;
//			static int    count = 0;
			float eval_variance = visits > 1 ? phg->squared_eval / (visits - 1) : 1.0f;
			auto stddev = std::sqrt(eval_variance / visits);
			double k = sqrt(stddev) * 4.0;	//stddev * 5.0;	// 根拠なしの式
//			const double AVG_STDDEV = 0.024; // 0.031;
//			double k = stddev / AVG_STDDEV;
			if ( k < 0.5 ) k = 0.5;
			if ( k > 1.4 ) k = 1.4;
			if ( 0 && ply==1 ) k = 1.0;
			double a = 1.0f / (1.0f+sqrt((double)visits/10000.0f));	// 1 -> 0
			k = a*k + (1.0f-a)*1.0f;	// visitsが大きいと1に近づく
			cINIT *= k;
//			stddev_sum += stddev;
//			if ( stddev > stddev_max ) stddev_max = stddev;
//			if ( stddev < stddev_min ) stddev_min = stddev;
//			count++;
//			PRT("%3d:%2d:games=%4d,mean=%7.4f,squared_eval=%6.2f,stddev=%7.4f,%7.4f,cINIT=%7.4f,ave_stddev=%6.4f(%6.4f,%6.4f)\n",ptree->nrep,ply,visits,phg->win_sum / visits,phg->squared_eval,stddev,k,cINIT,stddev_sum/count,stddev_min,stddev_max);
		}
	}
/*
	double c_bias[MAX_LEGAL_MOVES];
	if (1) {
		double temperature = 1.8;	// 1.0 より 1.4 - 1.8 の方が100-800playoutでは+50 ELO強い
		if ( ply==1 ) temperature = 1.2;
		double inv_temperature = 1.0 / temperature;
		double wheel[MAX_LEGAL_MOVES];
		double w_sum = 0.0;
		for (int i = 0; i < phg->child_num; i++) {
			double d = phg->child[i].bias;
			wheel[i] = pow(d, inv_temperature);
			w_sum += wheel[i];
		}
		double factor = 1.0 / w_sum;
		for (int i = 0; i < phg->child_num; i++) {
//			PRT("%2d:bias=%10f -> %10f, ply=%d\n",i,phg->child[i].bias,factor * wheel[i],ply);
			c_bias[i] = factor * wheel[i];
		}
	}
*/

	if ( ply==1 && ptree->sum_reached_ply==0 && average_winrate && ptree->nrep < nVisitCount ) {
 		for (loop=0; loop<child_num; loop++) {
			CHILD *pc  = &phg->child[loop];
			if ( pc->move == balanced_opening_move[ptree->nrep+1] && pc->games == 0 && pc->value != ILLEGAL_MOVE ) {
				select = loop;	// 必ず1回は探索する。千日手のチェックなども兼ねて
				PRT("average_winrate play:%d:select=%d,move=%08x(%s)\n",ply,select,pc->move,string_CSA_move(pc->move).c_str());
				goto skip_select;
			}
		}
	}

select_again:
	for (loop=0; loop<child_num; loop++) {
		CHILD *pc  = &phg->child[loop];
		if ( pc->value == ILLEGAL_MOVE ) continue;
		if ( is_use_exact() && pc->exact_value == EX_LOSS ) continue;
		if ( is_use_exact() && pc->exact_value == EX_WIN  ) { select = loop; break; }

		const double cBASE = 19652.0;
//		const double cINIT = 1.25;
		// cBASE has little effect on the value of c if games_sum is
		// sufficiently smaller than x.
		double c = std::log((1.0 + phg->games_sum + cBASE) / cBASE) + cINIT;
		
		// The number of visits to the parent is games_sum + 1.
		// There may by a bug in pseudocode.py regarding this.
		double puct = c * pc->bias     * std::sqrt(static_cast<double>(phg->games_sum + 1))
//		double puct = c * c_bias[loop] * std::sqrt(static_cast<double>(phg->games_sum + 1))
			       / static_cast<double>(pc->games + 1);
		// all values are initialized to loss value.  http://talkchess.com/forum3/viewtopic.php?f=2&t=69175&start=70#p781765
		double mean_action_value = (pc->games == 0) ? init_v : pc->value;

		// We must multiply puct by two because the range of
		// mean_action_value is [-1, 1] instead of [0, 1].
		double uct_value = mean_action_value + 2.0 * puct;

//		if ( ply==1 ) PRT("%3d:v=%5.3f,bias=%5.3f,p=%5.3f,u=%6.3f,g=%4d/%5d\n",loop,pc->value,pc->bias,puct,uct_value,pc->games,phg->games_sum);
		if ( uct_value > max_value ) {
			max_value = uct_value;
			select = loop;
		}
	}
	if ( select < 0 ) {
//		PRT("no legal move. mate? ply=%d,child_num=%d\n",ply,child_num);
		UnLock(phg->entry_lock);
		*pExactValue = EX_WIN;	// 1手前に指された手で勝
		return -1;
	}
skip_select:
	// 実際に着手
	CHILD *pc = &phg->child[select];
	if ( ! is_move_valid( ptree, pc->move, sideToMove ) ) {
		PRT("illegal move?=%08x(%s),ply=%d,select=%d,sideToMove=%d\n",pc->move,str_CSA_move(pc->move),ply,select,sideToMove); print_board(ptree);
		// this happens in 64bit sequence hash collision. We met this while 170000 training games. very rare case.
		pc->value = ILLEGAL_MOVE;
		select = -1;
		max_value = -10000;
		goto select_again;
	}
//	PRT("%2d:%s:SHash=%016" PRIx64,ply,str_CSA_move(pc->move),ptree->sequence_hash);
	MakeMove( sideToMove, pc->move, ply );
	ptree->path[ply] = pc->move;
//	PRT(" -> %016" PRIx64 "\n",ptree->sequence_hash);

	MOVE_CURR = pc->move;
	copy_min_posi(ptree, Flip(sideToMove), ply);
//	if ( ply==3 ) print_all_min_posi(ptree, ply+1);

//	PRT("%2d:tid=%d:%s(%3d/%5d):select=%3d,v=%6.3f\n",ply,get_thread_id(ptree),string_CSA_move(pc->move).c_str(),pc->games,phg->games_sum,select,max_value);

	if ( InCheck(sideToMove) ) {
		DEBUG_PRT("escape check err. %2d:%8s(%2d/%3d):selt=%3d,v=%.3f\n",ply,str_CSA_move(pc->move),pc->games,phg->games_sum,select,max_value);
	}

	int now_in_check = InCheck(Flip(sideToMove));

	enum { SENNITITE_NONE, SENNITITE_DRAW, SENNITITE_WIN };
	int flag_sennitite = SENNITITE_NONE;
	int flag_illegal_move = 0;

	const int np = ptree->nrep + ply - 1;
	// ptree->history_in_check[np] にはこの手を指す前に王手がかかっていたか、が入る
//	PRT("ptree->nrep=%2d,ply=%2d,sideToMove=%d,nrep=%2d,hist_in_check[]=%x\n",ptree->nrep,ply,sideToMove,np,ptree->history_in_check[np]);
	// 千日手判定
	// usiで局面を作るときはmake_move_root()を千日手無視で作っている。
	// 6手一組以上の連続王手の千日手はある？
	int i,sum = 0;
	uint64 key  = HASH_KEY;
//	uint64 hand = Flip(sideToMove) ? HAND_B : HAND_W;
	int same_i = 0;	// 同一局面になった一番小さい手数
	const int SUM_MAX = 3;		// 過去に3回同じ局面。つまり同一局面4回
	for (i=np-1; i>=0; i-=2) {
		if ( ptree->rep_board_list[i] == key && ptree->rep_hand_list[i] == HAND_B ) {
			same_i = i;
			sum++;
			if ( sum == SUM_MAX ) break;
		}
	}
	if ( sum >= SUM_MAX-1 ) {
		flag_sennitite = SENNITITE_DRAW;

		// 連続王手か？。王手をかけた場合と王が逃げた場合、の2通りあり
		int start_j = np-1;
		if ( now_in_check==0 ) start_j = np;
		int flag_consecutive_check = 1;
//		for (int j=0; j<=np; j++) PRT("%d",(ptree->history_in_check[j]!=0)); PRT("\n");
		for (int j=start_j; j>=same_i; j-=2) {
			if ( ptree->history_in_check[j]==0 ) { flag_consecutive_check = 0; break; }
		}
		if ( flag_consecutive_check ) {
			if ( now_in_check ) {
//				PRT("perpetual check! delete this move.  %d -> %d (%d)\n",start_j,same_i,(start_j-same_i)+1);
				flag_illegal_move = 1;
			} else {
//				PRT("perpetual check escape! %d -> %d (%d)\n",start_j,same_i,(start_j-same_i)+1);
				flag_sennitite = SENNITITE_WIN;
			}
		}
		// 王手の同一局面は3回目で指さない。王が逃げる手で4回目で負ける場合あり
		if ( sum != SUM_MAX && flag_illegal_move == 0 ) {
			flag_sennitite = SENNITITE_NONE;
		}
//		PRT("sum=%d,i=%d(%d),nrep=%d,ply=%d,%s,flag=%d,now_in_check=%d,i=%d,np=%d,start_j=%d\n",sum,i,np-i,ptree->nrep,ply,str_CSA_move(pc->move),flag_sennitite,now_in_check,i,np,start_j);
	}

	if ( flag_illegal_move ) {
		UnMakeMove( sideToMove, pc->move, ply );
		pc->value = ILLEGAL_MOVE;
		select = -1;
		max_value = -10000;
		goto select_again;
	}

	double win = 0;
	int skip_search = 0;
	if ( flag_sennitite != SENNITITE_NONE ) {
		// 先手なら 勝=+1 負=-1,  後手なら 勝=+1 負=-1
		win = 0;
		pc->exact_value = EX_DRAW;
		if ( flag_sennitite == SENNITITE_WIN ) {
			win = +1.0;
			pc->exact_value = EX_WIN;
		}
		skip_search = 1;
//		PRT("flag_sennitite=%d, win=%.1f, ply=%d\n",flag_sennitite,win,ply);
	}

	if ( !now_in_check ) {	// root(ply=1)では判定しない。自己対局では宣言勝ちはautousi、"-i" では最後に行う。
		if ( is_declare_win(ptree, Flip(sideToMove)) ) {
			win = -1.0;	// 宣言されて負け
			pc->exact_value = EX_LOSS;
			skip_search = 1;
		}
	}

	if ( skip_search == 0 && !now_in_check && is_do_mate3() && (pc->mate_bit & MATE_3)==0 ) {
		pc->mate_bit |= MATE_3;
		if ( is_mate_in3ply(ptree, Flip(sideToMove), ply+1) ) {
//			PRT("mated3ply: ply=%2d,col=%d,move=%08x(%s)\n",ply,sideToMove, MOVE_CURR,string_CSA_move(MOVE_CURR).c_str());	// str_CSA_move() は内部でstaticを持つので複数threadでは未定義
			win = -1.0;	// 3手で詰まされる
			pc->exact_value = EX_LOSS;
			skip_search = 1;
		}
	}

	if ( 0 && skip_search == 0 && !now_in_check && is_do_mate3() && (pc->mate_bit & MATE_DFPN_0)==0 && pc->games >= 0 ) {	// ( || hasLoss)
		pc->mate_bit |= MATE_DFPN_0;
		unsigned int move;
		dfpn(ptree, Flip(sideToMove), ply+1, &move, 1000);
		if ( move != MOVE_NA ) {
			PRT("dfpn: ply=%2d,col=%d,move=%08x(%s)\n",ply,sideToMove, move,string_CSA_move(move).c_str());
			win = -1.0;
			pc->exact_value = EX_LOSS;
			skip_search = 1;
		}
	}

	if ( nDrawMove ) {
		int d = ptree->nrep + ply - 1 + 1 + sfen_current_move_number;
		if ( d >= nDrawMove ) {
			win = 0;
			pc->exact_value = EX_DRAW;
			skip_search = 1;
//			PRT("nDrawMove=%d over. ply=%d,moves=%d,%s\n",nDrawMove,ply,d,str_CSA_move(pc->move));
		}
	}

	if ( ply >= PLY_MAX-10 ) { DEBUG_PRT("depth over=%d\n",ply); }

	if ( skip_search == 0 ) {
		int down_tree = 0;
		int do_playout = 0;
		int force_do_playout = (ply >= PLY_MAX-11);
		if ( pc->games == 0 || force_do_playout ) {
			do_playout = 1;
		} else {
			down_tree = 1;
		}
		if ( do_playout ) {	// evaluate this position
			UnLock(phg->entry_lock);

			HASH_SHOGI *phg2 = HashShogiReadLock(ptree, Flip(sideToMove));	// 1手進めた局面のデータ
			if ( phg2->deleted ) {
				create_node(ptree, Flip(sideToMove), ply+1, phg2);
			} else {
//				static int count; PRT("has come already? ply=%d,%d\n",ply,++count); //debug();	// 手順前後? 複数スレッドの場合
				down_tree = 1;
			}
			if ( fSkipOneReply && phg2->child_num == 1 ) {
//				PRT("down_tree:ply=%d\n",ply);
				down_tree = 1;
			}
			if ( fSkipKingCheck && now_in_check ) {
				down_tree = 1;
			}
			if (0) {
				static int count;
				if ( count++ >= 1 ) {
					count = 0;
				} else {
					down_tree = 1;	// 無理やり1手先で評価する(1 thread でのテスト)
				}
			}
	
			win = -phg2->net_value;

			UnLock(phg2->entry_lock);
			Lock(phg->entry_lock);
		}
//		PRT("down_tree=%d,do_playout=%d,ply=%d\n",down_tree,do_playout,ply);
		if ( down_tree && force_do_playout==0 ) {
			// down tree
			const int fVirtualLoss = 1;
			const int VL_N = 1;
			const int one_win = -1;	// 最初は負け、を仮定
			if ( fVirtualLoss ) {	// この手が負けた、とする。複数スレッドの時に、なるべく別の手を探索するように
				pc->value = (float)(((double)pc->games * pc->value + one_win*VL_N) / (pc->games + VL_N));	// games==0 の時はpc->value は無視されるので問題なし
				pc->games      += VL_N;
				phg->games_sum += VL_N;	// 末端のノードで減らしても意味がない、のでUCTの木だけで減らす
#ifdef USE_LCB
				pc->acc_virtual_loss += VL_N;
#endif
			}

			UnLock(phg->entry_lock);
			int ex_value = EX_NONE;
			win = -uct_tree(ptree, Flip(sideToMove), ply+1, &ex_value);
			Lock(phg->entry_lock);

//			PRT("down_tree:ply=%d:child_num=%3d,win=%5.2f\n",ply,phg->child_num,win);
			if ( fVirtualLoss ) {
#ifdef USE_LCB
				pc->acc_virtual_loss -= VL_N;
#endif
				phg->games_sum -= VL_N;
				pc->games      -= VL_N;		// gamesを減らすのは非常に危険！ あちこちで games==0 で判定してるので
				if ( pc->games < 0 ) { DEBUG_PRT("Err pc->games=%d\n",pc->games); }
				if ( pc->games == 0 ) pc->value = 0;
				else                  pc->value = (float)((((double)pc->games+VL_N) * pc->value - one_win*VL_N) / pc->games);
			}
			if ( pc->exact_value == EX_NONE ) pc->exact_value = ex_value;	// 複数スレッドだと順番によって上書きされる
		}
	}

	UnMakeMove( sideToMove, pc->move, ply );

	if ( is_use_exact() && pc->exact_value == EX_WIN ) {	// この手を指せば勝
		*pExactValue = EX_LOSS;
//		PRT("mate: ply=%2d,col=%d,move=%08x(%s)win=%5.2f -> +1.0\n",ply,sideToMove, MOVE_CURR,string_CSA_move(MOVE_CURR).c_str(),win);
		win = +1.0;
	}

#ifdef USE_LCB
	// LCB用 Welford's online algorithm for calculating variance.
	float eval       = win;								// eval は netの値そのもの。
	float old_eval   = (float)pc->games * pc->value + pc->acc_virtual_loss * 1;	// 累積。old_accumulate_eval が正しいか
	float old_visits = pc->games - pc->acc_virtual_loss;
	if ( old_visits < 0 ) DEBUG_PRT("");
	float old_delta  = old_visits > 0 ? eval - old_eval / old_visits : 0.0f;
	float new_delta  = eval - (old_eval + eval) / (old_visits + 1);
	float delta      = old_delta * new_delta;
	pc->squared_eval_diff += delta;

	if ( fDynamicPUCT ) {	// このノードの分散を求める。puctの動的変更に利用
		eval       = win;
		old_eval   = phg->win_sum;
		old_visits = phg->games_sum;
		old_delta  = old_visits > 0 ? eval - old_eval / old_visits : 0.0f;
		new_delta  = eval - (old_eval + eval) / (old_visits + 1);
		delta      = old_delta * new_delta;
		phg->squared_eval += delta;
	}
#endif

	double win_prob = ((double)pc->games * pc->value + win) / (pc->games + 1);	// 単純平均

	pc->value = (float)win_prob;
	pc->games++;			// この手を探索した回数
	phg->win_sum += win;
	phg->games_sum++;
	phg->age = thinking_age;

	UnLock(phg->entry_lock);
	return win;
}

void init_yss_zero()
{
	static int fDone = 0;
	if ( fDone ) return;
	fDone = 1;

	if ( is_process_batch() == false ) init_seqence_hash();
	if ( is_selfplay() == false && fFuriShitei == false ) set_rand_FuriPos();

	std::random_device rd;
	init_rnd521( (int)time(NULL)+getpid_YSS() + rd() );		// 起動ごとに異なる乱数列を生成
	inti_rehash();

	init_network();
	make_move_id_c_y_x();
}

void copy_min_posi(tree_t * restrict ptree, int sideToMove, int ply)
{
	if ( ptree->nrep < 0 || ptree->nrep >= REP_HIST_LEN ) { PRT("nrep Err=%d\n",ptree->nrep); debug(); }
	min_posi_t *p = &ptree->record_plus_ply_min_posi[ptree->nrep + ply];
	p->hand_black = HAND_B;
	p->hand_white = HAND_W;
	p->turn_to_move = sideToMove;
	int i;
	for (i=0;i<nsquare;i++) {
		p->asquare[i] = ptree->posi.asquare[i];
	}
}

void print_board(min_posi_t *p)
{
	const char *koma_kanji[32] = {
		"・","歩","香","桂","銀","金","角","飛","玉","と","杏","圭","全","","馬","龍",
  	};
	int i;
	for (i=0;i<nsquare;i++) {
		int k = p->asquare[i];
//		int ch = k < 0 ? '-' : '+';
		int ch = k < 0 ? 'v' : ' ';
//		if ( k < 0 ) k = abs(k) + 16;
//		PRT("%c%s", ch, astr_table_piece[ abs( k ) ] );
  	    PRT("%c%s", ch, koma_kanji[ abs( k ) ] );
		if ( i==8 || i==80 ) {
			int hand = p->hand_white;
			if ( i==80 ) hand= p->hand_black;
			PRT("   FU %2d,KY %2d,KE %2d,GI %2d,KI %2d, KA %2d,HI %2d", (int)I2HandPawn(hand), (int)I2HandLance(hand), (int)I2HandKnight(hand),(int)I2HandSilver(hand),(int)I2HandGold(hand), (int)I2HandBishop(hand), (int)I2HandRook(hand));
		}
		if ( ((i+1)%9)==0 ) PRT("\n");
	}
}
void print_all_min_posi(tree_t * restrict ptree, int ply)
{
	int i;
	PRT("depth=%d+%d=%d\n",ptree->nrep, ply, ptree->nrep + ply);
	for (i=0;i<ptree->nrep + ply; i++) {
		PRT("-depth=%d\n",i);
		print_board(&ptree->record_plus_ply_min_posi[i]);
	}
}

std::string keep_cmd_line;

int getCmdLineParam(int argc, char *argv[])
{
	int i;
	for (i=1; i<argc; i++) {
		char sa[2][TMP_BUF_LEN];
		memset(sa,0,sizeof(sa));
		strncpy(sa[0], argv[i],TMP_BUF_LEN-1);
		if ( i+1 < argc ) strncpy(sa[1], argv[i+1],TMP_BUF_LEN-1);
		keep_cmd_line += sa[0];
		if ( i < argc-1 ) keep_cmd_line += " ";
//		PRT("argv[%d]=%s\n",i,sa[0]);
		char *p = sa[0];
		char *q = sa[1];
		if ( strncmp(p,"-",1) != 0 ) continue;

		float nf = (float)atof(q);
		int n = (int)nf;
		// 2文字以上は先に判定してcontinueを付けること
		if ( strstr(p,"-nn_rand") ) {
			PRT("not use NN. set policy and value with random.\n",q);
			NOT_USE_NN = 1;
			continue;
		}
		if ( strstr(p,"-mtemp") ) {
			PRT("cfg_random_temp=%f\n",nf);
			cfg_random_temp = nf;
			continue;
		}
		if ( strstr(p,"-msafe") ) {
			nVisitCountSafe = n;
			PRT("play safe randomly first %d moves\n",n);
			continue;
		}
		if ( strstr(p,"-drawmove") ) {
			PRT("nDrawMove=%d\n",n);
			nDrawMove = n;
			continue;
		}
		if ( strstr(p,"-sel_rand") ) {
			PRT("dSelectRandom=%f\n",nf);
			dSelectRandom = nf;
			continue;
		}
#ifdef USE_LCB
		if ( strstr(p,"-lcb") ) {
			fLCB = true;
			PRT("fLCB=%d\n",fLCB);
			continue;
		}
#endif
		if ( strstr(p,"-kldgain") ) {
			PRT("MinimumKLDGainPerNode=%.10f\n",nf);
			MinimumKLDGainPerNode = nf;
			continue;
		}
		if ( strstr(p,"-kldinterval") ) {
			PRT("KLDGainAverageInterval=%d\n",n);
			KLDGainAverageInterval = n;
			continue;
		}
		if ( strstr(p,"-reset_root_visit") ) {
			fResetRootVisit = true;
			PRT("fResetRootVisit=%d\n",fResetRootVisit);
			continue;
		}
		if ( strstr(p,"-diff_root_visit") ) {
			fDiffRootVisit = true;
			PRT("fDiffRootVisit=%d\n",fDiffRootVisit);
			continue;
		}
		if ( strstr(p,"-name") ) {
			strcpy(engine_name, q);
			PRT("name=%s\n",q);
			continue;
		}
		if ( strstr(p,"-fs") ) {	// "-fs 001000000" 先手3間飛車
			for (int i=0; i<9; i++) nFuriPos[i+0] = (q[i]=='1');
			PRT("fs=%s\n",q);
			fFuriShitei = true;
			continue;
		}
		if ( strstr(p,"-fg") ) {	// "-fg 000001000" 後手4間飛車
			for (int i=0; i<9; i++) nFuriPos[i+9] = (q[i]=='1');
			PRT("fg=%s\n",q);
			for (int i=0; i<18; i++) {
				PRT("%d",nFuriPos[i]);
				if ( i==8 ) PRT(":");
			}
			PRT("\n");
			fFuriShitei = true;
			continue;
		}

#ifdef USE_OPENCL
		if ( strstr(p,"-dirtune") ) {
			PRT("DirTune=%s\n",q);
			sDirTune = q;
			continue;
		}
		if ( strstr(p,"-u") ) {
			default_gpus.push_back(n);
			PRT("default_gpus.size()=%d\n", default_gpus.size());
        	for (size_t i=0;i<default_gpus.size();i++) {
        		PRT("default_gpus[%d]=%d\n", i,default_gpus[i]);
			}
		}
		if ( strstr(p,"-b") ) {
			if ( n < 1 ) n = 1;
			cfg_batch_size = n;
			PRT("cfg_batch_size=%d\n",n);
		}
		if ( strstr(p,"-e") ) {
			PRT("nNNetServiceNumber=%d\n",n);
			if ( n < 0 ) DEBUG_PRT("Err. nNNetServiceNumber.\n");
			nNNetServiceNumber = n;
			default_gpus.push_back(n);
		}
		if ( strstr(p,"-h") ) {
			if ( n != 1 && n != 2 ) DEBUG_PRT("Err. set nUseWmma and nUseHalf.\n");
			if ( n==1 ) nUseWmma = 1;
			if ( n==2 ) nUseHalf = 1;
			PRT("nUseWmma=%d,nUseHalf=%d\n",nUseWmma,nUseHalf);
		}
#endif
		if ( strstr(p,"-p") ) {
			PRT("playouts=%d\n",n);
			nLimitUctLoop = n;
			set_Hash_Shogi_Table_Size(n);
		}
		if ( strstr(p,"-t") ) {
			if ( n < 1            ) n = 1;
			if ( n > TLP_NUM_WORK ) n = TLP_NUM_WORK;
			cfg_num_threads = n;
			PRT("cfg_num_threads=%d\n",n);
		}
		if ( strstr(p,"-s") ) {
			if ( nf <= 0 ) nf = 0;
			PRT("dLimitSec=%.3f\n",nf);
			dLimitSec = nf;
		}
		
		if ( strstr(p,"-w") ) {
			PRT("network path=%s\n",q);
			default_weights = q;
		}
		if ( strstr(p,"-q") ) {
			fVerbose = 0;
		}
		if ( strstr(p,"-n") ) {
			fAddNoise = 1;
			PRT("add dirichlet noise\n");
		}
		if ( strstr(p,"-m") ) {
			nVisitCount = n;
			PRT("play randomly first %d moves\n",n);
		}
		if ( strstr(p,"-i") ) {
			PRT("usi info enable\n");
			fUsiInfo = 1;
		}
		if ( strstr(p,"-r") ) {
			PRT("fAutoResign. resign winrate=%.5f\n",nf);
			fAutoResign = 1;
			dAutoResignWinrate = nf;
		}
//		PRT("%s,%s\n",sa[0],sa[1]);
	}

	for (i=0;i<(int)keep_cmd_line.size();i++) {
		char c = keep_cmd_line.at(i);
		if ( c < 0x20 || c > 0x7e ) c = '?';
		keep_cmd_line[i] = c;
	}
	if ( keep_cmd_line.size() > 127 ) keep_cmd_line.resize(127);
//	PRT("%s\n",keep_cmd_line.c_str());

#ifdef USE_OPENCL
	if ( is_process_batch() == false ) {
		if ( sDirTune.empty() ) {
			std::string d_dir = "data";
			sDirTune = d_dir;
#if defined(_MSC_VER)
			char path[MAX_PATH];
			if (::GetModuleFileNameA(NULL, path, MAX_PATH) != 0) {
				char *p = strstr(path, "bin\\aobaz.exe");
//				char *p = strstr(path, "Release\\aobaz.exe");
				if (p) {
					*p = 0;
					sDirTune = std::string(path) + d_dir;
				}
//				char drive[MAX_PATH], dir[MAX_PATH], fname[MAX_PATH], ext[MAX_PATH];
//				::_splitpath_s(path, drive, dir, fname, ext);
//				PRT("%s\n%s\n%s\n%s\n%s\n", path, drive, dir, fname, ext);
				PRT("%s\n", sDirTune.c_str());
			}
#endif
		}

		struct stat st_buf;
		if ( stat(sDirTune.c_str(), &st_buf) != 0 ) {
			PRT("DirTune does not exit. Tuning every time.\n");
			sDirTune = "";
		}
	}
#endif

	if ( default_weights.empty() ) {
		PRT("A network weights file is required to use the program.\n");
		debug();
	}

	return 1;
}

void set_default_param()
{
	// スレッド、バッチサイズが未定ならCPU版は最大数、OpenCLはb=7,t=21 を指定
	// OpenCLでスレッドのみ指定ならバッチサイズはb=3,7,14
	// OpenCLでバッチサイズのみ指定なら(b=3,t=7), (b=7,t=15),(b=14,t=29),(b=28,t=85)
	if ( is_process_batch() ) {
		cfg_num_threads = 1;
		return;
	}
#ifdef USE_OPENCL
	if ( default_gpus.size() == 0 ) default_gpus.push_back(-1);	// -1 でbestを自動選択
	int gpus = (int)default_gpus.size();
	if ( cfg_num_threads == 0 && cfg_batch_size == 0 ) {
		cfg_batch_size  = 7;
		cfg_num_threads = 21 * gpus;
	} else if ( cfg_num_threads == 0 ) {
		cfg_num_threads = (cfg_batch_size * 3 + 0) * gpus;
	} else if ( cfg_batch_size == 0 ) {
		cfg_batch_size  = (cfg_num_threads/gpus - 0) / 3;
		if ( cfg_batch_size < 1 ) cfg_batch_size = 1;
	} else {
		if ( cfg_batch_size > cfg_num_threads ) {
			DEBUG_PRT("Err. must be threads >= batch_size\n");
		}
	}
#else
	if ( cfg_num_threads == 0 ) {
		auto cfg_max_threads = std::min(SMP::get_num_cpus(), size_t{TLP_NUM_WORK});
		cfg_num_threads = cfg_max_threads;
	}
#endif
	if ( nLimitUctLoop < (int)cfg_num_threads ) {
		PRT("too small playouts. thread = 1, batch = 1\n");
		cfg_num_threads = 1;
		cfg_batch_size  = 1;
	}

}

const char *get_cmd_line_ptr()
{
	return keep_cmd_line.c_str();
}

int check_stop_input()
{
	if ( next_cmdline(0)==0 ) return 0;
	if ( strstr(str_cmdline,"stop") ) {
		return 1;
	}
	return 0;
}

int is_ignore_stop()
{
	if ( usi_go_count <= usi_bestmove_count ) {
		PRT("warning stop ignored(go_count=%d,bestmove_count=%d)\n",usi_go_count,usi_bestmove_count);
		return 1;	// このstopは無視
	}
	return 0;
}

int is_send_usi_info()
{
	if ( fUsiInfo == 0 ) return 0;
	static int prev_send_t = 0;
	int flag = 0;
//	if ( nodes ==   1 ) flag = 1;
	if ( prev_send_t == 0 ) flag = 1;
	double st = get_spend_time(prev_send_t);
	if ( st > 1.0 || flag ) {
		prev_send_t = get_clock();
		return 1;
	}
	return 0;
}
// https://twitter.com/issei_y/status/589642166818877440
// 評価値と勝率の関係。 Ponanzaは評価値と勝率に以下の式の関係があると仮定して学習しています。 大体300点くらいで勝率6割、800点で勝率8割くらいです。
// 勝率 = 1 / (1 + exp(-評価値/600))
// 評価値 = 600 * ln(勝率 / (1-勝率))       0.00 <= winrate <= 1.00   -->  -5000 <= value <= +5000
int winrate_to_score(float winrate)
{
	double v = 0;
	double w = winrate;
	// w= 0.9997, v= +4866.857
	if        ( w > 0.9997	) {
		v = +5000.0;
	} else if ( w < 0.0003	) {
		v = -5000.0;
	} else {
		v = 600 * log(w / (1-w));
	}
//	PRT("w=%8.4f, v=%8.3f\n",w,v);
	return (int)v;
}

void send_usi_info(tree_t * restrict ptree, int sideToMove, int ply, int nodes, int nps)
{
	HASH_SHOGI *phg = HashShogiReadLock(ptree, sideToMove);
	int max_i = -1;
	int max_games = 0;
	int i;
	for (i=0;i<phg->child_num;i++) {
		CHILD *pc = &phg->child[i];
		if ( pc->games > max_games ) {
			max_games = pc->games;
			max_i = i;
		}
	}
	UnLock(phg->entry_lock);
	if ( max_i < 0 ) return;

	CHILD *pc = &phg->child[max_i];
	int vloss = 0;
#ifdef USE_LCB
	vloss = pc->acc_virtual_loss;
#endif
	float old_eval   = (float)pc->games * pc->value + vloss * 1;
	float old_visits = pc->games - vloss;
	float v          = old_eval / (old_visits + (old_visits==0));
	float wr =         (v + 1.0f) / 2.0f;	// -1 <= x <= +1   -->   0 <= y <= +1
	int score = winrate_to_score(wr);
	
	char *pv_str = prt_pv_from_hash(ptree, ply, sideToMove, PV_USI);
//	char *pv_str = prt_pv_from_hash(ptree, ply, sideToMove, PV_CSA);
	int depth = (int)(1.0+log(nodes+1.0));
	depth = (1 + (int)strlen(pv_str)) / 5; // 2019.12.7 改造48
	char str[TMP_BUF_LEN];
	sprintf(str,"info depth %d score cp %d nodes %d nps %d pv %s",depth,score,nodes,nps,pv_str);
	strcat(str,"\n");	// info depth 2 score cp 33 nodes 148 pv 7g7f 8c8d
	USIOut( "%s", str);
}

bool is_selfplay()
{
#ifdef USE_OPENCL
	if ( nNNetServiceNumber >= 0 && fAddNoise && nVisitCount > 0 ) return true;
#endif
	return false;
}

// 自己対戦以外で未設定の場合は乱数で
void set_rand_FuriPos() {
	int rp[9] = { 100,100,100,100,100,50,50,1,100 };	// 居飛車は基本指さない。袖飛車、右4間も少な目に
	int sum = 0;
	for (int i=0;i<9;i++) sum += rp[i];
	if ( sum <= 0 ) DEBUG_PRT("");

	unsigned int r = rand_m521();
	int ri = (r % sum) + 1;
	int i,s = 0;
	for (i=0; i<9; i++) {
		s += rp[i];
		if ( s >= ri ) break;
	}
	if ( i==9 ) DEBUG_PRT("not found ri=%d,sum=%d", ri,sum);

	for (int j=0; j<9; j++) nFuriPos[j+0] = (j==i);
	for (int j=0; j<9; j++) nFuriPos[j+9] = (j==8-i);

	PRT("random nFuriPos=");
	for (int i=0; i<18; i++) {
		PRT("%d",nFuriPos[i]);
		if ( i==8 ) PRT(":");
	}
	PRT("\n");
}

void usi_newgame(tree_t * restrict ptree)
{
	usi_newgames++;
	hash_shogi_table_clear();
	if ( is_selfplay() ) {
		resign_winrate = FIXED_RESIGN_WINRATE;
		if ( (rand_m521() % 10) == 0 ) {	// 10%で投了を禁止。間違った投了が5％以下か確認するため
			resign_winrate = 0;
		}
		if ( 0 && average_winrate ) {
			char str[256] = "startpos moves ";	// "position startpos moves "
		 	char *lasts = str;
			usi_posi( ptree, &lasts );
			make_balanced_opening(ptree, root_turn, 1);
		}
	} else {
		if ( fFuriShitei == false ) set_rand_FuriPos();
	}
}


void test_dist()
{
	static std::mt19937_64 mt64;
//	std::random_device rd;
//	mt64.seed(rd());
	const int N = 10;
	int count[N];
	for (int i=0; i<N; i++) count[i] = 0;
	for (int loop=0; loop < 1000; loop++) {
		static std::uniform_real_distribution<> dist(0, 1);
//		double indicator = dist(mt64);
		double indicator = dist(get_mt_rand);

		double inv_temperature = 1.0 / 1.0;
		double wheel[N];
		int games[N] = { 500,250,100,50,30, 20,20,20,9,1 };
		double w_sum = 0.0;
		for (int i = 0; i < N; i++) {
			double d = static_cast<double>(games[i]);
			wheel[i] = pow(d, inv_temperature);
			w_sum += wheel[i];
		}
		double factor = 1.0 / w_sum;

		int best_i = -1;
		double sum = 0.0;
		for (int i = 0; i < N; i++) {
			sum += factor * wheel[i];
			if (sum <= indicator && i + 1 < N) continue;	// 誤差が出た場合は最後の手を必ず選ぶ
			best_i = i;
			break;
		}
		if ( best_i < 0 ) DEBUG_PRT("Err. nVisitCount not found.\n");
		count[best_i]++;
	}
	PRT("\n"); for (int i=0; i<N; i++) PRT("[%2d]=%5d\n",i,count[i]);

}
void test_dist_loop()
{
	for (int i = 0; i<10; i++) test_dist();
}

int is_declare_win(tree_t * restrict ptree, int sideToMove)
{
	int king_in3[2],sum_in3[2],pieces_in3[2];
	king_in3[0]   = king_in3[1]   = 0;
	sum_in3[0]    = sum_in3[1]    = 0;
	pieces_in3[0] = pieces_in3[1] = 0;

	int irank, ifile;
	for ( irank = rank1; irank <= rank9; irank++ ) {		// rank1 = 0
		for ( ifile = file1; ifile <= file9; ifile++ ) {	// file1 = 0
			int i = irank * nfile + ifile;
			int piece = BOARD[i];
			if ( piece == 0 ) continue;
			// "* ", "FU", "KY", "KE", "GI", "KI", "KA", "HI", "OU", "TO", "NY", "NK", "NG", "##", "UM", "RY"
			int k = abs(piece);	// 1...FU, 2...KY, 3...KE, ..., 15..RY
			int flag = 0;
			int sg = 0;
			if  ( piece > 0 ) {
				sg = 0;
				if ( irank <= rank3 ) flag = 1;
			} else {
				sg = 1;
				if ( irank >= rank7 ) flag = 1;
			}
			if ( flag == 0 ) continue;
			if ( k == 8 ) {
				king_in3[sg] = 1;
				continue;
			}
			pieces_in3[sg]++;
			if ( (k & 0x07) >= 6 ) {
				sum_in3[sg] += 5;
			} else {
				sum_in3[sg] += 1;
			}
		}
	}

	int hn[2][8];
	int i;
	for (i=0;i<2;i++) {
		unsigned int hand = HAND_B;	// 先手の持駒
		if ( i==1 )  hand = HAND_W;
		hn[i][1] = (int)I2HandPawn(  hand);
		hn[i][2] = (int)I2HandLance( hand);
		hn[i][3] = (int)I2HandKnight(hand);
		hn[i][4] = (int)I2HandSilver(hand);
		hn[i][5] = (int)I2HandGold(  hand);
		hn[i][6] = (int)I2HandBishop(hand);
		hn[i][7] = (int)I2HandRook(  hand);
	}

	sum_in3[0] += hn[0][1] + hn[0][2] + hn[0][3] + hn[0][4] + hn[0][5] + (hn[0][6] + hn[0][7])*5;
	sum_in3[1] += hn[1][1] + hn[1][2] + hn[1][3] + hn[1][4] + hn[1][5] + (hn[1][6] + hn[1][7])*5;

	if ( nHandicap == 1 ) sum_in3[1] += 1;	// ky
	if ( nHandicap == 2 ) sum_in3[1] += 5;	// ka
	if ( nHandicap == 3 ) sum_in3[1] += 5;	// hi
	if ( nHandicap == 4 ) sum_in3[1] += 10;	// 2mai
	if ( nHandicap == 5 ) sum_in3[1] += 12;	// 4mai
	if ( nHandicap == 6 ) sum_in3[1] += 14;	// 6mai

	int declare_ok = 0;
	if ( sum_in3[0] >= 28 && pieces_in3[0] >= 10 && king_in3[0] && sideToMove==black ) declare_ok = 1;
	if ( sum_in3[1] >= 27 && pieces_in3[1] >= 10 && king_in3[1] && sideToMove==white ) declare_ok = 1;
//	PRT("ok=%d,sum[]=%2d,%2d, pieces[]=%2d,%2d, king[]=%d,%d, side=%d\n", declare_ok,sum_in3[0],sum_in3[1],pieces_in3[0],pieces_in3[1],king_in3[0],king_in3[1],sideToMove);
	return declare_ok;
}

int is_declare_win_root(tree_t * restrict ptree, int sideToMove)
{
	int now_in_check = InCheck(sideToMove);
	if ( now_in_check ) return 0;
	return is_declare_win(ptree, sideToMove);
}

void find_temp_rate_sigmoid()
{
	const int TEMP_RATE_N = 13;
	double xTR[TEMP_RATE_N] = {    5,  13,  80, 194, 273, 639, 906,1123,1273,1372,1407,1470,1503 };
	double yTR[TEMP_RATE_N] = { 0.01, 0.1, 0.5, 0.7, 0.8, 1.2, 1.7, 2.5, 3.8, 6.0,  10, 100,10000 };

	double err_min = +INT_MAX; 
	double a,b,c;
	double ma=5.4480,mb=1444.5,mc=-0.1500;
	if (0) for (a=4; a<7; a+=0.002) {
		for (b=1350; b<1550; b+=0.1) {
			for (c=-0.2; c<=+0; c+=0.001) {
				double err = 0;
				for (int i=0;i<TEMP_RATE_N;i++) {
					double x = xTR[i];
					double y = log10(yTR[i]);
					double rate = b * 1.0 / (1.0 + exp( -a*(y+c)));
					double d = rate - x;
					err += d*d;
				}
				// err=14956.23, a=5.4480,b=1444.5,c=-0.1500
				// err= 6103.74, a=5.9200,b=1391.0,c=-0.1300 ... 最後の2個を無視
				if ( err < err_min ) {
					PRT("err=%.2f, a=%.4f,b=%.1f,c=%.4f\n",err,a,b,c);
					ma=a; mb=b; mc=c;
					err_min = err;
				}
			}
		}
	}
	for (int i=0;i<TEMP_RATE_N;i++) {
		double x = xTR[i];
		double y = log10(yTR[i]);
		double rate = mb * 1.0 / (1.0 + exp( -ma*(y+mc)));
		PRT("x=%6.1f log(y)=%8.3f, rate=%6.1f\n",x,y,rate);
	}
	for (double r=10;r<1400;r+=20) {
//		double yb = r / mb;
//		double logtemp = (1.0/ma)*log(yb / (1-yb)) - mc;
		double logtemp = (1.0/ma)*log(r / (mb-r)) - mc;
		PRT("r=%6.1f logtemp=%8.3f, temp=%8.3f\n",r,logtemp,pow(10,logtemp));
	}
/*
err=14956.23, a=5.4480,b=1444.5,c=-0.1500
x=   5.0 log(y)=  -2.000, rate=   0.0
x=  13.0 log(y)=  -1.000, rate=   2.7
x=  80.0 log(y)=  -0.301, rate= 114.0
x= 194.0 log(y)=  -0.155, rate= 230.6
x= 273.0 log(y)=  -0.097, rate= 298.5
x= 639.0 log(y)=   0.079, rate= 584.6
x= 906.0 log(y)=   0.230, rate= 878.0
x=1123.0 log(y)=   0.398, rate=1147.3
x=1273.0 log(y)=   0.580, rate=1317.7
x=1372.0 log(y)=   0.778, rate=1398.8
x=1407.0 log(y)=   1.000, rate=1430.6
x=1470.0 log(y)=   2.000, rate=1444.4
x=1503.0 log(y)=   4.000, rate=1444.5
*/
}
double get_sigmoid_temperature_from_rate(int rate)
{
	double ma=5.4480,mb=1444.5,mc=-0.1500;
	double r = rate;
	if ( r > TEMP_RATE_MAX ) DEBUG_PRT("");
	double logtemp = (1.0/ma)*log(r / (mb-r)) - mc;
	return pow(10,logtemp);
}

// mtemp_6.068 で合法手からランダムにある確率で選ぶ、場合のrateと確率xの関係
// 2次の近似曲線だと rate = 803.6*x*x + 353.6*x,     x=1.0 で1157.2差
double get_sel_rand_prob_from_rate(int rate)
{
	if ( rate < 0 || rate > 1157 ) DEBUG_PRT("");
	double a = 803.6;
	double b = 353.6;
	double c = -rate;
	// x = (-b +- sqrt(b*b - 4*a*c)) / (2*a) = -353.6 + sqrt(125032.96 + 4*803.6 * rate) / 2*803.6
	double x = (-b + sqrt(b*b - 4.0*a*c)) / (2.0*a);
	if ( x < 0 || x > 1 ) DEBUG_PRT("");
	return x;
}

/*
前回の調整から
過去1000棋譜の勝率が0.60を超えてたら、調整(+70の半分、+35を引く)
過去2000棋譜の勝率が0.58を超えてたら、調整
過去4000棋譜の勝率が0.56を超えてたら、調整
過去8000棋譜の勝率で無条件で        、調整
*/


std::vector<int> prev_dist_;
int prev_dist_visits_total_;

bool isKLDGainSmall(tree_t * restrict ptree, int sideToMove) {
	if ( MinimumKLDGainPerNode <= 0 ) return false;
	bool ret = false;
	HASH_SHOGI *phg = HashShogiReadLock(ptree, sideToMove);
	UnLock(phg->entry_lock);

 	if ( phg->games_sum >= KLDGainAverageInterval && prev_dist_.size() == 0 ) {
		// 探索木の再利用で100以上ならそれを基準に
	} else {
	 	if ( phg->games_sum < prev_dist_visits_total_ + KLDGainAverageInterval ) return false;
	}

	double min_kld;
	if ( ptree->nrep < 4 ) {
		min_kld = MinimumKLDGainPerNode*6;	// 初手から4手目までは深く読まない
//	} else if ( phg->games_sum < 500 ) {
//		min_kld = 0.000001;	// 少ないノード数では厳しい条件の方が強い。総playout数の増加もそれほどなし。
	} else {
		min_kld = MinimumKLDGainPerNode;
	}

	std::vector<int> new_visits;
    for (int i=0;i<phg->child_num;i++) {
		new_visits.push_back(phg->child[i].games);
	}

	if (prev_dist_.size() != 0) {
		double sum1 = 0.0;
		double sum2 = 0.0;
		for (decltype(new_visits)::size_type i = 0; i < new_visits.size(); i++) {
			sum1 += prev_dist_[i];
			sum2 += new_visits[i];
		}
		if ( sum1==0 || sum2==0 || sum1==sum2 ) return false;
		double kldgain = 0.0;
		for (decltype(new_visits)::size_type i = 0; i < new_visits.size(); i++) {
			double old_p = prev_dist_[i] / sum1;
			double new_p = new_visits[i] / sum2;
			if ( old_p != 0 && new_p != 0 ) {
				kldgain += old_p * log(old_p / new_p);
			}
		}
		double x = kldgain / (sum2 - sum1);
		if ( x < min_kld ) ret = true;
		PRT("%8d:kldgain=%.7f,%.7f,sum1=%.0f,sum2=%.0f,ret=%d\n",phg->games_sum,kldgain,x,sum1,sum2,ret);
	}
	prev_dist_.swap(new_visits);
	prev_dist_visits_total_ = phg->games_sum;
	return ret;
}

void init_KLDGain_prev_dist_visits_total(int games_sum) {
	prev_dist_visits_total_ = games_sum;
	prev_dist_.clear();
//	PRT("prev_dist_.size()=%d\n",prev_dist_.size());
}

static int n_vCount[PLY_MAX];
// Policyの分布(ノイズあり)だけから形勢が互角に近い30手後の局面を作成
int balanced_opening(tree_t * restrict ptree, int sideToMove, int ply, int fPolicyBest, int *pUndo) {
	*pUndo = 0;

	HASH_SHOGI one_hash;
	HASH_SHOGI *phg = ReadOpeningHash(ptree, sideToMove);
	if ( phg->deleted ) {
		if ( IsOpeningHashFull() || ply >= Opening_Hash_Stop_Ply+1 ) {
			phg = &one_hash;
			phg->deleted = 1;
		} else {
			opening_hash_use++;
		}
		create_node(ptree, sideToMove, ply, phg, true);
	}

	int fHashFull = 0;
	int stop_ply = nVisitCount+1;
	const int VALUE_N = 1;	// 0ですべてノードでValueを調べる。1で1回以上なら調べる
	if ( (phg->games_sum >= VALUE_N || ply <= 2) && phg->child_num > 0 && phg->child[0].value == 0 && ply != stop_ply ) {
		int i;
		for (i = 0; i < phg->child_num; i++) {
			CHILD *pc = &phg->child[i];
			MakeMove(   sideToMove, pc->move, ply );
			MOVE_CURR = pc->move;
			copy_min_posi(ptree, Flip(sideToMove), ply);	// 過去7手前までがNNには必要なので

			HASH_SHOGI *phg2 = ReadOpeningHash(ptree, Flip(sideToMove));
			if ( phg2->deleted ) {
				fHashFull = IsOpeningHashFull();
				if ( fHashFull == 0 ) {
					create_node(ptree, Flip(sideToMove), ply+1, phg2, true);
 					opening_hash_use++;
				}
			}
//			for (int i=1; i<ply+1; i++) PRT("%s:",str_CSA_move(path_move[i], true));
			float v = (-phg2->net_value+1.0)/2.0;
			pc->value = v;
//			PRT(",v=%9f,bias=%9f,v=%f,fHashFull=%d\n",100.0*v,pc->bias*100.0,pc->value,fHashFull);
			UnMakeMove( sideToMove, pc->move, ply );
			if ( fHashFull ) break;
		}
		if ( fHashFull == 0 ) n_vCount[ply-1]++;
	}

	int fUseValue = (phg->child_num > 0 && phg->child[0].value != 0 && fHashFull == 0);	// valueの差をPolicyの代わりに

//fUseValue = 0;

	float v = phg->net_value;
	if ( (ply+1)&1 ) v = -v;
	v = (1.0+v) / 2.0;
//	const float vd = 0.20; // 0.20で17%が失敗。0.15で20%が失敗。0.55 だと 0.35 < x < 0.75,  投了が0.23 なのでほぼぎりぎり

	double b = 0;
	double c = 150;	// 上下にこのレート差の勝率まで。wr=0.5 で 0.3 < v < 0.5, wr=0.7 で 0.49 < v < 0.84
	double wr = average_winrate;
	if ( wr < 0.01 ) wr = 0.01;
	if ( wr > 0.99 ) wr = 0.99;
	double x = -400.0*log(1.0/wr - 1.0) / log(10.0);
	double r0 = 1.0 / (1.0 + pow(10.0,(b+c-x)/400.0));
	double r1 = 1.0 / (1.0 + pow(10.0,(b-c-x)/400.0));
	// r0 < v < r1 ならOK
//	PRT("v=%f, %f < %f < %f\n",v, r0,wr,r1);
	int ret = 0;
	if ( ply <= stop_ply && (v > r1 || v < r0) ) ret = 1;

	// Undoして勝率調整すると、0手目の全部の局面が同じ勝率、で学習されてしまう。
	if ( 0 && fPolicyBest == 0 && ret ) {
		*pUndo = 1;
		return ret;
	}

	if ( ply == stop_ply ) {	// 31で30手
		static int all,n[10];
		static int v_dist[101];
		static int furi_file[2][9] = {0}, furi[2] = {0};
		all++;
//		if ( BOARD[33]==-1 ) n[0]++;	// 34歩
//		if ( BOARD[47]==+1 ) n[0]++;	// 76歩
//		if ( BOARD[52]==+1 ) n[0]++;	// 26歩
//		if ( BOARD[47]==+1 && BOARD[48]==+1 ) n[0]++;	// 76歩、66歩
		if ( BOARD[43]==+1 && BOARD[24]==-6 ) n[0]++;	// ▲25歩、△33角
//		if ( BOARD[24]==-6 ) n[0]++;	// ▲25歩、△33角
		v_dist[(int)(100.0*v) % 101]++;	// 0.90 - 0.10
		for (int i=0;i<81;i++) {
			if ( BOARD[i] == +7 ) { furi_file[0][i%9]++; furi[0]++; }
			if ( BOARD[i] == -7 ) { furi_file[1][i%9]++; furi[1]++; }
		}


		const int SAME_MAX = 100000;
		static uint64 same_hash[SAME_MAX];
		static int same_num = 0;
		uint64 hash64pos = get_marge_hash(ptree, sideToMove);

		int j;
		for (j=0; j<same_num; j++) {
			if ( same_hash[j] == hash64pos ) break;
		}
		if ( j == same_num && SAME_MAX > same_num ) {
			same_hash[same_num++] = hash64pos;
		}
		PRT("v=%.3f(%d),%d/%d=%f,unique=%d(%.3f)\n",v,ret,n[0],all,(float)n[0]/all,same_num,(float)same_num/all);
//		for (j=0;j<101;j++) PRT("%d(%.3f),",v_dist[j],(float)v_dist[j]/all); PRT("\n");

//		PRT("v=%.3f(%d),%d/%d\n",v,ret,n[0],all);
		for (int j=0;j<2;j++) {
			for (int i=0;i<9;i++) PRT("%6.3f,",(float)furi_file[j][i]/(furi[j]+(furi[j]==0)));
			PRT("\n");
		}
		print_board(ptree);

		return ret;
	}

	if ( phg->child_num == 0 ) { PRT("mate\n"); return 1; }
	float max_v = -999;
	float max_i = -1;
	int i;
	for (i = 0; i < phg->child_num; i++) {
		CHILD *pc = &phg->child[i];
		if ( pc->value > max_v ) { max_v = pc->value; max_i = i; }
	}
	if ( max_i < 0 ) DEBUG_PRT("");

	HASH_SHOGI hs;	// ノイズを足した後の値に
	hs.child.reserve(phg->child_num);
	hs.child_num  = phg->child_num;
	double sum = 0.0;
	for (i = 0; i < phg->child_num; i++) {
		CHILD *pc = &phg->child[i];
		hs.child[i].bias = pc->bias;
		hs.child[i].move = pc->move;
		float d = max_v - pc->value;
//		PRT("%s:bias=%9f,value=%9f,diff=%9f,%f,%f\n",str_CSA_move(pc->move),pc->bias,pc->value,d,1.0/exp(d*70.0),1.0/exp(d*90.0));
		if ( fUseValue ) hs.child[i].bias = 1.0/exp(d*70.0);
		sum += hs.child[i].bias;
	}

	for (i = 0; i < phg->child_num; i++) {
//		CHILD *pc = &phg->child[i];
		hs.child[i].bias /= sum;
//		PRT("%s:->%9f\n",str_CSA_move(pc->move),hs.child[i].bias);
	}

//	const float epsilon = 0.25f;	// epsilon = 0.25
	const float epsilon = 0.05f;	// epsilon = 0.25 が通常だが探索なしはノイズの影響が大きいので減らす
	const float alpha   = 0.15f;	// alpha ... Chess = 0.3, Shogi = 0.15, Go = 0.03
	if ( fAddNoise ) add_dirichlet_noise(epsilon, alpha, &hs);

	static std::uniform_real_distribution<> dist(0, 1);	// 0以上1未満の値を等確率で発生
	double indicator = dist(get_mt_rand);

	double inv_temperature = 1.0 / cfg_random_temp;
	double wheel[MAX_LEGAL_MOVES];
	double w_sum = 0.0;
	for (i = 0; i < phg->child_num; i++) {
		double d = hs.child[i].bias;
		wheel[i] = pow(d, inv_temperature);
		w_sum += wheel[i];
	}
	double factor = 1.0 / w_sum;

	CHILD *pc = NULL;
	sum = 0;
	for (i = 0; i < phg->child_num; i++) {
		sum += factor * wheel[i];
		if ( sum <= indicator && i + 1 < phg->child_num ) continue;	// 誤差が出た場合は最後の手を必ず選ぶ
		pc = &phg->child[i];
		break;
	}
	if ( pc==NULL || i==phg->child_num ) DEBUG_PRT("Err.\n");

undo:
//	char sg[2] = { '+','-' };
//	PRT("%2d:fUseValue=%d,%2d:net_v=%7.4f(%6.3f)ret=%d:%c%s,bias=%6.3f,r=%.3f/%.3f\n",ply,fUseValue,i,phg->net_value,v,ret,sg[(ptree->nrep+ply+1) & 1],str_CSA_move(pc->move),pc->bias,indicator,w_sum);
	MakeMove(   sideToMove, pc->move, ply );
	MOVE_CURR = pc->move;
	balanced_opening_move[ply] = pc->move;
	copy_min_posi(ptree, Flip(sideToMove), ply);	// 過去7手前までがNNには必要なので
	int fUndo;
	ret = balanced_opening(ptree, Flip(sideToMove), ply+1, (pc == &phg->child[0]), &fUndo);
	UnMakeMove( sideToMove, pc->move, ply );
	if ( fUndo ) {
		pc = &phg->child[0];
		ret = 0;
		goto undo;
	}

	phg->games_sum++;
	return ret;
}

void make_balanced_opening(tree_t * restrict ptree, int sideToMove, int ply) {
	static int fDone = 0;
	if ( fDone == 0 ) {
		clear_opening_hash();
		fDone = 1;
	}
	double st = get_clock();
	int i,out = 0, fUndo;
	for (i=0;i<10000;i++) {
		int ret = balanced_opening(ptree, sideToMove, ply, 0, &fUndo);
		if ( ret ) out++;
		PRT("hash=%d/%d,out=%d/%d\n",opening_hash_use,Opening_Hash_Size,out,i);
		if ( ret==0 ) return;
	}
	PRT("hash=%d/%d,out=%d/%d,sec=%.1f\n",opening_hash_use,Opening_Hash_Size,out,i,get_spend_time(st));
	for (i=0;i<30;i++) PRT("%d,",n_vCount[i]);
	PRT("\n");
	count_OpeningHash();
//	print_board(ptree);
	DEBUG_PRT("make_balanced_opening fail. average_winrate=%f\n",average_winrate);
}

