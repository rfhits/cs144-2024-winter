#include "byte_stream.hh"

using namespace std;

ByteStream::ByteStream( uint64_t capacity ) : capacity_( capacity ), content_( capacity_ + 1, ' ' )
{
  if ( capacity_ + 1 == 0 ) {
    std::cerr << "to big capacity" << std::endl;
    error_ = true;
    return;
  }
}

bool Writer::is_closed() const
{
  // Your code here.
  return is_closed_;
}

void Writer::push( string data )
{
  // Your code here.
  if ( is_closed_ ) {
    return;
  }

  // check if need to move begin to head
  if ( end_ + data.size() > capacity_ ) { // direct copy
    // move begin to head
    content_.replace( 0, end_ - begin_, content_, begin_, end_ - begin_ );
    end_ = end_ - begin_;
    begin_ = 0;
  }

  if ( end_ + data.size() <= capacity_ ) { // copy all data
    content_.replace( end_, data.size(), data );
    pushed_cnt_ += data.size();
    end_ += data.size();
  } else {
    content_.replace( end_, capacity_ - end_, data, 0, capacity_ - end_ );
    pushed_cnt_ += ( capacity_ - end_ );
    end_ = capacity_;
  }
}

void Writer::close()
{
  // Your code here.
  is_closed_ = true;
}

// How many bytes can be pushed to the stream right now?
uint64_t Writer::available_capacity() const
{
  // Your code here.
  return capacity_ - ( end_ - begin_ );
}

uint64_t Writer::bytes_pushed() const
{
  // Your code here.
  return pushed_cnt_;
}

bool Reader::is_finished() const
{
  // Your code here.
  return is_closed_ && ( begin_ == end_ );
}

uint64_t Reader::bytes_popped() const
{
  // Your code here.
  return popped_cnt_;
}

string_view Reader::peek() const
{
  // Your code here.
  return string_view( content_.data() + begin_, end_ - begin_ );
}

// Remove `len` bytes from the buffer
void Reader::pop( uint64_t len )
{
  // Your code here.
  if ( begin_ + len <= end_ ) {
    begin_ += len;
    popped_cnt_ += len;
  } else {
    std::cerr << "pop len greater than size" << std::endl;
    begin_ = end_;
    popped_cnt_ += ( end_ - begin_ );
  }
}

uint64_t Reader::bytes_buffered() const
{
  // Your code here.
  return end_ - begin_;
}
