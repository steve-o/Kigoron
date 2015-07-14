/* UPA provider client session.
 */

#ifndef CLIENT_HH_
#define CLIENT_HH_

#include <cstdint>
#include <memory>
#include <unordered_map>
#include <unordered_set>

/* Boost Posix Time */
#include <boost/date_time/posix_time/posix_time.hpp>

/* UPA 7.6 */
#include <upa/upa.h>

#include "chromium/debug/leak_tracker.hh"
#include "chromium/string_piece.hh"
#include "upa.hh"
#include "config.hh"
#include "deleter.hh"

namespace kigoron
{
/* Performance Counters */
	enum {
		CLIENT_PC_RSSL_MSGS_SENT,
		CLIENT_PC_RSSL_MSGS_RECEIVED,
		CLIENT_PC_RSSL_MSGS_REJECTED,
		CLIENT_PC_REQUEST_MSGS_RECEIVED,
		CLIENT_PC_REQUEST_MSGS_REJECTED,
		CLIENT_PC_CLOSE_MSGS_RECEIVED,
		CLIENT_PC_CLOSE_MSGS_DISCARDED,
		CLIENT_PC_MMT_LOGIN_RECEIVED,
		CLIENT_PC_MMT_LOGIN_MALFORMED,
		CLIENT_PC_MMT_LOGIN_REJECTED,
		CLIENT_PC_MMT_LOGIN_ACCEPTED,
		CLIENT_PC_MMT_LOGIN_RESPONSE_VALIDATED,
		CLIENT_PC_MMT_LOGIN_RESPONSE_MALFORMED,
		CLIENT_PC_MMT_LOGIN_EXCEPTION,
		CLIENT_PC_MMT_LOGIN_CLOSE_RECEIVED,
		CLIENT_PC_MMT_DIRECTORY_REQUEST_RECEIVED,
		CLIENT_PC_MMT_DIRECTORY_VALIDATED,
		CLIENT_PC_MMT_DIRECTORY_MALFORMED,
		CLIENT_PC_MMT_DIRECTORY_SENT,
		CLIENT_PC_MMT_DIRECTORY_EXCEPTION,
		CLIENT_PC_MMT_DIRECTORY_CLOSE_RECEIVED,
		CLIENT_PC_MMT_DICTIONARY_REQUEST_RECEIVED,
		CLIENT_PC_MMT_DICTIONARY_CLOSE_RECEIVED,
		CLIENT_PC_ITEM_REQUEST_RECEIVED,
		CLIENT_PC_ITEM_REQUEST_MALFORMED,
		CLIENT_PC_ITEM_REQUEST_BEFORE_LOGIN,
		CLIENT_PC_ITEM_STREAMING_REQUEST_RECEIVED,
		CLIENT_PC_ITEM_REISSUE_REQUEST_RECEIVED,
		CLIENT_PC_ITEM_SNAPSHOT_REQUEST_RECEIVED,
		CLIENT_PC_ITEM_DUPLICATE_SNAPSHOT,
		CLIENT_PC_ITEM_REQUEST_REJECTED,
		CLIENT_PC_ITEM_VALIDATED,
		CLIENT_PC_ITEM_MALFORMED,
		CLIENT_PC_ITEM_NOT_FOUND,
		CLIENT_PC_ITEM_SENT,
		CLIENT_PC_ITEM_CLOSED,
		CLIENT_PC_ITEM_EXCEPTION,
		CLIENT_PC_ITEM_CLOSE_RECEIVED,
		CLIENT_PC_ITEM_CLOSE_MALFORMED,
		CLIENT_PC_ITEM_CLOSE_VALIDATED,
		CLIENT_PC_OMM_INACTIVE_CLIENT_SESSION_RECEIVED,
		CLIENT_PC_OMM_INACTIVE_CLIENT_SESSION_EXCEPTION,
		CLIENT_PC_MAX
	};

	class provider_t;

	class client_t :
		public std::enable_shared_from_this<client_t>
	{
	public:
/* Delegate handles specific behaviour of an item request. */
		class Delegate {
		public:
		    Delegate() {}

		    virtual bool OnRequest (const boost::posix_time::ptime& now, uintptr_t handle, uint16_t rwf_version, int32_t token, uint16_t service_id, const std::string& item_name, bool use_attribinfo_in_updates) = 0;
/* TBD */
//		    virtual bool OnCancel (uintptr_t handle, uint16_t rwf_version, int32_t token, uint16_t service_id, const std::string& item_name, bool use_attribinfo_in_updates) = 0;

		protected:
		    virtual ~Delegate() {}
		};

		explicit client_t (const boost::posix_time::ptime& now, std::shared_ptr<provider_t> provider, Delegate* delegate, RsslChannel* handle, const char* address);
		~client_t();

		bool Initialize();
		bool Close();

		bool OnSourceDirectoryUpdate();
		bool SendReply (int32_t token, const void* data, size_t length);

/* RSSL client socket */
		RsslChannel*const handle() const {
			return handle_;
		}
		uint8_t rwf_major_version() const {
			return handle_->majorVersion;
		}
		uint8_t rwf_minor_version() const {
			return handle_->minorVersion;
		}
		uint16_t rwf_version() const {
			return (rwf_major_version() * 256) + rwf_minor_version();
		}
		const std::unordered_set<int32_t>& tokens() const {
			return tokens_;
		}

	private:
		bool OnMsg (const boost::posix_time::ptime& now, RsslDecodeIterator* it, const RsslMsg* msg);
		bool OnRequestMsg (RsslDecodeIterator* it, const RsslRequestMsg* msg);
		bool OnLoginRequest (RsslDecodeIterator* it, const RsslRequestMsg* msg);
		bool OnLoginAttribInfo (RsslDecodeIterator* it);
		bool OnDirectoryRequest (RsslDecodeIterator* it, const RsslRequestMsg* msg);
		bool OnDictionaryRequest (RsslDecodeIterator* it, const RsslRequestMsg* msg);
		bool OnItemRequest (RsslDecodeIterator* it, const RsslRequestMsg* msg);

		bool OnCloseMsg (RsslDecodeIterator* it, const RsslCloseMsg* msg);
		bool OnItemClose (const RsslCloseMsg* msg);

		bool RejectLogin (const RsslRequestMsg* msg, int32_t login_token);
		bool AcceptLogin (const RsslRequestMsg* msg, int32_t login_token);

		bool SendDirectoryRefresh (int32_t token, const char* service_name, uint32_t filter_mask);
		bool SendDirectoryUpdate (int32_t token, const char* service_name);
		bool SendClose (int32_t token, uint16_t service_id, uint8_t model_type, const chromium::StringPiece& item_name, bool use_attribinfo_in_updates, uint8_t stream_state, uint8_t status_code, const chromium::StringPiece& status_text);
		int Submit (RsslBuffer* buf);

		const boost::posix_time::ptime& NextPing() const {
			return next_ping_;
		}
		const boost::posix_time::ptime& NextPong() const {
			return next_pong_;
		}
		void SetNextPing (const boost::posix_time::ptime& time_) {
			next_ping_ = time_;
		}
		void SetNextPong (const boost::posix_time::ptime& time_) {
			next_pong_ = time_;
		}
		void IncrementPendingCount() {
			pending_count_++;
		}
		void ClearPendingCount() {
			pending_count_ = 0;
		}
		unsigned GetPendingCount() const {
			return pending_count_;
		}

		std::shared_ptr<provider_t> provider_;
		Delegate* delegate_;

/* unique id per connection. */
		std::string prefix_;

/* client details. */
		std::string address_;
		std::string name_;

/* UPA socket. */
		RsslChannel* handle_;
/* Pending messages to flush. */
		unsigned pending_count_;

/* Watchlist of all items. */
		std::unordered_set<int32_t> tokens_;
/* Item requests may appear before login success has been granted.  */
		bool is_logged_in_;
		int32_t directory_token_;
		int32_t login_token_;
/* RSSL keepalive state. */
		boost::posix_time::ptime next_ping_;
		boost::posix_time::ptime next_pong_;
		unsigned ping_interval_;

		friend provider_t;

/** Performance Counters **/
		boost::posix_time::ptime creation_time_, last_activity_;
		uint32_t cumulative_stats_[CLIENT_PC_MAX];
		uint32_t snap_stats_[CLIENT_PC_MAX];

#ifdef KIGORONMIB_H
		friend Netsnmp_Next_Data_Point kigoronClientTable_get_next_data_point;
		friend Netsnmp_Node_Handler kigoronClientTable_handler;

		friend Netsnmp_Next_Data_Point kigoronClientPerformanceTable_get_next_data_point;
		friend Netsnmp_Node_Handler kigoronClientPerformanceTable_handler;
#endif /* KIGORONMIB_H */

		chromium::debug::LeakTracker<client_t> leak_tracker_;
	};

} /* namespace kigoron */

#endif /* CLIENT_HH_ */

/* eof */
