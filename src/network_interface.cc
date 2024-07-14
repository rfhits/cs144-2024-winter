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
  debug_print("send datagram to ip: " << dst_ip);

  // search next_hop in rtable
  // if found, send
  // else queue and sent arp

  for ( auto& entry : rtable ) {
    auto [ip_addr, eth_addr, live_time] = entry;
    if ( ip_addr == next_hop.ipv4_numeric() ) {
      EthernetFrame eth_frame;

      eth_frame.header.dst = eth_addr;
      eth_frame.header.src = ethernet_address_;
      eth_frame.header.type = EthernetHeader::TYPE_IPv4;

      Serializer serializer;
      dgram.serialize( serializer );

      eth_frame.payload = std::move( serializer.output() );

      port_->transmit( *this, eth_frame );
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

  debug_print( "can't find ip in table and pending arps:" << dst_ip);
  EthernetFrame eth_frame = pack_arp( generate_request_arp( dst_ip ) );
  port_->transmit( *this, eth_frame );
  pending_arp_reqs_.push_back( make_pair( dst_ip, 0 ) );
  datagrams_queued_.push_back( make_pair( dgram, dst_ip ) );
}

//! \param[in] frame the incoming Ethernet frame
void NetworkInterface::recv_frame( const EthernetFrame& frame )
{
  // Your code here.
  (void)frame;
}

//! \param[in] ms_since_last_tick the number of milliseconds since the last call to this method
void NetworkInterface::tick( const size_t ms_since_last_tick )
{
  // Your code here.
  (void)ms_since_last_tick;
}

ARPMessage NetworkInterface::generate_request_arp( uint32_t const query_ip ) const
{
  ARPMessage arp_msg;
  arp_msg.opcode = ARPMessage::OPCODE_REQUEST;
  arp_msg.sender_ethernet_address = ethernet_address_;
  arp_msg.sender_ip_address = ip_address_.ipv4_numeric();

  arp_msg.target_ethernet_address = ETHERNET_BROADCAST;
  arp_msg.target_ip_address = query_ip;

  return arp_msg;
}

EthernetFrame NetworkInterface::pack_arp( ARPMessage const& arp_msg ) const
{
  debug_print( "pack arg message to eth frame" );
  debug_print( arp_msg.to_string() );

  EthernetFrame eth_frame;
  eth_frame.header.src = ethernet_address_;
  eth_frame.header.dst = arp_msg.target_ethernet_address;
  eth_frame.header.type = EthernetHeader::TYPE_ARP;

  Serializer serializer;
  arp_msg.serialize( serializer );
  eth_frame.payload = serializer.output();

  return eth_frame;
}
