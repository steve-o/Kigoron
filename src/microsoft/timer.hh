/* http://msdn.microsoft.com/en-us/magazine/hh580731.aspx
 */

#ifndef __MS_TIMER_HH__
#define __MS_TIMER_HH__

#include "unique_handle.hh"

namespace ms
{

	struct timer_traits
	{
		static PTP_TIMER invalid() throw()
		{
			return nullptr;
		}

		static void close (PTP_TIMER value) throw()
		{
#if _WIN32_WINNT >= _WIN32_WINNT_WS08
			CloseThreadpoolTimer (value);
#endif
		}
	};

/* Example usage:
 *
 * timer t (CreateThreadpoolTimer (on_timer, context, nullptr));
 */
	typedef unique_handle<PTP_TIMER, timer_traits> timer;

} /* namespace ms */

#endif /* __MS_TIMER_HH__ */

/* eof */
