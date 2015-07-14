/* lock_impl.cc
 *
 * A basic platform specific spin-lock.
 *
 * Copyright (c) 2011 The Chromium Authors. All rights reserved.
 */

#include "lock_impl.hh"

namespace chromium {
namespace internal {

LockImpl::LockImpl() {
/* The second parameter is the spin count, for short-held locks it avoid the
 * contending thread from going to sleep which helps performance greatly.
 */
	::InitializeCriticalSectionAndSpinCount (&os_lock_, 2000);
}

LockImpl::~LockImpl() {
	::DeleteCriticalSection (&os_lock_);
}

bool
LockImpl::Try()
{
	if (::TryEnterCriticalSection (&os_lock_) != FALSE) {
		return true;
	}
	return false;
}

void
LockImpl::Lock()
{
	::EnterCriticalSection (&os_lock_);
}

void
LockImpl::Unlock()
{
	::LeaveCriticalSection (&os_lock_);
}

} /* namespace internal */
} /* namespace chromium */

/* eof */