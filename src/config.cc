/* User-configurable settings.
 */

#include "config.hh"

static const char* kAppName = "Kigoron";
static const char* kDefaultRsslPort = "14002";
static const char* kVendorName = "Thomson Reuters";

kigoron::config_t::config_t() :
/* default values */
	service_name ("NOCACHE_VTA"),
	rssl_port ("24002"),
#ifdef _WIN32
	send_buffer_size ("65535"),
	recv_buffer_size ("65535"),
#else
	send_buffer_size (""),
	recv_buffer_size (""),
#endif
	application_name (kAppName),
	vendor_name (kVendorName),
	session_capacity (8),
	open_window (1000),
	max_age ("720:00:00")
{
/* C++11 initializer lists not supported in MSVC2010 */
}

/* eof */
