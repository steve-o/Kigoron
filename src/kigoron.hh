/* UPA interactive fake snapshot provider.
 *
 * An interactive provider sits listening on a port for RSSL connections,
 * once a client is connected requests may be submitted for snapshots or
 * subscriptions to item streams.  This application will broadcast updates
 * continuously independent of client interest and the provider side will
 * perform fan-out as required.
 *
 * The provider is not required to perform last value caching, forcing the
 * client to wait for a subsequent broadcast to actually see data.
 */

#ifndef KIGORON_HH_
#define KIGORON_HH_

#include <cstdint>
#include <memory>
#include <boost/unordered_map.hpp>

#include "chromium/string_piece.hh"
#include "client.hh"
#include "provider.hh"
#include "config.hh"

/* Maximum encoded size of an RSSL provider to client message. */
#define MAX_MSG_SIZE 4096

namespace kigoron
{
	class upa_t;
	class provider_t;

	class item_t
	{
	public:
		explicit item_t (const std::string& ric_, const std::string& exchange_, const std::string& class_, const std::string& name_, const std::string& currency_, const boost::posix_time::ptime& last_write_, const boost::posix_time::ptime& max_age_)
			: primary_ric (ric_)
			, exchange_code (exchange_)
			, class_code (class_)
			, display_name (name_)
			, currency_name (currency_)
			, modification_time (last_write_)
			, expiration_time (max_age_)
		{
		}

		std::string primary_ric;
		std::string exchange_code;
		std::string class_code;
		std::string display_name;
		std::string currency_name;
		std::string isin_code;
		std::string cusip_code;
		std::string sedol_code;
		std::string gics_code;

		boost::posix_time::ptime modification_time;
		boost::posix_time::ptime expiration_time;		/* item marked stale after this timestamp */
	};

	class kigoron_t
/* Permit global weak pointer to application instance for shutdown notification. */
		: public std::enable_shared_from_this<kigoron_t>
		, public client_t::Delegate	/* Rssl requests */
	{
	public:
		explicit kigoron_t();
		virtual ~kigoron_t();

/* Run as an application.  Blocks until Quit is called.  Returns the error code
 * Returns the error code to be returned by main().
 */
		int Run();
/* Quit an earlier call to Run(). */
		void Quit();

		virtual bool OnRequest (const boost::posix_time::ptime& now, uintptr_t handle, uint16_t rwf_version, int32_t token, uint16_t service_id, const std::string& item_name, bool use_attribinfo_in_updates) override;

		bool Initialize();
		void Reset();

	private:
/* Run core event loop. */
		void MainLoop();

/* Start the encapsulated provider instance until Stop is called.  Stop may be
 * called to pre-emptively prevent execution.
 */
		bool Start();
		void Stop();

		bool WriteRaw (const boost::posix_time::ptime& now, uint16_t rwf_version, int32_t token, uint16_t service_id, const chromium::StringPiece& item_name, const chromium::StringPiece& dacs_lock, std::shared_ptr<item_t> item, void* data, size_t* length);

/* Mainloop procesing thread. */
		std::unique_ptr<boost::thread> event_thread_;

/* Asynchronous shutdown notification mechanism. */
		boost::condition_variable mainloop_cond_;
		boost::mutex mainloop_lock_;
		bool mainloop_shutdown_;
/* Flag to indicate Stop has be called and thus prohibit start of new provider. */
		boost::atomic_bool shutting_down_;
/* Application configuration. */
		config_t config_;
/* UPA context. */
		std::shared_ptr<upa_t> upa_;
/* UPA provider */
		std::shared_ptr<provider_t> provider_;	

/* Symbol map. */
		boost::unordered_map<std::string, std::shared_ptr<item_t>> map_;
/* As worker state: */
/* Rssl message buffer */
		char rssl_buf_[MAX_MSG_SIZE];
		size_t rssl_length_;
	};

} /* namespace kigoron */

#endif /* KIGORON_HH_ */

/* eof */
