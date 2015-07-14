/* UPA provider client session.
 */

#include "client.hh"

#include <algorithm>
#include <utility>

#include <windows.h>

#include "chromium/logging.hh"
#include "chromium/string_piece.hh"
#include "upaostream.hh"
#include "provider.hh"


#define MAX_MSG_SIZE 4096

static const std::string kErrorNone = "";
static const std::string kErrorUnsupportedMsgClass = "Unsupported message class.";
static const std::string kErrorUnsupportedRequest = "Unsupported domain type in request.";
static const std::string kErrorUnsupportedDictionary = "Unsupported dictionary request.";
static const std::string kErrorUnsupportedNonStreaming = "Unsupported non-streaming request.";
static const std::string kErrorLoginRequired = "Login required for request.";


kigoron::client_t::client_t (
	const boost::posix_time::ptime& now,
	std::shared_ptr<kigoron::provider_t> provider,
	Delegate* delegate, 
	RsslChannel* handle,
	const char* address
	) :
	creation_time_ (now),
	last_activity_ (now),
	provider_ (provider),
	delegate_ (delegate),
	address_ (address),
	handle_ (handle),
	pending_count_ (0),
	is_logged_in_ (false),
	login_token_ (0)
{
	ZeroMemory (cumulative_stats_, sizeof (cumulative_stats_));
	ZeroMemory (snap_stats_, sizeof (snap_stats_));

/* Set logger ID */
	std::ostringstream ss;
	ss << handle_ << ':';
	prefix_.assign (ss.str());
}

kigoron::client_t::~client_t()
{
	DLOG(INFO) << "~client_t";
/* Remove reference on containing provider. */
	provider_.reset();

	using namespace boost::posix_time;
	const auto uptime = second_clock::universal_time() - creation_time_;
	VLOG(3) << prefix_ << "Summary: {"
		 " \"Uptime\": \"" << to_simple_string (uptime) << "\""
		", \"MsgsReceived\": " << cumulative_stats_[CLIENT_PC_RSSL_MSGS_RECEIVED] <<
		", \"MsgsSent\": " << cumulative_stats_[CLIENT_PC_RSSL_MSGS_SENT] <<
		", \"MsgsRejected\": " << cumulative_stats_[CLIENT_PC_RSSL_MSGS_REJECTED] <<
		" }";
}

bool
kigoron::client_t::Initialize()
{
	RsslChannelInfo info;
	RsslError rssl_err;
	RsslRet rc;

	DCHECK(nullptr != handle_);

/* Relog negotiated state. */
	std::stringstream client_hostname, client_ip;
	if (nullptr == handle_->clientHostname) 
		client_hostname << "null";
	else	
		client_hostname << '"' << handle_->clientHostname << '"';
	if (nullptr == handle_->clientIP)	
		client_ip << "null";
	else	
		client_ip << '"' << handle_->clientIP << '"';
	LOG(INFO) << prefix_ <<
		  "RSSL negotiated state: { "
		  "\"clientHostname\": " << client_hostname.str() << ""
		", \"clientIP\": " << client_ip.str() << ""
		", \"connectionType\": \"" << internal::connection_type_string (handle_->connectionType) << "\""
		", \"majorVersion\": " << static_cast<unsigned> (rwf_major_version()) << ""
		", \"minorVersion\": " << static_cast<unsigned> (rwf_minor_version()) << ""
		", \"pingTimeout\": " << handle_->pingTimeout << ""
		", \"protocolType\": \"" << internal::protocol_type_string (handle_->protocolType) << "\""
		", \"socketId\": " << handle_->socketId << ""
		", \"state\": \"" << internal::channel_state_string (handle_->state) << "\""
		" }";

/* Set new temporal buffer sizing, Windows default 8192. */
	if (!provider_->send_buffer_size().empty()) {
		const uint32_t send_buffer_size = std::atol (provider_->send_buffer_size().c_str());
		rc = rsslIoctl (handle_, RSSL_SYSTEM_WRITE_BUFFERS, const_cast<uint32_t*> (&send_buffer_size), &rssl_err);
		if (RSSL_RET_SUCCESS != rc) {
			LOG(WARNING) << prefix_ << "rssIoctl: { "
				  "\"rsslErrorId\": " << rssl_err.rsslErrorId << ""
				", \"sysError\": " << rssl_err.sysError << ""
				", \"text\": \"" << rssl_err.text << "\""
				", \"ioctlCode\": \"RSSL_SYSTEM_WRITE_BUFFERS\""
				", \"value\": " << send_buffer_size << ""
				" }";
		}
	}
	if (!provider_->recv_buffer_size().empty()) {
		const uint32_t recv_buffer_size = std::atol (provider_->recv_buffer_size().c_str());
		rc = rsslIoctl (handle_, RSSL_SYSTEM_READ_BUFFERS, const_cast<uint32_t*> (&recv_buffer_size), &rssl_err);
		if (RSSL_RET_SUCCESS != rc) {
			LOG(WARNING) << prefix_ << "rssIoctl: { "
				  "\"rsslErrorId\": " << rssl_err.rsslErrorId << ""
				", \"sysError\": " << rssl_err.sysError << ""
				", \"text\": \"" << rssl_err.text << "\""
				", \"ioctlCode\": \"RSSL_SYSTEM_READ_BUFFERS\""
				", \"value\": " << recv_buffer_size << ""
				" }";
		}
	}

/* Store negotiated Reuters Wire Format version information. */
	rc = rsslGetChannelInfo (handle_, &info, &rssl_err);
	if (RSSL_RET_SUCCESS != rc) {
		LOG(ERROR) << prefix_ << "rsslGetChannelInfo: { "
			  "\"rsslErrorId\": " << rssl_err.rsslErrorId << ""
			", \"sysError\": " << rssl_err.sysError << ""
			", \"text\": \"" << rssl_err.text << "\""
			" }";
		return false;
	}

/* Log connected infrastructure. */
	std::stringstream components;
	components << "[ ";
	for (unsigned i = 0; i < info.componentInfoCount; ++i) {
		if (i > 0) components << ", ";
		components << "{ "
			"\"componentVersion\": \"" << std::string (info.componentInfo[i]->componentVersion.data, info.componentInfo[i]->componentVersion.length) << "\""
			" }";
	}
	components << " ]";

	LOG(INFO) << prefix_ <<
                  "channelInfo: { "
                  "\"clientToServerPings\": \"" << (info.clientToServerPings ? "true" : "false") << "\""
                ", \"componentInfo\": " << components.str() << ""
                ", \"compressionThreshold\": " << static_cast<unsigned> (info.compressionThreshold) << ""
                ", \"compressionType\": \"" << internal::compression_type_string (info.compressionType) << "\""
                ", \"guaranteedOutputBuffers\": " << static_cast<unsigned> (info.guaranteedOutputBuffers) << ""
                ", \"maxFragmentSize\": " << static_cast<unsigned> (info.maxFragmentSize) << ""
                ", \"maxOutputBuffers\": " << static_cast<unsigned> (info.maxOutputBuffers) << ""
                ", \"numInputBuffers\": " << static_cast<unsigned> (info.numInputBuffers) << ""
                ", \"pingTimeout\": " << static_cast<unsigned> (info.pingTimeout) << ""
/* null terminated but max length RSSL_RSSL_MAX_FLUSH_STRATEGY, not well documented. */
                ", \"priorityFlushStrategy\": \"" << info.priorityFlushStrategy << "\""
                ", \"serverToClientPings\": " << (info.serverToClientPings ? "true" : "false") << ""
                ", \"sysRecvBufSize\": " << static_cast<unsigned> (info.sysRecvBufSize) << ""
                ", \"sysSendBufSize\": " << static_cast<unsigned> (info.sysSendBufSize) << ""
                ", \"tcpRecvBufSize\": " << static_cast<unsigned> (info.tcpRecvBufSize) << ""
                ", \"tcpSendBufSize\": " << static_cast<unsigned> (info.tcpSendBufSize) << ""
                " }";
/* Derive expected RSSL ping interval from negotiated timeout. */
	ping_interval_ = handle_->pingTimeout / 3;
/* Schedule first RSSL ping. */
	next_ping_ = last_activity_ + boost::posix_time::seconds (ping_interval_);
/* Treat connect as first RSSL pong. */
	next_pong_ = last_activity_ + boost::posix_time::seconds (handle_->pingTimeout);
	return true;
}

/* Propagate close notification to RSSL channel before closing the socket.
 */
bool
kigoron::client_t::Close()
{
/* client_t exists when client session is active but not necessarily logged in. */
	if (is_logged_in_) {
/* reject new item requests. */
		is_logged_in_ = false;
/* drop active requests. */
		VLOG(2) << prefix_ << "Removing " << tokens_.size() << " item streams.";
		tokens_.clear();
/* notify client session is no longer valid via login stream. */
		return SendClose (
			login_token_,
			provider_->service_id(),
			RSSL_DMT_LOGIN,
			nullptr,
			false, /* no AttribInfo in MMT_LOGIN */
			RSSL_STREAM_CLOSED, RSSL_SC_NONE, kErrorNone
			);
	} else {
		return true;
	}
}

/* Returns true if message processed successfully, returns false to abort the connection.
 */
bool
kigoron::client_t::OnMsg (
	const boost::posix_time::ptime& now,
	RsslDecodeIterator* it,
	const RsslMsg* msg
	)
{
	DCHECK (nullptr != it);
	DCHECK (nullptr != msg);
	last_activity_ = now;
	cumulative_stats_[CLIENT_PC_RSSL_MSGS_RECEIVED]++;
	switch (msg->msgBase.msgClass) {
	case RSSL_MC_REQUEST:
		return OnRequestMsg (it, reinterpret_cast<const RsslRequestMsg*> (msg));
	case RSSL_MC_CLOSE:
		return OnCloseMsg (it, reinterpret_cast<const RsslCloseMsg*> (msg));
	case RSSL_MC_REFRESH:
	case RSSL_MC_STATUS:
	case RSSL_MC_UPDATE:
	case RSSL_MC_ACK:
	case RSSL_MC_GENERIC:
	case RSSL_MC_POST:
	default:
		cumulative_stats_[CLIENT_PC_RSSL_MSGS_REJECTED]++;
		LOG(WARNING) << prefix_ << "Uncaught message: " << msg;
/* abort connection if status message fails. */
		return SendClose (
			msg->msgBase.streamId,
			msg->msgBase.msgKey.serviceId,
			msg->msgBase.domainType,
			chromium::StringPiece (msg->msgBase.msgKey.name.data, msg->msgBase.msgKey.name.length),
			true, /* always send AttribInfo */
			RSSL_STREAM_CLOSED, RSSL_SC_USAGE_ERROR, kErrorUnsupportedMsgClass
			);
	}
}

/* Returns true if message processed successfully, returns false to abort the connection.
 */
bool
kigoron::client_t::OnRequestMsg (
	RsslDecodeIterator* it,
	const RsslRequestMsg* request_msg
	)
{
	DCHECK (nullptr != request_msg);
	cumulative_stats_[CLIENT_PC_REQUEST_MSGS_RECEIVED]++;
	switch (request_msg->msgBase.domainType) {
	case RSSL_DMT_LOGIN:
		return OnLoginRequest (it, request_msg);
	case RSSL_DMT_SOURCE:	/* Directory */
		return OnDirectoryRequest (it, request_msg);
	case RSSL_DMT_DICTIONARY:
		return OnDictionaryRequest (it, request_msg);
	case RSSL_DMT_MARKET_PRICE:
		return OnItemRequest (it, request_msg);
	case RSSL_DMT_MARKET_BY_ORDER:
	case RSSL_DMT_MARKET_BY_PRICE:
	case RSSL_DMT_MARKET_MAKER:
	case RSSL_DMT_SYMBOL_LIST:
	case RSSL_DMT_YIELD_CURVE:
	default:
		cumulative_stats_[CLIENT_PC_REQUEST_MSGS_REJECTED]++;
		LOG(WARNING) << prefix_ << "Uncaught request message: " << request_msg;
/* abort connection if status message fails. */
		return SendClose (
			request_msg->msgBase.streamId,
			request_msg->msgBase.msgKey.serviceId,
			request_msg->msgBase.domainType,
			chromium::StringPiece (request_msg->msgBase.msgKey.name.data, request_msg->msgBase.msgKey.name.length),
			RSSL_RQMF_MSG_KEY_IN_UPDATES == (request_msg->flags & RSSL_RQMF_MSG_KEY_IN_UPDATES),
			RSSL_STREAM_CLOSED, RSSL_SC_USAGE_ERROR, kErrorUnsupportedRequest
			);
	}
}

/* 7.3. Perform Login Process.
 * The message model type MMT_LOGIN represents a login request. Specific
 * information about the user e.g., name,name type, permission information,
 * single open, etc is available from the AttribInfo in the ReqMsg accessible
 * via getAttribInfo(). The Provider is responsible for processing this
 * information to determine whether to accept the login request.
 *
 * RFA assumes default values for all attributes not specified in the Provider’s
 * login response. For example, if a provider does not specify SingleOpen
 * support in its login response, RFA assumes the provider supports it.
 *
 *   InteractionType:	  Streaming request || Pause request.
 *   QualityOfServiceReq: Not used.
 *   Priority:		  Not used.
 *   Header:		  Not used.
 *   Payload:		  Not used.
 *
 * RDM 3.4.4 Authentication: multiple logins per client session are not supported.
 */
bool
kigoron::client_t::OnLoginRequest (
	RsslDecodeIterator* it,
	const RsslRequestMsg* login_msg
	)
{
	DCHECK (nullptr != login_msg);
	cumulative_stats_[CLIENT_PC_MMT_LOGIN_RECEIVED]++;

	static const uint16_t streaming_request = RSSL_RQMF_STREAMING;
	static const uint16_t pause_request	= RSSL_RQMF_PAUSE;

	const bool is_streaming_request = ((streaming_request == login_msg->flags)
					|| ((streaming_request | pause_request) == login_msg->flags));
	const bool is_pause_request	= (pause_request == login_msg->flags);

/* RDM 3.2.4: All message types except GenericMsg should include an AttribInfo.
 * RFA example code verifies existence of AttribInfo with an assertion.
 */
	const bool has_attribinfo = true;
	const bool has_name	  = has_attribinfo && (RSSL_MKF_HAS_NAME      == (login_msg->msgBase.msgKey.flags & RSSL_MKF_HAS_NAME));
	const bool has_nametype   = has_attribinfo && (RSSL_MKF_HAS_NAME_TYPE == (login_msg->msgBase.msgKey.flags & RSSL_MKF_HAS_NAME_TYPE));

	DVLOG(4) << prefix_
		  << "is_streaming_request: " << is_streaming_request
		<< ", is_pause_request: " << is_pause_request
		<< ", has_attribinfo: " << has_attribinfo
		<< ", has_name: " << has_name
		<< ", has_nametype: " << has_nametype;

/* invalid RDM login. */
	if ((!is_streaming_request && !is_pause_request)
		|| !has_attribinfo
		|| !has_name
		|| !has_nametype)
	{
		goto invalid_login;
	}

/* Extract application details from payload */
	if (has_attribinfo) {
		RsslRet rc = rsslDecodeMsgKeyAttrib (it, &login_msg->msgBase.msgKey);
		if (RSSL_RET_SUCCESS != rc) {
			LOG(WARNING) << prefix_ << "rsslDecodeMsgKeyAttrib: { "
				  "\"returnCode\": " << static_cast<signed> (rc) << ""
				", \"enumeration\": \"" << rsslRetCodeToString (rc) << "\""
				", \"text\": \"" << rsslRetCodeInfo (rc) << "\""
				" }";
			goto invalid_login;
		}
		if (RSSL_DT_ELEMENT_LIST != login_msg->msgBase.msgKey.attribContainerType) {
			LOG(WARNING) << prefix_ << "AttribInfo container type is not an element list.";
		} else if (!OnLoginAttribInfo (it)) {
			goto invalid_login;
		}
	}

	if (!AcceptLogin (login_msg, login_msg->msgBase.streamId)) {
/* disconnect on failure. */
		return false;
	} else {
		is_logged_in_ = true;
		login_token_ = login_msg->msgBase.streamId;
	}
	return true;
invalid_login:
	cumulative_stats_[CLIENT_PC_MMT_LOGIN_MALFORMED]++;
	LOG(WARNING) << prefix_ << "Rejecting MMT_LOGIN as RDM validation failed: " << login_msg;
	return RejectLogin (login_msg, login_msg->msgBase.streamId);
}

bool
kigoron::client_t::OnLoginAttribInfo (
	RsslDecodeIterator*const it
	)
{
	DCHECK (nullptr != it);

	RsslElementList	element_list;
	RsslElementEntry element;
	RsslRet rc;

	rc = rsslDecodeElementList (it, &element_list, nullptr /* no dictionary */);
	if (RSSL_RET_SUCCESS != rc) {
		LOG(ERROR) << prefix_ << "rsslDecodeElementList: { "
			  "\"returnCode\": " << static_cast<signed> (rc) << ""
			", \"enumeration\": \"" << rsslRetCodeToString (rc) << "\""
			", \"text\": \"" << rsslRetCodeInfo (rc) << "\""
			" }";
		return false;
	}
	do {
		rc = rsslDecodeElementEntry (it, &element);
		switch (rc) {
		case RSSL_RET_END_OF_CONTAINER:
			break;
		case RSSL_RET_SUCCESS:
/* ApplicationName */
			if (rsslBufferIsEqual (&element.name, &RSSL_ENAME_APPNAME)) {
				if (RSSL_DT_ASCII_STRING == element.dataType) {
					chromium::StringPiece application_name (element.encData.data, element.encData.length);
					LOG(INFO) << prefix_ << "applicationName: \"" << application_name << "\"";
				} else {
					LOG(WARNING) << prefix_ << "RSSL_ENAME_APPNAME found in element list but entry data type is not RSSL_DT_ASCII_STRING.";
				}
			}
			break;
		default:
			LOG(ERROR) << prefix_ << "rsslDecodeElementEntry: { "
				  "\"returnCode\": " << static_cast<signed> (rc) << ""
				", \"enumeration\": \"" << rsslRetCodeToString (rc) << "\""
				", \"text\": \"" << rsslRetCodeInfo (rc) << "\""
				" }";
			return false;
		}
	} while (RSSL_RET_SUCCESS == rc);
	return true;
}

/** Rejecting Login **
 * In the case where the Provider rejects the login, it should create a RespMsg
 * as above, but set the RespType and RespStatus to the reject semantics
 * specified in RFA API 7 RDM Usage Guide. The provider application should
 * populate an OMMSolicitedItemCmd with this RespMsg, set the corresponding
 * request token and call submit() on the OMM Provider.
 *
 * Once the Provider determines that the login is to be logged out (rejected),
 * it is responsible to clean up all references to request tokens for that
 * particular client session. In addition, any incoming requests that may be
 * received after the login rejection has been submitted should be ignored.
 *
 * NB: The provider application can reject a login at any time after it has
 *     accepted a particular login.
 */
bool
kigoron::client_t::RejectLogin (
	const RsslRequestMsg* login_msg,
	int32_t login_token
	)
{
#ifndef NDEBUG
/* Static initialisation sets all fields rather than only the minimal set
 * required.  Use for debug mode and optimise for release builds.
 */
	RsslStatusMsg response = RSSL_INIT_STATUS_MSG;
	RsslEncodeIterator it = RSSL_INIT_ENCODE_ITERATOR;
#else
	RsslStatusMsg response;
	RsslEncodeIterator it;
	rsslClearStatusMsg (&response);
	rsslClearEncodeIterator (&it);
#endif
	RsslBuffer* buf;
	RsslError rssl_err;
	RsslRet rc;

	DCHECK (nullptr != login_msg);
	VLOG(2) << prefix_ << "Sending MMT_LOGIN rejection.";

/* Set the message model type. */
	response.msgBase.domainType = RSSL_DMT_LOGIN;
/* Set response type. */
	response.msgBase.msgClass = RSSL_MC_STATUS;
/* No payload. */
	response.msgBase.containerType = RSSL_DT_NO_DATA;
/* Set the login token. */
	response.msgBase.streamId = login_token;

/* Item interaction state. */
	response.state.streamState = RSSL_STREAM_CLOSED;
/* Data quality state. */
	response.state.dataState = RSSL_DATA_SUSPECT;
/* Error code. */
	response.state.code = RSSL_SC_NOT_ENTITLED; // RSSL_SC_TOO_MANY_ITEMS would be more suitable, but does not follow RDM spec.
	response.flags |= RSSL_STMF_HAS_STATE;

	buf = rsslGetBuffer (handle_, MAX_MSG_SIZE, RSSL_FALSE /* not packed */, &rssl_err);
	if (nullptr == buf) {
		LOG(ERROR) << prefix_ << "rsslGetBuffer: { "
			  "\"rsslErrorId\": " << rssl_err.rsslErrorId << ""
			", \"sysError\": " << rssl_err.sysError << ""
			", \"text\": \"" << rssl_err.text << "\""
			", \"size\": " << MAX_MSG_SIZE << ""
			", \"packedBuffer\": false"
			" }";
		return false;
	}
	rc = rsslSetEncodeIteratorBuffer (&it, buf);
	if (RSSL_RET_SUCCESS != rc) {
		LOG(ERROR) << prefix_ << "rsslSetEncodeIteratorBuffer: { "
			  "\"returnCode\": " << static_cast<signed> (rc) << ""
			", \"enumeration\": \"" << rsslRetCodeToString (rc) << "\""
			", \"text\": \"" << rsslRetCodeInfo (rc) << "\""
			" }";
		goto cleanup;
	}
	rc = rsslSetEncodeIteratorRWFVersion (&it, rwf_major_version(), rwf_minor_version());
	if (RSSL_RET_SUCCESS != rc) {
		LOG(ERROR) << prefix_ << "rsslSetEncodeIteratorRWFVersion: { "
			  "\"returnCode\": " << static_cast<signed> (rc) << ""
			", \"enumeration\": \"" << rsslRetCodeToString (rc) << "\""
			", \"text\": \"" << rsslRetCodeInfo (rc) << "\""
			", \"majorVersion\": " << static_cast<unsigned> (rwf_major_version()) << ""
			", \"minorVersion\": " << static_cast<unsigned> (rwf_minor_version()) << ""
			" }";
		goto cleanup;
	}
	rc = rsslEncodeMsg (&it, reinterpret_cast<RsslMsg*> (&response));
	if (RSSL_RET_SUCCESS != rc) {
		LOG(ERROR) << prefix_ << "rsslEncodeMsg: { "
			  "\"returnCode\": " << static_cast<signed> (rc) << ""
			", \"enumeration\": \"" << rsslRetCodeToString (rc) << "\""
			", \"text\": \"" << rsslRetCodeInfo (rc) << "\""
			" }";
		goto cleanup;
	}
	buf->length = rsslGetEncodedBufferLength (&it);
	LOG_IF(WARNING, 0 == buf->length) << prefix_ << "rsslGetEncodedBufferLength returned 0.";

/* Message validation: must use ASSERT libraries for error description :/ */
	if (!rsslValidateMsg (reinterpret_cast<RsslMsg*> (&response))) {
		cumulative_stats_[CLIENT_PC_MMT_LOGIN_RESPONSE_MALFORMED]++;
		LOG(ERROR) << prefix_ << "rsslValidateMsg failed.";
		goto cleanup;
	} else {
		cumulative_stats_[CLIENT_PC_MMT_LOGIN_RESPONSE_VALIDATED]++;
		DVLOG(4) << prefix_ << "rsslValidateMsg succeeded.";
	}

	if (!Submit (buf)) {
		goto cleanup;
	}
	cumulative_stats_[CLIENT_PC_MMT_LOGIN_REJECTED]++;
	return true;
cleanup:
	cumulative_stats_[CLIENT_PC_MMT_LOGIN_EXCEPTION]++;
	if (RSSL_RET_SUCCESS != rsslReleaseBuffer (buf, &rssl_err)) {
		LOG(WARNING) << prefix_ << "rsslReleaseBuffer: { "
			  "\"rsslErrorId\": " << rssl_err.rsslErrorId << ""
			", \"sysError\": " << rssl_err.sysError << ""
			", \"text\": \"" << rssl_err.text << "\""
			" }";
	}
	return false;
}

/** Accepting Login **
 * In the case where the Provider accepts the login, it should create a RespMsg
 * with RespType and RespStatus set according to RFA API 7 RDM Usage Guide. The
 * provider application should populate an OMMSolicitedItemCmd with this
 * RespMsg, set the corresponding request token and call submit() on the OMM
 * Provider.
 *
 * NB: There can only be one login per client session.
 */
bool
kigoron::client_t::AcceptLogin (
	const RsslRequestMsg* login_msg,
	int32_t login_token
	)
{
#ifndef NDEBUG
	RsslRefreshMsg response = RSSL_INIT_REFRESH_MSG;
	RsslEncodeIterator it = RSSL_INIT_ENCODE_ITERATOR;
	RsslElementList	element_list = RSSL_INIT_ELEMENT_LIST;
	RsslElementEntry element_entry = RSSL_INIT_ELEMENT_ENTRY;
	RsslBuffer data_buffer = RSSL_INIT_BUFFER;
#else
	RsslRefreshMsg response;
	RsslEncodeIterator it;
	RsslElementList	element_list;
	RsslElementEntry element_entry;
	RsslBuffer data_buffer;
	rsslClearRefreshMsg (&response);
	rsslClearEncodeIterator (&it);
	rsslClearElementList (&element_list);
	rsslClearElementEntry (&element_entry);
	rsslClearBuffer (&data_buffer);
#endif
	RsslBuffer* buf;
	RsslError rssl_err;
	RsslRet rc;

	DCHECK (nullptr != login_msg);
	VLOG(2) << prefix_ << "Sending MMT_LOGIN accepted.";

/* Set the message model type. */
	response.msgBase.domainType = RSSL_DMT_LOGIN;
/* Set response type. */
	response.msgBase.msgClass = RSSL_MC_REFRESH;
	response.flags = RSSL_RFMF_SOLICITED | RSSL_RFMF_REFRESH_COMPLETE;
/* No payload. */
	response.msgBase.containerType = RSSL_DT_NO_DATA;
/* Set the login token. */
	response.msgBase.streamId = login_token;

/* In RFA lingo an attribute object */
	response.msgBase.msgKey.nameType = login_msg->msgBase.msgKey.nameType;
	response.msgBase.msgKey.name.data = login_msg->msgBase.msgKey.name.data;
	response.msgBase.msgKey.name.length = login_msg->msgBase.msgKey.name.length;
	response.msgBase.msgKey.flags = RSSL_MKF_HAS_NAME_TYPE | RSSL_MKF_HAS_NAME;
	response.flags |= RSSL_RFMF_HAS_MSG_KEY;

/* RDM 3.3.2 Login Response Elements */
	response.msgBase.msgKey.attribContainerType = RSSL_DT_ELEMENT_LIST;
	response.msgBase.msgKey.flags |= RSSL_MKF_HAS_ATTRIB;

/* Item interaction state. */
	response.state.streamState = RSSL_STREAM_OPEN;
/* Data quality state. */
	response.state.dataState = RSSL_DATA_OK;
/* Error code. */
	response.state.code = RSSL_SC_NONE;

	buf = rsslGetBuffer (handle_, MAX_MSG_SIZE, RSSL_FALSE /* not packed */, &rssl_err);
	if (nullptr == buf) {
		LOG(ERROR) << prefix_ << "rsslGetBuffer: { "
			  "\"rsslErrorId\": " << rssl_err.rsslErrorId << ""
			", \"sysError\": " << rssl_err.sysError << ""
			", \"text\": \"" << rssl_err.text << "\""
			", \"size\": " << MAX_MSG_SIZE << ""
			", \"packedBuffer\": false"
			" }";
		return false;
	}	
	rc = rsslSetEncodeIteratorBuffer (&it, buf);
	if (RSSL_RET_SUCCESS != rc) {
		LOG(ERROR) << prefix_ << "rsslSetEncodeIteratorBuffer: { "
			  "\"returnCode\": " << static_cast<signed> (rc) << ""
			", \"enumeration\": \"" << rsslRetCodeToString (rc) << "\""
			", \"text\": \"" << rsslRetCodeInfo (rc) << "\""
			" }";
		goto cleanup;
	}
	rc = rsslSetEncodeIteratorRWFVersion (&it, rwf_major_version(), rwf_minor_version());
	if (RSSL_RET_SUCCESS != rc) {
		LOG(ERROR) << prefix_ << "rsslSetEncodeIteratorRWFVersion: { "
			  "\"returnCode\": " << static_cast<signed> (rc) << ""
			", \"enumeration\": \"" << rsslRetCodeToString (rc) << "\""
			", \"text\": \"" << rsslRetCodeInfo (rc) << "\""
			", \"majorVersion\": " << static_cast<unsigned> (rwf_major_version()) << ""
			", \"minorVersion\": " << static_cast<unsigned> (rwf_minor_version()) << ""
			" }";
		goto cleanup;
	}
	rc = rsslEncodeMsgInit (&it, reinterpret_cast<RsslMsg*> (&response), MAX_MSG_SIZE);
	if (RSSL_RET_ENCODE_MSG_KEY_OPAQUE != rc) {
		LOG(ERROR) << prefix_ << "rsslEncodeMsgInit: { "
			  "\"returnCode\": " << static_cast<signed> (rc) << ""
			", \"enumeration\": \"" << rsslRetCodeToString (rc) << "\""
			", \"text\": \"" << rsslRetCodeInfo (rc) << "\""
			", \"dataMaxSize\": " << MAX_MSG_SIZE << ""
			" }";
		goto cleanup;
	}

/* Encode attribute object after message instead of before as per RFA. */
	element_list.flags = RSSL_ELF_HAS_STANDARD_DATA;
	rc = rsslEncodeElementListInit (&it, &element_list, nullptr /* element id dictionary */, 4 /* count of elements */);
	if (RSSL_RET_SUCCESS != rc) {
		LOG(ERROR) << prefix_ << "rsslEncodeElementListInit: { "
			  "\"returnCode\": " << static_cast<signed> (rc) << ""
			", \"enumeration\": \"" << rsslRetCodeToString (rc) << "\""
			", \"text\": \"" << rsslRetCodeInfo (rc) << "\""
			", \"flags\": \"RSSL_ELF_HAS_STANDARD_DATA\""
			" }";
		goto cleanup;
	}

/* Images can be stale if process is left running without symbology update. */
	static const uint64_t allow_suspect_data = 1;
	element_entry.dataType	= RSSL_DT_UINT;
	element_entry.name	= RSSL_ENAME_ALLOW_SUSPECT_DATA;
	rc = rsslEncodeElementEntry (&it, &element_entry, &allow_suspect_data);
	if (RSSL_RET_SUCCESS != rc) {
		LOG(ERROR) << prefix_ << "rsslEncodeElementEntry: { "
			  "\"returnCode\": " << static_cast<signed> (rc) << ""
			", \"enumeration\": \"" << rsslRetCodeToString (rc) << "\""
			", \"text\": \"" << rsslRetCodeInfo (rc) << "\""
			", \"name\": \"RSSL_ENAME_ALLOW_SUSPECT_DATA\""
			", \"dataType\": \"" << rsslDataTypeToString (element_entry.dataType) << "\""
			", \"allowSuspectData\": " << allow_suspect_data << ""
			" }";
		return false;
	}
/* Application name. */
        element_entry.dataType  = RSSL_DT_ASCII_STRING;
        element_entry.name      = RSSL_ENAME_APPNAME;
        data_buffer.data   = const_cast<char*> (provider_->application_name().c_str());
        data_buffer.length = static_cast<uint32_t> (provider_->application_name().size());
        rc = rsslEncodeElementEntry (&it, &element_entry, &data_buffer);
        if (RSSL_RET_SUCCESS != rc) {
                LOG(ERROR) << prefix_ << "rsslEncodeElementEntry: { "
                          "\"returnCode\": " << static_cast<signed> (rc) << ""
                        ", \"enumeration\": \"" << rsslRetCodeToString (rc) << "\""
                        ", \"text\": \"" << rsslRetCodeInfo (rc) << "\""
                        ", \"name\": \"RSSL_ENAME_APPNAME\""
                        ", \"dataType\": \"" << rsslDataTypeToString (element_entry.dataType) << "\""
                        ", \"applicationName\": " << provider_->application_name() << ""
                        " }";
                return false;
        }
/* No DACS locks. */
	static const uint64_t provide_permission_expressions = 0;
	element_entry.dataType	= RSSL_DT_UINT;
	element_entry.name	= RSSL_ENAME_PROV_PERM_EXP;
	rc = rsslEncodeElementEntry (&it, &element_entry, &provide_permission_expressions);
	if (RSSL_RET_SUCCESS != rc) {
		LOG(ERROR) << prefix_ << "rsslEncodeElementEntry: { "
			  "\"returnCode\": " << static_cast<signed> (rc) << ""
			", \"enumeration\": \"" << rsslRetCodeToString (rc) << "\""
			", \"text\": \"" << rsslRetCodeInfo (rc) << "\""
			", \"name\": \"RSSL_ENAME_PROV_PERM_EXP\""
			", \"dataType\": \"" << rsslDataTypeToString (element_entry.dataType) << "\""
			", \"providePermissionExpressions\": " << provide_permission_expressions << ""
			" }";
		goto cleanup;
	}
/* No permission profile. */
	static const uint64_t provide_permission_profile = 0;
	element_entry.dataType	= RSSL_DT_UINT;
	element_entry.name	= RSSL_ENAME_PROV_PERM_PROF;
	rc = rsslEncodeElementEntry (&it, &element_entry, &provide_permission_profile);
	if (RSSL_RET_SUCCESS != rc) {
		LOG(ERROR) << prefix_ << "rsslEncodeElementEntry: { "
			  "\"returnCode\": " << static_cast<signed> (rc) << ""
			", \"enumeration\": \"" << rsslRetCodeToString (rc) << "\""
			", \"text\": \"" << rsslRetCodeInfo (rc) << "\""
			", \"name\": \"RSSL_ENAME_PROV_PERM_PROF\""
			", \"dataType\": \"" << rsslDataTypeToString (element_entry.dataType) << "\""
			", \"providePermissionProfile\": " << provide_permission_profile << ""
			" }";
		goto cleanup;
	}
/* Downstream application drives stream recovery. */
	static const uint64_t multiple_open = 0;
	element_entry.dataType	= RSSL_DT_UINT;
	element_entry.name	= RSSL_ENAME_SINGLE_OPEN;
	rc = rsslEncodeElementEntry (&it, &element_entry, &multiple_open);
	if (RSSL_RET_SUCCESS != rc) {
		LOG(ERROR) << prefix_ << "rsslEncodeElementEntry: { "
			  "\"returnCode\": " << static_cast<signed> (rc) << ""
			", \"enumeration\": \"" << rsslRetCodeToString (rc) << "\""
			", \"text\": \"" << rsslRetCodeInfo (rc) << "\""
			", \"name\": \"RSSL_ENAME_SINGLE_OPEN\""
			", \"dataType\": \"" << rsslDataTypeToString (element_entry.dataType) << "\""
			", \"singleOpen\": " << multiple_open << ""
			" }";
		goto cleanup;
	}
/* Batch requests not supported. */
/* OMM posts not supported. */
/* Optimized pause and resume not supported. */
/* Views not supported. */
/* Warm standby not supported. */
/* Binding complete. */
	rc = rsslEncodeElementListComplete (&it, RSSL_TRUE /* commit */);
	if (RSSL_RET_SUCCESS != rc) {
		LOG(ERROR) << prefix_ << "rsslEncodeElementListComplete: { "
			  "\"returnCode\": " << static_cast<signed> (rc) << ""
			", \"enumeration\": \"" << rsslRetCodeToString (rc) << "\""
			", \"text\": \"" << rsslRetCodeInfo (rc) << "\""
			" }";
		goto cleanup;
	}
	rc = rsslEncodeMsgKeyAttribComplete (&it, RSSL_TRUE /* commit */);
	if (RSSL_RET_SUCCESS != rc) {
		LOG(ERROR) << prefix_ << "rsslEncodeMsgKeyAttribComplete: { "
			  "\"returnCode\": " << static_cast<signed> (rc) << ""
			", \"enumeration\": \"" << rsslRetCodeToString (rc) << "\""
			", \"text\": \"" << rsslRetCodeInfo (rc) << "\""
			" }";
		goto cleanup;
	}
	if (RSSL_RET_SUCCESS != rsslEncodeMsgComplete (&it, RSSL_TRUE /* commit */)) {
		LOG(ERROR) << prefix_ << "rsslEncodeMsgComplete: { "
			  "\"returnCode\": " << static_cast<signed> (rc) << ""
			", \"enumeration\": \"" << rsslRetCodeToString (rc) << "\""
			", \"text\": \"" << rsslRetCodeInfo (rc) << "\""
			" }";
		goto cleanup;
	}
	buf->length = rsslGetEncodedBufferLength (&it);
	LOG_IF(WARNING, 0 == buf->length) << prefix_ << "rsslGetEncodedBufferLength returned 0.";

/* Message validation: must use ASSERT libraries for error description :/ */
//	if (!rsslValidateMsg (reinterpret_cast<RsslMsg*> (&response))) {
//		cumulative_stats_[CLIENT_PC_MMT_LOGIN_RESPONSE_MALFORMED]++;
//		LOG(ERROR) << prefix_ << "rsslValidateMsg failed.";
//		goto cleanup;
//	} else {
//		cumulative_stats_[CLIENT_PC_MMT_LOGIN_RESPONSE_VALIDATED]++;
//		DVLOG(4) << prefix_ << "rsslValidateMsg succeeded.";
//	}

	if (!Submit (buf)) {
		goto cleanup;
	}
	cumulative_stats_[CLIENT_PC_MMT_LOGIN_ACCEPTED]++;
	return true;
cleanup:
	cumulative_stats_[CLIENT_PC_MMT_LOGIN_EXCEPTION]++;
	if (RSSL_RET_SUCCESS != rsslReleaseBuffer (buf, &rssl_err)) {
		LOG(WARNING) << prefix_ << "rsslReleaseBuffer: { "
			  "\"rsslErrorId\": " << rssl_err.rsslErrorId << ""
			", \"sysError\": " << rssl_err.sysError << ""
			", \"text\": \"" << rssl_err.text << "\""
			" }";
	}
	return false;
}

/* 7.4. Provide Source Directory Information.
 * RDM 4.2.1 ReqMsg
 * Streaming request or Nonstreaming request. No special semantics or
 * restrictions. Pause request is not supported.
 */
bool
kigoron::client_t::OnDirectoryRequest (
	RsslDecodeIterator* it,
	const RsslRequestMsg* request_msg
	)
{
	DCHECK (nullptr != request_msg);
	cumulative_stats_[CLIENT_PC_MMT_DIRECTORY_REQUEST_RECEIVED]++;

	static const uint16_t streaming_request = RSSL_RQMF_STREAMING;
/* NB: snapshot_request == !streaming_request */

	const bool is_streaming_request = (streaming_request == request_msg->flags);
	const bool is_snapshot_request	= !is_streaming_request;

/* RDM 4.2.4 AttribInfo required for ReqMsg. */
	const bool has_attribinfo = true;

/* Filtering of directory contents. */
	const bool has_service_name = has_attribinfo && (RSSL_MKF_HAS_NAME	 == (request_msg->msgBase.msgKey.flags & RSSL_MKF_HAS_NAME));
	const bool has_service_id   = has_attribinfo && (RSSL_MKF_HAS_SERVICE_ID == (request_msg->msgBase.msgKey.flags & RSSL_MKF_HAS_SERVICE_ID));
	const uint32_t filter_mask  = request_msg->msgBase.msgKey.filter;

/* Fall over on multiple directory tokens. */
	const int32_t request_token = directory_token_ = request_msg->msgBase.streamId;
	if (has_service_name)
	{
		const char* service_name = request_msg->msgBase.msgKey.name.data;
		return SendDirectoryRefresh (request_token, service_name, filter_mask);
	}
	else if (has_service_id && 0 != request_msg->msgBase.msgKey.serviceId)
	{
		const uint16_t service_id = request_msg->msgBase.msgKey.serviceId;
		if (service_id == provider_->service_id()) {
			return SendDirectoryRefresh (request_token, provider_->service_name().c_str(), filter_mask);
		} else {
/* default to full directory if id does not match */
			LOG(WARNING) << prefix_ << "Received MMT_DIRECTORY request for unknown service id #" << service_id << ", returning entire directory.";
			return SendDirectoryRefresh (request_token, nullptr, filter_mask);
		}
	}
/* Provide all services directory. */
	else
	{
		return SendDirectoryRefresh (request_token, nullptr, filter_mask);
	}
}

bool
kigoron::client_t::OnDictionaryRequest (
	RsslDecodeIterator* it,
	const RsslRequestMsg* request_msg
	)
{
	DCHECK (nullptr != request_msg);
	cumulative_stats_[CLIENT_PC_MMT_DICTIONARY_REQUEST_RECEIVED]++;
	VLOG(10) << prefix_ << "DictionaryRequest:" << *request_msg;

/* Unsupported for this provider and declared so in the directory. */
	return SendClose (
		request_msg->msgBase.streamId,
		request_msg->msgBase.msgKey.serviceId,
		request_msg->msgBase.domainType,
		chromium::StringPiece (request_msg->msgBase.msgKey.name.data, request_msg->msgBase.msgKey.name.length),
		RSSL_RQMF_MSG_KEY_IN_UPDATES == (request_msg->flags & RSSL_RQMF_MSG_KEY_IN_UPDATES),
		RSSL_STREAM_CLOSED, RSSL_SC_USAGE_ERROR, kErrorUnsupportedDictionary
		);
}

bool
kigoron::client_t::OnItemRequest (
	RsslDecodeIterator* it,
	const RsslRequestMsg* request_msg
	)
{
	DCHECK (nullptr != request_msg);
	cumulative_stats_[CLIENT_PC_ITEM_REQUEST_RECEIVED]++;
	VLOG(10) << prefix_ << "ItemRequest:" << *request_msg;

/* 10.3.6 Handling Item Requests
 * - Ensure that the requesting session is logged in.
 * - Determine whether the requested QoS can be satisified.
 * - Ensure that the same stream is not already provisioned.
 */

/* A response is not required to be immediately generated, for example
 * forwarding the clients request to an upstream resource and waiting for
 * a reply.
 */
	const uint16_t service_id    = request_msg->msgBase.msgKey.serviceId;
	const uint8_t  model_type    = request_msg->msgBase.domainType;
	const std::string item_name (request_msg->msgBase.msgKey.name.data, request_msg->msgBase.msgKey.name.length);
	const bool use_attribinfo_in_updates = !!(request_msg->flags & RSSL_RQMF_MSG_KEY_IN_UPDATES);

/* 7.4.3.2 Request Tokens
 * Providers should not attempt to submit data after the provider has received a close request for an item. */
	const int32_t request_token = request_msg->msgBase.streamId;

	if (!is_logged_in_) {
		cumulative_stats_[CLIENT_PC_ITEM_REQUEST_REJECTED]++;
		cumulative_stats_[CLIENT_PC_ITEM_REQUEST_BEFORE_LOGIN]++;
		LOG(INFO) << prefix_ << "Closing request for client without accepted login.";
		return SendClose (
			request_token,
			service_id,
			model_type,
			item_name,
			use_attribinfo_in_updates,
			RSSL_STREAM_CLOSED, RSSL_SC_USAGE_ERROR, kErrorLoginRequired
			);
	}

/* Filtered before entry. */
	CHECK(RSSL_DMT_MARKET_PRICE == model_type);

	const bool is_streaming_request = (RSSL_RQMF_STREAMING == (request_msg->flags & RSSL_RQMF_STREAMING));
	const uint8_t stream_state = is_streaming_request ? RSSL_STREAM_OPEN : RSSL_STREAM_NON_STREAMING;
	if (is_streaming_request) {
		cumulative_stats_[CLIENT_PC_ITEM_STREAMING_REQUEST_RECEIVED]++;
	} else {
		cumulative_stats_[CLIENT_PC_ITEM_SNAPSHOT_REQUEST_RECEIVED]++;
	}
	const auto jt = tokens_.find (request_token);
	if (jt != tokens_.end()) {
		cumulative_stats_[CLIENT_PC_ITEM_REISSUE_REQUEST_RECEIVED]++;
/* Explicitly ignore reissue as it does not alter response data. */
		return true;
	} else {
		tokens_.emplace (request_token);
	}

	return delegate_->OnRequest (last_activity_, reinterpret_cast<uintptr_t> (handle_), rwf_version(), request_token, service_id, item_name, use_attribinfo_in_updates);
}

bool
kigoron::client_t::OnSourceDirectoryUpdate()
{
	return SendDirectoryUpdate (directory_token_, provider_->service_name().c_str());
}

bool
kigoron::client_t::SendReply (
	int32_t request_token,
	const void* data,
	size_t length
	)
{
	RsslBuffer* buf;
	RsslError rssl_err;
	DCHECK(length <= MAX_MSG_SIZE);
/* Drop response if token already canceled */
	if (0 == tokens_.erase (request_token))
		return true;
/* Copy into RSSL channel buffer pool */
	buf = rsslGetBuffer (handle_, MAX_MSG_SIZE, RSSL_FALSE /* not packed */, &rssl_err);
	if (nullptr == buf) {
		LOG(ERROR) << prefix_ << "rsslGetBuffer: { "
			  "\"rsslErrorId\": " << rssl_err.rsslErrorId << ""
			", \"sysError\": " << rssl_err.sysError << ""
			", \"text\": \"" << rssl_err.text << "\""
			", \"size\": " << MAX_MSG_SIZE << ""
			", \"packedBuffer\": false"
			" }";
		return false;
	}
	CopyMemory (buf->data, data, length);
	buf->length = static_cast<uint32_t> (length);
	if (!Submit (buf)) {
		goto cleanup;
	}
	cumulative_stats_[CLIENT_PC_ITEM_SENT]++;
	return true;
cleanup:
	if (RSSL_RET_SUCCESS != rsslReleaseBuffer (buf, &rssl_err)) {
		LOG(WARNING) << prefix_ << "rsslReleaseBuffer: { "
			  "\"rsslErrorId\": " << rssl_err.rsslErrorId << ""
			", \"sysError\": " << rssl_err.sysError << ""
			", \"text\": \"" << rssl_err.text << "\""
			" }";
	}
	return false;
}

bool
kigoron::client_t::OnCloseMsg (
	RsslDecodeIterator* it,
	const RsslCloseMsg* close_msg
	)
{
	DCHECK (nullptr != close_msg);
	cumulative_stats_[CLIENT_PC_CLOSE_MSGS_RECEIVED]++;
	switch (close_msg->msgBase.domainType) {
	case RSSL_DMT_MARKET_PRICE:
		return OnItemClose (close_msg);
	case RSSL_DMT_LOGIN:
/* toggle login status. */
		cumulative_stats_[CLIENT_PC_MMT_LOGIN_CLOSE_RECEIVED]++;
		if (!is_logged_in_) {
			cumulative_stats_[CLIENT_PC_CLOSE_MSGS_DISCARDED]++;
			LOG(WARNING) << prefix_ << "Close on MMT_LOGIN whilst not logged in.";
		} else {
			is_logged_in_ = false;
			login_token_ = 0;
/* TODO: cleanup client state. */
			LOG(INFO) << prefix_ << "Client session logged out.";
		}
		break;
	case RSSL_DMT_SOURCE:	/* Directory */
/* directory subscription maintains no state, close is a no-op. */
		cumulative_stats_[CLIENT_PC_MMT_DIRECTORY_CLOSE_RECEIVED]++;
		LOG(INFO) << prefix_ << "Directory closed.";
		break;
	case RSSL_DMT_DICTIONARY:
/* dictionary is unsupported so a usage error. */
		cumulative_stats_[CLIENT_PC_MMT_DICTIONARY_CLOSE_RECEIVED]++;
	case RSSL_DMT_MARKET_BY_ORDER:
	case RSSL_DMT_MARKET_BY_PRICE:
	case RSSL_DMT_MARKET_MAKER:
	case RSSL_DMT_SYMBOL_LIST:
	case RSSL_DMT_YIELD_CURVE:
	default:
		cumulative_stats_[CLIENT_PC_CLOSE_MSGS_DISCARDED]++;
		LOG(WARNING) << prefix_ << "Uncaught close message: " << close_msg;
		break;
	}

	return true;
}

bool
kigoron::client_t::OnItemClose (
	const RsslCloseMsg* close_msg
	)
{
	DCHECK (nullptr != close_msg);
	cumulative_stats_[CLIENT_PC_ITEM_CLOSE_RECEIVED]++;
	VLOG(10) << prefix_ << "ItemClose:" << *close_msg;

	const uint16_t service_id    = close_msg->msgBase.msgKey.serviceId;
	const uint8_t  model_type    = close_msg->msgBase.domainType;
	const chromium::StringPiece item_name (close_msg->msgBase.msgKey.name.data, close_msg->msgBase.msgKey.name.length);
/* Close message does not define this flag, go with lowest common denominator. */
	const bool use_attribinfo_in_updates = true;

/* 7.4.3.2 Request Tokens
 * Providers should not attempt to submit data after the provider has received a close request for an item. */
	const int32_t request_token = close_msg->msgBase.streamId;

	if (!is_logged_in_) {
		cumulative_stats_[CLIENT_PC_CLOSE_MSGS_DISCARDED]++;
		LOG(INFO) << prefix_ << "Discarding close for client without accepted login.";
		return true;
	}

/* Verify domain model */
	if (RSSL_DMT_MARKET_PRICE != model_type)
	{
		cumulative_stats_[CLIENT_PC_CLOSE_MSGS_DISCARDED]++;
		LOG(INFO) << prefix_ << "Discarding close request for unsupported message model type.";
		return true;
	}

/* Remove token */
	const auto it = tokens_.find (request_token);
	if (it == tokens_.end()) {
		cumulative_stats_[CLIENT_PC_CLOSE_MSGS_DISCARDED]++;
		LOG(INFO) << prefix_ << "Discarding close request on closed item.";
	} else {		
		tokens_.erase (it);
		cumulative_stats_[CLIENT_PC_ITEM_CLOSED]++;
		DLOG(INFO) << prefix_ << "Closed open request.";
	}
/* Question: close on streaming or non-streaming request? */
	return true;
}

/* 10.3.4 Providing Service Directory (Interactive)
 * A Consumer typically requests a Directory from a Provider to retrieve
 * information about available services and their capabilities, and it is the
 * responsibility of the Provider to encode and supply the directory.
 */
bool
kigoron::client_t::SendDirectoryRefresh (
	int32_t request_token,
	const char* service_name,
	uint32_t filter_mask
	)
{
/* 7.5.9.1 Create a response message (4.2.2) */
	RsslRefreshMsg response = RSSL_INIT_REFRESH_MSG;
#ifndef NDEBUG
	RsslEncodeIterator it = RSSL_INIT_ENCODE_ITERATOR;
#else
	RsslEncodeIterator it;
	rsslClearEncodeIterator (&it);
#endif
	RsslBuffer* buf;
	RsslError rssl_err;
	RsslRet rc;

	VLOG(2) << prefix_ << "Sending directory refresh.";

/* 7.5.9.2 Set the message model type of the response. */
	response.msgBase.domainType = RSSL_DMT_SOURCE;
/* 7.5.9.3 Set response type. */
	response.msgBase.msgClass = RSSL_MC_REFRESH;
/* 7.5.9.4 Set the response type enumeration.
 * Note type is unsolicited despite being a mandatory requirement before
 * publishing.
 */
	response.flags = RSSL_RFMF_SOLICITED | RSSL_RFMF_REFRESH_COMPLETE;
/* Directory map. */
	response.msgBase.containerType = RSSL_DT_MAP;
/* DataMask: required for refresh RespMsg
 *   SERVICE_INFO_FILTER  - Static information about service.
 *   SERVICE_STATE_FILTER - Refresh or update state.
 *   SERVICE_GROUP_FILTER - Transient groups within service.
 *   SERVICE_LOAD_FILTER  - Statistics about concurrent stream support.
 *   SERVICE_DATA_FILTER  - Broadcast data.
 *   SERVICE_LINK_FILTER  - Load balance grouping.
 */
	response.msgBase.msgKey.filter = filter_mask & (RDM_DIRECTORY_SERVICE_INFO_FILTER | RDM_DIRECTORY_SERVICE_STATE_FILTER | RDM_DIRECTORY_SERVICE_LOAD_FILTER);
/* Name:	Not used */
/* NameType:	Not used */
/* ServiceName: Not used */
/* ServiceId:	Not used */
/* Id:		Not used */
/* Attrib:	Not used */
	response.msgBase.msgKey.flags = RSSL_MKF_HAS_FILTER;
	response.flags |= RSSL_RFMF_HAS_MSG_KEY;
/* set token */
	response.msgBase.streamId = request_token;

/* Item interaction state. */
	response.state.streamState = RSSL_STREAM_OPEN;
/* Data quality state. */
	response.state.dataState = RSSL_DATA_OK;
/* Error code. */
	response.state.code = RSSL_SC_NONE;

/* pop buffer from RSSL memory pool */
	buf = rsslGetBuffer (handle_, MAX_MSG_SIZE, RSSL_FALSE /* not packed */, &rssl_err);
	if (nullptr == buf) {
		LOG(ERROR) << prefix_ << "rsslGetBuffer: { "
			  "\"rsslErrorId\": " << rssl_err.rsslErrorId << ""
			", \"sysError\": " << rssl_err.sysError << ""
			", \"text\": \"" << rssl_err.text << "\""
			", \"size\": " << MAX_MSG_SIZE << ""
			", \"packedBuffer\": false"
			" }";
		return false;
	}
/* tie buffer to RSSL write iterator */
	rc = rsslSetEncodeIteratorBuffer (&it, buf);
	if (RSSL_RET_SUCCESS != rc) {
		LOG(ERROR) << prefix_ << "rsslSetEncodeIteratorBuffer: { "
			  "\"returnCode\": " << static_cast<signed> (rc) << ""
			", \"enumeration\": \"" << rsslRetCodeToString (rc) << "\""
			", \"text\": \"" << rsslRetCodeInfo (rc) << "\""
			" }";
		goto cleanup;
	}
/* encode with clients preferred protocol version */
	rc = rsslSetEncodeIteratorRWFVersion (&it, rwf_major_version(), rwf_minor_version());
	if (RSSL_RET_SUCCESS != rc) {
		LOG(ERROR) << prefix_ << "rsslSetEncodeIteratorRWFVersion: { "
			  "\"returnCode\": " << static_cast<signed> (rc) << ""
			", \"enumeration\": \"" << rsslRetCodeToString (rc) << "\""
			", \"text\": \"" << rsslRetCodeInfo (rc) << "\""
			", \"majorVersion\": " << static_cast<unsigned> (rwf_major_version()) << ""
			", \"minorVersion\": " << static_cast<unsigned> (rwf_minor_version()) << ""
			" }";
		goto cleanup;
	}
/* start multi-step encoder */
	rc = rsslEncodeMsgInit (&it, reinterpret_cast<RsslMsg*> (&response), MAX_MSG_SIZE);
	if (RSSL_RET_ENCODE_CONTAINER != rc) {
		LOG(ERROR) << prefix_ << "rsslEncodeMsgInit failed: { "
			  "\"returnCode\": " << static_cast<signed> (rc) << ""
			", \"enumeration\": \"" << rsslRetCodeToString (rc) << "\""
			", \"text\": \"" << rsslRetCodeInfo (rc) << "\""
			", \"dataMaxSize\": " << MAX_MSG_SIZE << ""
			" }";
		goto cleanup;
	}
/* populate directory map */
	if (!provider_->GetDirectoryMap (&it, service_name, filter_mask, RSSL_MPEA_ADD_ENTRY)) {
		LOG(ERROR) << prefix_ << "GetDirectoryMap failed.";
		goto cleanup;
	}
/* finalize multi-step encoder */
	rc = rsslEncodeMsgComplete (&it, RSSL_TRUE /* commit */);
	if (RSSL_RET_SUCCESS != rc) {
		LOG(ERROR) << prefix_ << "rsslEncodeMsgComplete: { "
			  "\"returnCode\": " << static_cast<signed> (rc) << ""
			", \"enumeration\": \"" << rsslRetCodeToString (rc) << "\""
			", \"text\": \"" << rsslRetCodeInfo (rc) << "\""
			" }";
		goto cleanup;
	}
	buf->length = rsslGetEncodedBufferLength (&it);
	LOG_IF(WARNING, 0 == buf->length) << prefix_ << "rsslGetEncodedBufferLength returned 0.";

/* Message validation. */
	if (!rsslValidateMsg (reinterpret_cast<RsslMsg*> (&response))) {
		cumulative_stats_[CLIENT_PC_MMT_DIRECTORY_MALFORMED]++;
		LOG(ERROR) << prefix_ << "rsslValidateMsg failed.";
		goto cleanup;
	} else {
		cumulative_stats_[CLIENT_PC_MMT_DIRECTORY_VALIDATED]++;
		DVLOG(4) << prefix_ << "rsslValidateMsg succeeded.";
	}

	if (!Submit (buf)) {
		LOG(ERROR) << prefix_ << "Submit failed.";
		goto cleanup;
	}
	cumulative_stats_[CLIENT_PC_MMT_DIRECTORY_SENT]++;
	return true;
cleanup:
	if (RSSL_RET_SUCCESS != rsslReleaseBuffer (buf, &rssl_err)) {
		LOG(WARNING) << prefix_ << "rsslReleaseBuffer: { "
			  "\"rsslErrorId\": " << rssl_err.rsslErrorId << ""
			", \"sysError\": " << rssl_err.sysError << ""
			", \"text\": \"" << rssl_err.text << "\""
			" }";
	}
	return false;
}

bool
kigoron::client_t::SendDirectoryUpdate (
	int32_t directory_token,
	const char* service_name	/* can by nullptr */
	)
{
	RsslUpdateMsg response = RSSL_INIT_UPDATE_MSG;
#ifndef NDEBUG
	RsslEncodeIterator it = RSSL_INIT_ENCODE_ITERATOR;
#else
	RsslEncodeIterator it;
	rsslClearEncodeIterator (&it);
#endif
	RsslBuffer* buf;
	RsslError rssl_err;
	RsslRet rc;

	VLOG(2) << prefix_ << "Sending directory update.";

	response.msgBase.domainType = RSSL_DMT_SOURCE;
	response.msgBase.msgClass = RSSL_MC_UPDATE;
	response.flags = RSSL_UPMF_DO_NOT_CONFLATE;
	response.msgBase.containerType = RSSL_DT_MAP;
	response.msgBase.msgKey.filter = RDM_DIRECTORY_SERVICE_STATE_FILTER;
	response.msgBase.msgKey.flags = RSSL_MKF_HAS_FILTER;
	response.flags |= RSSL_UPMF_HAS_MSG_KEY;
	response.msgBase.streamId = directory_token_;

	buf = rsslGetBuffer (handle_, MAX_MSG_SIZE, RSSL_FALSE /* not packed */, &rssl_err);
	if (nullptr == buf) {
		LOG(ERROR) << prefix_ << "rsslGetBuffer: { "
			  "\"rsslErrorId\": " << rssl_err.rsslErrorId << ""
			", \"sysError\": " << rssl_err.sysError << ""
			", \"text\": \"" << rssl_err.text << "\""
			", \"size\": " << MAX_MSG_SIZE << ""
			", \"packedBuffer\": false"
			" }";
		return false;
	}
	rc = rsslSetEncodeIteratorBuffer (&it, buf);
	if (RSSL_RET_SUCCESS != rc) {
		LOG(ERROR) << prefix_ << "rsslSetEncodeIteratorBuffer: { "
			  "\"returnCode\": " << static_cast<signed> (rc) << ""
			", \"enumeration\": \"" << rsslRetCodeToString (rc) << "\""
			", \"text\": \"" << rsslRetCodeInfo (rc) << "\""
			" }";
		goto cleanup;
	}
	rc = rsslSetEncodeIteratorRWFVersion (&it, rwf_major_version(), rwf_minor_version());
	if (RSSL_RET_SUCCESS != rc) {
		LOG(ERROR) << prefix_ << "rsslSetEncodeIteratorRWFVersion: { "
			  "\"returnCode\": " << static_cast<signed> (rc) << ""
			", \"enumeration\": \"" << rsslRetCodeToString (rc) << "\""
			", \"text\": \"" << rsslRetCodeInfo (rc) << "\""
			", \"majorVersion\": " << static_cast<unsigned> (rwf_major_version()) << ""
			", \"minorVersion\": " << static_cast<unsigned> (rwf_minor_version()) << ""
			" }";
		goto cleanup;
	}
	rc = rsslEncodeMsgInit (&it, reinterpret_cast<RsslMsg*> (&response), MAX_MSG_SIZE);
	if (RSSL_RET_ENCODE_CONTAINER != rc) {
		LOG(ERROR) << prefix_ << "rsslEncodeMsgInit failed: { "
			  "\"returnCode\": " << static_cast<signed> (rc) << ""
			", \"enumeration\": \"" << rsslRetCodeToString (rc) << "\""
			", \"text\": \"" << rsslRetCodeInfo (rc) << "\""
			", \"dataMaxSize\": " << MAX_MSG_SIZE << ""
			" }";
		goto cleanup;
	}
	if (!provider_->GetDirectoryMap (&it, service_name, RDM_DIRECTORY_SERVICE_STATE_FILTER, RSSL_MPEA_UPDATE_ENTRY)) {
		LOG(ERROR) << prefix_ << "GetDirectoryMap failed.";
		goto cleanup;
	}
	rc = rsslEncodeMsgComplete (&it, RSSL_TRUE /* commit */);
	if (RSSL_RET_SUCCESS != rc) {
		LOG(ERROR) << prefix_ << "rsslEncodeMsgComplete: { "
			  "\"returnCode\": " << static_cast<signed> (rc) << ""
				", \"enumeration\": \"" << rsslRetCodeToString (rc) << "\""
			", \"text\": \"" << rsslRetCodeInfo (rc) << "\""
			" }";
		goto cleanup;
	}
	buf->length = rsslGetEncodedBufferLength (&it);
	LOG_IF(WARNING, 0 == buf->length) << prefix_ << "rsslGetEncodedBufferLength returned 0.";
	if (!rsslValidateMsg (reinterpret_cast<RsslMsg*> (&response))) {
		cumulative_stats_[CLIENT_PC_MMT_DIRECTORY_MALFORMED]++;
		LOG(ERROR) << prefix_ << "rsslValidateMsg failed.";
		goto cleanup;
	} else {
		cumulative_stats_[CLIENT_PC_MMT_DIRECTORY_VALIDATED]++;
		DVLOG(4) << prefix_ << "rsslValidateMsg succeeded.";
	}

	if (!Submit (buf)) {
		LOG(ERROR) << prefix_ << "Submit failed.";
		goto cleanup;
	}
	cumulative_stats_[CLIENT_PC_MMT_DIRECTORY_SENT]++;
	return true;
cleanup:
	if (RSSL_RET_SUCCESS != rsslReleaseBuffer (buf, &rssl_err)) {
		LOG(WARNING) << prefix_ << "rsslReleaseBuffer: { "
			  "\"rsslErrorId\": " << rssl_err.rsslErrorId << ""
			", \"sysError\": " << rssl_err.sysError << ""
			", \"text\": \"" << rssl_err.text << "\""
			" }";
	}
	return false;
}

bool
kigoron::client_t::SendClose (
	int32_t request_token,
	uint16_t service_id,
	uint8_t model_type,
	const chromium::StringPiece& item_name,
	bool use_attribinfo_in_updates,
	uint8_t stream_state,
	uint8_t status_code,
	const chromium::StringPiece& status_text
	)
{
	RsslBuffer* buf;
	RsslError rssl_err;
	buf = rsslGetBuffer (handle_, MAX_MSG_SIZE, RSSL_FALSE /* not packed */, &rssl_err);
	if (nullptr == buf) {
		LOG(ERROR) << prefix_ << "rsslGetBuffer: { "
			  "\"rsslErrorId\": " << rssl_err.rsslErrorId << ""
			", \"sysError\": " << rssl_err.sysError << ""
			", \"text\": \"" << rssl_err.text << "\""
			", \"size\": " << MAX_MSG_SIZE << ""
			", \"packedBuffer\": false"
			" }";
		return false;
	}
	size_t rssl_length = buf->length;
	VLOG(2) << prefix_ << "Sending item close { "
		  "\"RequestToken\": " << request_token << ""
		", \"ServiceID\": " << service_id << ""
		", \"MsgModelType\": " << internal::domain_type_string (static_cast<RsslDomainTypes> (model_type)) << ""
		", \"Name\": \"" << item_name << "\""
		", \"NameLen\": " << item_name.size() << ""
		", \"AttribInfoInUpdates\": " << (use_attribinfo_in_updates ? "true" : "false") << ""
		", \"StatusCode\": " << rsslStateCodeToString (status_code) << ""
		", \"StatusText\": \"" << status_text << "\""
		" }";
	if (!provider_t::WriteRawClose (rwf_version(), request_token, service_id, model_type, item_name, use_attribinfo_in_updates, stream_state, status_code, status_text, buf->data, &rssl_length)) {
		goto cleanup;
	}
	buf->length = static_cast<uint32_t> (rssl_length);
	if (!Submit (buf)) {
		goto cleanup;
	}
	cumulative_stats_[CLIENT_PC_ITEM_CLOSED]++;
	return true;
cleanup:
	if (RSSL_RET_SUCCESS != rsslReleaseBuffer (buf, &rssl_err)) {
		LOG(WARNING) << prefix_ << "rsslReleaseBuffer: { "
			  "\"rsslErrorId\": " << rssl_err.rsslErrorId << ""
			", \"sysError\": " << rssl_err.sysError << ""
			", \"text\": \"" << rssl_err.text << "\""
			" }";
	}
	return false;
}

/* Forward submit requests to containing provider.
 */
int
kigoron::client_t::Submit (
	RsslBuffer* buf
	)
{
	DCHECK(nullptr != buf);
	const int status = provider_->Submit (handle_, buf);
	if (status) cumulative_stats_[CLIENT_PC_RSSL_MSGS_SENT]++;
	return status;
}

/* eof */
