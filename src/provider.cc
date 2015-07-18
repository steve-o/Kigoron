/* UPA interactive provider.
 *
 * One single provider, and hence wraps a UPA session for simplicity.
 */

#include "provider.hh"

#include <algorithm>
#include <utility>

#include <windows.h>

#include "chromium/logging.hh"
#include "upaostream.hh"
#include "client.hh"

/* Reuters Wire Format nomenclature for RDM dictionary names. */
static const std::string kRdmFieldDictionaryName ("RWFFld");
static const std::string kEnumTypeDictionaryName ("RWFEnum");

kigoron::provider_t::provider_t (
	const kigoron::config_t& config,
	std::shared_ptr<kigoron::upa_t> upa,
	kigoron::client_t::Delegate* request_delegate 
	) :
	creation_time_ (boost::posix_time::second_clock::universal_time()),
	last_activity_ (creation_time_),
	config_ (config),
	upa_ (upa),
	request_delegate_ (request_delegate),
	rssl_sock_ (nullptr),
	keep_running_ (true),
	min_rwf_version_ (0),
	service_id_ (1),	// first and only service
	is_accepting_connections_ (true),
	is_accepting_requests_ (true)
{
	ZeroMemory (cumulative_stats_, sizeof (cumulative_stats_));
	ZeroMemory (snap_stats_, sizeof (snap_stats_));
}

kigoron::provider_t::~provider_t()
{
	DLOG(INFO) << "~provider_t";
	Close();
/* Cleanup RSSL stack. */
	upa_.reset();
/* Summary output */
	using namespace boost::posix_time;
	auto uptime = second_clock::universal_time() - creation_time_;
	VLOG(3) << "Provider summary: {"
		 " \"Uptime\": \"" << to_simple_string (uptime) << "\""
		", \"ConnectionsReceived\": " << cumulative_stats_[PROVIDER_PC_CONNECTION_RECEIVED] <<
		", \"ClientSessions\": " << cumulative_stats_[PROVIDER_PC_CLIENT_SESSION_ACCEPTED] <<
		", \"MsgsReceived\": " << cumulative_stats_[PROVIDER_PC_RSSL_MSGS_RECEIVED] <<
		", \"MsgsMalformed\": " << cumulative_stats_[PROVIDER_PC_RSSL_MSGS_MALFORMED] <<
		", \"MsgsSent\": " << cumulative_stats_[PROVIDER_PC_RSSL_MSGS_SENT] <<
		", \"MsgsEnqueued\": " << cumulative_stats_[PROVIDER_PC_RSSL_MSGS_ENQUEUED] <<
		" }";
}

/* 7.2. Establish Network Communication.
 * Open RSSL port and listen for incoming connection attempts.
 */
bool
kigoron::provider_t::Initialize()
{
#ifndef NDEBUG
	RsslBindOptions addr = RSSL_INIT_BIND_OPTS;
#else
	RsslBindOptions addr;
	rsslClearBindOpts (&addr);
#endif
	RsslError rssl_err;

	last_activity_ = boost::posix_time::second_clock::universal_time();

/* RSSL Version Info. */
	if (!upa_->VerifyVersion())
		return false;

/* 9.4.1. Bind server socket. */
	VLOG(3) << "Binding RSSL server socket.";
	addr.serviceName	     = const_cast<char*> (config_.rssl_port.c_str());	// port or service name
	addr.protocolType	     = RSSL_RWF_PROTOCOL_TYPE;
	addr.majorVersion	     = RSSL_RWF_MAJOR_VERSION;
	addr.minorVersion	     = RSSL_RWF_MINOR_VERSION;

	RsslServer* s = rsslBind (&addr, &rssl_err);
/* Hard failure on bind as likely a configuration issue. */
	if (nullptr == s) {
		LOG(ERROR) << "rsslBind: { "
			  "\"rsslErrorId\": " << rssl_err.rsslErrorId << ""
			", \"sysError\": " << rssl_err.sysError << ""
			", \"text\": \"" << rssl_err.text << "\""
			", \"serviceName\": \"" << addr.serviceName << "\""
			", \"protocolType\": \"" << internal::protocol_type_string (addr.protocolType) << "\""
			", \"majorVersion\": " << static_cast<unsigned> (addr.majorVersion) << ""
			", \"minorVersion\": " << static_cast<unsigned> (addr.minorVersion) << ""
			" }";
		return false;
	} else {
		LOG(INFO) << "RSSL server socket created: { "
			  "\"portNumber\": " << s->portNumber << ""
			", \"protocolType\": \"" << internal::protocol_type_string (addr.protocolType) << "\""
			", \"majorVersion\": " << static_cast<unsigned> (addr.majorVersion) << ""
			", \"minorVersion\": " << static_cast<unsigned> (addr.minorVersion) << ""
			", \"socketId\": " << s->socketId << ""
			", \"state\": \"" << internal::channel_state_string (s->state) << "\""
			" }";
		rssl_sock_ = s;
	}

/* Built in HTTPD server. */
	httpd_.reset (new httpd_t());
	if (!(bool)httpd_ || !httpd_->Initialize())
		return false;

	return true;
}

void
kigoron::provider_t::Close()
{
	RsslError rssl_err;
	RsslRet rc;

/* Prevent new applications from connecting. */
	is_accepting_connections_ = false;

/* clients: five pass strategy */
/* 1) Disable new requests via source directory update */
	is_accepting_requests_ = false;
	VLOG_IF(3, clients_.size() > 0) << "Updating source directory image, provider is not accepting new requests.";
	for (auto it = clients_.begin(); it != clients_.end(); ++it) {
		auto client = it->second;
		client->OnSourceDirectoryUpdate();
	}

/* 2) IFF tokens, pump messages until empty. */
	if (nullptr != rssl_sock_ && !clients_.empty())
	{
		FD_ZERO (&in_rfds_); FD_SET (rssl_sock_->socketId, &in_rfds_);
		FD_ZERO (&in_wfds_);
		FD_ZERO (&in_efds_);
		in_nfds_ = out_nfds_ = 0;
		in_tv_.tv_sec = 0;
		in_tv_.tv_usec = 1000 * 100;	// 100ms timeout

		for (;;) {
			bool did_work = DoWork();

			size_t active_tokens = 0;
			for (auto it = clients_.begin(); it != clients_.end(); ++it) {
				auto client = it->second;
				active_tokens += client->tokens().size();
			}
			if (0 == active_tokens) {
				break;
			} else {
				VLOG(3) << "Waiting on " << active_tokens << " active tokens in " << clients_.size() << " active clients.";
			}

			if (did_work)
				continue;

			out_rfds_ = in_rfds_;
			out_wfds_ = in_wfds_;
			out_efds_ = in_efds_;
			out_tv_.tv_sec = in_tv_.tv_sec;
			out_tv_.tv_usec = in_tv_.tv_usec;

			out_nfds_ = select (in_nfds_ + 1, &out_rfds_, &out_wfds_, &out_efds_, &out_tv_);
		}
	}

/* 3) Send session close notification */
	VLOG_IF(3, clients_.size() > 0) << "Closing " << clients_.size() << " client sessions.";
	for (auto it = clients_.begin(); it != clients_.end(); ++it) {
		auto client = it->second;
		client->Close();
/* 4) Flush message stream */
		RsslChannel* c = client->handle();
/* channel still open */
		if ((RSSL_CH_STATE_ACTIVE == c->state) &&
/* data pending */
			FD_ISSET (c->socketId, &in_wfds_))
		{
			do {
				DVLOG(1) << "rsslFlush";
				rc = rsslFlush (c, &rssl_err);
/* flushed */
				if (RSSL_RET_SUCCESS == rc) {
					cumulative_stats_[PROVIDER_PC_RSSL_MSGS_SENT] += client->GetPendingCount();
					client->ClearPendingCount();
					cumulative_stats_[PROVIDER_PC_RSSL_FLUSH]++;
					FD_CLR (c->socketId, &in_wfds_);
					break;
				}
			} while (rc > 0);
/* RSSL failure */
			if (RSSL_RET_SUCCESS != rc) {
				LOG(ERROR) << "rsslFlush: { "
					  "\"rsslErrorId\": " << rssl_err.rsslErrorId << ""
					", \"sysError\": " << rssl_err.sysError << ""
					", \"text\": \"" << rssl_err.text << "\""
					" }";
			}
		}
	}
/* 5) Cleanup */
	clients_.clear();

/* Drop http port. */
	httpd_.reset();

/* Closing listening socket. */
	if (nullptr != rssl_sock_) {
		RsslServerInfo server_info;
		RsslError rssl_err;
		VLOG(3) << "Closing RSSL server socket.";
		VLOG_IF(3, RSSL_RET_SUCCESS == rsslGetServerInfo (rssl_sock_, &server_info, &rssl_err))
			<< "RSSL server summary: {"
			 " \"currentBufferUsage\": " << server_info.currentBufferUsage << ""
			", \"peakBufferUsage\": " << server_info.peakBufferUsage << ""
			" }";
		if (RSSL_RET_SUCCESS != rsslCloseServer (rssl_sock_, &rssl_err)) {
			LOG(ERROR) << "rsslCloseServer: { "
				  "\"rsslErrorId\": " << rssl_err.rsslErrorId << ""
				", \"sysError\": " << rssl_err.sysError << ""
				", \"text\": \"" << rssl_err.text << "\""
				" }";
		}
		rssl_sock_ = nullptr;
	}

/* Close all RSSL client connections. */
	VLOG_IF(3, connections_.size() > 0) << "Closing " << connections_.size() << " client connections.";
	for (auto it = connections_.begin(); it != connections_.end(); ++it) {
		Close (*it);
	}
	connections_.clear();
	VLOG(3) << "Provider closed.";
}

bool
kigoron::provider_t::WriteRawClose (
	uint16_t rwf_version,
	int32_t request_token,
	uint16_t service_id,
	uint8_t model_type,
	const chromium::StringPiece& item_name,
	bool use_attribinfo_in_updates,
	uint8_t stream_state,
	uint8_t status_code,
	const chromium::StringPiece& status_text,
	void* data,
	size_t* length
	)
{
	RsslStatusMsg response = RSSL_INIT_STATUS_MSG;
#ifndef NDEBUG
/* Static initialisation sets all fields rather than only the minimal set
 * required.  Use for debug mode and optimise for release builds.
 */
	RsslEncodeIterator it = RSSL_INIT_ENCODE_ITERATOR;
#else
	RsslEncodeIterator it;
	rsslClearEncodeIterator (&it);
#endif
	RsslBuffer buf = { static_cast<uint32_t> (*length), static_cast<char*> (data) };
	RsslRet rc;

/* 7.5.9.2 Set the message model type of the response. */
	response.msgBase.domainType = model_type;
/* 7.5.9.3 Set response type. */
	response.msgBase.msgClass = RSSL_MC_STATUS;
/* No payload. */
	response.msgBase.containerType = RSSL_DT_NO_DATA;
/* Set the request token. */
	response.msgBase.streamId = request_token;

/* RDM 6.2.3 AttribInfo
 * if the ReqMsg set AttribInfoInUpdates, then the AttribInfo must be provided for all
 * Refresh, Status, and Update RespMsgs.
 */
	if (use_attribinfo_in_updates) {
		DCHECK(!item_name.empty());
		response.msgBase.msgKey.serviceId   = service_id;
		response.msgBase.msgKey.nameType    = RDM_INSTRUMENT_NAME_TYPE_RIC;
		response.msgBase.msgKey.name.data   = const_cast<char*> (item_name.data());
		response.msgBase.msgKey.name.length = static_cast<uint32_t> (item_name.size());
		response.msgBase.msgKey.flags = RSSL_MKF_HAS_SERVICE_ID | RSSL_MKF_HAS_NAME_TYPE | RSSL_MKF_HAS_NAME;
		response.flags |= RSSL_STMF_HAS_MSG_KEY;
	}
	
/* Item interaction state. */
	response.state.streamState = stream_state;
/* Data quality state. */
	response.state.dataState = RSSL_DATA_SUSPECT;
/* 11.2.6.1 Structure Members
 * Note: An application should not trigger specific behavior based on this content
 */
	response.state.code = status_code;
	response.state.text.data = const_cast<char*> (status_text.data()); /* 1-11361563014: text encoding undefined */
	response.state.text.length = static_cast<uint32_t> (status_text.size()); /* Maximum 32,767 bytes */
	response.flags |= RSSL_STMF_HAS_STATE;

	rc = rsslSetEncodeIteratorBuffer (&it, &buf);
	if (RSSL_RET_SUCCESS != rc) {
		LOG(ERROR) << "rsslSetEncodeIteratorBuffer: { "
			  "\"returnCode\": " << static_cast<signed> (rc) << ""
			", \"enumeration\": \"" << rsslRetCodeToString (rc) << "\""
			", \"text\": \"" << rsslRetCodeInfo (rc) << "\""
			" }";
		return false;
	}
	rc = rsslSetEncodeIteratorRWFVersion (&it, rwf_major_version (rwf_version), rwf_minor_version (rwf_version));
	if (RSSL_RET_SUCCESS != rc) {
		LOG(ERROR) << "rsslSetEncodeIteratorRWFVersion: { "
			  "\"returnCode\": " << static_cast<signed> (rc) << ""
			", \"enumeration\": \"" << rsslRetCodeToString (rc) << "\""
			", \"text\": \"" << rsslRetCodeInfo (rc) << "\""
			", \"majorVersion\": " << static_cast<unsigned> (rwf_major_version (rwf_version)) << ""
			", \"minorVersion\": " << static_cast<unsigned> (rwf_minor_version (rwf_version)) << ""
			" }";
		return false;
	}
	rc = rsslEncodeMsg (&it, reinterpret_cast<RsslMsg*> (&response));
	if (RSSL_RET_SUCCESS != rc) {
		LOG(ERROR) << "rsslEncodeMsg: { "
			  "\"returnCode\": " << static_cast<signed> (rc) << ""
			", \"enumeration\": \"" << rsslRetCodeToString (rc) << "\""
			", \"text\": \"" << rsslRetCodeInfo (rc) << "\""
			" }";
		return false;
	}
	buf.length = rsslGetEncodedBufferLength (&it);
	LOG_IF(WARNING, 0 == buf.length) << "rsslGetEncodedBufferLength returned 0.";

	if (DCHECK_IS_ON()) {
/* Message validation. */
		if (!rsslValidateMsg (reinterpret_cast<RsslMsg*> (&response))) {
//			cumulative_stats_[CLIENT_PC_ITEM_CLOSE_MALFORMED]++;
			LOG(ERROR) << "rsslValidateMsg failed.";
			return false;
		} else {
//			cumulative_stats_[CLIENT_PC_ITEM_CLOSE_VALIDATED]++;
			DVLOG(4) << "rsslValidateMsg succeeded.";
		}
	}
	*length = static_cast<size_t> (buf.length);
	return true;
}

bool
kigoron::provider_t::SendReply (
	RsslChannel*const handle,
	int32_t token,
	const void* data,
	size_t length
	)
{
	boost::shared_lock<boost::shared_mutex> lock (clients_lock_);
	auto client = clients_.find (handle);
	lock.unlock();
/* client may have disconnected before reply is available. */
	if (clients_.end() != client)
		return client->second->SendReply (token, data, length);
	else
		return false;
}

void
kigoron::provider_t::Run()
{
	DCHECK(keep_running_) << "Quit must have been called outside of Run!";

	FD_ZERO (&in_rfds_); FD_SET (rssl_sock_->socketId, &in_rfds_); FD_ZERO (&out_rfds_);
	FD_ZERO (&in_wfds_); FD_ZERO (&out_wfds_);
	FD_ZERO (&in_efds_); FD_ZERO (&out_efds_);
	in_nfds_ = out_nfds_ = 0;
	in_tv_.tv_sec = 0;
	in_tv_.tv_usec = 1000 * 100;	// 100ms timeout

	for (;;) {
		bool did_work = DoWork();

		if (!keep_running_)
			break;

		if (did_work)
			continue;

/* Reset fd state */
		out_rfds_ = in_rfds_;
		out_wfds_ = in_wfds_;
		out_efds_ = in_efds_;
		out_tv_.tv_sec = in_tv_.tv_sec;
		out_tv_.tv_usec = in_tv_.tv_usec;

		out_nfds_ = select (in_nfds_ + 1, &out_rfds_, &out_wfds_, &out_efds_, &out_tv_);
	}

	keep_running_ = true;
}

bool
kigoron::provider_t::DoWork()
{
	bool did_work = false;

	last_activity_ = boost::posix_time::second_clock::universal_time();

/* Only check keepalives on timeout */
	if (out_nfds_ <= 0)
	{
		for (auto it = connections_.begin(); it != connections_.end();) {
			RsslChannel* c = *it;
			if (nullptr != c->userSpecPtr && RSSL_CH_STATE_ACTIVE == c->state) {
				auto client = reinterpret_cast<client_t*> (c->userSpecPtr);
				if (last_activity_ >= client->NextPing()) {
					Ping (c);
				}
				if (last_activity_ >= client->NextPong()) {
					cumulative_stats_[PROVIDER_PC_RSSL_PONG_TIMEOUT]++;
					LOG(ERROR) << "Pong timeout from peer, aborting connection.";
					Abort (c);
				}
			}
			if (FD_ISSET (c->socketId, &out_efds_)) {
				cumulative_stats_[PROVIDER_PC_CONNECTION_EXCEPTION]++;
				DVLOG(3) << "Socket exception.";
/* Remove connection from list */
				auto jt = it++;
				connections_.erase (jt);
/* Remove client from map */
				{
					boost::lock_guard<boost::shared_mutex> lock (clients_lock_);
					auto kt = clients_.find (c);
					if (clients_.end() != kt)
						clients_.erase (kt);
				}
/* Remove RSSL socket from further event notification */
				FD_CLR (c->socketId, &in_rfds_);
				FD_CLR (c->socketId, &in_wfds_);
				FD_CLR (c->socketId, &in_efds_);
/* Ensure RSSL has closed out */
				if (RSSL_CH_STATE_CLOSED != c->state)
					Close (c);
			} else {
				++it;
			}
		}
		return false;
	}

/* New client connection */
	if (FD_ISSET (rssl_sock_->socketId, &out_rfds_)) {
		FD_CLR (rssl_sock_->socketId, &out_rfds_);
		OnConnection (rssl_sock_);
		did_work = true;
	}

/* Iterate over client set */
	for (auto it = connections_.begin(); it != connections_.end();) {
		RsslChannel* c = *it;
/* incoming */
		if (FD_ISSET (c->socketId, &out_rfds_)) {
			FD_CLR (c->socketId, &out_rfds_);
			OnCanReadWithoutBlocking (c);
			did_work = true;
		}
/* outgoing */
		if (FD_ISSET (c->socketId, &out_wfds_)) {
			FD_CLR (c->socketId, &out_wfds_);
			OnCanWriteWithoutBlocking (c);
			did_work = true;
		}
/* Keepalive timeout on active session above connection */
		if (nullptr != c->userSpecPtr && RSSL_CH_STATE_ACTIVE == c->state) {
			auto client = reinterpret_cast<client_t*> (c->userSpecPtr);
			if (last_activity_ >= client->NextPing()) {
				Ping (c);
			}
			if (last_activity_ >= client->NextPong()) {
				cumulative_stats_[PROVIDER_PC_RSSL_PONG_TIMEOUT]++;
				LOG(ERROR) << "Pong timeout from peer, aborting connection.";
				Abort (c);
			}
		}
/* disconnects */
		if (FD_ISSET (c->socketId, &out_efds_)) {
			cumulative_stats_[PROVIDER_PC_CONNECTION_EXCEPTION]++;
			DVLOG(3) << "Socket exception.";
/* Remove connection from list */
			auto jt = it++;
			connections_.erase (jt);
/* Remove client from map */
			{
				boost::lock_guard<boost::shared_mutex> lock (clients_lock_);
				auto kt = clients_.find (c);
				if (clients_.end() != kt)
					clients_.erase (kt);
			}
/* Remove RSSL socket from further event notification */
			FD_CLR (c->socketId, &in_rfds_);
			FD_CLR (c->socketId, &in_wfds_);
			FD_CLR (c->socketId, &in_efds_);
/* Ensure RSSL has closed out */
			if (RSSL_CH_STATE_CLOSED != c->state)
				Close (c);
		} else {
			++it;
		}
	}
	return did_work;
}

void
kigoron::provider_t::Quit()
{
	keep_running_ = false;
}

/* 7.2. Establish Network Communication.
 * When an OMM consumer application attempts to connection begin the initialization process.
 */
void
kigoron::provider_t::OnConnection (
	RsslServer* rssl_sock
	)
{
	DCHECK (nullptr != rssl_sock);
	cumulative_stats_[PROVIDER_PC_CONNECTION_RECEIVED]++;
	if (!is_accepting_connections_ || connections_.size() == config_.session_capacity)
		RejectConnection (rssl_sock);
	else
		AcceptConnection (rssl_sock);
}

void
kigoron::provider_t::RejectConnection (
	RsslServer* rssl_sock
	)
{
#ifndef NDEBUG
	RsslAcceptOptions addr = RSSL_INIT_ACCEPT_OPTS;
#else
	RsslAcceptOptions addr;
	rsslClearAcceptOpts (&addr);
#endif
	RsslError rssl_err;

	DCHECK (nullptr != rssl_sock);
	VLOG(2) << "Rejecting new connection request.";

	addr.nakMount = RSSL_TRUE;
	RsslChannel* c = rsslAccept (rssl_sock, &addr, &rssl_err);
	if (nullptr == c) {
		LOG(ERROR) << "rsslAccept: { "
			  "\"rsslErrorId\": " << rssl_err.rsslErrorId << ""
			", \"sysError\": " << rssl_err.sysError << ""
			", \"text\": \"" << rssl_err.text << "\""
			", \"nakMount\": " << (addr.nakMount ? "true" : "false") << ""
			" }";
	}
	cumulative_stats_[PROVIDER_PC_CONNECTION_REJECTED]++;
}

void
kigoron::provider_t::AcceptConnection (
	RsslServer* rssl_sock
	)
{
#ifndef NDEBUG
	RsslAcceptOptions addr = RSSL_INIT_ACCEPT_OPTS;
#else
	RsslAcceptOptions addr;
	rsslClearAcceptOpts (&addr);
#endif
	RsslError rssl_err;

	DCHECK (nullptr != rssl_sock);
	VLOG(2) << "Accepting new connection request.";

	addr.nakMount = RSSL_FALSE;
	RsslChannel* c = rsslAccept (rssl_sock, &addr, &rssl_err);
	if (nullptr == c) {
		LOG(ERROR) << "rsslAccept: { "
			  "\"rsslErrorId\": " << rssl_err.rsslErrorId << ""
			", \"sysError\": " << rssl_err.sysError << ""
			", \"text\": \"" << rssl_err.text << "\""
			", \"nakMount\": " << (addr.nakMount ? "true" : "false") << ""
			" }";
	} else {
/* Add to directory of all client connections */
		connections_.emplace_back (c);

/* Wait for client session */
		FD_SET (c->socketId, &in_rfds_);
		FD_SET (c->socketId, &in_efds_);

		cumulative_stats_[PROVIDER_PC_CONNECTION_ACCEPTED]++;

		std::stringstream client_hostname, client_ip;
		if (nullptr == c->clientHostname) 
			client_hostname << "null";
		else	
			client_hostname << '"' << c->clientHostname << '"';
		if (nullptr == c->clientIP)	
			client_ip << "null";
		else	
			client_ip << '"' << c->clientIP << '"';

		LOG(INFO) << "RSSL client socket created: { "
			  "\"clientHostname\": " << client_hostname.str() << ""
			", \"clientIP\": " << client_ip.str() << ""
			", \"connectionType\": \"" << internal::connection_type_string (c->connectionType) << "\""
			", \"majorVersion\": " << static_cast<unsigned> (c->majorVersion) << ""
			", \"minorVersion\": " << static_cast<unsigned> (c->minorVersion) << ""
			", \"pingTimeout\": " << c->pingTimeout << ""
			", \"protocolType\": \"" << internal::protocol_type_string (c->protocolType) << "\""
			", \"socketId\": " << c->socketId << ""
			", \"state\": \"" << internal::channel_state_string (c->state) << "\""
			" }";
	}
}

void
kigoron::provider_t::OnCanReadWithoutBlocking (
	RsslChannel* c
	)
{
	DCHECK (nullptr != c);
	switch (c->state) {
	case RSSL_CH_STATE_CLOSED:
		LOG(INFO) << "socket state is closed.";
/* Raise internal exception flags to remove socket */
		Abort (c);
		break;
	case RSSL_CH_STATE_INACTIVE:
		LOG(INFO) << "socket state is inactive.";
		break;
	case RSSL_CH_STATE_INITIALIZING:
		LOG(INFO) << "socket state is initializing.";
		OnInitializingState (c);
		break;
	case RSSL_CH_STATE_ACTIVE:
		OnActiveState (c);
		break;

	default:
		LOG(ERROR) << "socket state is unknown.";
		break;
	}
}

void
kigoron::provider_t::OnInitializingState (
	RsslChannel* c
	)
{
	RsslInProgInfo state;
	RsslError rssl_err;
	RsslRet rc;

	DCHECK (nullptr != c);

/* In place of absent API: rsslClearError (&rssl_err); */
	rssl_err.rsslErrorId = 0;
	rssl_err.sysError = 0;
	rssl_err.text[0] = '\0';

	rc = rsslInitChannel (c, &state, &rssl_err);
	switch (rc) {
	case RSSL_RET_CHAN_INIT_IN_PROGRESS:
		if ((state.flags & RSSL_IP_FD_CHANGE) == RSSL_IP_FD_CHANGE) {
			cumulative_stats_[PROVIDER_PC_RSSL_PROTOCOL_DOWNGRADE]++;
			LOG(INFO) << "RSSL protocol downgrade, reconnected.";
			FD_CLR (state.oldSocket, &in_rfds_); FD_CLR (state.oldSocket, &in_efds_);
			FD_SET (c->socketId, &in_rfds_); FD_SET (c->socketId, &in_efds_);
		} else {
			LOG(INFO) << "RSSL connection in progress.";
		}
		break;
	case RSSL_RET_SUCCESS:
		OnActiveClientSession (c);
		FD_SET (c->socketId, &in_rfds_); FD_SET (c->socketId, &in_efds_);
		break;
	default:
		LOG(ERROR) << "rsslInitChannel: { "
			  "\"rsslErrorId\": " << rssl_err.rsslErrorId << ""
			", \"sysError\": " << rssl_err.sysError << ""
			", \"text\": \"" << rssl_err.text << "\""
			" }";
		break;
	}
}

void
kigoron::provider_t::OnCanWriteWithoutBlocking (
	RsslChannel* c
	)
{
	RsslError rssl_err;
	RsslRet rc;

	DCHECK (nullptr != c);

/* In place of absent API: rsslClearError (&rssl_err); */
	rssl_err.rsslErrorId = 0;
	rssl_err.sysError = 0;
	rssl_err.text[0] = '\0';

	DVLOG(1) << "rsslFlush";
	rc = rsslFlush (c, &rssl_err);
	if (RSSL_RET_SUCCESS == rc) {
		cumulative_stats_[PROVIDER_PC_RSSL_FLUSH]++;
		FD_CLR (c->socketId, &in_wfds_);
/* Sent data equivalent to a ping. */
		if (nullptr != c->userSpecPtr) {
			auto client = reinterpret_cast<client_t*> (c->userSpecPtr);
			cumulative_stats_[PROVIDER_PC_RSSL_MSGS_SENT] += client->GetPendingCount();
			client->ClearPendingCount();
			client->SetNextPing (last_activity_ + boost::posix_time::seconds (client->ping_interval_));
		}
	} else if (rc > 0) {
		DVLOG(1) << static_cast<signed> (rc) << " bytes pending.";
	} else {
		LOG(ERROR) << "rsslFlush: { "
			  "\"rsslErrorId\": " << rssl_err.rsslErrorId << ""
			", \"sysError\": " << rssl_err.sysError << ""
			", \"text\": \"" << rssl_err.text << "\""
			" }";
	}
}

void
kigoron::provider_t::Abort (
	RsslChannel* c
	)
{
	DCHECK (nullptr != c);
	FD_CLR (c->socketId, &out_rfds_);
	FD_CLR (c->socketId, &out_wfds_);
	FD_SET (c->socketId, &out_efds_);
}

void
kigoron::provider_t::Close (
	RsslChannel* c
	)
{
	RsslError rssl_err;

	DCHECK (nullptr != c);

	LOG(INFO) << "Closing RSSL connection.";
	if (RSSL_RET_SUCCESS != rsslCloseChannel (c, &rssl_err)) {
		LOG(WARNING) << "rsslCloseChannel: { "
			  "\"rsslErrorId\": " << rssl_err.rsslErrorId << ""
			", \"sysError\": " << rssl_err.sysError << ""
			", \"text\": \"" << rssl_err.text << "\""
			" }";
	}
}

/* Handling Consumer Client Session Events: New client session request.
 *
 * There are many reasons why a provider might reject a connection. For
 * example, it might have a maximum supported number of connections.
 */
void
kigoron::provider_t::OnActiveClientSession (
	RsslChannel* c
	)
{
	DCHECK (nullptr != c);
	cumulative_stats_[PROVIDER_PC_OMM_ACTIVE_CLIENT_SESSION_RECEIVED]++;
	try {
		auto handle = c;
		const auto address = c->clientIP;
		boost::shared_lock<boost::shared_mutex> lock (clients_lock_);
		const auto connection_count = clients_.size();
		lock.unlock();
		if (!is_accepting_connections_ || connection_count == config_.session_capacity)
			RejectClientSession (handle, address);
		else if (!AcceptClientSession (handle, address))
			RejectClientSession (handle, address);
/* ignore any error */
	} catch (const std::exception& e) {
		cumulative_stats_[PROVIDER_PC_OMM_ACTIVE_CLIENT_SESSION_EXCEPTION]++;
		LOG(ERROR) << "Exception: { "
			"\"What\": \"" << e.what() << "\""
			" }";
	}
}

void
kigoron::provider_t::OnActiveState (
	RsslChannel* c
	)
{
	RsslBuffer* buf;
	RsslReadInArgs in_args;
	RsslReadOutArgs out_args;
	RsslError rssl_err;
	RsslRet rc;

	DCHECK (nullptr != c);

	rsslClearReadInArgs (&in_args);

	if (logging::DEBUG_MODE) {
		rsslClearReadOutArgs (&out_args);
/* In place of absent API: rsslClearError (&rssl_err); */
		rssl_err.rsslErrorId = 0;
		rssl_err.sysError = 0;
		rssl_err.text[0] = '\0';
	}
	buf = rsslReadEx (c, &in_args, &out_args, &rc, &rssl_err);
	if (logging::DEBUG_MODE) {
		std::stringstream return_code;
		if (rc > 0) {
			return_code << "\"pendingBytes\": " << static_cast<signed> (rc);
		} else {
			return_code << "\"returnCode\": \"" << static_cast<signed> (rc) << ""
				     ", \"enumeration\": \"" << rsslRetCodeToString (rc) << "\"";
		}
		VLOG(1) << "rsslReadEx: { "
			  << return_code.str() << ""
			", \"bytesRead\": " << out_args.bytesRead << ""
			", \"uncompressedBytesRead\": " << out_args.uncompressedBytesRead << ""
			", \"rsslErrorId\": " << rssl_err.rsslErrorId << ""
			", \"sysError\": " << rssl_err.sysError << ""
			", \"text\": \"" << rssl_err.text << "\""
			" }";
	}

	cumulative_stats_[PROVIDER_PC_BYTES_RECEIVED] += out_args.bytesRead;
	cumulative_stats_[PROVIDER_PC_UNCOMPRESSED_BYTES_RECEIVED] += out_args.uncompressedBytesRead;

	switch (rc) {
/* Reliable multicast events with hard-fail override. */
	case RSSL_RET_CONGESTION_DETECTED:
		cumulative_stats_[PROVIDER_PC_RSSL_CONGESTION_DETECTED]++;
		goto check_closed_state;
	case RSSL_RET_SLOW_READER:
		cumulative_stats_[PROVIDER_PC_RSSL_SLOW_READER]++;
		goto check_closed_state;
	case RSSL_RET_PACKET_GAP_DETECTED:
		cumulative_stats_[PROVIDER_PC_RSSL_PACKET_GAP_DETECTED]++;
		goto check_closed_state;
check_closed_state:
		if (RSSL_CH_STATE_CLOSED != c->state) {
			LOG(WARNING) << "rsslReadEx: { "
				  "\"rsslErrorId\": " << rssl_err.rsslErrorId << ""
				", \"sysError\": " << rssl_err.sysError << ""
				", \"text\": \"" << rssl_err.text << "\""
				" }";
			break;
		}
	case RSSL_RET_READ_FD_CHANGE:
		cumulative_stats_[PROVIDER_PC_RSSL_RECONNECT]++;
		LOG(INFO) << "RSSL reconnected.";
		FD_CLR (c->oldSocketId, &in_rfds_); FD_CLR (c->oldSocketId, &in_efds_);
		FD_SET (c->socketId, &in_rfds_); FD_SET (c->socketId, &in_efds_);
		break;
	case RSSL_RET_READ_PING:
		cumulative_stats_[PROVIDER_PC_RSSL_PONG_RECEIVED]++;
		if (nullptr != c->userSpecPtr) {
			auto client = reinterpret_cast<client_t*> (c->userSpecPtr);
			client->SetNextPong (last_activity_ + boost::posix_time::seconds (c->pingTimeout));
		}
		DVLOG(1) << "RSSL pong.";
		break;
	case RSSL_RET_FAILURE:
		cumulative_stats_[PROVIDER_PC_RSSL_READ_FAILURE]++;
		LOG(ERROR) << "rsslReadEx: { "
			  "\"rsslErrorId\": " << rssl_err.rsslErrorId << ""
			", \"sysError\": " << rssl_err.sysError << ""
			", \"text\": \"" << rssl_err.text << "\""
			" }";
		break;
/* It is possible for rsslRead to succeed and return a NULL buffer. When this
 * occurs, it indicates that a portion of a fragmented buffer has been
 * received. The RSSL Reliable Transport is internally reassembling all parts
 * of the fragmented buffer and the entire buffer will be returned to the user
 * through rsslRead upon the arrival of the last fragment.
 */
	case RSSL_RET_SUCCESS:
	default: 
		if (nullptr != buf) {
			cumulative_stats_[PROVIDER_PC_RSSL_MSGS_RECEIVED]++;
			OnMsg (c, buf);
/* Received data equivalent to a heartbeat pong. */
			if (nullptr != c->userSpecPtr) {
				auto client = reinterpret_cast<client_t*> (c->userSpecPtr);
				client->SetNextPong (last_activity_ + boost::posix_time::seconds (c->pingTimeout));
			}
		}
		if (rc > 0) {
/* pending buffer needs flushing out before IO notification can resume */
			FD_SET (c->socketId, &out_rfds_);
		}
		break;
	}
}

void
kigoron::provider_t::OnMsg (
	RsslChannel* handle,
	RsslBuffer* buf		/* nullptr indicates a partially received fragmented message and thus invalid for processing */
	)
{
#ifndef NDEBUG
	RsslDecodeIterator it = RSSL_INIT_DECODE_ITERATOR;
	RsslMsg msg = RSSL_INIT_MSG;
#else
	RsslDecodeIterator it;
	RsslMsg msg;
	rsslClearDecodeIterator (&it);
	rsslClearMsg (&msg);
#endif
	RsslRet rc;

	DCHECK(handle != nullptr);
	DCHECK(buf != nullptr);

/* Prepare codec */
	rc = rsslSetDecodeIteratorRWFVersion (&it, handle->majorVersion, handle->minorVersion);
	if (RSSL_RET_SUCCESS != rc) {
/* Unsupported version or internal error, close out the connection. */
		cumulative_stats_[PROVIDER_PC_RWF_VERSION_UNSUPPORTED]++;
		Abort (handle);
		LOG(ERROR) << "rsslSetDecodeIteratorRWFVersion: { "
			  "\"returnCode\": " << static_cast<signed> (rc) << ""
			", \"enumeration\": \"" << rsslRetCodeToString (rc) << "\""
			", \"text\": \"" << rsslRetCodeInfo (rc) << "\""
			", \"majorVersion\": " << static_cast<unsigned> (handle->majorVersion) << ""
			", \"minorVersion\": " << static_cast<unsigned> (handle->minorVersion) << ""
			" }";
		return;
	}
	rc = rsslSetDecodeIteratorBuffer (&it, buf);
	if (RSSL_RET_SUCCESS != rc) {
/* Invalid buffer or internal error, discard the message. */
		cumulative_stats_[PROVIDER_PC_RSSL_MSGS_MALFORMED]++;
		Abort (handle);
		LOG(ERROR) << "rsslSetDecodeIteratorBuffer: { "
			  "\"returnCode\": " << static_cast<signed> (rc) << ""
			", \"enumeration\": \"" << rsslRetCodeToString (rc) << "\""
			", \"text\": \"" << rsslRetCodeInfo (rc) << "\""
			" }";
		return;
	}

/* Decode data buffer into RSSL message */
	rc = rsslDecodeMsg (&it, &msg);
	if (RSSL_RET_SUCCESS != rc) {
		cumulative_stats_[PROVIDER_PC_RSSL_MSGS_MALFORMED]++;
		Abort (handle);
		LOG(WARNING) << "rsslDecodeMsg: { "
			  "\"returnCode\": " << static_cast<signed> (rc) << ""
			", \"enumeration\": \"" << rsslRetCodeToString (rc) << "\""
			", \"text\": \"" << rsslRetCodeInfo (rc) << "\""
			" }";
		return;
	} else {
		cumulative_stats_[PROVIDER_PC_RSSL_MSGS_DECODED]++;
		if (logging::DEBUG_MODE) {
/* Pass through RSSL validation and report exceptions */
			if (!rsslValidateMsg (&msg)) {
				cumulative_stats_[PROVIDER_PC_RSSL_MSGS_MALFORMED]++;
				LOG(WARNING) << "rsslValidateMsg failed.";
				Abort (handle);
				return;
			} else {
				cumulative_stats_[PROVIDER_PC_RSSL_MSGS_VALIDATED]++;
				DVLOG(4) << "rsslValidateMsg success.";
			}
			DVLOG(3) << msg;
		}
		DCHECK (nullptr != handle->userSpecPtr);
		auto client = reinterpret_cast<client_t*> (handle->userSpecPtr);
		if (!client->OnMsg (last_activity_, &it, &msg))
			Abort (handle);
	}
}

void
kigoron::provider_t::RejectClientSession (
	RsslChannel* handle,
	const char* address
	)
{
	VLOG(2) << "Rejecting new client session request: { \"Address\": \"" << address << "\" }";
		
/* Closing down a client session. */
	Close (handle);
	cumulative_stats_[PROVIDER_PC_CLIENT_SESSION_REJECTED]++;
}

bool
kigoron::provider_t::AcceptClientSession (
	RsslChannel* handle,
	const char* address
	)
{
	VLOG(2) << "Accepting new client session request: { \"Address\": \"" << address << "\" }";

	auto client = std::make_shared<client_t> (last_activity_, shared_from_this(), request_delegate_, handle, address);
	if (!(bool)client || !client->Initialize()) {
		cumulative_stats_[PROVIDER_PC_CLIENT_INIT_EXCEPTION]++;
		LOG(ERROR) << "Client session initialisation failed, aborting connection.";
		return false;
	}

/* Associate RSSL socket with smart pointer. */
	handle->userSpecPtr = client.get();

/* Determine lowest common Reuters Wire Format (RWF) version */
	const uint16_t client_rwf_version = client->rwf_version();
	if (0 == min_rwf_version_)
	{
		LOG(INFO) << "Setting RWF: { "
				  "\"MajorVersion\": " << static_cast<unsigned> (rwf_major_version (client_rwf_version)) <<
				", \"MinorVersion\": " << static_cast<unsigned> (rwf_minor_version (client_rwf_version)) <<
				" }";
		min_rwf_version_.store (client_rwf_version);
	}
	else if (min_rwf_version_ > client_rwf_version)
	{
		LOG(INFO) << "Degrading RWF: { "
				  "\"MajorVersion\": " << static_cast<unsigned> (rwf_major_version (client_rwf_version)) <<
				", \"MinorVersion\": " << static_cast<unsigned> (rwf_minor_version (client_rwf_version)) <<
				" }";
		min_rwf_version_.store (client_rwf_version);
	}

	boost::lock_guard<boost::shared_mutex> lock (clients_lock_);
	clients_.emplace (std::make_pair (handle, client));
	cumulative_stats_[PROVIDER_PC_CLIENT_SESSION_ACCEPTED]++;
	return true;
}

/* 7.3.5.5 Making Request for Service Directory
 * By default, information about all available services is returned. If an
 * application wishes to make a request for information pertaining to a 
 * specific service only, it can use the setServiceName() method of the request
 * AttribInfo.
 *
 * The setDataMask() method accepts a bit mask that determines what information
 * is returned for each service. The bit mask values for the Service Filter are
 * defined in Include/RDM/RDM.h. The data associated with each specified bit 
 * mask value is returned in a separate ElementList contained in a FilterEntry.
 * The ServiceInfo ElementList contains the name and capabilities of the 
 * source. The ServiceState ElementList contains information related to the 
 * availability of the service.
 *
 * On failure encoding is not rolled back to original state.
 */
bool
kigoron::provider_t::GetDirectoryMap (
	RsslEncodeIterator*const it,
	const char* service_name,	/* nullptr for all services */
	uint32_t filter_mask,
	unsigned map_action
	)
{
#ifndef NDEBUG
	RsslMap map = RSSL_INIT_MAP;
	RsslMapEntry map_entry = RSSL_INIT_MAP_ENTRY;
#else
	RsslMap map;
	RsslMapEntry map_entry;
	rsslClearMap (&map);
	rsslClearMapEntry (&map_entry);
#endif
	RsslRet rc;

	DCHECK(nullptr != it);

	map.keyPrimitiveType = RSSL_DT_UINT;
	map.containerType    = RSSL_DT_FILTER_LIST;
	rc = rsslEncodeMapInit (it, &map, 0 /* summary data */, 0 /* payload */);
	if (RSSL_RET_SUCCESS != rc) {
		cumulative_stats_[PROVIDER_PC_DIRECTORY_MAP_EXCEPTION]++;
		LOG(ERROR) << "rsslEncodeMapInit: { "
			  "\"returnCode\": " << static_cast<signed> (rc) << ""
			", \"enumeration\": \"" << rsslRetCodeToString (rc) << "\""
			", \"text\": \"" << rsslRetCodeInfo (rc) << "\""
			", \"keyPrimitiveType\": \"" << internal::primitive_type_string (map.keyPrimitiveType) << "\""
			", \"containerType\": \"" << internal::container_type_string (map.containerType) << "\""
			" }";
		return false;
	}
	map_entry.action     = map_action;
	const uint64_t service_id = this->service_id();
	if (0 == service_id) {
		cumulative_stats_[PROVIDER_PC_DIRECTORY_MAP_EXCEPTION]++;
		LOG(ERROR) << "Service ID undefined for this provider, cannot generate directory map.";
		return false;
	}
	rc = rsslEncodeMapEntryInit (it, &map_entry, &service_id, 0);
	if (RSSL_RET_SUCCESS != rc) {
		cumulative_stats_[PROVIDER_PC_DIRECTORY_MAP_EXCEPTION]++;
		LOG(ERROR) << "rsslEncodeMapEntryInit: { "
			  "\"returnCode\": " << static_cast<signed> (rc) << ""
			", \"enumeration\": \"" << rsslRetCodeToString (rc) << "\""
			", \"text\": \"" << rsslRetCodeInfo (rc) << "\""
			", \"action\": \"" << internal::map_entry_action_string (static_cast<RsslMapEntryActions> (map_entry.action)) << "\""
			", \"serviceId\": " << service_id << ""
			" }";
		return false;
	}
	if (!GetServiceDirectory (it, service_name, filter_mask)) {
		cumulative_stats_[PROVIDER_PC_DIRECTORY_MAP_EXCEPTION]++;
		LOG(ERROR) << "GetServiceDirectory failed.";
		return false;
	}
	rc = rsslEncodeMapEntryComplete (it, RSSL_TRUE /* commit */);
	if (RSSL_RET_SUCCESS != rc) {
		cumulative_stats_[PROVIDER_PC_DIRECTORY_MAP_EXCEPTION]++;
		LOG(ERROR) << "rsslEncodeMapEntryComplete: { "
			  "\"returnCode\": " << static_cast<signed> (rc) << ""
			", \"enumeration\": \"" << rsslRetCodeToString (rc) << "\""
			", \"text\": \"" << rsslRetCodeInfo (rc) << "\""
			" }";
		return false;
	}
	rc = rsslEncodeMapComplete (it, RSSL_TRUE /* commit */);
	if (RSSL_RET_SUCCESS != rc) {
		cumulative_stats_[PROVIDER_PC_DIRECTORY_MAP_EXCEPTION]++;
		LOG(ERROR) << "rsslEncodeMapComplete: { "
			  "\"returnCode\": " << static_cast<signed> (rc) << ""
			", \"enumeration\": \"" << rsslRetCodeToString (rc) << "\""
			", \"text\": \"" << rsslRetCodeInfo (rc) << "\""
			" }";
		return false;
	}
	return true;
}

/* Populate map with service directory, must be cleared before call.
 */
bool
kigoron::provider_t::GetServiceDirectory (
	RsslEncodeIterator*const it,
	const char* service_name,	/* nullptr for all services */
	uint32_t filter_mask
	)
{
	DCHECK(nullptr != it);
/* Request service filter does not match provided service. */
	if (nullptr != service_name && 0 != this->service_name().compare (service_name))
	{
		LOG(ERROR) << "Service filter \"" << service_name << "\" does not match service directory \"" << this->service_name() << "\".";
		return false;
	}
	if (!GetServiceFilterList (it, filter_mask)) {
		LOG(ERROR) << "GetServiceFilterList failed.";
		return false;
	}
	return true;
}

bool
kigoron::provider_t::GetServiceFilterList (
	RsslEncodeIterator*const it,
	uint32_t filter_mask
	)
{
#ifndef NDEBUG
	RsslFilterList filter_list = RSSL_INIT_FILTER_LIST;
#else
	RsslFilterList filter_list;
	rsslClearFilterList (&filter_list);
#endif
	RsslRet rc;

	DCHECK(nullptr != it);

/* Determine entry count for encoder hinting */
	const bool use_info_filter  = (0 != (filter_mask & RDM_DIRECTORY_SERVICE_INFO_FILTER));
	const bool use_state_filter = (0 != (filter_mask & RDM_DIRECTORY_SERVICE_STATE_FILTER));
	const bool use_load_filter  = (0 != (filter_mask & RDM_DIRECTORY_SERVICE_LOAD_FILTER));
	const unsigned filter_count = (use_info_filter ? 1 : 0) 
				    + (use_state_filter ? 1 : 0)
				    + (use_load_filter ? 1 : 0);
	
/* 5.3.8 Encoding with a SingleWriteIterator
 * Re-use of SingleWriteIterator permitted cross MapEntry and FieldList.
 */
	filter_list.flags	   = RSSL_FTF_HAS_TOTAL_COUNT_HINT;
	filter_list.containerType  = RSSL_DT_ELEMENT_LIST;
	filter_list.totalCountHint = filter_count;
	rc = rsslEncodeFilterListInit (it, &filter_list);
	if (RSSL_RET_SUCCESS != rc) {
		LOG(ERROR) << "rsslEncodeFilterListInit: { "
			  "\"returnCode\": " << static_cast<signed> (rc) << ""
			", \"enumeration\": \"" << rsslRetCodeToString (rc) << "\""
			", \"text\": \"" << rsslRetCodeInfo (rc) << "\""
			", \"flags\": \"RSSL_FTF_HAS_TOTAL_COUNT_HINT\""
			", \"containerType\": \"" << internal::container_type_string (filter_list.containerType) << "\""
			", \"totalCountHint\": " << filter_list.totalCountHint << ""
			" }";
		return false;
	}

	if (use_info_filter) {
#ifndef NDEBUG
		RsslFilterEntry filter_entry = RSSL_INIT_FILTER_ENTRY;
#else
		RsslFilterEntry filter_entry;
		rsslClearFilterEntry (&filter_entry);
#endif
		filter_entry.id      = RDM_DIRECTORY_SERVICE_INFO_ID;
		filter_entry.action  = RSSL_FTEA_SET_ENTRY;
		rc = rsslEncodeFilterEntryInit (it, &filter_entry, 0 /* size */);
		if (RSSL_RET_SUCCESS != rc) {
			LOG(ERROR) << "rsslEncodeFilterEntryInit: { "
				  "\"returnCode\": " << static_cast<signed> (rc) << ""
				", \"enumeration\": \"" << rsslRetCodeToString (rc) << "\""
				", \"text\": \"" << rsslRetCodeInfo (rc) << "\""
				", \"id\": \"" << internal::filter_entry_id_string (static_cast<RDMDirectoryServiceFilterIds> (filter_entry.id)) << "\""
				", \"action\": \"" << internal::filter_entry_action_string (static_cast<RsslFilterEntryActions> (filter_entry.action)) << "\""
				" }";
			return false;
		}
		if (!GetServiceInformation (it)) {
			LOG(ERROR) << "GetServiceInformation failed.";
			return false;
		}
		rc = rsslEncodeFilterEntryComplete (it, RSSL_TRUE /* commit */);
		if (RSSL_RET_SUCCESS != rc) {
			LOG(ERROR) << "rsslEncodeFilterEntryComplete: { "
				  "\"returnCode\": " << static_cast<signed> (rc) << ""
				", \"enumeration\": \"" << rsslRetCodeToString (rc) << "\""
				", \"text\": \"" << rsslRetCodeInfo (rc) << "\""
				" }";
			return false;
		}
	}
	if (use_state_filter) {
#ifndef NDEBUG
		RsslFilterEntry filter_entry = RSSL_INIT_FILTER_ENTRY;
#else
		RsslFilterEntry filter_entry;
		rsslClearFilterEntry (&filter_entry);
#endif
		filter_entry.id      = RDM_DIRECTORY_SERVICE_STATE_ID;
		filter_entry.action  = RSSL_FTEA_SET_ENTRY;
		rc = rsslEncodeFilterEntryInit (it, &filter_entry, 0 /* size */);
		if (RSSL_RET_SUCCESS != rc) {
			LOG(ERROR) << "rsslEncodeFilterEntryInit: { "
				  "\"returnCode\": " << static_cast<signed> (rc) << ""
				", \"enumeration\": \"" << rsslRetCodeToString (rc) << "\""
				", \"text\": \"" << rsslRetCodeInfo (rc) << "\""
				", \"id\": \"" << internal::filter_entry_id_string (static_cast<RDMDirectoryServiceFilterIds> (filter_entry.id)) << "\""
				", \"action\": \"" << internal::filter_entry_action_string (static_cast<RsslFilterEntryActions> (filter_entry.action)) << "\""
				" }";
			return false;
		}
		if (!GetServiceState (it)) {
			LOG(ERROR) << "GetServiceState failed.";
			return false;
		}
		rc = rsslEncodeFilterEntryComplete (it, RSSL_TRUE /* commit */);
		if (RSSL_RET_SUCCESS != rc) {
			LOG(ERROR) << "rsslEncodeFilterEntryComplete: { "
				  "\"returnCode\": " << static_cast<signed> (rc) << ""
				", \"enumeration\": \"" << rsslRetCodeToString (rc) << "\""
				", \"text\": \"" << rsslRetCodeInfo (rc) << "\""
				" }";
			return false;
		}
	}
	if (use_load_filter) {
#ifndef NDEBUG
		RsslFilterEntry filter_entry = RSSL_INIT_FILTER_ENTRY;
#else
		RsslFilterEntry filter_entry;
		rsslClearFilterEntry (&filter_entry);
#endif
		filter_entry.id      = RDM_DIRECTORY_SERVICE_LOAD_ID;
		filter_entry.action  = RSSL_FTEA_SET_ENTRY;
		rc = rsslEncodeFilterEntryInit (it, &filter_entry, 0 /* size */);
		if (RSSL_RET_SUCCESS != rc) {
			LOG(ERROR) << "rsslEncodeFilterEntryInit: { "
				  "\"returnCode\": " << static_cast<signed> (rc) << ""
				", \"enumeration\": \"" << rsslRetCodeToString (rc) << "\""
				", \"text\": \"" << rsslRetCodeInfo (rc) << "\""
				", \"id\": \"" << internal::filter_entry_id_string (static_cast<RDMDirectoryServiceFilterIds> (filter_entry.id)) << "\""
				", \"action\": \"" << internal::filter_entry_action_string (static_cast<RsslFilterEntryActions> (filter_entry.action)) << "\""
				" }";
			return false;
		}
		if (!GetServiceLoad (it)) {
			LOG(ERROR) << "GetServiceLoad failed.";
			return false;
		}
		rc = rsslEncodeFilterEntryComplete (it, RSSL_TRUE /* commit */);
		if (RSSL_RET_SUCCESS != rc) {
			LOG(ERROR) << "rsslEncodeFilterEntryComplete: { "
				  "\"returnCode\": " << static_cast<signed> (rc) << ""
				", \"enumeration\": \"" << rsslRetCodeToString (rc) << "\""
				", \"text\": \"" << rsslRetCodeInfo (rc) << "\""
				" }";
			return false;
		}
	}

	rc = rsslEncodeFilterListComplete (it, RSSL_TRUE /* commit */);
	if (RSSL_RET_SUCCESS != rc) {
		LOG(ERROR) << "rsslEncodeFilterListComplete: { "
			  "\"returnCode\": " << static_cast<signed> (rc) << ""
			", \"enumeration\": \"" << rsslRetCodeToString (rc) << "\""
			", \"text\": \"" << rsslRetCodeInfo (rc) << "\""
			" }";
		return false;
	}
	return true;
}

/* SERVICE_INFO_ID
 * Information about a service that does not update very often.
 */
bool
kigoron::provider_t::GetServiceInformation (
	RsslEncodeIterator*const it
	)
{
#ifndef NDEBUG
	RsslElementList	element_list = RSSL_INIT_ELEMENT_LIST;
	RsslElementEntry element = RSSL_INIT_ELEMENT_ENTRY;
	RsslBuffer data_buffer = RSSL_INIT_BUFFER;
#else
	RsslElementList	element_list;
	RsslElementEntry element;
	RsslBuffer data_buffer;
	rsslClearElementList (&element_list);
	rsslClearElementEntry (&element);
	rsslClearBuffer (&data_buffer);
#endif
	RsslRet rc;

	DCHECK(nullptr != it);

	element_list.flags = RSSL_ELF_HAS_STANDARD_DATA;
	rc = rsslEncodeElementListInit (it, &element_list, nullptr /* no dictionary */, 0);
	if (RSSL_RET_SUCCESS != rc) {
		LOG(ERROR) << "rsslEncodeElementListInit: { "
			  "\"returnCode\": " << static_cast<signed> (rc) << ""
			", \"enumeration\": \"" << rsslRetCodeToString (rc) << "\""
			", \"text\": \"" << rsslRetCodeInfo (rc) << "\""
			", \"flags\": \"RSSL_ELF_HAS_STANDARD_DATA\""
			" }";
		return false;
	}

/* Name<AsciiString>
 * Service name. This will match the concrete service name or the service group
 * name that is in the Map.Key.
 */
	element.name	   = RSSL_ENAME_NAME;
	element.dataType   = RSSL_DT_ASCII_STRING;
	data_buffer.data   = const_cast<char*> (this->service_name().c_str());
	data_buffer.length = static_cast<uint32_t> (this->service_name().size());
	rc = rsslEncodeElementEntry (it, &element, &data_buffer);
	if (RSSL_RET_SUCCESS != rc) {
		LOG(ERROR) << "rsslEncodeElementEntry: { "
			  "\"returnCode\": " << static_cast<signed> (rc) << ""
			", \"enumeration\": \"" << rsslRetCodeToString (rc) << "\""
			", \"text\": \"" << rsslRetCodeInfo (rc) << "\""
			", \"name\": \"RSSL_ENAME_NAME\""
			", \"dataType\": \"" << rsslDataTypeToString (element.dataType) << "\""
			", \"buffer\": { "
				  "\"data\": \"" << std::string (data_buffer.data, data_buffer.length) << "\""
				", \"length\": " << data_buffer.length << ""
			" }"
			" }";
		return false;
	}

/* Capabilities<Array of UInt>
 * Array of valid MessageModelTypes that the service can provide. The UInt
 * MesageModelType is extensible, using values defined in the RDM Usage Guide
 * (1-255). Login and Service Directory are omitted from this list. This
 * element must be set correctly because RFA will only request an item from a
 * service if the MessageModelType of the request is listed in this element.
 */
	element.name	   = RSSL_ENAME_CAPABILITIES;
	element.dataType   = RSSL_DT_ARRAY;
	rc = rsslEncodeElementEntryInit (it, &element, 0 /* size */);
	if (RSSL_RET_SUCCESS != rc) {
		LOG(ERROR) << "rsslEncodeElementEntry: { "
			  "\"returnCode\": " << static_cast<signed> (rc) << ""
			", \"enumeration\": \"" << rsslRetCodeToString (rc) << "\""
			", \"text\": \"" << rsslRetCodeInfo (rc) << "\""
			", \"name\": \"RSSL_ENAME_CAPABILITIES\""
			", \"dataType\": \"" << rsslDataTypeToString (element.dataType) << "\""
			" }";
		return false;
	}
	if (!GetServiceCapabilities (it)) {
		LOG(ERROR) << "GetServiceCapabilities failed.";
		return false;
	}
	rc = rsslEncodeElementEntryComplete (it, RSSL_TRUE /* commit */);
	if (RSSL_RET_SUCCESS != rc) {
		LOG(ERROR) << "rsslEncodeElementEntryComplete: { "
			  "\"returnCode\": " << static_cast<signed> (rc) << ""
			", \"enumeration\": \"" << rsslRetCodeToString (rc) << "\""
			", \"text\": \"" << rsslRetCodeInfo (rc) << "\""
			" }";
		return false;
	}

/* DictionariesUsed<Array of AsciiString>
 * List of Dictionary names that may be required to process all of the data 
 * from this service. Whether or not the dictionary is required depends on 
 * the needs of the consumer (e.g. display application, caching application)
 */
	element.name	   = RSSL_ENAME_DICTIONARYS_USED;
	element.dataType   = RSSL_DT_ARRAY;
	rc = rsslEncodeElementEntryInit (it, &element, 0 /* size */);
	if (RSSL_RET_SUCCESS != rc) {
		LOG(ERROR) << "rsslEncodeElementEntry: { "
			  "\"returnCode\": " << static_cast<signed> (rc) << ""
			", \"enumeration\": \"" << rsslRetCodeToString (rc) << "\""
			", \"text\": \"" << rsslRetCodeInfo (rc) << "\""
			", \"name\": \"RSSL_ENAME_DICTIONARYS_USED\""
			", \"dataType\": \"" << rsslDataTypeToString (element.dataType) << "\""
			" }";
		return false;
	}
	if (!GetServiceDictionaries (it)) {
		LOG(ERROR) << "GetServiceDictionaries failed.";
		return false;
	}
	rc = rsslEncodeElementEntryComplete (it, RSSL_TRUE /* commit */);
	if (RSSL_RET_SUCCESS != rc) {
		LOG(ERROR) << "rsslEncodeElementEntryComplete: { "
			  "\"returnCode\": " << static_cast<signed> (rc) << ""
			", \"enumeration\": \"" << rsslRetCodeToString (rc) << "\""
			", \"text\": \"" << rsslRetCodeInfo (rc) << "\""
			" }";
		return false;
	}

/* QoS wrt. timeliness.  Provided for defect workaround with rsslConsumer demo & ADH fan-out.
 */
	element.name	   = RSSL_ENAME_QOS;
	element.dataType   = RSSL_DT_ARRAY;
	rc = rsslEncodeElementEntryInit (it, &element, 0 /* size */);
	if (RSSL_RET_SUCCESS != rc) {
		LOG(ERROR) << "rsslEncodeElementEntry: { "
			  "\"returnCode\": " << static_cast<signed> (rc) << ""
			", \"enumeration\": \"" << rsslRetCodeToString (rc) << "\""
			", \"text\": \"" << rsslRetCodeInfo (rc) << "\""
			", \"name\": \"RSSL_ENAME_QOS\""
			", \"dataType\": \"" << rsslDataTypeToString (element.dataType) << "\""
			" }";
		return false;
	}
	if (!GetServiceQoS (it)) {
		LOG(ERROR) << "GetServiceDictionaries failed.";
		return false;
	}
	rc = rsslEncodeElementEntryComplete (it, RSSL_TRUE /* commit */);
	if (RSSL_RET_SUCCESS != rc) {
		LOG(ERROR) << "rsslEncodeElementEntryComplete: { "
			  "\"returnCode\": " << static_cast<signed> (rc) << ""
			", \"enumeration\": \"" << rsslRetCodeToString (rc) << "\""
			", \"text\": \"" << rsslRetCodeInfo (rc) << "\""
			" }";
		return false;
	}

/* SupportsOutOfBandSnapshots<Unsigned>
 * Indicates whether Snapshot requests can be made even when the 
 * OpenLimit has been reached.
 */
	element.name	   = RSSL_ENAME_SUPPS_OOB_SNAPSHOTS;
	element.dataType   = RSSL_DT_UINT;
	static const uint64_t supports_oob_snapshots = 0;
	rc = rsslEncodeElementEntry (it, &element, &supports_oob_snapshots);
	if (RSSL_RET_SUCCESS != rc) {
		LOG(ERROR) << "rsslEncodeElementEntry: { "
			  "\"returnCode\": " << static_cast<signed> (rc) << ""
			", \"enumeration\": \"" << rsslRetCodeToString (rc) << "\""
			", \"text\": \"" << rsslRetCodeInfo (rc) << "\""
			", \"name\": \"RSSL_ENAME_SUPPS_OOB_SNAPSHOTS\""
			", \"dataType\": \"" << rsslDataTypeToString (element.dataType) << "\""
			", \"supportsOobSnapshots\": " << supports_oob_snapshots << ""
			" }";
		return false;
	}

/* AcceptingConsumerStatus<Unsigned>
 * Indicates whether a service can accept and process messages related 
 * to Source Mirroring
 */
	element.name	   = RSSL_ENAME_ACCEPTING_CONS_STATUS;
	element.dataType   = RSSL_DT_UINT;
	static const uint64_t accepts_consumer_status = 0;
	rc = rsslEncodeElementEntry (it, &element, &accepts_consumer_status);
	if (RSSL_RET_SUCCESS != rc) {
		LOG(ERROR) << "rsslEncodeElementEntry: { "
			  "\"returnCode\": " << static_cast<signed> (rc) << ""
			", \"enumeration\": \"" << rsslRetCodeToString (rc) << "\""
			", \"text\": \"" << rsslRetCodeInfo (rc) << "\""
			", \"name\": \"RSSL_ENAME_ACCEPTING_CONS_STATUS\""
			", \"dataType\": \"" << rsslDataTypeToString (element.dataType) << "\""
			", \"acceptsConsumerStatus\": " << accepts_consumer_status << ""
			" }";
		return false;
	}

	rc = rsslEncodeElementListComplete (it, RSSL_TRUE /* commit */);
	if (RSSL_RET_SUCCESS != rc) {
		LOG(ERROR) << "rsslEncodeElementListComplete: { "
			  "\"returnCode\": " << static_cast<signed> (rc) << ""
			", \"enumeration\": \"" << rsslRetCodeToString (rc) << "\""
			", \"text\": \"" << rsslRetCodeInfo (rc) << "\""
			" }";
		return false;
	}
	return true;
}

/* Array of valid MessageModelTypes that the service can provide.
 * RsslArray does not require version tagging according to examples.
 *
 * Encoding hard fails: rolling back is not implemented.  A return of false
 * must treat the attached buffer as undefined state and should be discarded.
 */
bool
kigoron::provider_t::GetServiceCapabilities (
	RsslEncodeIterator*const it
	)
{
#ifndef NDEBUG
	RsslArray rssl_array = RSSL_INIT_ARRAY;
#else
	RsslArray rssl_array;
	rsslClearArray (&rssl_array);
#endif
	RsslRet rc;

	DCHECK(nullptr != it);

	rssl_array.primitiveType = RSSL_DT_UINT;
	rssl_array.itemLength	 = 1;	// one byte for domain type
	rc = rsslEncodeArrayInit (it, &rssl_array);
	if (RSSL_RET_SUCCESS != rc) {
		LOG(ERROR) << "rsslEncodeArrayInit: { "
			  "\"returnCode\": " << static_cast<signed> (rc) << ""
			", \"enumeration\": \"" << rsslRetCodeToString (rc) << "\""
			", \"text\": \"" << rsslRetCodeInfo (rc) << "\""
			", \"primitiveType\": \"" << rsslDataTypeToString (rssl_array.primitiveType) << "\""
			", \"itemLength\": " << rssl_array.itemLength << ""
			" }";
		return false;
	}

/* 1: MarketPrice = 6 */
	static const uint64_t rdm_domain = RSSL_DMT_MARKET_PRICE;
	rc = rsslEncodeArrayEntry (it, nullptr /* no pre-encoded data */, &rdm_domain);
	if (RSSL_RET_SUCCESS != rc) {
		LOG(ERROR) << "rsslEncodeArrayEntry: { "
			  "\"returnCode\": " << static_cast<signed> (rc) << ""
			", \"enumeration\": \"" << rsslRetCodeToString (rc) << "\""
			", \"text\": \"" << rsslRetCodeInfo (rc) << "\""
			", \"domainType\": \"" << rsslDomainTypeToString (rdm_domain) << "\""
			" }";
		return false;
	}

	rc = rsslEncodeArrayComplete (it, RSSL_TRUE /* commit */);
	if (RSSL_RET_SUCCESS != rc) {
		LOG(ERROR) << "rsslEncodeArrayComplete: { "
			  "\"returnCode\": " << static_cast<signed> (rc) << ""
			", \"enumeration\": \"" << rsslRetCodeToString (rc) << "\""
			", \"text\": \"" << rsslRetCodeInfo (rc) << "\""
			" }";
		return false;
	}
	return true;
}

bool
kigoron::provider_t::GetServiceDictionaries (
	RsslEncodeIterator*const it
	)
{
#ifndef NDEBUG
	RsslArray rssl_array = RSSL_INIT_ARRAY;
	RsslBuffer data_buffer = RSSL_INIT_BUFFER;
#else
	RsslArray rssl_array;
	RsslBuffer data_buffer;
	rsslClearArray (&rssl_array);
	rsslClearBuffer (&data_buffer);
#endif
	RsslRet rc;

	DCHECK(nullptr != it);

	rssl_array.primitiveType = RSSL_DT_ASCII_STRING;
	rssl_array.itemLength	 = 0;	// variable length string
	rc = rsslEncodeArrayInit (it, &rssl_array);
	if (RSSL_RET_SUCCESS != rc) {
		LOG(ERROR) << "rsslEncodeArrayInit: { "
			  "\"returnCode\": " << static_cast<signed> (rc) << ""
			", \"enumeration\": \"" << rsslRetCodeToString (rc) << "\""
			", \"text\": \"" << rsslRetCodeInfo (rc) << "\""
			", \"primitiveType\": \"" << rsslDataTypeToString (rssl_array.primitiveType) << "\""
			", \"itemLength\": " << rssl_array.itemLength << ""
			" }";
		return false;
	}

/* 1: RDM Field Dictionary */
	data_buffer.data   = const_cast<char*> (kRdmFieldDictionaryName.c_str());
	data_buffer.length = static_cast<uint32_t> (kRdmFieldDictionaryName.size());
	rc = rsslEncodeArrayEntry (it, nullptr /* no pre-encoded data */, &data_buffer);
	if (RSSL_RET_SUCCESS != rc) {
		LOG(ERROR) << "rsslEncodeArrayEntry: { "
			  "\"returnCode\": " << static_cast<signed> (rc) << ""
			", \"enumeration\": \"" << rsslRetCodeToString (rc) << "\""
			", \"text\": \"" << rsslRetCodeInfo (rc) << "\""
			", \"buffer\": { "
				  "\"data\": \"" << std::string (data_buffer.data, data_buffer.length) << "\""
				", \"length\": " << data_buffer.length << ""
			" }"
			" }";
		return false;
	}

/* 2: Enumerated Type Dictionary */
	data_buffer.data   = const_cast<char*> (kEnumTypeDictionaryName.c_str());
	data_buffer.length = static_cast<uint32_t> (kEnumTypeDictionaryName.size());
	rc = rsslEncodeArrayEntry (it, nullptr /* no pre-encoded data */, &data_buffer);
	if (RSSL_RET_SUCCESS != rc) {
		LOG(ERROR) << "rsslEncodeArrayEntry: { "
			  "\"returnCode\": " << static_cast<signed> (rc) << ""
			", \"enumeration\": \"" << rsslRetCodeToString (rc) << "\""
			", \"text\": \"" << rsslRetCodeInfo (rc) << "\""
			", \"buffer\": { "
				  "\"data\": \"" << std::string (data_buffer.data, data_buffer.length) << "\""
				", \"length\": " << data_buffer.length << ""
			" }"
			" }";
		return false;
	}

	rc = rsslEncodeArrayComplete (it, RSSL_TRUE /* commit */);
	if (RSSL_RET_SUCCESS != rc) {
		LOG(ERROR) << "rsslEncodeArrayComplete: { "
			  "\"returnCode\": " << static_cast<signed> (rc) << ""
			", \"enumeration\": \"" << rsslRetCodeToString (rc) << "\""
			", \"text\": \"" << rsslRetCodeInfo (rc) << "\""
			" }";
		return false;
	}
	return true;
}

bool
kigoron::provider_t::GetServiceQoS (
	RsslEncodeIterator*const it
	)
{
#ifndef NDEBUG
	RsslArray rssl_array = RSSL_INIT_ARRAY;
	RsslQos qos = RSSL_INIT_QOS;
#else
	RsslArray rssl_array;
	RsslQos qos;
	rsslClearArray (&rssl_array);
	rsslClearQos (&qos);
#endif
	RsslRet rc;

	DCHECK(nullptr != it);

	rssl_array.primitiveType = RSSL_DT_QOS;
	rssl_array.itemLength	 = 0;
	rc = rsslEncodeArrayInit (it, &rssl_array);
	if (RSSL_RET_SUCCESS != rc) {
		LOG(ERROR) << "rsslEncodeArrayInit: { "
			  "\"returnCode\": " << static_cast<signed> (rc) << ""
			", \"enumeration\": \"" << rsslRetCodeToString (rc) << "\""
			", \"text\": \"" << rsslRetCodeInfo (rc) << "\""
			", \"primitiveType\": \"" << rsslDataTypeToString (rssl_array.primitiveType) << "\""
			", \"itemLength\": " << rssl_array.itemLength << ""
			" }";
		return false;
	}

/* QoS default settings */
	qos.dynamic    = RSSL_FALSE;
	qos.rate       = RSSL_QOS_RATE_TICK_BY_TICK;
	qos.timeliness = RSSL_QOS_TIME_REALTIME;
	rc = rsslEncodeArrayEntry (it, nullptr /* no pre-encoded data */, &qos);
	if (RSSL_RET_SUCCESS != rc) {
		LOG(ERROR) << "rsslEncodeArrayEntry: { "
			  "\"returnCode\": " << static_cast<signed> (rc) << ""
			", \"enumeration\": \"" << rsslRetCodeToString (rc) << "\""
			", \"text\": \"" << rsslRetCodeInfo (rc) << "\""
			", \"qos\": " << qos << ""
			" }";
		return false;
	}

	rc = rsslEncodeArrayComplete (it, RSSL_TRUE /* commit */);
	if (RSSL_RET_SUCCESS != rc) {
		LOG(ERROR) << "rsslEncodeArrayComplete: { "
			  "\"returnCode\": " << static_cast<signed> (rc) << ""
			", \"enumeration\": \"" << rsslRetCodeToString (rc) << "\""
			", \"text\": \"" << rsslRetCodeInfo (rc) << "\""
			" }";
		return false;
	}

	return true;
}

/* SERVICE_STATE_ID
 * State of a service.
 */
bool
kigoron::provider_t::GetServiceState (
	RsslEncodeIterator*const it
	)
{
#ifndef NDEBUG
	RsslElementList	element_list = RSSL_INIT_ELEMENT_LIST;
	RsslElementEntry element = RSSL_INIT_ELEMENT_ENTRY;
#else
	RsslElementList	element_list;
	RsslElementEntry element;
	rsslClearElementList (&element_list);
	rsslClearElementEntry (&element);
#endif
	RsslRet rc;

	DCHECK(nullptr != it);

	element_list.flags = RSSL_ELF_HAS_STANDARD_DATA;
	rc = rsslEncodeElementListInit (it, &element_list, nullptr /* no dictionary */, 0 /* maximum size */);
	if (RSSL_RET_SUCCESS != rc) {
		LOG(ERROR) << "rsslEncodeElementListInit failed: { "
			  "\"returnCode\": " << static_cast<signed> (rc) << ""
			", \"enumeration\": \"" << rsslRetCodeToString (rc) << "\""
			", \"text\": \"" << rsslRetCodeInfo (rc) << "\""
			", \"flags\": \"RSSL_ELF_HAS_STANDARD_DATA\""
			" }";
		return false;
	}

/* ServiceState<UInt>
 * 1: Up/Yes
 * 0: Down/No
 * Is the original provider of the data responding to new requests. All
 * existing streams are left unchanged.
 */
	element.name	   = RSSL_ENAME_SVC_STATE;
	element.dataType   = RSSL_DT_UINT;
	static const uint64_t service_state = RDM_DIRECTORY_SERVICE_STATE_UP;
	rc = rsslEncodeElementEntry (it, &element, &service_state);
	if (RSSL_RET_SUCCESS != rc) {
		LOG(ERROR) << "rsslEncodeElementEntry failed: { "
			  "\"returnCode\": " << static_cast<signed> (rc) << ""
			", \"enumeration\": \"" << rsslRetCodeToString (rc) << "\""
			", \"text\": \"" << rsslRetCodeInfo (rc) << "\""
			", \"name\": \"RSSL_ENAME_SVC_STATE\""
			", \"dataType\": \"" << rsslDataTypeToString (element.dataType) << "\""
			", \"serviceState\": " << service_state << ""
			" }";
		return false;
	}

/* AcceptingRequests<UInt>
 * 1: Yes
 * 0: No
 * If the value is 0, then consuming applications should not send any new
 * requests to the service provider. (Reissues may still be sent.) If an RFA
 * application makes new requests to the service, they will be queued. All
 * existing streams are left unchanged.
 */
	element.name	   = RSSL_ENAME_ACCEPTING_REQS;
	element.dataType   = RSSL_DT_UINT;
	const uint64_t is_accepting_requests = is_accepting_requests_ ? 1 : 0;
	rc = rsslEncodeElementEntry (it, &element, &is_accepting_requests);
	if (RSSL_RET_SUCCESS != rc) {
		LOG(ERROR) << "rsslEncodeElementEntry failed: { "
			  "\"returnCode\": " << static_cast<signed> (rc) << ""
			", \"enumeration\": \"" << rsslRetCodeToString (rc) << "\""
			", \"text\": \"" << rsslRetCodeInfo (rc) << "\""
			", \"name\": \"RSSL_ENAME_ACCEPTING_REQS\""
			", \"dataType\": \"" << rsslDataTypeToString (element.dataType) << "\""
			", \"isAcceptingRequests\": " << is_accepting_requests_ << ""
			" }";
		return false;
	}

	rc = rsslEncodeElementListComplete (it, RSSL_TRUE /* commit */);
	if (RSSL_RET_SUCCESS != rc) {
		LOG(ERROR) << "rsslEncodeElementListComplete failed: { "
			  "\"returnCode\": " << static_cast<signed> (rc) << ""
			", \"enumeration\": \"" << rsslRetCodeToString (rc) << "\""
			", \"text\": \"" << rsslRetCodeInfo (rc) << "\""
			" }";
		return false;
	}
	return true;
}

/* SERVICE_LOAD_ID
 * Load information of a service.
 */
bool
kigoron::provider_t::GetServiceLoad (
	RsslEncodeIterator*const it
	)
{
#ifndef NDEBUG
	RsslElementList	element_list = RSSL_INIT_ELEMENT_LIST;
	RsslElementEntry element = RSSL_INIT_ELEMENT_ENTRY;
#else
	RsslElementList	element_list;
	RsslElementEntry element;
	rsslClearElementList (&element_list);
	rsslClearElementEntry (&element);
#endif
	RsslRet rc;

	DCHECK(nullptr != it);

	element_list.flags = RSSL_ELF_HAS_STANDARD_DATA;
	rc = rsslEncodeElementListInit (it, &element_list, nullptr /* no dictionary */, 0 /* maximum size */);
	if (RSSL_RET_SUCCESS != rc) {
		LOG(ERROR) << "rsslEncodeElementListInit failed: { "
			  "\"returnCode\": " << static_cast<signed> (rc) << ""
			", \"enumeration\": \"" << rsslRetCodeToString (rc) << "\""
			", \"text\": \"" << rsslRetCodeInfo (rc) << "\""
			", \"flags\": \"RSSL_ELF_HAS_STANDARD_DATA\""
			" }";
		return false;
	}

/* OpenWindow<UInt>
 * Maximum number of outstanding requests (i.e. requests for items not yet open) that 
 * the service will allow at any given time.
 */
	element.name	   = RSSL_ENAME_OPEN_WINDOW;
	element.dataType   = RSSL_DT_UINT;
	static const uint64_t open_window = this->open_window();	/* ZMQ_SNDHWM */
	rc = rsslEncodeElementEntry (it, &element, &open_window);
	if (RSSL_RET_SUCCESS != rc) {
		LOG(ERROR) << "rsslEncodeElementEntry failed: { "
			  "\"returnCode\": " << static_cast<signed> (rc) << ""
			", \"enumeration\": \"" << rsslRetCodeToString (rc) << "\""
			", \"text\": \"" << rsslRetCodeInfo (rc) << "\""
			", \"name\": \"RSSL_ENAME_OPEN_WINDOW\""
			", \"dataType\": \"" << rsslDataTypeToString (element.dataType) << "\""
			", \"openWindow\": " << open_window << ""
			" }";
		return false;
	}

	rc = rsslEncodeElementListComplete (it, RSSL_TRUE /* commit */);
	if (RSSL_RET_SUCCESS != rc) {
		LOG(ERROR) << "rsslEncodeElementListComplete failed: { "
			  "\"returnCode\": " << static_cast<signed> (rc) << ""
			", \"enumeration\": \"" << rsslRetCodeToString (rc) << "\""
			", \"text\": \"" << rsslRetCodeInfo (rc) << "\""
			" }";
		return false;
	}
	return true;
}

int
kigoron::provider_t::Submit (
	RsslChannel* c,
	RsslBuffer* buf
	)
{
	RsslWriteInArgs in_args;
	RsslWriteOutArgs out_args;
	RsslError rssl_err;
	RsslRet rc;

	DCHECK(nullptr != c);
	DCHECK(nullptr != buf);

	rsslClearWriteInArgs (&in_args);
	in_args.rsslPriority = RSSL_LOW_PRIORITY;	/* flushing priority */
/* direct write on clear socket, enqueue when writes are pending */
	const bool should_write_direct = !FD_ISSET (c->socketId, &in_wfds_);
	in_args.writeInFlags = should_write_direct ? RSSL_WRITE_DIRECT_SOCKET_WRITE : 0;

try_again:
	if (logging::DEBUG_MODE) {
		rsslClearWriteOutArgs (&out_args);
/* rsslClearError (&rssl_err); */
		rssl_err.rsslErrorId = 0;
		rssl_err.sysError = 0;
		rssl_err.text[0] = '\0';
	}
	rc = rsslWriteEx (c, buf, &in_args, &out_args, &rssl_err);
	if (logging::DEBUG_MODE) {
		std::stringstream return_code;
		if (rc > 0) {
			return_code << "\"pendingBytes\": " << rc;
		} else {
			return_code << "\"returnCode\": \"" << static_cast<signed> (rc) << ""
				     ", \"enumeration\": \"" << rsslRetCodeToString (rc) << "\"";
		}
		VLOG(1) << "rsslWriteEx: { "
			  << return_code.str() << ""
			", \"bytesWritten\": " << out_args.bytesWritten << ""
			", \"uncompressedBytesWritten\": " << out_args.uncompressedBytesWritten << ""
			", \"rsslErrorId\": " << rssl_err.rsslErrorId << ""
			", \"sysError\": " << rssl_err.sysError << ""
			", \"text\": \"" << rssl_err.text << "\""
			" }";
	}
	if (rc > 0) {
		if (nullptr != c->userSpecPtr) {
			auto client = reinterpret_cast<client_t*> (c->userSpecPtr);
			client->IncrementPendingCount();
		}
		cumulative_stats_[PROVIDER_PC_RSSL_MSGS_ENQUEUED]++;
		goto pending;
	}
	switch (rc) {
	case RSSL_RET_WRITE_CALL_AGAIN:			/* fragmenting the buffer and needs to be called again with the same buffer. */
		goto try_again;
	case RSSL_RET_WRITE_FLUSH_FAILED:		/* attempted to flush data to the connection but was blocked. */
		cumulative_stats_[PROVIDER_PC_RSSL_WRITE_FLUSH_FAILED]++;
		goto pending;
	case RSSL_RET_BUFFER_NO_BUFFERS:		/* empty buffer pool: spin wait until buffer is available. */
		cumulative_stats_[PROVIDER_PC_RSSL_WRITE_NO_BUFFERS]++;
pending:
		FD_SET (c->socketId, &in_wfds_);	/* pending output */
		return -1;
	case RSSL_RET_SUCCESS:				/* sent, no flush required. */
		cumulative_stats_[PROVIDER_PC_RSSL_MSGS_SENT]++;
/* Sent data equivalent to a ping. */
		if (nullptr != c->userSpecPtr) {
			auto client = reinterpret_cast<client_t*> (c->userSpecPtr);
			client->SetNextPing (last_activity_ + boost::posix_time::seconds (client->ping_interval_));
		}
		return 1;
	default:
		cumulative_stats_[PROVIDER_PC_RSSL_WRITE_EXCEPTION]++;
		LOG(ERROR) << "rsslWriteEx: { "
			  "\"rsslErrorId\": " << rssl_err.rsslErrorId << ""
			", \"sysError\": " << rssl_err.sysError << ""
			", \"text\": \"" << rssl_err.text << "\""
			" }";
		return 0;
	}
}

int
kigoron::provider_t::Ping (
	RsslChannel* c
	)
{
	RsslError rssl_err;
	RsslRet rc;

	DCHECK(nullptr != c);

	if (logging::DEBUG_MODE) {
/* In place of absent API: rsslClearError (&rssl_err); */
		rssl_err.rsslErrorId = 0;
		rssl_err.sysError = 0;
		rssl_err.text[0] = '\0';
	}
	rc = rsslPing (c, &rssl_err);
	if (logging::DEBUG_MODE && VLOG_IS_ON(1)) {
		std::stringstream return_code;
		if (rc > 0) {
			return_code << "\"pendingBytes\": " << static_cast<signed> (rc);
		} else {
			return_code << "\"returnCode\": \"" << static_cast<signed> (rc) << ""
				     ", \"enumeration\": \"" << rsslRetCodeToString (rc) << "\"";
		}
		VLOG(1) << "rsslPing: { "
			  << return_code.str() << ""
			", \"rsslErrorId\": " << rssl_err.rsslErrorId << ""
			", \"sysError\": " << rssl_err.sysError << ""
			", \"text\": \"" << rssl_err.text << "\""
			" }";
	}
	if (rc > 0) goto pending;
	switch (rc) {
	case RSSL_RET_WRITE_FLUSH_FAILED:		/* attempted to flush data to the connection but was blocked. */
		cumulative_stats_[PROVIDER_PC_RSSL_PING_FLUSH_FAILED]++;
		goto pending;
	case RSSL_RET_BUFFER_NO_BUFFERS:		/* empty buffer pool: spin wait until buffer is available. */
		cumulative_stats_[PROVIDER_PC_RSSL_PING_NO_BUFFERS]++;
pending:
/* Pings should only occur when no writes are pending, thus rsslPing internally calls rsslFlush
 * automatically.  If this fails then either the client has stalled or the systems is out of 
 * resources.  Suitable consequence is to force a disconnect.
 */
		FD_SET (c->socketId, &out_efds_);
		LOG(INFO) << "rsslPing: { "
			  "\"rsslErrorId\": " << rssl_err.rsslErrorId << ""
			", \"sysError\": " << rssl_err.sysError << ""
			", \"text\": \"" << rssl_err.text << "\""
			" }";
		return -1;
	case RSSL_RET_SUCCESS:				/* sent, no flush required. */
		cumulative_stats_[PROVIDER_PC_RSSL_PING_SENT]++;
/* Advance ping expiration only on success. */
		if (nullptr != c->userSpecPtr) {
			auto client = reinterpret_cast<client_t*> (c->userSpecPtr);
			client->SetNextPing (last_activity_ + boost::posix_time::seconds (client->ping_interval_));
		}
		return 1;
	default:
		cumulative_stats_[PROVIDER_PC_RSSL_PING_EXCEPTION]++;
		LOG(ERROR) << "rsslPing: { "
			  "\"rsslErrorId\": " << rssl_err.rsslErrorId << ""
			", \"sysError\": " << rssl_err.sysError << ""
			", \"text\": \"" << rssl_err.text << "\""
			" }";
		return 0;
	}
}

/* eof */
