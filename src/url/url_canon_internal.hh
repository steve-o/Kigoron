// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef URL_URL_CANON_INTERNAL_HH_
#define URL_URL_CANON_INTERNAL_HH_

// This file is intended to be included in another C++ file where the character
// types are defined. This allows us to write mostly generic code, but not have
// templace bloat because everything is inlined when anybody calls any of our
// functions.

#include <stdlib.h>

#include "chromium/logging.hh"
#include "url/url_canon.hh"

namespace url {

// Character type handling -----------------------------------------------------

// Bits that identify different character types. These types identify different
// bits that are set for each 8-bit character in the kSharedCharTypeTable.
enum SharedCharTypes {
  // Characters that do not require escaping in queries. Characters that do
  // not have this flag will be escaped; see url_canon_query.cc
  CHAR_QUERY = 1,

  // Valid in the username/password field.
  CHAR_USERINFO = 2,

  // Valid in a IPv4 address (digits plus dot and 'x' for hex).
  CHAR_IPV4 = 4,

  // Valid in an ASCII-representation of a hex digit (as in %-escaped).
  CHAR_HEX = 8,

  // Valid in an ASCII-representation of a decimal digit.
  CHAR_DEC = 16,

  // Valid in an ASCII-representation of an octal digit.
  CHAR_OCT = 32,

  // Characters that do not require escaping in encodeURIComponent.  Characters
  // that do not have this flag will be escaped; see url_util.cc.
  CHAR_COMPONENT = 64,
};

// This table contains the flags in SharedCharTypes for each 8-bit character.
// Some canonicalization functions have their own specialized lookup table.
// For those with simple requirements, we have collected the flags in one
// place so there are fewer lookup tables to load into the CPU cache.
//
// Using an unsigned char type has a small but measurable performance benefit
// over using a 32-bit number.
extern const unsigned char kSharedCharTypeTable[0x100];

// More readable wrappers around the character type lookup table.
inline bool IsCharOfType(unsigned char c, SharedCharTypes type) {
  return !!(kSharedCharTypeTable[c] & type);
}
inline bool IsQueryChar(unsigned char c) {
  return IsCharOfType(c, CHAR_QUERY);
}
inline bool IsIPv4Char(unsigned char c) {
  return IsCharOfType(c, CHAR_IPV4);
}
inline bool IsHexChar(unsigned char c) {
  return IsCharOfType(c, CHAR_HEX);
}
inline bool IsComponentChar(unsigned char c) {
  return IsCharOfType(c, CHAR_COMPONENT);
}

// Appends the given string to the output, escaping characters that do not
// match the given |type| in SharedCharTypes.
void AppendStringOfType(const char* source, int length,
                        SharedCharTypes type,
                        CanonOutput* output);

// Maps the hex numerical values 0x0 to 0xf to the corresponding ASCII digit
// that will be used to represent it.
extern const char kHexCharLookup[0x10];

// This lookup table allows fast conversion between ASCII hex letters and their
// corresponding numerical value. The 8-bit range is divided up into 8
// regions of 0x20 characters each. Each of the three character types (numbers,
// uppercase, lowercase) falls into different regions of this range. The table
// contains the amount to subtract from characters in that range to get at
// the corresponding numerical value.
//
// See HexDigitToValue for the lookup.
extern const char kCharToHexLookup[8];

// Assumes the input is a valid hex digit! Call IsHexChar before using this.
inline unsigned char HexCharToValue(unsigned char c) {
  return c - kCharToHexLookup[c / 0x20];
}

// Indicates if the given character is a dot or dot equivalent, returning the
// number of characters taken by it. This will be one for a literal dot, 3 for
// an escaped dot. If the character is not a dot, this will return 0.
template<typename CHAR>
inline int IsDot(const CHAR* spec, int offset, int end) {
  if (spec[offset] == '.') {
    return 1;
  } else if (spec[offset] == '%' && offset + 3 <= end &&
             spec[offset + 1] == '2' &&
             (spec[offset + 2] == 'e' || spec[offset + 2] == 'E')) {
    // Found "%2e"
    return 3;
  }
  return 0;
}

// Returns the canonicalized version of the input character according to scheme
// rules. This is implemented alongside the scheme canonicalizer, and is
// required for relative URL resolving to test for scheme equality.
//
// Returns 0 if the input character is not a valid scheme character.
char CanonicalSchemeChar(char ch);

// Write a single character, escaped, to the output. This always escapes: it
// does no checking that thee character requires escaping.
// Escaping makes sense only 8 bit chars, so code works in all cases of
// input parameters (8/16bit).
template<typename UINCHAR, typename OUTCHAR>
inline void AppendEscapedChar(UINCHAR ch,
                              CanonOutputT<OUTCHAR>* output) {
  output->push_back('%');
  output->push_back(kHexCharLookup[(ch >> 4) & 0xf]);
  output->push_back(kHexCharLookup[ch & 0xf]);
}

// UTF-8 functions ------------------------------------------------------------

// Generic To-UTF-8 converter. This will call the given append method for each
// character that should be appended, with the given output method. Wrappers
// are provided below for escaped and non-escaped versions of this.
//
// The char_value must have already been checked that it's a valid Unicode
// character.
template<class Output, void Appender(unsigned char, Output*)>
inline void DoAppendUTF8(unsigned char_value, Output* output) {
  if (char_value <= 0x7f) {
    Appender(static_cast<unsigned char>(char_value), output);
  } else if (char_value <= 0x7ff) {
    // 110xxxxx 10xxxxxx
    Appender(static_cast<unsigned char>(0xC0 | (char_value >> 6)),
             output);
    Appender(static_cast<unsigned char>(0x80 | (char_value & 0x3f)),
             output);
  } else if (char_value <= 0xffff) {
    // 1110xxxx 10xxxxxx 10xxxxxx
    Appender(static_cast<unsigned char>(0xe0 | (char_value >> 12)),
             output);
    Appender(static_cast<unsigned char>(0x80 | ((char_value >> 6) & 0x3f)),
             output);
    Appender(static_cast<unsigned char>(0x80 | (char_value & 0x3f)),
             output);
  } else if (char_value <= 0x10FFFF) {  // Max unicode code point.
    // 11110xxx 10xxxxxx 10xxxxxx 10xxxxxx
    Appender(static_cast<unsigned char>(0xf0 | (char_value >> 18)),
             output);
    Appender(static_cast<unsigned char>(0x80 | ((char_value >> 12) & 0x3f)),
             output);
    Appender(static_cast<unsigned char>(0x80 | ((char_value >> 6) & 0x3f)),
             output);
    Appender(static_cast<unsigned char>(0x80 | (char_value & 0x3f)),
             output);
  } else {
    // Invalid UTF-8 character (>20 bits).
    NOTREACHED();
  }
}

// Helper used by AppendUTF8Value below. We use an unsigned parameter so there
// are no funny sign problems with the input, but then have to convert it to
// a regular char for appending.
inline void AppendCharToOutput(unsigned char ch, CanonOutput* output) {
  output->push_back(static_cast<char>(ch));
}

// Writes the given character to the output as UTF-8. This does NO checking
// of the validity of the unicode characters; the caller should ensure that
// the value it is appending is valid to append.
inline void AppendUTF8Value(unsigned char_value, CanonOutput* output) {
  DoAppendUTF8<CanonOutput, AppendCharToOutput>(char_value, output);
}

// Writes the given character to the output as UTF-8, escaping ALL
// characters (even when they are ASCII). This does NO checking of the
// validity of the unicode characters; the caller should ensure that the value
// it is appending is valid to append.
inline void AppendUTF8EscapedValue(unsigned char_value, CanonOutput* output) {
  DoAppendUTF8<CanonOutput, AppendEscapedChar>(char_value, output);
}

// Escaping functions ---------------------------------------------------------

// Writes the given character to the output as UTF-8, escaped. Call this
// function only when the input is wide. Returns true on success. Failure
// means there was some problem with the encoding, we'll still try to
// update the |*begin| pointer and add a placeholder character to the
// output so processing can continue.
//
// We will append the character starting at ch[begin] with the buffer ch
// being |length|. |*begin| will be updated to point to the last character
// consumed (we may consume more than one for UTF-16) so that if called in
// a loop, incrementing the pointer will move to the next character.
//
// Every single output character will be escaped. This means that if you
// give it an ASCII character as input, it will be escaped. Some code uses
// this when it knows that a character is invalid according to its rules
// for validity. If you don't want escaping for ASCII characters, you will
// have to filter them out prior to calling this function.
//
// Assumes that ch[begin] is within range in the array, but does not assume
// that any following characters are.

// Given a '%' character at |*begin| in the string |spec|, this will decode
// the escaped value and put it into |*unescaped_value| on success (returns
// true). On failure, this will return false, and will not write into
// |*unescaped_value|.
//
// |*begin| will be updated to point to the last character of the escape
// sequence so that when called with the index of a for loop, the next time
// through it will point to the next character to be considered. On failure,
// |*begin| will be unchanged.
inline bool Is8BitChar(char c) {
  return true;  // this case is specialized to avoid a warning
}

template<typename CHAR>
inline bool DecodeEscaped(const CHAR* spec, int* begin, int end,
                          unsigned char* unescaped_value) {
  if (*begin + 3 > end ||
      !Is8BitChar(spec[*begin + 1]) || !Is8BitChar(spec[*begin + 2])) {
    // Invalid escape sequence because there's not enough room, or the
    // digits are not ASCII.
    return false;
  }

  unsigned char first = static_cast<unsigned char>(spec[*begin + 1]);
  unsigned char second = static_cast<unsigned char>(spec[*begin + 2]);
  if (!IsHexChar(first) || !IsHexChar(second)) {
    // Invalid hex digits, fail.
    return false;
  }

  // Valid escape sequence.
  *unescaped_value = (HexCharToValue(first) << 4) + HexCharToValue(second);
  *begin += 2;
  return true;
}

// Appends the given substring to the output, escaping "some" characters that
// it feels may not be safe. It assumes the input values are all contained in
// 8-bit although it allows any type.
//
// This is used in error cases to append invalid output so that it looks
// approximately correct. Non-error cases should not call this function since
// the escaping rules are not guaranteed!
void AppendInvalidNarrowString(const char* spec, int begin, int end,
                               CanonOutput* output);

// Misc canonicalization helpers ----------------------------------------------

// Applies the replacements to the given component source. The component source
// should be pre-initialized to the "old" base. That is, all pointers will
// point to the spec of the old URL, and all of the Parsed components will
// be indices into that string.
//
// The pointers and components in the |source| for all non-NULL strings in the
// |repl| (replacements) will be updated to reference those strings.
// Canonicalizing with the new |source| and |parsed| can then combine URL
// components from many different strings.
void SetupOverrideComponents(const char* base,
                             const Replacements<char>& repl,
                             URLComponentSource<char>* source,
                             Parsed* parsed);

// Implemented in url_canon_path.cc, these are required by the relative URL
// resolver as well, so we declare them here.
bool CanonicalizePartialPath(const char* spec,
                             const Component& path,
                             int path_begin_in_output,
                             CanonOutput* output);

#ifndef WIN32

// Implementations of Windows' int-to-string conversions
int _itoa_s(int value, char* buffer, size_t size_in_chars,
                       int radix);

// Secure template overloads for these functions
template<size_t N>
inline int _itoa_s(int value, char (&buffer)[N], int radix) {
  return _itoa_s(value, buffer, N, radix);
}

// _strtoui64 and strtoull behave the same
inline unsigned long long _strtoui64(const char* nptr,
                                     char** endptr, int base) {
  return strtoull(nptr, endptr, base);
}

#endif  // WIN32

}  // namespace url

#endif  // URL_URL_CANON_INTERNAL_HH_
