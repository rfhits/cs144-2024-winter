#include "wrapping_integers.hh"

using namespace std;

Wrap32 Wrap32::wrap( uint64_t n, Wrap32 zero_point )
{
  return Wrap32 { zero_point + (uint32_t)( n & ( 0xffffffff ) ) };
}

uint64_t dis( uint64_t a, uint64_t b )
{
  return ( a >= b ) ? ( a - b ) : ( b - a );
}

uint64_t Wrap32::unwrap( Wrap32 zero_point, uint64_t checkpoint ) const
{
  // ignore upper overflow
  // https://blog.csdn.net/LostUnravel/article/details/124810142
  int32_t d = -static_cast<uint32_t>( checkpoint ) + ( this->raw_value_ - zero_point.raw_value_ );
  int64_t result = checkpoint + d;
  return result >= 0 ? result : result + ( 1UL << 32 );

  //   uint64_t diff = ( zero_point.raw_value_ <= this->raw_value_ )
  //                     ? this->raw_value_ - zero_point.raw_value_
  //                     : 0xffffffff - zero_point.raw_value_ + this->raw_value_ + 1;

  //   uint64_t mid = (checkpoint & 0xffffffff00000000) + diff;
  //   uint64_t r = (mid + 0x100000000);
  //   uint64_t l = mid - 0x100000000;
  //   uint64_t dis_mid = dis(mid, checkpoint);
  //   uint64_t dis_l = dis(l, checkpoint);
  //   uint64_t dis_r = dis(r, checkpoint);

  //   if (dis_mid <= dis_l && dis_mid <= dis_r) {
  //     return mid;
  //   } else if (dis_l <= dis_r) {
  //     return l;
  //   } else {
  //     return r;
  //   }
}
