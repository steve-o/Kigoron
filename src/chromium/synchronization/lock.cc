// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file is used for debugging assertion support.  The Lock class
// is functionally a wrapper around the LockImpl class, so the only
// real intelligence in the class is in the debugging logic.

#if !defined(NDEBUG)

#include "lock.hh"
#include "../logging.hh"

namespace chromium {

Lock::Lock() : lock_() {
  boost::thread::id null_id;
  owned_by_thread_ = false;
  owning_thread_id_ = null_id;
}

void Lock::AssertAcquired() const {
  DCHECK(owned_by_thread_);
  DCHECK_EQ(owning_thread_id_, boost::this_thread::get_id());
}

void Lock::CheckHeldAndUnmark() {
  DCHECK(owned_by_thread_);
  DCHECK_EQ(owning_thread_id_, boost::this_thread::get_id());
  boost::thread::id null_id;
  owned_by_thread_ = false;
  owning_thread_id_ = null_id;
}

void Lock::CheckUnheldAndMark() {
  DCHECK(!owned_by_thread_);
  owned_by_thread_ = true;
  owning_thread_id_ = boost::this_thread::get_id();
}

}  // namespace chromium

#endif  // NDEBUG

/* eof */
