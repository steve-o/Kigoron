// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Canonicalizers for random bits that aren't big enough for their own files.

#include <string.h>

#include "url/url_canon.hh"
#include "url/url_canon_internal.hh"

namespace url {

namespace {

// Returns true if the given character should be removed from the middle of a
// URL.
inline bool IsRemovableURLWhitespace(int ch) {
  return ch == '\r' || ch == '\n' || ch == '\t';
}

// Backend for RemoveURLWhitespace (see declaration in url_canon.h).
// It sucks that we have to do this, since this takes about 13% of the total URL
// canonicalization time.
template<typename CHAR>
const CHAR* DoRemoveURLWhitespace(const CHAR* input, int input_len,
                                  CanonOutputT<CHAR>* buffer,
                                  int* output_len) {
  // Fast verification that there's nothing that needs removal. This is the 99%
  // case, so we want it to be fast and don't care about impacting the speed
  // when we do find whitespace.
  int found_whitespace = false;
  for (int i = 0; i < input_len; i++) {
    if (!IsRemovableURLWhitespace(input[i]))
      continue;
    found_whitespace = true;
    break;
  }

  if (!found_whitespace) {
    // Didn't find any whitespace, we don't need to do anything. We can just
    // return the input as the output.
    *output_len = input_len;
    return input;
  }

  // Remove the whitespace into the new buffer and return it.
  for (int i = 0; i < input_len; i++) {
    if (!IsRemovableURLWhitespace(input[i]))
      buffer->push_back(input[i]);
  }
  *output_len = buffer->length();
  return buffer->data();
}

// Contains the canonical version of each possible input letter in the scheme
// (basically, lower-cased). The corresponding entry will be 0 if the letter
// is not allowed in a scheme.
const char kSchemeCanonical[0x80] = {
// 00-1f: all are invalid
     0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
     0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
//  ' '   !    "    #    $    %    &    '    (    )    *    +    ,    -    .    /
     0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,  '+',  0,  '-', '.',  0,
//   0    1    2    3    4    5    6    7    8    9    :    ;    <    =    >    ?
    '0', '1', '2', '3', '4', '5', '6', '7', '8', '9',  0 ,  0 ,  0 ,  0 ,  0 ,  0 ,
//   @    A    B    C    D    E    F    G    H    I    J    K    L    M    N    O
     0 , 'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 'i', 'j', 'k', 'l', 'm', 'n', 'o',
//   P    Q    R    S    T    U    V    W    X    Y    Z    [    \    ]    ^    _
    'p', 'q', 'r', 's', 't', 'u', 'v', 'w', 'x', 'y', 'z',  0,   0 ,  0,   0 ,  0,
//   `    a    b    c    d    e    f    g    h    i    j    k    l    m    n    o
     0 , 'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 'i', 'j', 'k', 'l', 'm', 'n', 'o',
//   p    q    r    s    t    u    v    w    x    y    z    {    |    }    ~
    'p', 'q', 'r', 's', 't', 'u', 'v', 'w', 'x', 'y', 'z',  0 ,  0 ,  0 ,  0 ,  0 };

// This could be a table lookup as well by setting the high bit for each
// valid character, but it's only called once per URL, and it makes the lookup
// table easier to read not having extra stuff in it.
inline bool IsSchemeFirstChar(unsigned char c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

// Helper functions for converting port integers to strings.
inline void WritePortInt(char* output, int output_len, int port) {
  _itoa_s(port, output, output_len, 10);
}

}  // namespace

const char* RemoveURLWhitespace(const char* input, int input_len,
                                CanonOutputT<char>* buffer,
                                int* output_len) {
  return DoRemoveURLWhitespace(input, input_len, buffer, output_len);
}

char CanonicalSchemeChar(char ch) {
  if (ch >= 0x80)
    return 0;  // Non-ASCII is not supported by schemes.
  return kSchemeCanonical[ch];
}

}  // namespace url
