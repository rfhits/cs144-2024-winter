#include "tcp_sender.hh"
#include "tcp_config.hh"

using namespace std;

uint64_t TCPSender::sequence_numbers_in_flight() const
{
  // Your code here.
  return {};
}

uint64_t TCPSender::consecutive_retransmissions() const
{
  // Your code here.
  return {};
}

void TCPSender::push( const TransmitFunction& transmit )
{
  // Your code here.
  if ( has_fin_ ) {
    cerr << "I don't know how to deal with that" << endl;
    return;
  }

  auto remain_wnd_size = wnd_size_ == 0 ? 1 : wnd_size_;

  auto data = input_.reader().peek();

  bool has_SYN_sent = false;
  uint64_t send_index = abs_last_ackno_ == 0 ? 0 : abs_last_ackno_ - 1; // should send from input_[index]
  // decltype( data.size() ) remain_data_size = ( abs_last_ackno_ == 0 ) + data.size() - send_index + !has_fin_;

  while ( remain_data_size > 0 && remain_wnd_size > 0 ) {
    TCPSenderMessage cur_msg;

    if ( abs_last_ackno_ == 0 && !has_SYN_sent ) {
      cur_msg.SYN = true;
      cur_msg.seqno = isn_;
      remain_data_size -= 1;
      remain_wnd_size -= 1;
      has_SYN_sent = true;
    } else {
      cur_msg.seqno = Wrap32::wrap( abs_last_ackno_ + send_index, isn_ );
    }

    // put into payload

    // u can't put payload and FIN into message
    if ( remain_wnd_size == 0 ) {
      // pass
    }
    // we have window size
    else {
      // empty data, no need for payload
      if ( remain_data_size == 1 ) {
        cur_msg.FIN = true;
        remain_data_size -= 1;
        remain_wnd_size -= 1;
      }
      // have data to send
      else {
        // can we reach FIN so that FIN took a seqno?
        bool set_FIN
          = ( ( remain_data_size - 1 ) <= TCPConfig::MAX_PAYLOAD_SIZE ) && ( remain_data_size <= remain_wnd_size );
        uint64_t payload_len
          = std::min( { remain_data_size - 1, TCPConfig::MAX_PAYLOAD_SIZE, remain_wnd_size - set_FIN } );

        cur_msg.payload = data.substr( send_index, payload_len );
        remain_data_size -= ( payload_len + set_FIN );
        send_index += payload_len;
        remain_wnd_size -= ( payload_len + set_FIN );
        cur_msg.FIN = set_FIN;
      }
    }

    abs_exp_ackno_ += cur_msg.sequence_length();
    transmit( cur_msg );
    if ( !timer_.is_running_ ) {
      timer_.reset( cur_RTO_ms_, abs_exp_ackno_, cur_msg );
    }
    cur_msg.reset();
  }
}

TCPSenderMessage TCPSender::make_empty_message() const
{
  // Your code here.
  return {};
}

void TCPSender::receive( const TCPReceiverMessage& msg )
{
  // Your code here.
  (void)msg;
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
      transmit( gen_msg( ost_segs_.begin()->first, ost_segs_.begin()->second ) );
    }

    if ( !is_con_retx_ ) {
      retx_cnt_ = 1;
    } else {
      retx_cnt_ += 1;
    }
    cur_RTO_ms_ *= 2;
  }

  timer_.restart( cur_RTO_ms_ );
}
