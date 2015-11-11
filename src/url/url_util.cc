// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "url/url_util.hh"

#include <string.h>
#include <vector>

#include "chromium/logging.hh"
#include "url/url_canon_internal.hh"
#include "url/url_file.hh"
#include "url/url_util_internal.hh"

namespace url {

namespace {

// ASCII-specific tolower.  The standard library's tolower is locale sensitive,
// so we don't want to use it here.
template<class Char>
inline Char ToLowerASCII(Char c) {
  return (c >= 'A' && c <= 'Z') ? (c + ('a' - 'A')) : c;
}

// Backend for LowerCaseEqualsASCII.
template<typename Iter>
inline bool DoLowerCaseEqualsASCII(Iter a_begin, Iter a_end, const char* b) {
  for (Iter it = a_begin; it != a_end; ++it, ++b) {
    if (!*b || ToLowerASCII(*it) != *b)
      return false;
  }
  return *b == 0;
}

const int kNumStandardURLSchemes = 8;
const char* kStandardURLSchemes[kNumStandardURLSchemes] = {
  kHttpScheme,
  kHttpsScheme,
  kFileScheme,  // Yes, file urls can have a hostname!
  kFtpScheme,
  kGopherScheme,
  kWsScheme,    // WebSocket.
  kWssScheme,   // WebSocket secure.
  kFileSystemScheme,
};

// List of the currently installed standard schemes. This list is lazily
// initialized by InitStandardSchemes and is leaked on shutdown to prevent
// any destructors from being called that will slow us down or cause problems.
std::vector<const char*>* standard_schemes = NULL;

// See the LockStandardSchemes declaration in the header.
bool standard_schemes_locked = false;

// Ensures that the standard_schemes list is initialized, does nothing if it
// already has values.
void InitStandardSchemes() {
  if (standard_schemes)
    return;
  standard_schemes = new std::vector<const char*>;
  for (int i = 0; i < kNumStandardURLSchemes; i++)
    standard_schemes->push_back(kStandardURLSchemes[i]);
}

// Given a string and a range inside the string, compares it to the given
// lower-case |compare_to| buffer.
template<typename CHAR>
inline bool DoCompareSchemeComponent(const CHAR* spec,
                                     const Component& component,
                                     const char* compare_to) {
  if (!component.is_nonempty())
    return compare_to[0] == 0;  // When component is empty, match empty scheme.
  return LowerCaseEqualsASCII(&spec[component.begin],
                              &spec[component.end()],
                              compare_to);
}

// Returns true if the given scheme identified by |scheme| within |spec| is one
// of the registered "standard" schemes.
template<typename CHAR>
bool DoIsStandard(const CHAR* spec, const Component& scheme) {
  if (!scheme.is_nonempty())
    return false;  // Empty or invalid schemes are non-standard.

  InitStandardSchemes();
  for (size_t i = 0; i < standard_schemes->size(); i++) {
    if (LowerCaseEqualsASCII(&spec[scheme.begin], &spec[scheme.end()],
                             standard_schemes->at(i)))
      return true;
  }
  return false;
}

template<typename CHAR>
bool DoFindAndCompareScheme(const CHAR* str,
                            int str_len,
                            const char* compare,
                            Component* found_scheme) {
  // Before extracting scheme, canonicalize the URL to remove any whitespace.
  // This matches the canonicalization done in DoCanonicalize function.
  RawCanonOutputT<CHAR> whitespace_buffer;
  int spec_len;
  const CHAR* spec = RemoveURLWhitespace(str, str_len,
                                         &whitespace_buffer, &spec_len);

  Component our_scheme;
  if (!ExtractScheme(spec, spec_len, &our_scheme)) {
    // No scheme.
    if (found_scheme)
      *found_scheme = Component();
    return false;
  }
  if (found_scheme)
    *found_scheme = our_scheme;
  return DoCompareSchemeComponent(spec, our_scheme, compare);
}

template<typename CHAR>
bool DoCanonicalize(const CHAR* in_spec,
                    int in_spec_len,
                    bool trim_path_end,
                    CanonOutput* output,
                    Parsed* output_parsed) {
  // Remove any whitespace from the middle of the relative URL, possibly
  // copying to the new buffer.
  RawCanonOutputT<CHAR> whitespace_buffer;
  int spec_len;
  const CHAR* spec = RemoveURLWhitespace(in_spec, in_spec_len,
                                         &whitespace_buffer, &spec_len);

  Parsed parsed_input;
#ifdef WIN32
  // For Windows, we allow things that look like absolute Windows paths to be
  // fixed up magically to file URLs. This is done for IE compatability. For
  // example, this will change "c:/foo" into a file URL rather than treating
  // it as a URL with the protocol "c". It also works for UNC ("\\foo\bar.txt").
  // There is similar logic in url_canon_relative.cc for
  //
  // For Max & Unix, we don't do this (the equivalent would be "/foo/bar" which
  // has no meaning as an absolute path name. This is because browsers on Mac
  // & Unix don't generally do this, so there is no compatibility reason for
  // doing so.
  if (DoesBeginUNCPath(spec, 0, spec_len, false) ||
      DoesBeginWindowsDriveSpec(spec, 0, spec_len)) {
    ParseFileURL(spec, spec_len, &parsed_input);
    return CanonicalizeFileURL(spec, spec_len, parsed_input,
                               output, output_parsed);
  }
#endif

  Component scheme;
  if (!ExtractScheme(spec, spec_len, &scheme))
    return false;

  // This is the parsed version of the input URL, we have to canonicalize it
  // before storing it in our object.
  bool success;
  if (DoCompareSchemeComponent(spec, scheme, url::kFileScheme)) {
    // File URLs are special.
    ParseFileURL(spec, spec_len, &parsed_input);
    success = CanonicalizeFileURL(spec, spec_len, parsed_input,
                                  output, output_parsed);
  } else if (DoCompareSchemeComponent(spec, scheme, url::kFileSystemScheme)) {
    // Filesystem URLs are special.
    ParseFileSystemURL(spec, spec_len, &parsed_input);
    success = CanonicalizeFileSystemURL(spec, spec_len, parsed_input,
                                        output,
                                        output_parsed);

  } else if (DoIsStandard(spec, scheme)) {
    // All "normal" URLs.
    ParseStandardURL(spec, spec_len, &parsed_input);
    success = CanonicalizeStandardURL(spec, spec_len, parsed_input,
                                      output, output_parsed);

  } else if (DoCompareSchemeComponent(spec, scheme, url::kMailToScheme)) {
    // Mailto are treated like a standard url with only a scheme, path, query
    ParseMailtoURL(spec, spec_len, &parsed_input);
    success = CanonicalizeMailtoURL(spec, spec_len, parsed_input, output,
                                    output_parsed);

  } else {
    // "Weird" URLs like data: and javascript:
    ParsePathURL(spec, spec_len, trim_path_end, &parsed_input);
    success = CanonicalizePathURL(spec, spec_len, parsed_input, output,
                                  output_parsed);
  }
  return success;
}

}  // namespace

void Initialize() {
  InitStandardSchemes();
}

void Shutdown() {
  if (standard_schemes) {
    delete standard_schemes;
    standard_schemes = NULL;
  }
}

void AddStandardScheme(const char* new_scheme) {
  // If this assert triggers, it means you've called AddStandardScheme after
  // LockStandardSchemes have been called (see the header file for
  // LockStandardSchemes for more).
  //
  // This normally means you're trying to set up a new standard scheme too late
  // in your application's init process. Locate where your app does this
  // initialization and calls LockStandardScheme, and add your new standard
  // scheme there.
  DCHECK(!standard_schemes_locked) <<
      "Trying to add a standard scheme after the list has been locked.";

  size_t scheme_len = strlen(new_scheme);
  if (scheme_len == 0)
    return;

  // Dulicate the scheme into a new buffer and add it to the list of standard
  // schemes. This pointer will be leaked on shutdown.
  char* dup_scheme = new char[scheme_len + 1];
//  ANNOTATE_LEAKING_OBJECT_PTR(dup_scheme);
  memcpy(dup_scheme, new_scheme, scheme_len + 1);

  InitStandardSchemes();
  standard_schemes->push_back(dup_scheme);
}

void LockStandardSchemes() {
  standard_schemes_locked = true;
}

bool IsStandard(const char* spec, const Component& scheme) {
  return DoIsStandard(spec, scheme);
}

bool FindAndCompareScheme(const char* str,
                          int str_len,
                          const char* compare,
                          Component* found_scheme) {
  return DoFindAndCompareScheme(str, str_len, compare, found_scheme);
}

bool Canonicalize(const char* spec,
                  int spec_len,
                  bool trim_path_end,
                  CanonOutput* output,
                  Parsed* output_parsed) {
  return DoCanonicalize(spec, spec_len, trim_path_end,
                        output, output_parsed);
}

// Front-ends for LowerCaseEqualsASCII.
bool LowerCaseEqualsASCII(const char* a_begin,
                          const char* a_end,
                          const char* b) {
  return DoLowerCaseEqualsASCII(a_begin, a_end, b);
}

bool LowerCaseEqualsASCII(const char* a_begin,
                          const char* a_end,
                          const char* b_begin,
                          const char* b_end) {
  while (a_begin != a_end && b_begin != b_end &&
         ToLowerASCII(*a_begin) == *b_begin) {
    a_begin++;
    b_begin++;
  }
  return a_begin == a_end && b_begin == b_end;
}

void EncodeURIComponent(const char* input, int length, CanonOutput* output) {
  for (int i = 0; i < length; ++i) {
    unsigned char c = static_cast<unsigned char>(input[i]);
    if (IsComponentChar(c))
      output->push_back(c);
    else
      AppendEscapedChar(c, output);
  }
}

bool CompareSchemeComponent(const char* spec,
                            const Component& component,
                            const char* compare_to) {
  return DoCompareSchemeComponent(spec, component, compare_to);
}

}  // namespace url
