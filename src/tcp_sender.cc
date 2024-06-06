#include "tcp_sender.hh"
#include "tcp_config.hh"

using namespace std;

uint64_t TCPSender::sequence_numbers_in_flight() const
{
  // Your code here.
  return abs_exp_ackno_ - abs_last_ackno_;
}

uint64_t TCPSender::consecutive_retransmissions() const
{
  // Your code here.
  return retx_cnt_;
}

void TCPSender::push( const TransmitFunction& transmit )
{
  // Your code here.
  if ( is_FIN_acked ) {
    return;
  }

  auto remain_wnd_size = wnd_size_ == 0 ? 1 : wnd_size_;

  bool has_SYN_sent = false;

  if ( abs_last_ackno_ + remain_wnd_size <= abs_exp_ackno_ ) {
    return;
  }

  auto abs_cur_seqno = abs_exp_ackno_; // send from this seqno, change in while loop
  remain_wnd_size = abs_last_ackno_ + remain_wnd_size - abs_exp_ackno_;

  if ( remain_wnd_size == 0 ) {
    return;
  }

  bool is_input_finished = input_.writer().is_closed();
  uint64_t remain_data_size = ( !has_SYN_sent & ( abs_cur_seqno == 0 ) ) + input_.reader().bytes_buffered()
                              + ( is_input_finished & !has_FIN_sent_ );

  while ( remain_data_size > 0 && remain_wnd_size > 0 ) {
    TCPSenderMessage cur_msg;
    cur_msg.seqno = Wrap32::wrap( abs_cur_seqno, isn_ );

    if ( abs_cur_seqno == 0 && !has_SYN_sent ) {
      cur_msg.SYN = true;
      cur_msg.seqno = isn_;
      remain_data_size -= 1;
      remain_wnd_size -= 1;
      has_SYN_sent = true;
    }

    // put into payload
    // u can't put payload and FIN into message
    if ( remain_wnd_size == 0 ) {
      // pass
    }
    // we have window size
    else {
      // extract min(Max_Payload_Size, wnd_size) from bs
      auto bytes_buffered = input_.reader().bytes_buffered();

      read( input_.reader(),
            std::min( { bytes_buffered, remain_wnd_size, TCPConfig::MAX_PAYLOAD_SIZE } ),
            cur_msg.payload );
      // std::cout << "remain window size:" << remain_wnd_size << std::endl;
      // std::cout << "payload:" << cur_msg.payload << std::endl;
      remain_wnd_size -= cur_msg.payload.size();
      remain_data_size -= cur_msg.payload.size();

      if ( remain_wnd_size > 0 && input_.reader().is_finished() ) {
        remain_wnd_size -= 1;
        cur_msg.FIN = true;
        remain_data_size = 0;
        has_FIN_sent_ = true;
      }
    }

    cur_msg.RST = input_.has_error();

    transmit( cur_msg );
    ost_segs_.insert( { abs_cur_seqno, cur_msg } );
    abs_exp_ackno_ += cur_msg.sequence_length();
    abs_cur_seqno += cur_msg.sequence_length();
    if ( !timer_.is_running_ ) {
      timer_.reset( cur_RTO_ms_ );
    }
    cur_msg.reset();
  }
}

TCPSenderMessage TCPSender::make_empty_message() const
{
  TCPSenderMessage msg;
  msg.seqno = Wrap32::wrap( abs_exp_ackno_, isn_ );
  msg.RST = input_.has_error();
  return msg;
}

void TCPSender::receive( const TCPReceiverMessage& msg )
{
  // Your code here.
  if ( msg.RST ) {
    input_.set_error();
    return;
  }

  uint64_t abs_rcv_ackno = 0;

  if ( !msg.ackno.has_value() ) {
    abs_last_ackno_ = abs_exp_ackno_ = 0;
    abs_rcv_ackno = 0;
    wnd_size_ = msg.window_size;
    ost_segs_.clear();
  } else {
    abs_rcv_ackno = msg.ackno.value().unwrap( isn_, abs_last_ackno_ );
  }

  // ignore impossible ack
  if ( abs_rcv_ackno > abs_exp_ackno_ ) {
    return;
  }

  // rcv_ackno + ack_wnd = last_ackno + wnd
  // wnd = rcv_ackno + ack_wnd - last_ackno
  is_FIN_acked = has_FIN_sent_ & ( abs_rcv_ackno == abs_exp_ackno_ );
  bool has_new_data_acked = abs_rcv_ackno > abs_last_ackno_;
  //
  if ( abs_rcv_ackno <= abs_last_ackno_ ) {
    wnd_size_ = ( abs_rcv_ackno + msg.window_size > abs_last_ackno_ )
                  ? abs_rcv_ackno + msg.window_size - abs_last_ackno_
                  : 0;
  } else // if (abs_rcv_ackno > abs_last_ackno_) // new segment get acked
  {
    cur_RTO_ms_ = initial_RTO_ms_;
    retx_cnt_ = 0;
    is_con_retx_ = false;
    abs_last_ackno_ = abs_rcv_ackno;
    wnd_size_ = msg.window_size;
    auto it = ost_segs_.begin();
    while ( it != ost_segs_.end() ) {
      if ( it->first + it->second.sequence_length() <= abs_rcv_ackno ) {
        it = ost_segs_.erase( it );
      } else {
        break;
      }
    }
  }

  if ( ost_segs_.empty() ) {
    timer_.turnoff();
  } else {
    if ( has_new_data_acked ) {
      timer_.reset( cur_RTO_ms_ );
    }
  }
}

void TCPSender::tick( uint64_t ms_since_last_tick, const TransmitFunction& transmit )
{
  // Your code here.
  if ( !timer_.is_running_ ) {
    return;
  }

  timer_.grow( ms_since_last_tick );
  if ( !timer_.is_expired() ) {
    return;
  }

  // now timer is expired, let's check

  // expire with nothing in flight should not happen, because timer_ is already turnoff in receive()
  if ( ost_segs_.empty() ) {
    cerr << "timer_ is already off in tick() if receive() get all segments acknowledged" << endl;
    if ( ( abs_last_ackno_ != abs_exp_ackno_ ) ) {
      cerr << "error: abs_last_ackno_ != abs_exp_ackno_" << endl;
      std::cout << "abs_last_ackno_:" << abs_last_ackno_ << std::endl;
      std::cout << "abs_exp_ackno_:" << abs_exp_ackno_ << std::endl;
    }
    timer_.turnoff();
    return;
  }

  transmit( ost_segs_.begin()->second );

  if ( wnd_size_ != 0 ) {
    if ( !is_con_retx_ ) {
      retx_cnt_ = 1;
      is_con_retx_ = true;
    } else {
      retx_cnt_ += 1;
    }
    cur_RTO_ms_ *= 2;
  }
  timer_.reset( cur_RTO_ms_ );
}
