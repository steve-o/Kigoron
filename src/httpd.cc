/* HTTP embedded server.
 */

#include "httpd.hh"

#include "chromium/logging.hh"

kigoron::httpd_t::httpd_t (
	)
{
}

kigoron::httpd_t::~httpd_t()
{
	DLOG(INFO) << "~httpd_t";
/* Summary output */
	VLOG(3) << "Httpd summary: {"
		" }";
}

/* Open HTTP port and listen for incoming connection attempts.
 */
bool
kigoron::httpd_t::Initialize()
{
	return true;
}

/* eof */
