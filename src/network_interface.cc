#include <iostream>

#include "arp_message.hh"
#include "exception.hh"
#include "network_interface.hh"

using namespace std;

//! \param[in] ethernet_address Ethernet (what ARP calls "hardware") address of the interface
//! \param[in] ip_address IP (what ARP calls "protocol") address of the interface
NetworkInterface::NetworkInterface( string_view name,
                                    shared_ptr<OutputPort> port,
                                    const EthernetAddress& ethernet_address,
                                    const Address& ip_address )
  : name_( name )
  , port_( notnull( "OutputPort", move( port ) ) )
  , ethernet_address_( ethernet_address )
  , ip_address_( ip_address )
{
  cerr << "DEBUG: Network interface has Ethernet address " << to_string( ethernet_address ) << " and IP address "
       << ip_address.ip() << "\n";
}

//! \param[in] dgram the IPv4 datagram to be sent
//! \param[in] next_hop the IP address of the interface to send it to (typically a router or default gateway, but
//! may also be another host if directly connected to the same network as the destination) Note: the Address type
//! can be converted to a uint32_t (raw 32-bit IP address) by using the Address::ipv4_numeric() method.
void NetworkInterface::send_datagram( const InternetDatagram& dgram, const Address& next_hop )
{
  // Your code here.
  auto dst_ip = next_hop.ipv4_numeric();
  debug_print( "send datagram to ip: " << dst_ip );

  // search next_hop in rtable_
  // if found, send
  // else queue and sent arp

  for ( auto const& entry : rtable_ ) {
    auto [ip_addr, eth_addr, live_time] = entry;
    if ( ip_addr == next_hop.ipv4_numeric() ) {
      port_->transmit( *this, pack_dgram( dgram, eth_addr ) );
      return;
    }
  }

  // if already sent arp, just wait
  for ( auto const& entry : pending_arp_reqs_ ) {
    auto const [ip_addr, time_pass] = entry;
    if ( ip_addr == next_hop.ipv4_numeric() ) {
      datagrams_queued_.push_back( make_pair( dgram, ip_addr ) );
      return;
    }
  }

  debug_print( "can't find ip in table and pending arps:" << dst_ip );
  EthernetFrame eth_frame = pack_arp( generate_arp_request( dst_ip ) );
  port_->transmit( *this, eth_frame );
  pending_arp_reqs_.push_back( make_pair( dst_ip, 0 ) );
  datagrams_queued_.push_back( make_pair( dgram, dst_ip ) );
}

//! \param[in] frame the incoming Ethernet frame
void NetworkInterface::recv_frame( const EthernetFrame& frame )
{
  // Your code here.
  (void)frame;
  EthernetAddress dst_eth_addr = frame.header.dst;
  if ( dst_eth_addr != ETHERNET_BROADCAST && dst_eth_addr != ethernet_address_ ) {
    debug_print( "receiver an address not self and broadcast" );
    return;
  }

  if ( frame.header.type == EthernetHeader::TYPE_IPv4 ) {
    debug_print( "receive ipv4 datagram" );
    Parser p( frame.payload );
    InternetDatagram dgram;
    dgram.parse( p );
    datagrams_received_.push( dgram );
    return;
  }

  if ( frame.header.type == EthernetHeader::TYPE_ARP ) {
    debug_print( "receive arp message" );
    // remember the mapping
    auto arp_msg = extract_arp_msg( frame );
    auto src_ip = arp_msg.sender_ip_address;
    auto src_eth_addr = arp_msg.sender_ethernet_address;
    rtable_.push_back( { src_ip, src_eth_addr, 0 } );

    flush_pending( src_ip, src_eth_addr );

    // if arp request, reply
    if ( arp_msg.opcode == ARPMessage::OPCODE_REQUEST && arp_msg.target_ip_address == ip_address_.ipv4_numeric() ) {
      debug_print( "reply for received arp" );
      ARPMessage const& arp_reply = generate_arp_reply( arp_msg );
      debug_print( arp_reply.to_string() );
      port_->transmit( *this, pack_arp( arp_reply ) );
    }
  }
}

//! \param[in] ms_since_last_tick the number of milliseconds since the last call to this method
void NetworkInterface::tick( const size_t ms_since_last_tick )
{
  // Your code here.
  for ( auto it = rtable_.begin(); it != rtable_.end(); ) {
    auto& entry = *it;
    get<2>( entry ) += ms_since_last_tick;
    if ( get<2>( entry ) >= rtable_entry_expire_time_ ) {
      it = rtable_.erase( it );
    } else {
      ++it;
    }
  }

  for ( auto it = pending_arp_reqs_.begin(); it != pending_arp_reqs_.end(); ) {
    it->second += ms_since_last_tick;
    if ( it->second > pending_arp_req_expire_time_ ) {
      it = pending_arp_reqs_.erase( it );
    } else {
      ++it;
    }
  }
}

ARPMessage NetworkInterface::generate_arp_request( uint32_t const query_ip ) const
{
  ARPMessage arp_msg;
  arp_msg.opcode = ARPMessage::OPCODE_REQUEST;
  arp_msg.sender_ethernet_address = ethernet_address_;
  arp_msg.sender_ip_address = ip_address_.ipv4_numeric();

  arp_msg.target_ethernet_address = ETHERNET_ZERO;
  arp_msg.target_ip_address = query_ip;

  return arp_msg;
}

ARPMessage NetworkInterface::generate_arp_reply( ARPMessage const& arp_req ) const
{
  ARPMessage arp_msg;
  arp_msg.opcode = ARPMessage::OPCODE_REPLY;
  arp_msg.sender_ip_address = ip_address_.ipv4_numeric();
  arp_msg.sender_ethernet_address = ethernet_address_;

  arp_msg.target_ip_address = arp_req.sender_ip_address;
  arp_msg.target_ethernet_address = arp_req.sender_ethernet_address;
  return arp_msg;
}

EthernetFrame NetworkInterface::pack_arp( ARPMessage const& arp_msg ) const
{
  debug_print( "pack arg message to eth frame" );
  debug_print( arp_msg.to_string() );

  EthernetFrame eth_frame;
  eth_frame.header.src = ethernet_address_;

  eth_frame.header.dst
    = ( arp_msg.target_ethernet_address == ETHERNET_ZERO ) ? ETHERNET_BROADCAST : arp_msg.target_ethernet_address;
  eth_frame.header.type = EthernetHeader::TYPE_ARP;

  Serializer serializer;
  arp_msg.serialize( serializer );
  eth_frame.payload = std::move( serializer.output() );

  return eth_frame;
}

ARPMessage NetworkInterface::extract_arp_msg( EthernetFrame const& eth_frame )
{
  debug_print( "extract arp msg from eth frame, extracted arp msg:" );

  ARPMessage arp_msg;
  Parser p( eth_frame.payload );
  arp_msg.parse( p );

  debug_print( arp_msg.to_string() );
  return arp_msg;
}

EthernetFrame NetworkInterface::pack_dgram( InternetDatagram const& dgram,
                                            EthernetAddress const& dst_eth_addr ) const
{
  EthernetFrame frame;
  frame.header.dst = dst_eth_addr;
  frame.header.src = ethernet_address_;
  frame.header.type = EthernetHeader::TYPE_IPv4;

  Serializer serializer;
  dgram.serialize( serializer );

  frame.payload = std::move( serializer.output() );
  return frame;
}

void NetworkInterface::flush_pending( uint32_t ip, EthernetAddress const& eth_addr )
{
  debug_print( "flush pending use ip:" << ip << " eth_addr: " << to_string( eth_addr ) );

  // auto match_ip = [ip]( pair<uint32_t, uint64_t> const& ip_time ) { return ( ip_time.first == ip ); };

  debug_print( "before flush pending_arps.size: " << pending_arp_reqs_.size() );
  pending_arp_reqs_.erase( remove_if( pending_arp_reqs_.begin(),
                                      pending_arp_reqs_.end(),
                                      [ip]( auto const& ip_time ) { return ( ip_time.first == ip ); } ),
                           pending_arp_reqs_.end() );

  debug_print( "after flush: " << pending_arp_reqs_.size() );

  for ( auto it = datagrams_queued_.begin(); it != datagrams_queued_.end(); ) {
    if ( it->second == ip ) {
      debug_print( "in flush find an ip match in queued datagram" );
      port_->transmit( *this, pack_dgram( it->first, eth_addr ) );
      it = datagrams_queued_.erase( it );
    } else {
      ++it;
    }
  }
}
