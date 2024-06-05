#pragma once

#include "byte_stream.hh"
#include "tcp_receiver_message.hh"
#include "tcp_sender_message.hh"

#include <cassert>
#include <cstdint>
#include <functional>
#include <list>
#include <map>
#include <memory>
#include <optional>
#include <queue>

class TCPSender
{
public:
  /* Construct TCP sender with given default Retransmission Timeout and possible ISN */
  TCPSender( ByteStream&& input, Wrap32 isn, uint64_t initial_RTO_ms )
    : input_( std::move( input ) ), isn_( isn ), initial_RTO_ms_( initial_RTO_ms ), cur_RTO_ms_( initial_RTO_ms )
  {}

  /* Generate an empty TCPSenderMessage */
  TCPSenderMessage make_empty_message() const;

  /* Receive and process a TCPReceiverMessage from the peer's receiver */
  void receive( const TCPReceiverMessage& msg );

  /* Type of the `transmit` function that the push and tick methods can use to send messages */
  using TransmitFunction = std::function<void( const TCPSenderMessage& )>;

  /* Push bytes from the outbound stream */
  void push( const TransmitFunction& transmit );

  /* Time has passed by the given # of milliseconds since the last time the tick() method was called */
  void tick( uint64_t ms_since_last_tick, const TransmitFunction& transmit );

  // Accessors
  uint64_t sequence_numbers_in_flight() const;  // How many sequence numbers are outstanding?
  uint64_t consecutive_retransmissions() const; // How many consecutive *re*transmissions have happened?
  Writer& writer() { return input_.writer(); }
  const Writer& writer() const { return input_.writer(); }

  // Access input stream reader, but const-only (can't read from outside)
  const Reader& reader() const { return input_.reader(); }

  struct Timer
  {
    Timer() {}
    Timer( uint64_t exp_time, uint64_t abs_exp_ackno ) : exp_time_( exp_time ), is_running_( true ) {}

    uint64_t grow( uint64_t time )
    {
      if ( !is_running_ ) {
        cerr << "grow a un-run timer!" << endl;
        return;
      }
      if ( cur_time_ + time < cur_time_ ) { // check for overflow
        is_expired_ = true;
        return;
      }

      cur_time_ += time;
      is_expired_ = ( cur_time_ >= exp_time_ );
    };

    bool is_expired() { return is_expired; }

    void reset( uint64_t exp_time )
    {
      is_running_ = true;
      is_expired_ = false;
      exp_time_ = exp_time;
      cur_time_ = 0;
    }

    void restart( uint64_t exp_time )
    {
      exp_time_ = exp_time;
      cur_time_ = 0;
    }

    void turnoff() { is_running_ = false; }

    bool is_running_ { false };
    bool is_expired_ { false };
    // uint64_t abs_exp_ackno_ { 0 }; // after expire, hope ack has been acknowledged
    uint64_t exp_time_ { 0 }; // expire time, should be init in constructor, in ms
    uint64_t cur_time_ { 0 };
  };

private:
  // generate a message from [SYN, input_, FIN]
  TCPSenderMessage gen_msg( uint64_t seq_no, uint64_t len ) const
  {
    TCPSenderMessage msg;
    uint64_t payload_len = len;
    uint64_t payload_index = seq_no - 1;
    if ( seq_no == 0 ) {
      msg.SYN = true;
      payload_len -= 1;
      payload_index = 0;
    }
    auto data = input_.reader().peek();
    // no FIN
    if ( payload_len + payload_index <= data.size() ) {
      msg.payload = data.substr( payload_index, payload_len );
    } else if ( payload_len + payload_index == data.size() + 1 ) {
      msg.payload = data.substr( payload_index );
      msg.FIN = true;
    } else {
      std::cerr << "gen_msg() out of input_ boundary" << endl;
    }
    return msg;
  }

private:
  // Variables initialized in constructor
  ByteStream input_;
  Wrap32 isn_;
  uint64_t initial_RTO_ms_;
  uint64_t cur_RTO_ms_;

  // receiver: please send me the first byte, equals 0 if NOT ack SYN, promise aligned with ost_segs_
  uint64_t abs_last_ackno_ { 0 };

  uint64_t abs_exp_ackno_ { 0 }; // before this seqno are sent
  uint64_t is_FIN_acked { false };

  uint retx_cnt_ { 0 };        // retransmit count
  bool is_con_retx_ { false }; // consecutive retransmit

  Timer timer_;

  uint64_t wnd_size_ { 1 };
  std::map<uint64_t, TCPSenderMessage> ost_segs_; // outstanding segments, <seqno, msg>
};

// ByteStream with SYN, FIN: [ SYN, ByteStream, FIN ]
// SYN with absolute seqno 0
class WrappedByteStreamTool
{

public:
  // calculate how many bytes in a ByteStream haven't be sent
  // promise FIN haven't be sent
  static void count_pending_bytes( const ByteStream& bs, uint64_t& cnt, bool& has_fin )
  {
    has_fin = bs.writer().is_closed();
    cnt = has_fin + bs.reader().bytes_buffered();
    return;
    uint64_t abs_seqno = 0;

    if ( abs_seqno == 0 ) {
      cnt += 1;
      if ( bs.reader().bytes_popped() > 0 ) {
        cerr << "try to count total length of a popped ByteStream" << endl;
        // return;
      }
      cnt += bs.reader().bytes_buffered() + bs.reader().bytes_popped() + has_fin;
      return;
    }

    uint64_t send_index = abs_seqno - 1;
    if ( send_index < bs.reader().bytes_popped() ) {
      cerr << "try to count bytes pending in popped segment of ByteStream" << endl;
    }
    if ( send_index > bs.reader().bytes_buffered() + bs.reader().bytes_popped() ) {
      cerr << "error: abs_seqno out of FIN index!" << endl;
      return;
    } else if ( bs.reader().bytes_buffered() + bs.reader().bytes_popped() == send_index ) {
      if ( !has_fin ) {
        cerr << "error: abs_seqno point to a un-appeared byte" << endl;
        return;
      } else {
        cnt = 1;
        return;
      }
    } else {
      cnt = bs.reader().bytes_buffered() + bs.reader().bytes_popped() - send_index;
      cnt += has_fin;
      return;
    }
  }

  void view_bytes( const ByteStream& bs,
                   uint64_t abs_seqno,
                   uint64_t len,
                   string& s,
                   bool& contains_SYN,
                   bool& contains_FIN )
  {
    uint64_t send_idx;
    uint64_t str_len = len; // extract from bs.string
    if ( abs_seqno == 0 ) {
      contains_SYN = true;
      str_len -= 1;
      send_idx = 0;
    } else {
      contains_SYN = false;
      send_idx = abs_seqno - 1;
    }

    if ( bs.writer().is_closed() ) {
      if ( send_idx < bs.reader().bytes_popped() ) {
        cerr << "error: abs_seq in ByteStream's popped bytes" << endl;
        return;
      }
      uint64_t rel_send_idx = send_idx - bs.reader().bytes_popped();
      if ( rel_send_idx + str_len <= bs.reader().bytes_buffered() ) {
        s = bs.reader().peek();
      }
    } else {
    }
  }
};