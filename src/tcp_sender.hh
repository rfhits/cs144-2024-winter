#pragma once

#include "byte_stream.hh"
#include "tcp_receiver_message.hh"
#include "tcp_sender_message.hh"

#include <cstdint>
#include <functional>
#include <list>
#include <memory>
#include <optional>
#include <queue>

class TCPSender
{
public:
  /* Construct TCP sender with given default Retransmission Timeout and possible ISN */
  TCPSender( ByteStream&& input, Wrap32 isn, uint64_t initial_RTO_ms )
    : input_( std::move( input ) ), isn_( isn ), initial_RTO_ms_( initial_RTO_ms )
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

  class Timer
  {
  public:
    Timer( uint64_t exp_time ) : exp_time_( exp_time ), is_running_( true ) {}

    uint64_t grow( uint64_t time )
    {
      if ( !is_running_ ) {
        cerr << "grow a unrunned timer!" << endl;
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

    void reset(uint64_t exp_time) {
        is_running_ = true;
        is_expired_ = false;
        exp_time_ = exp_time;
        cur_time_ = 0;
    }

  private:
    bool is_running_ { false };
    bool is_expired_ { false };
    uint64_t exp_time_ { 0 }; // expire time, should be init in constructor, in ms
    uint64_t cur_time_ { 0 };
  };

private:
  // Variables initialized in constructor
  ByteStream input_;
  Wrap32 isn_;
  uint64_t initial_RTO_ms_;
};