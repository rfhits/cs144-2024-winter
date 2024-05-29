#include "tcp_receiver.hh"

using namespace std;

void TCPReceiver::receive( TCPSenderMessage message )
{
  // Your code here.
  //   (void)message;

  if ( message.RST ) {
    
  }

  if ( message.SYN ) {
    this->isn_ = message.seqno;
    this->max_abs_seqno = 0;
    if ( !message.payload.empty() ) {
      cerr << "SYN contains data!" << std::endl;
    }
    return;
  }

  max_abs_seqno = message.seqno.unwrap( isn_, max_abs_seqno );
  reassembler_.insert( max_abs_seqno - 1, message.payload, message.FIN );
}

TCPReceiverMessage TCPReceiver::send() const
{
  // Your code here.
  return {};
}
