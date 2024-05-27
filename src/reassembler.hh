#pragma once

#include "byte_stream.hh"
#include <list>
#include <set>
#include <string_view>
using std::list;
using std::string_view;

class Reassembler
{
public:
  // Construct Reassembler to write into given ByteStream.
  explicit Reassembler( ByteStream&& output ) : output_( std::move( output ) ) {}

  /*
   * Insert a new substring to be reassembled into a ByteStream.
   *   `first_index`: the index of the first byte of the substring
   *   `data`: the substring itself
   *   `is_last_substring`: this substring represents the end of the stream
   *   `output`: a mutable reference to the Writer
   *
   * The Reassembler's job is to reassemble the indexed substrings (possibly out-of-order
   * and possibly overlapping) back into the original ByteStream. As soon as the Reassembler
   * learns the next byte in the stream, it should write it to the output.
   *
   * If the Reassembler learns about bytes that fit within the stream's available capacity
   * but can't yet be written (because earlier bytes remain unknown), it should store them
   * internally until the gaps are filled in.
   *
   * The Reassembler should discard any bytes that lie beyond the stream's available capacity
   * (i.e., bytes that couldn't be written even if earlier gaps get filled in).
   *
   * The Reassembler should close the stream after writing the last byte.
   */
  void insert( uint64_t first_index, std::string data, bool is_last_substring );

  // How many bytes are stored in the Reassembler itself?
  uint64_t bytes_pending() const;

  // Access output stream reader
  Reader& reader() { return output_.reader(); }
  const Reader& reader() const { return output_.reader(); }

  // Access output stream writer, but const-only (can't write from outside)
  const Writer& writer() const { return output_.writer(); }

  class seg
  {
  public:
    // string view
    seg( uint64_t begin, string_view const& sv ) :begin_(begin){
        data_ = string(sv);
        end_ = begin_ + data_.size();
    }

    // whole s is data
    seg( uint64_t begin, string&& s ) : begin_( begin )
    {
      data_ = std::move( s );
      end_ = begin_ + s.size();
    }

    // part of s is data
    seg( string&& s, uint64_t begin, uint64_t end, uint64_t index ) : begin_( begin ), end_( end ), index_( index )
    {
      data_ = std::move( s );
    }
    uint64_t begin_ { 0 };
    uint64_t end_ { 0 };
    string data_ { "" };
    uint64_t index_ { 0 };                   // begin from data[index]
    bool operator<( const seg& other ) const // head first, size second
    {
      if ( begin_ < other.begin_ ) {
        return true;
      } else if ( begin_ > other.begin_ ) {
        return false;
      } else {
        return end_ > other.end_;
      }
    }
  };

private:
  ByteStream output_; // the Reassembler writes to this ByteStream
  std::set<seg> segs_ {};
  uint64_t next_ { 0 };
  uint64_t last_ { 0 };
  bool has_last_ { false };
};
