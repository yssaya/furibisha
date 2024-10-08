// 2019 Team AobaZero
// This source code is in the public domain.
#pragma once
#include "iobase.hpp"
#include "osi.hpp"
#include "param.hpp"
#include <atomic>
#include <mutex>
#include <thread>
#include <set>
#include <memory>
#include <cstdint>

class Job;
template <typename T> class JQueue;

class WghtFile {
  using uint = unsigned int;
  static std::set<FNameID> _all;
  static std::mutex _m;
  FNameID _fname;
  uint64_t _crc64;
  uint _keep_wght;
  
public:
  static void cleanup();
  explicit WghtFile(const FNameID &fxz, uint keep_wght) noexcept;
  ~WghtFile() noexcept;
  uint64_t get_crc64() const noexcept { return _crc64; }
  uint get_len_fname() const noexcept { return _fname.get_len_fname(); }
  const FNameID &get_fname() const noexcept { return _fname; }
  int64_t get_id() const noexcept { return _fname.get_id(); }
};

class Client {
  using uint = unsigned int;
  std::atomic<bool> _quit, _has_conn, _downloading;
  std::atomic<uint> _nsend, _ndiscard;
  std::atomic<int64_t> _wght_id;
  std::unique_ptr<JQueue<Job>> _pJQueue;
  std::unique_ptr<char []> _prec_xz, _saddr;
  std::thread _thread_reader, _thread_sender;
  std::mutex _m_wght;
  std::shared_ptr<const WghtFile> _wght;
  char _buf_wght_time[256];
  FName _dwght;
  float _th_resign;
  uint _handicap_rate[HANDICAP_TYPE];
  uint _average_winrate;
  uint _kld_playout;
  uint _kld_gain;
  uint _kld_interval;
  uint _mtemp;
  uint _rook_file_prob[ROOK_PROB_NUM];
  int _rook_handicap[ROOK_HANDICAP_NUM];
  uint _max_retry, _retry_count, _recvTO, _recv_bufsiz, _sendTO, _send_bufsiz;
  uint _read_bufsiz, _port, _keep_wght;
  
  Client() noexcept;
  ~Client() noexcept;
  Client(const Client &) = delete;
  Client & operator=(const Client &) = delete;

  void get_new_wght();
  float receive_header(const OSI::Conn &conn, uint TO, uint bufsiz);
  void sender() noexcept;
  void reader() noexcept;
  
public:
  static Client & get() noexcept;
  void add_rec(const char *p, size_t len) noexcept;
  void start(const char *dwght, const char *cstr_addr, uint port, uint recvTO,
	     uint recv_bufsiz, uint sendTO, uint send_bufsiz, uint max_retry,
	     uint size_queue, uint keep_wght) noexcept;
  void end() noexcept;
  uint64_t get_wght_id() noexcept;
  std::shared_ptr<const WghtFile> get_wght() noexcept;
  const char *get_buf_wght_time() { return _buf_wght_time; }
  bool is_downloading() { return _downloading; }
  uint get_nsend() const noexcept { return _nsend; };
  uint get_ndiscard() const noexcept { return _ndiscard; }
  float get_th_resign() const noexcept { return _th_resign; }
  const uint *get_handicap_rate() { return _handicap_rate; }
  uint get_average_winrate() { return _average_winrate; }
  uint get_kld_playout()  { return _kld_playout; }
  uint get_kld_gain()     { return _kld_gain; }
  uint get_kld_interval() { return _kld_interval; }
  uint get_mtemp()        { return _mtemp; }
  const uint *get_rook_file_prob() { return _rook_file_prob; }
  const  int *get_rook_handicap()  { return _rook_handicap; }
  bool has_conn() const noexcept { return _has_conn; }
};
