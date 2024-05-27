#include "reassembler.hh"

using namespace std;

void Reassembler::insert( uint64_t first_index, string data, bool is_last_substring )
{
  // Your code here.
  if ( is_last_substring ) {
    last_ = first_index + data.size();
    has_last_ = true;
  }

  uint64_t avail_cap_ = output_.writer().available_capacity();

  if ( first_index < next_ ) {
    // buf:       |-------|
    // data: |--|
    if ( first_index + data.size() <= next_ ) { // already written to ByteStream, discard
      return;
    }
    // cover buffer but too big, leave we need
    // buf :      |--------|
    // data:   |--------------|
    else if ( data.size() + first_index >= next_ + avail_cap_ ) {
      output_.writer().push( string( data.data() + next_ - first_index, avail_cap_ ) );
      next_ += avail_cap_;
      segs_.clear();
    }
    // buf:     |-----------|
    // data: |--------|
    else {
      output_.writer().push( string( data.data() + next_ - first_index, data.size() - ( next_ - first_index ) ) );
      next_ = first_index + data.size();
    }
  } else if ( first_index == next_ ) {
    if ( data.size() <= avail_cap_ ) {
      output_.writer().push( data );
      next_ += data.size();
    } else {
      output_.writer().push( data );
      next_ += avail_cap_;
      segs_.clear();
    }
  }
  // buf: |-------|
  // seg:     |
  else if ( first_index < next_ + avail_cap_ ) {
    // buf:    |-----------|
    // seg:        |------------|
    if ( next_ + avail_cap_ < first_index + data.size() ) {
      segs_.emplace( std::move( data ), first_index, next_ + avail_cap_, 0 );
    } else {
      segs_.emplace( first_index, std::move( data ) );
    }
  } else {
    return;
  }

  for ( auto it = segs_.begin(); it != segs_.end(); ) {
    // next:  |
    // seg:        |--------|
    if ( next_ < it->begin_ ) {
      break;
    }

    // next:        |
    // seg:  |---|
    if ( next_ >= it->end_ ) {
      it = segs_.erase( it );
      continue;
    }

    // next:  |
    // seg: |----|
    if ( next_ >= it->begin_ ) { // we can promise next_ < seg.end_
      if ( next_ == it->begin_ && it->index_ == 0 ) {
        output_.writer().push( it->data_ );
      } else {
        output_.writer().push( string( it->data_.data() + ( next_ - it->begin_ ) + it->index_, it->end_ - next_ ) );
      }

      next_ = it->end_;
      it = segs_.erase( it );
    }
  }

  if ( has_last_ && next_ == last_ ) {
    output_.writer().close();
  }
}

uint64_t Reassembler::bytes_pending() const
{
  // Your code here.
  if ( segs_.empty() ) {
    return 0;
  }

  set<seg>::iterator it = segs_.begin();
  uint64_t res = it->end_ - it->begin_;
  uint64_t end = it->end_;
  ++it;

  while ( it != segs_.end() ) {
    if ( it->end_ <= end ) {
      ++it;
      continue;
    } else {
      // end:   |
      // seg: |----|
      if ( it->begin_ <= end ) {
        res += ( it->end_ - end );
      } else {
        // end:   |
        // seg:      |----|
        res += ( it->end_ - it->begin_ );
      }
      end = it->end_;
      ++it;
    }
  }
  return res;
}
