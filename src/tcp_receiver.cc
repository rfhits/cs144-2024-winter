#include "tcp_receiver.hh"

using namespace std;

void TCPReceiver::receive( TCPSenderMessage message )
{
  // Your code here.
  //   (void)message;

  if ( message.RST ) {
    // cerr << "RST comes and I haven't figure out how to handle it" << endl;
    has_rst_ = true;
    reassembler_.set_error();
    return;
  }

  if ( message.SYN ) {
    is_init_ = true;
    has_fin_ = message.FIN; // reset fin
    has_rst_ = message.RST;
    this->isn_ = message.seqno;
    this->max_abs_seqno = 0;
    reassembler_.insert( 0, message.payload, message.FIN );
    return;
  }
  if ( message.FIN ) {
    has_fin_ = true;
  }
  if ( message.RST ) {
    has_rst_ = true;
  }
  max_abs_seqno = message.seqno.unwrap( isn_, max_abs_seqno );
  reassembler_.insert( max_abs_seqno - 1, message.payload, message.FIN );
}

TCPReceiverMessage TCPReceiver::send() const
{
  // Your code here.

  TCPReceiverMessage msg;
  if ( is_init_ ) {
    // std::cout << "abs seq: " << this->reassembler_.next() + 1 + has_fin_ << std::endl;
    if ( this->reassembler_.bytes_pending() ) {
    //   std::cout << "bytes pending: " << reassembler_.bytes_pending() << std::endl;
      msg.ackno = Wrap32::wrap( this->reassembler_.next() + 1, isn_ );
    } else {
    //   std::cout << "no bytes pending: " << reassembler_.bytes_pending() << std::endl;
    //   std::cout << "next: " << reassembler_.next() << std::endl;
    //   std::cout << "has_fin_:" << has_fin_ << std::endl;
      msg.ackno = Wrap32::wrap( this->reassembler_.next() + 1 + has_fin_, isn_ );
    }
  }

  decltype( msg.window_size ) max_win_size = -1;
  msg.window_size = reassembler_.avail_cap() > max_win_size ? max_win_size : reassembler_.avail_cap();
  msg.RST = has_rst_ | reassembler_.has_error();
  return msg;
}
