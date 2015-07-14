/* http://en.wikipedia.org/wiki/Unix_epoch
 */

#ifndef UNIX_EPOCH_HH_
#define UNIX_EPOCH_HH_

/* Boost Posix Time */
#include <boost/date_time/posix_time/posix_time.hpp>
/* Boost Gregorian Calendar */
#include <boost/date_time/gregorian/gregorian_types.hpp>

namespace internal {

static const boost::gregorian::date kUnixEpoch (1970, 1, 1);

/* Convert Posix time to Unix Epoch time.
 */
static inline
__time32_t
to_unix_epoch (
	const boost::posix_time::ptime t
	)
{
	return (t - boost::posix_time::ptime (kUnixEpoch)).total_seconds();
}

} /* namespace internal */

#endif /* UNIX_EPOCH_HH_ */

/* eof */