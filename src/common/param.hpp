// 2019 Team AobaZero
// This source code is in the public domain.
#pragma once
namespace Ver {
  constexpr unsigned char major       = 4;	// 2...komaochi, 3...Swish, 4...furibisha
  constexpr unsigned char minor       = 6;	//
  // usi_engine does not use these. MUST increase "minor" for kicking old engine by server. Only major and minor are sent to client.
  constexpr unsigned short usi_engine = 106;	// 1...18 AobaZero, 16...26 komaochi, 27...Swish AobaZero, 100... furibisha
}

#define AOBA_UNIQUE ".e8Du6fhHJh"

namespace Param {
  using uint = unsigned int;
  constexpr uint maxnum_child         = 1024U;
  constexpr uint maxlen_play_learn    = 513U;
  constexpr uint maxlen_play          = 4096U;
  // 81*81*2 + (81*7) = 13122 + 567 = 13689 * 512 = 7008768
  constexpr uint len_seq_prn          = 7008768U;
//constexpr char name_autousi[]       = "/tmp/autousi"   AOBA_UNIQUE;
  constexpr char name_server[]        = "/tmp/server"    AOBA_UNIQUE;
  constexpr char name_sem_nnet[]      = "/sem-nnet"      AOBA_UNIQUE;
  constexpr char name_sem_lock_nnet[] = "/sem-lock-nnet" AOBA_UNIQUE;
  constexpr char name_seq_prn[]       = "/mmap-seq-prn"  AOBA_UNIQUE;
  constexpr char name_mmap_nnet[]     = "/mmap-nnet"     AOBA_UNIQUE;
}

const int HEADER_SIZE = 512;// version 2 byte(major,minor), resign_th(2 byte)           4
							// handicap rate 14 byte (2*7), average winrate(2 byte)    16
							// kld max playout(2 byte), gain(2 byte), interval(2 byte)  6
							// mtemp(2 byte)                                            2
							// rook file probability 9*9, any file 9*2, any 8,        214 byte(107*2) unsigned
							// rook handicap                                          162 byte( 81*2)   signed   all 404 byte

const int HANDICAP_TYPE = 7;// hirate(0),ky(1),ka(2),hi(3),2mai(4),4mai(5),6mai(6)
const int ROOK_PROB_NUM     = 107; // sente and gote rook(81), Specify multiple(8 ways,2-9) 18, distributions(share) 8, Total 81+18+8=107
const int ROOK_HANDICAP_NUM =  81; // sente and gote rook(81) 9x9
const int KLD_NUM = 4;

//const int FORMAT_VER = 2;	// AobaZero, AobaKomaochi
//const int FORMAT_VER = 3;	// AobaZero(Swish, NN input are same as AobaKomaochi)
const int FORMAT_VER = 4;	// AobaFuribisha(ReLU, NN input are same as AobaKomaochi, but TSTEP=1)

