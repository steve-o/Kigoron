/* User-configurable settings.
 *
 * NB: all strings are locale bound, UPA provides no Unicode support.
 */

#ifndef CONFIG_HH_
#define CONFIG_HH_

#include <string>
#include <sstream>
#include <vector>

namespace kigoron
{

	struct config_t
	{
		config_t();

//  TREP-RT service name, e.g. IDN_RDF, hEDD, ELEKTRON_DD.
		std::string service_name;

//  TREP-RT RSSL port, e.g. 14002, 14003.
		std::string rssl_port;

//  TCP buffer sizes.
		std::string send_buffer_size;
		std::string recv_buffer_size;

//  Name to present for infrastructure amusement.
		std::string application_name;

//  RSSL vendor name presented in directory.
		std::string vendor_name;

//  Client session capacity.
		size_t session_capacity;

//  Maximum number of requests to be enqueued for service.
		size_t open_window;

//  Symbol map.
		std::string symbol_path;

//  Maximum age of symbols before tagging suspect.
		std::string max_age;
	};

	inline
	std::ostream& operator<< (std::ostream& o, const config_t& config) {
		std::ostringstream ss;
		o << "config_t: { "
			  "\"service_name\": \"" << config.service_name << "\""
			", \"rssl_port\": \"" << config.rssl_port << "\""
			", \"send_buffer_size\": \"" << config.send_buffer_size << "\""
			", \"recv_buffer_size\": \"" << config.recv_buffer_size << "\""
			", \"application_name\": \"" << config.application_name << "\""
			", \"vendor_name\": \"" << config.vendor_name << "\""
			", \"session_capacity\": " << config.session_capacity << ""
			", \"open_window\": " << config.open_window << ""
			", \"symbol_path\": \"" << config.symbol_path << "\""
			", \"max_age\": \"" << config.max_age << "\""
			" }";
		return o;
	}

} /* namespace kigoron */

#endif /* CONFIG_HH_ */

/* eof */
