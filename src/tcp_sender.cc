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
    cerr << "I don't know how to deal with that" << endl;
    return;
  }

  // [last_ackno, last_ackno + wnd_size)
  // send outstanding segments overlap with the interval

  auto remain_wnd_size = wnd_size_ == 0 ? 1 : wnd_size_;

  bool has_SYN_sent = false;

  bool is_input_finished = input_.writer().is_closed();
  uint64_t remain_data_size = ( abs_last_ackno_ == 0 ) + input_.reader().bytes_buffered() + is_input_finished;

  uint64_t abs_cur_seqno = abs_last_ackno_; // send from this seqno, change in while loop

  while ( remain_data_size > 0 && remain_wnd_size > 0 ) {
    TCPSenderMessage cur_msg;
    cur_msg.seqno = Wrap32::wrap( abs_cur_seqno, isn_ );

    if ( abs_last_ackno_ == 0 && !has_SYN_sent ) {
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
      remain_wnd_size -= cur_msg.payload.size();
      remain_data_size -= cur_msg.payload.size();

      if ( remain_wnd_size > 0 && input_.reader().is_finished() && is_input_finished ) {
        remain_wnd_size -= 1;
        cur_msg.FIN = true;
        remain_data_size = 0;
        has_FIN_sent_ = true;
      }
    }

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
  return msg;
  ;
}

void TCPSender::receive( const TCPReceiverMessage& msg )
{
  // Your code here.
  (void)msg;
  if ( msg.RST ) {
    input_.set_error();
    return;
  }

  uint64_t abs_rcv_ackno = 0;

  if ( msg.ackno == nullopt ) {
    abs_last_ackno_ = 0;
    abs_rcv_ackno = 0;
    ost_segs_.clear();
  } else {
    abs_rcv_ackno = msg.ackno.value().unwrap( isn_, abs_last_ackno_ );
  }

  is_FIN_acked = has_FIN_sent_ & ( abs_rcv_ackno == abs_exp_ackno_ );

  auto it = ost_segs_.begin();
  while ( it != ost_segs_.end() ) {
    if ( it->first + it->second.sequence_length() <= abs_rcv_ackno ) {
      it = ost_segs_.erase( it );
    } else {
      abs_last_ackno_ = it->first;

      // right bound same: last_ackno + wnd_size == rcv_ackno + msg.window_size
      wnd_size_ = abs_rcv_ackno + msg.window_size - abs_last_ackno_;
      break;
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

  if ( wnd_size_ != 0 ) {
    // transmit(timer_.retx_msg_);
    if ( ost_segs_.size() ) {
      if ( ost_segs_.find( abs_last_ackno_ ) != ost_segs_.end() ) {
        transmit( ost_segs_.find( abs_last_ackno_ )->second );
      } else {
        cerr << "tick can't find lack ackno corresponding message" << endl;
      }
    }

    if ( !is_con_retx_ ) {
      retx_cnt_ = 1;
    } else {
      retx_cnt_ += 1;
    }
    cur_RTO_ms_ *= 2;
  }

  timer_.reset( cur_RTO_ms_ );
}
