// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef URL_URL_CANON_HH_
#define URL_URL_CANON_HH_

#include <stdlib.h>
#include <string.h>

#include "googleurl/url_parse.h"

namespace url {

// Canonicalizer output -------------------------------------------------------

// Base class for the canonicalizer output, this maintains a buffer and
// supports simple resizing and append operations on it.
//
// It is VERY IMPORTANT that no virtual function calls be made on the common
// code path. We only have two virtual function calls, the destructor and a
// resize function that is called when the existing buffer is not big enough.
// The derived class is then in charge of setting up our buffer which we will
// manage.
template<typename T>
class CanonOutputT {
 public:
  CanonOutputT() : buffer_(NULL), buffer_len_(0), cur_len_(0) {
  }
  virtual ~CanonOutputT() {
  }

  // Implemented to resize the buffer. This function should update the buffer
  // pointer to point to the new buffer, and any old data up to |cur_len_| in
  // the buffer must be copied over.
  //
  // The new size |sz| must be larger than buffer_len_.
  virtual void Resize(int sz) = 0;

  // Accessor for returning a character at a given position. The input offset
  // must be in the valid range.
  inline char at(int offset) const {
    return buffer_[offset];
  }

  // Sets the character at the given position. The given position MUST be less
  // than the length().
  inline void set(int offset, int ch) {
    buffer_[offset] = ch;
  }

  // Returns the number of characters currently in the buffer.
  inline int length() const {
    return cur_len_;
  }

  // Returns the current capacity of the buffer. The length() is the number of
  // characters that have been declared to be written, but the capacity() is
  // the number that can be written without reallocation. If the caller must
  // write many characters at once, it can make sure there is enough capacity,
  // write the data, then use set_size() to declare the new length().
  int capacity() const {
    return buffer_len_;
  }

  // Called by the user of this class to get the output. The output will NOT
  // be NULL-terminated. Call length() to get the
  // length.
  const T* data() const {
    return buffer_;
  }
  T* data() {
    return buffer_;
  }

  // Shortens the URL to the new length. Used for "backing up" when processing
  // relative paths. This can also be used if an external function writes a lot
  // of data to the buffer (when using the "Raw" version below) beyond the end,
  // to declare the new length.
  //
  // This MUST NOT be used to expand the size of the buffer beyond capacity().
  void set_length(int new_len) {
    cur_len_ = new_len;
  }

  // This is the most performance critical function, since it is called for
  // every character.
  void push_back(T ch) {
    // In VC2005, putting this common case first speeds up execution
    // dramatically because this branch is predicted as taken.
    if (cur_len_ < buffer_len_) {
      buffer_[cur_len_] = ch;
      cur_len_++;
      return;
    }

    // Grow the buffer to hold at least one more item. Hopefully we won't have
    // to do this very often.
    if (!Grow(1))
      return;

    // Actually do the insertion.
    buffer_[cur_len_] = ch;
    cur_len_++;
  }

  // Appends the given string to the output.
  void Append(const T* str, int str_len) {
    if (cur_len_ + str_len > buffer_len_) {
      if (!Grow(cur_len_ + str_len - buffer_len_))
        return;
    }
    for (int i = 0; i < str_len; i++)
      buffer_[cur_len_ + i] = str[i];
    cur_len_ += str_len;
  }

 protected:
  // Grows the given buffer so that it can fit at least |min_additional|
  // characters. Returns true if the buffer could be resized, false on OOM.
  bool Grow(int min_additional) {
    static const int kMinBufferLen = 16;
    int new_len = (buffer_len_ == 0) ? kMinBufferLen : buffer_len_;
    do {
      if (new_len >= (1 << 30))  // Prevent overflow below.
        return false;
      new_len *= 2;
    } while (new_len < buffer_len_ + min_additional);
    Resize(new_len);
    return true;
  }

  T* buffer_;
  int buffer_len_;

  // Used characters in the buffer.
  int cur_len_;
};

// Simple implementation of the CanonOutput using new[]. This class
// also supports a static buffer so if it is allocated on the stack, most
// URLs can be canonicalized with no heap allocations.
template<typename T, int fixed_capacity = 1024>
class RawCanonOutputT : public CanonOutputT<T> {
 public:
  RawCanonOutputT() : CanonOutputT<T>() {
    this->buffer_ = fixed_buffer_;
    this->buffer_len_ = fixed_capacity;
  }
  virtual ~RawCanonOutputT() {
    if (this->buffer_ != fixed_buffer_)
      delete[] this->buffer_;
  }

  virtual void Resize(int sz) {
    T* new_buf = new T[sz];
    memcpy(new_buf, this->buffer_,
           sizeof(T) * (this->cur_len_ < sz ? this->cur_len_ : sz));
    if (this->buffer_ != fixed_buffer_)
      delete[] this->buffer_;
    this->buffer_ = new_buf;
    this->buffer_len_ = sz;
  }

 protected:
  T fixed_buffer_[fixed_capacity];
};

// Normally, all canonicalization output is in narrow characters. We support
// the templates so it can also be used internally if a wide buffer is
// required.
typedef CanonOutputT<char> CanonOutput;

template<int fixed_capacity>
class RawCanonOutput : public RawCanonOutputT<char, fixed_capacity> {};

}  // namespace url

#endif  // URL_URL_CANON_HH_
