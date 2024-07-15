#include "router.hh"

#include <iostream>
#include <limits>

using namespace std;

// route_prefix: The "up-to-32-bit" IPv4 address prefix to match the datagram's destination address against
// prefix_length: For this route to be applicable, how many high-order (most-significant) bits of
//    the route_prefix will need to match the corresponding bits of the datagram's destination address?
// next_hop: The IP address of the next hop. Will be empty if the network is directly attached to the router (in
//    which case, the next hop address should be the datagram's final destination).
// interface_num: The index of the interface to send the datagram out on.
void Router::add_route( const uint32_t route_prefix,
                        const uint8_t prefix_length,
                        const optional<Address> next_hop,
                        const size_t interface_num )
{
  cerr << "DEBUG: adding route " << Address::from_ipv4_numeric( route_prefix ).ip() << "/"
       << static_cast<int>( prefix_length ) << " => " << ( next_hop.has_value() ? next_hop->ip() : "(direct)" )
       << " on interface " << interface_num << "\n";

  // Your code here.
  for ( auto it = rtable_.begin(); it != rtable_.end(); ++it ) {
    if ( get<0>( *it ) == route_prefix && get<1>( *it ) == prefix_length ) {
      get<2>( *it ) = next_hop;
      get<3>( *it ) = interface_num;
      debug_print( "add an existing route. route_prefix: " << route_prefix << " prefix_length: " << prefix_length );
      return;
    }
  }

  rtable_.emplace_back( route_prefix, prefix_length, next_hop, interface_num );
}

bool Router::if_match( uint32_t ip, uint32_t route_prefix, uint8_t prefix_length )
{
  if ( prefix_length == 0 ) {
    debug_print( "prefix length is 0!" );
    return true;
  }

  uint32_t mask = 0xffffffff;
  mask <<= ( 32 - prefix_length );
  return ( ( ip & mask ) == ( route_prefix & mask ) );
}

// Go through all the interfaces, and route every incoming datagram to its proper outgoing interface.
void Router::route()
{
  // Your code here.
  for ( auto& interface_ptr : _interfaces ) {
    auto& dgrams = interface_ptr->datagrams_received();

    while ( !dgrams.empty() ) {
      auto& dgram = dgrams.front();
      // send dgram
      if ( dgram.header.ttl <= 1 ) {
        // drop
        dgrams.pop();
        continue;
      }

      dgram.header.ttl -= 1;
      auto dst_ip = dgram.header.dst;
      int match_length = -1;
      size_t interface_num = -1;
      uint32_t next_hop = 0;
      for ( auto const& entry : rtable_ ) {
        if ( if_match( dst_ip, get<0>( entry ), get<1>( entry ) ) && get<1>( entry ) > match_length ) {
          match_length = get<1>( entry );
          next_hop = get<2>( entry ).has_value()? get<2>(entry).value().ipv4_numeric() : dst_ip;
          interface_num = get<3>( entry );
        }
      }
      if ( match_length != -1 ) {
        interface(interface_num)->send_datagram( dgram, Address::from_ipv4_numeric( next_hop ) );
      }
      dgrams.pop();
    }
  }
}
