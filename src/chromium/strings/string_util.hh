// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// This file defines utility functions for working with strings.

#ifndef CHROMIUM_STRINGS_STRING_UTIL_HH__
#define CHROMIUM_STRINGS_STRING_UTIL_HH__

#include <string>
#include <vector>

#include "string_piece.hh"  // For implicit conversions.

namespace chromium {

// Wrapper for vsnprintf that always null-terminates and always returns the
// number of characters that would be in an untruncated formatted
// string, even when truncation occurs.
int vsnprintf(char* buffer, size_t size, const char* format, va_list arguments);

// BSD-style safe and consistent string copy functions.
// Copies |src| to |dst|, where |dst_size| is the total allocated size of |dst|.
// Copies at most |dst_size|-1 characters, and always NULL terminates |dst|, as
// long as |dst_size| is not 0.  Returns the length of |src| in characters.
// If the return value is >= dst_size, then the output was truncated.
// NOTE: All sizes are in number of characters, NOT in bytes.
size_t strlcpy(char* dst, const char* src, size_t dst_size);

}  // namespace chromium

#if defined(_WIN32)
#include "string_util_win.hh"
#else
#include "string_util_posix.hh"
#endif

// Safe standard library wrappers for all platforms.

extern const char kWhitespaceASCII[];

// Removes characters in |trim_chars| from the beginning and end of |input|.
// |trim_chars| must be null-terminated.
// NOTE: Safe to use the same variable for both |input| and |output|.
bool TrimString(const std::string& input,
                            const char trim_chars[],
                            std::string* output);

// Trims any whitespace from either end of the input string.  Returns where
// whitespace was found.
// The non-wide version has two functions:
// * TrimWhitespaceASCII()
//   This function is for ASCII strings and only looks for ASCII whitespace;
// Please choose the best one according to your usage.
// NOTE: Safe to use the same variable for both input and output.
enum TrimPositions {
  TRIM_NONE     = 0,
  TRIM_LEADING  = 1 << 0,
  TRIM_TRAILING = 1 << 1,
  TRIM_ALL      = TRIM_LEADING | TRIM_TRAILING,
};
TrimPositions TrimWhitespaceASCII(const std::string& input,
                                              TrimPositions positions,
                                              std::string* output);

// Deprecated. This function is only for backward compatibility and calls
// TrimWhitespaceASCII().
TrimPositions TrimWhitespace(const std::string& input,
                                         TrimPositions positions,
                                         std::string* output);

// ASCII-specific tolower.  The standard library's tolower is locale sensitive,
// so we don't want to use it here.
template <class Char> inline Char ToLowerASCII(Char c) {
  return (c >= 'A' && c <= 'Z') ? (c + ('a' - 'A')) : c;
}

// ASCII-specific toupper.  The standard library's toupper is locale sensitive,
// so we don't want to use it here.
template <class Char> inline Char ToUpperASCII(Char c) {
  return (c >= 'a' && c <= 'z') ? (c + ('A' - 'a')) : c;
}

// Converts the elements of the given string.  This version uses a pointer to
// clearly differentiate it from the non-pointer variant.
template <class str> inline void StringToLowerASCII(str* s) {
  for (typename str::iterator i = s->begin(); i != s->end(); ++i)
    *i = ToLowerASCII(*i);
}

template <class str> inline str StringToLowerASCII(const str& s) {
  // for std::string and std::wstring
  str output(s);
  StringToLowerASCII(&output);
  return output;
}

// Determines the type of ASCII character, independent of locale (the C
// library versions will change based on locale).
template <typename Char>
inline bool IsAsciiWhitespace(Char c) {
  return c == ' ' || c == '\r' || c == '\n' || c == '\t';
}
template <typename Char>
inline bool IsAsciiAlpha(Char c) {
  return ((c >= 'A') && (c <= 'Z')) || ((c >= 'a') && (c <= 'z'));
}
template <typename Char>
inline bool IsAsciiDigit(Char c) {
  return c >= '0' && c <= '9';
}

template <typename Char>
inline bool IsHexDigit(Char c) {
  return (c >= '0' && c <= '9') ||
         (c >= 'A' && c <= 'F') ||
         (c >= 'a' && c <= 'f');
}

template <typename Char>
inline Char HexDigitToInt(Char c) {
  DCHECK(IsHexDigit(c));
  if (c >= '0' && c <= '9')
    return c - '0';
  if (c >= 'A' && c <= 'F')
    return c - 'A' + 10;
  if (c >= 'a' && c <= 'f')
    return c - 'a' + 10;
  return 0;
}

// Starting at |start_offset| (usually 0), replace the first instance of
// |find_this| with |replace_with|.
void ReplaceFirstSubstringAfterOffset(
    std::string* str,
    std::string::size_type start_offset,
    const std::string& find_this,
    const std::string& replace_with);

//-----------------------------------------------------------------------------

// Splits a string into its fields delimited by any of the characters in
// |delimiters|.  Each field is added to the |tokens| vector.  Returns the
// number of tokens found.
size_t Tokenize(const std::string& str,
                const std::string& delimiters,
                std::vector<std::string>* tokens);
size_t Tokenize(const chromium::StringPiece& str,
                const chromium::StringPiece& delimiters,
                std::vector<chromium::StringPiece>* tokens);

// Hack to convert any char-like type to its unsigned counterpart.
// For example, it will convert char, signed char and unsigned char to unsigned
// char.
template<typename T>
struct ToUnsigned {
  typedef T Unsigned;
};

template<>
struct ToUnsigned<char> {
  typedef unsigned char Unsigned;
};
template<>
struct ToUnsigned<signed char> {
  typedef unsigned char Unsigned;
};
template<>
struct ToUnsigned<short> {
  typedef unsigned short Unsigned;
};

#endif  // CHROMIUM_STRINGS_STRING_UTIL_HH__
