/* UPA output streaming.
 */

#ifndef UPA_OSTREAM_HH_
#define UPA_OSTREAM_HH_

#include <ostream>
#include <sstream>

/* UPA 8.0 headers */
#include <upa/upa.h>
#include <rtr/rsslErrorInfo.h>

namespace internal {

/* enumerated types */
const char* channel_state_string (const RsslChannelState state_);
const char* compression_type_string (const RsslCompTypes type_);
const char* connection_type_string (const RsslConnectionTypes type_);
const char* container_type_string (const RsslContainerType type_);
const char* data_type_string (const RsslDataTypes type_);
const char* domain_type_string (const RsslDomainTypes type_);
const char* filter_entry_id_string (const RDMDirectoryServiceFilterIds id_);
const char* filter_entry_action_string (const RsslFilterEntryActions action_);
const char* instrument_type_string (const RDMInstrumentNameTypes type_);
const char* login_type_string (const RDMLoginUserIdTypes type_);
const char* map_entry_action_string (const RsslMapEntryActions action_);
const char* message_class_string (const RsslMsgClasses class_);
const char* primitive_type_string (const RsslPrimitiveType type_);
const char* protocol_type_string (const RsslUInt8 type_);
const char* real_hint_string (const RsslRealHints hint_);
const char* return_code_string (const RsslReturnCodes rc_);
const char* qos_rate_string (const RsslQosRates rate_);
const char* qos_timeliness_string (const RsslQosTimeliness timeliness_);
/* ValueAdd enumerations */
const char* error_info_code_string (const RsslErrorInfoCode code_);

/* complex types */
class RsslLoginKey {
	const RsslMsgKey& msg_key_;
public:
	RsslLoginKey (const RsslMsgKey& msg_key) :
		msg_key_ (msg_key)
	{
	}
	const RsslMsgKey& getRsslMsgKey() const
	{
		return msg_key_;
	}
};

inline
std::ostream& operator<< (std::ostream& o, const RsslLoginKey& wrapper) {
	std::ostringstream flags, service_id, name_type, name, filter, identifier, attrib;
	const RsslMsgKey& msg_key (wrapper.getRsslMsgKey());
	if (msg_key.flags & RSSL_MKF_HAS_SERVICE_ID) {
		flags << "RSSL_MKF_HAS_SERVICE_ID";
		service_id << ", \"serviceId\": " << msg_key.serviceId;
	}
	if (msg_key.flags & RSSL_MKF_HAS_NAME) {
		if (!flags.str().empty()) flags << '|';
		flags << "RSSL_MKF_HAS_NAME";
		name << ", \"name\": \"" << std::string (msg_key.name.data, msg_key.name.length) << "\"";
	}
	if (msg_key.flags & RSSL_MKF_HAS_NAME_TYPE) {
		if (!flags.str().empty()) flags << '|';
		flags << "RSSL_MKF_HAS_NAME_TYPE";
		name_type << ", \"nameType\": \"" << internal::login_type_string (static_cast<RDMLoginUserIdTypes> (msg_key.nameType)) << "\"";
	}
	if (msg_key.flags & RSSL_MKF_HAS_FILTER) {
		if (!flags.str().empty()) flags << '|';
		flags << "RSSL_MKF_HAS_FILTER";
		filter << ", \"filter\": " << msg_key.filter << "";
	}
	if (msg_key.flags & RSSL_MKF_HAS_IDENTIFIER) {
		if (!flags.str().empty()) flags << '|';
		flags << "RSSL_MKF_HAS_IDENTIFIER";
		identifier << ", \"identifier\": " << msg_key.identifier << "";
	}
	if (msg_key.flags & RSSL_MKF_HAS_ATTRIB) {
		if (!flags.str().empty()) flags << '|';
		flags << "RSSL_MKF_HAS_ATTRIB";
		attrib << ", \"attribContainerType\": \"" << internal::data_type_string (static_cast<RsslDataTypes> (msg_key.attribContainerType)) << "\"";
	}
	o << "\"MsgKey\": { "
		  "\"flags\": \"" << flags.str() << "\""
		<< service_id.str()
		<< name_type.str()
		<< name.str()
		<< filter.str()
		<< identifier.str()
		<< attrib.str() << " }";
	return o;
}

class RsslDirectoryKey {
	const RsslMsgKey& msg_key_;
public:
	RsslDirectoryKey (const RsslMsgKey& msg_key) :
		msg_key_ (msg_key)
	{
	}
	const RsslMsgKey& getRsslMsgKey() const
	{
		return msg_key_;
	}
};

inline
std::ostream& operator<< (std::ostream& o, const RsslDirectoryKey& wrapper) {
	std::ostringstream flags, service_id, name_type, name, filter, identifier, attrib;
	const RsslMsgKey& msg_key (wrapper.getRsslMsgKey());
	if (msg_key.flags & RSSL_MKF_HAS_SERVICE_ID) {
		flags << "RSSL_MKF_HAS_SERVICE_ID";
		service_id << ", \"serviceId\": " << msg_key.serviceId;
	}
	if (msg_key.flags & RSSL_MKF_HAS_NAME) {
		if (!flags.str().empty()) flags << '|';
		flags << "RSSL_MKF_HAS_NAME";
		name << ", \"name\": \"" << std::string (msg_key.name.data, msg_key.name.length) << "\"";
	}
	if (msg_key.flags & RSSL_MKF_HAS_NAME_TYPE) {
		if (!flags.str().empty()) flags << '|';
		flags << "RSSL_MKF_HAS_NAME_TYPE";
		name_type << ", \"nameType\": \"" << static_cast<unsigned> (msg_key.nameType) << "\"";
	}
	if (msg_key.flags & RSSL_MKF_HAS_FILTER) {
		if (!flags.str().empty()) flags << '|';
		flags << "RSSL_MKF_HAS_FILTER";
		std::ostringstream filter_flags;		
		if (msg_key.filter & RDM_DIRECTORY_SERVICE_INFO_FILTER) {
			filter_flags << "RDM_DIRECTORY_SERVICE_INFO_FILTER";
		}
		if (msg_key.filter & RDM_DIRECTORY_SERVICE_STATE_FILTER) {
			if (!filter_flags.str().empty()) filter_flags << '|';
			filter_flags << "RDM_DIRECTORY_SERVICE_STATE_FILTER";
		}
		if (msg_key.filter & RDM_DIRECTORY_SERVICE_GROUP_FILTER) {
			if (!filter_flags.str().empty()) filter_flags << '|';
			filter_flags << "RDM_DIRECTORY_SERVICE_GROUP_FILTER";
		}
		if (msg_key.filter & RDM_DIRECTORY_SERVICE_LOAD_FILTER) {
			if (!filter_flags.str().empty()) filter_flags << '|';
			filter_flags << "RDM_DIRECTORY_SERVICE_LOAD_FILTER";
		}
		if (msg_key.filter & RDM_DIRECTORY_SERVICE_DATA_FILTER) {
			if (!filter_flags.str().empty()) filter_flags << '|';
			filter_flags << "RDM_DIRECTORY_SERVICE_DATA_FILTER";
		}
		if (msg_key.filter & RDM_DIRECTORY_SERVICE_LINK_FILTER) {
			if (!filter_flags.str().empty()) filter_flags << '|';
			filter_flags << "RDM_DIRECTORY_SERVICE_LINK_FILTER";
		}
		filter << ", \"filter\": \"" << filter_flags.str() << "\"";
	}
	if (msg_key.flags & RSSL_MKF_HAS_IDENTIFIER) {
		if (!flags.str().empty()) flags << '|';
		flags << "RSSL_MKF_HAS_IDENTIFIER";
		identifier << ", \"identifier\": " << msg_key.identifier << "";
	}
	if (msg_key.flags & RSSL_MKF_HAS_ATTRIB) {
		if (!flags.str().empty()) flags << '|';
		flags << "RSSL_MKF_HAS_ATTRIB";
		attrib << ", \"attribContainerType\": \"" << internal::data_type_string (static_cast<RsslDataTypes> (msg_key.attribContainerType)) << "\"";
	}
	o << "\"MsgKey\": { "
		  "\"flags\": \"" << flags.str() << "\""
		<< service_id.str()
		<< name_type.str()
		<< name.str()
		<< filter.str()
		<< identifier.str()
		<< attrib.str() << " }";
	return o;
}

class RsslInstrumentKey {
	const RsslMsgKey& msg_key_;
public:
	RsslInstrumentKey (const RsslMsgKey& msg_key) :
		msg_key_ (msg_key)
	{
	}
	const RsslMsgKey& getRsslMsgKey() const
	{
		return msg_key_;
	}
};

inline
std::ostream& operator<< (std::ostream& o, const RsslInstrumentKey& wrapper) {
	std::ostringstream flags, service_id, name_type, name, filter, identifier, attrib;
	const RsslMsgKey& msg_key (wrapper.getRsslMsgKey());
	if (msg_key.flags & RSSL_MKF_HAS_SERVICE_ID) {
		flags << "RSSL_MKF_HAS_SERVICE_ID";
		service_id << ", \"serviceId\": " << msg_key.serviceId;
	}
	if (msg_key.flags & RSSL_MKF_HAS_NAME) {
		if (!flags.str().empty()) flags << '|';
		flags << "RSSL_MKF_HAS_NAME";
		name << ", \"name\": \"" << std::string (msg_key.name.data, msg_key.name.length) << "\"";
	}
	if (msg_key.flags & RSSL_MKF_HAS_NAME_TYPE) {
		if (!flags.str().empty()) flags << '|';
		flags << "RSSL_MKF_HAS_NAME_TYPE";
		name_type << ", \"nameType\": \"" << internal::instrument_type_string (static_cast<RDMInstrumentNameTypes> (msg_key.nameType)) << "\"";
	}
	if (msg_key.flags & RSSL_MKF_HAS_FILTER) {
		if (!flags.str().empty()) flags << '|';
		flags << "RSSL_MKF_HAS_FILTER";
		filter << ", \"filter\": " << msg_key.filter << "";
	}
	if (msg_key.flags & RSSL_MKF_HAS_IDENTIFIER) {
		if (!flags.str().empty()) flags << '|';
		flags << "RSSL_MKF_HAS_IDENTIFIER";
		identifier << ", \"identifier\": " << msg_key.identifier << "";
	}
	if (msg_key.flags & RSSL_MKF_HAS_ATTRIB) {
		if (!flags.str().empty()) flags << '|';
		flags << "RSSL_MKF_HAS_ATTRIB";
		attrib << ", \"attribContainerType\": \"" << internal::data_type_string (static_cast<RsslDataTypes> (msg_key.attribContainerType)) << "\"";
	}
	o << "\"MsgKey\": { "
		  "\"flags\": \"" << flags.str() << "\""
		<< service_id.str()
		<< name_type.str()
		<< name.str()
		<< filter.str()
		<< identifier.str()
		<< attrib.str() << " }";
	return o;
}

} /* namespace internal */

inline
std::ostream& operator<< (std::ostream& o, const RsslMsgKey& msg_key) {
	std::ostringstream flags, service_id, name_type, name, filter, identifier, attrib;
	if (msg_key.flags & RSSL_MKF_HAS_SERVICE_ID) {
		flags << "RSSL_MKF_HAS_SERVICE_ID";
		service_id << ", \"serviceId\": " << msg_key.serviceId;
	}
	if (msg_key.flags & RSSL_MKF_HAS_NAME) {
		if (!flags.str().empty()) flags << '|';
		flags << "RSSL_MKF_HAS_NAME";
		name << ", \"name\": \"" << std::string (msg_key.name.data, msg_key.name.length) << "\"";
	}
	if (msg_key.flags & RSSL_MKF_HAS_NAME_TYPE) {
		if (!flags.str().empty()) flags << '|';
		flags << "RSSL_MKF_HAS_NAME_TYPE";
/** Serialize value as enumeration not defined **/
		name_type << ", \"nameType\": \"" << static_cast<unsigned> (msg_key.nameType) << "\"";
	}
	if (msg_key.flags & RSSL_MKF_HAS_FILTER) {
		if (!flags.str().empty()) flags << '|';
		flags << "RSSL_MKF_HAS_FILTER";
		filter << ", \"filter\": " << msg_key.filter << "";
	}
	if (msg_key.flags & RSSL_MKF_HAS_IDENTIFIER) {
		if (!flags.str().empty()) flags << '|';
		flags << "RSSL_MKF_HAS_IDENTIFIER";
		identifier << ", \"identifier\": " << msg_key.identifier << "";
	}
	if (msg_key.flags & RSSL_MKF_HAS_ATTRIB) {
		if (!flags.str().empty()) flags << '|';
		flags << "RSSL_MKF_HAS_ATTRIB";
		attrib << ", \"attribContainerType\": \"" << internal::data_type_string (static_cast<RsslDataTypes> (msg_key.attribContainerType)) << "\"";
	}
	o << "\"MsgKey\": { "
		  "\"flags\": \"" << flags.str() << "\""
		<< service_id.str()
		<< name_type.str()
		<< name.str()
		<< filter.str()
		<< identifier.str()
		<< attrib.str() << " }";
	return o;
}

inline
std::ostream& operator<< (std::ostream& o, const RsslQos& qos) {
	o << "{ "
		  "\"dynamic\": " << ((1 == qos.dynamic) ? "true" : "false") << ""
		", \"rate\": \"" << internal::qos_rate_string (static_cast<RsslQosRates> (qos.rate)) << "\""
		", \"rateInfo\": " << qos.rateInfo << ""
		", \"timeInfo\": " << qos.timeInfo << ""
		", \"timeliness\": \"" << internal::qos_timeliness_string (static_cast<RsslQosTimeliness> (qos.timeliness)) << "\""
		" }";
	return o;
}

inline
std::ostream& operator<< (std::ostream& o, const RsslRequestMsg& msg) {
	std::ostringstream flags, msg_key, priority, qos, worst_qos;
	if (msg.flags & RSSL_RQMF_HAS_EXTENDED_HEADER) {
		flags << "RSSL_RQMF_HAS_EXTENDED_HEADER";
	}
	if (msg.flags & RSSL_RQMF_HAS_PRIORITY) {
		if (!flags.str().empty()) flags << '|';
		flags << "RSSL_RQMF_HAS_PRIORITY";
		priority << ", \"priorityClass\": " << (unsigned)msg.priorityClass << ""
			    ", \"priorityCount\": " << (unsigned)msg.priorityCount << "";
	}
	if (msg.flags & RSSL_RQMF_STREAMING) {
		if (!flags.str().empty()) flags << '|';
		flags << "RSSL_RQMF_STREAMING";
	}
	if (msg.flags & RSSL_RQMF_MSG_KEY_IN_UPDATES) {
		if (!flags.str().empty()) flags << '|';
		flags << "RSSL_RQMF_MSG_KEY_IN_UPDATES";
	}
	if (msg.flags & RSSL_RQMF_CONF_INFO_IN_UPDATES) {
		if (!flags.str().empty()) flags << '|';
		flags << "RSSL_RQMF_CONF_INFO_IN_UPDATES";
	}
	if (msg.flags & RSSL_RQMF_NO_REFRESH) {
		if (!flags.str().empty()) flags << '|';
		flags << "RSSL_RQMF_NO_REFRESH";
	}
	if (msg.flags & RSSL_RQMF_HAS_QOS) {
		if (!flags.str().empty()) flags << '|';
		flags << "RSSL_RQMF_HAS_QOS";
		qos << ", \"qos\": " << msg.qos;
	}
	if (msg.flags & RSSL_RQMF_HAS_WORST_QOS) {
		if (!flags.str().empty()) flags << '|';
		flags << "RSSL_RQMF_HAS_WORST_QOS";
		worst_qos << ", \"worstQos\": " << msg.worstQos;
	}
	if (msg.flags & RSSL_RQMF_PRIVATE_STREAM) {
		if (!flags.str().empty()) flags << '|';
		flags << "RSSL_RQMF_PRIVATE_STREAM";
	}
	if (msg.flags & RSSL_RQMF_PAUSE) {
		if (!flags.str().empty()) flags << '|';
		flags << "RSSL_RQMF_PAUSE";
	}
	if (msg.flags & RSSL_RQMF_HAS_VIEW) {
		if (!flags.str().empty()) flags << '|';
		flags << "RSSL_RQMF_HAS_VIEW";
	}
	if (msg.flags & RSSL_RQMF_HAS_BATCH) {
		if (!flags.str().empty()) flags << '|';
		flags << "RSSL_RQMF_HAS_BATCH";
	}
	switch (msg.msgBase.domainType) {
	case RSSL_DMT_LOGIN: {
		const internal::RsslLoginKey login_key (msg.msgBase.msgKey);
		msg_key << login_key;
		break;
	}
	case RSSL_DMT_SOURCE: {
		const internal::RsslDirectoryKey directory_key (msg.msgBase.msgKey);
		msg_key << directory_key;
		break;
	}
	case RSSL_DMT_MARKET_PRICE:
	case RSSL_DMT_MARKET_BY_ORDER:
	case RSSL_DMT_MARKET_BY_PRICE:
	case RSSL_DMT_MARKET_MAKER:
	case RSSL_DMT_SYMBOL_LIST:
	case RSSL_DMT_YIELD_CURVE: {
		const internal::RsslInstrumentKey instrument_key (msg.msgBase.msgKey);
		msg_key << instrument_key;
		break;
	}
	default:
		msg_key << msg.msgBase.msgKey;
		break;
	}
	o << "\"RsslRequestMsg\": { "
		  "\"msgClass\": \"RSSL_MC_REQUEST\""
		", \"domainType\": \"" << internal::domain_type_string (static_cast<RsslDomainTypes> (msg.msgBase.domainType)) << "\""
		", \"containerType\": \"" << internal::data_type_string (static_cast<RsslDataTypes> (msg.msgBase.containerType)) << "\""
		", \"streamId\": " << msg.msgBase.streamId << ""
		", " << msg_key.str() << ""
		", \"flags\": \"" << flags.str() << "\""
		  "" << priority.str() << ""
		  "" << qos.str() << ""
		  "" << worst_qos.str() << ""
	" }";
	return o;
}

inline
std::ostream& operator<< (std::ostream& o, const RsslRefreshMsg& msg) {
	std::ostringstream flags;
	if (msg.flags & RSSL_RFMF_HAS_EXTENDED_HEADER) {
		flags << "RSSL_RFMF_HAS_EXTENDED_HEADER";
	}
	if (msg.flags & RSSL_RFMF_HAS_PERM_DATA) {
		if (!flags.str().empty()) flags << '|';
		flags << "RSSL_RFMF_HAS_PERM_DATA";
	}
	if (msg.flags & RSSL_RFMF_HAS_MSG_KEY) {
		if (!flags.str().empty()) flags << '|';
		flags << "RSSL_RFMF_HAS_MSG_KEY";
	}
	if (msg.flags & RSSL_RFMF_HAS_SEQ_NUM) {
		if (!flags.str().empty()) flags << '|';
		flags << "RSSL_RFMF_HAS_SEQ_NUM";
	}
	if (msg.flags & RSSL_RFMF_SOLICITED) {
		if (!flags.str().empty()) flags << '|';
		flags << "RSSL_RFMF_SOLICITED";
	}
	if (msg.flags & RSSL_RFMF_REFRESH_COMPLETE) {
		if (!flags.str().empty()) flags << '|';
		flags << "RSSL_RFMF_REFRESH_COMPLETE";
	}
	if (msg.flags & RSSL_RFMF_HAS_QOS) {
		if (!flags.str().empty()) flags << '|';
		flags << "RSSL_RFMF_HAS_QOS";
	}
	if (msg.flags & RSSL_RFMF_CLEAR_CACHE) {
		if (!flags.str().empty()) flags << '|';
		flags << "RSSL_RFMF_CLEAR_CACHE";
	}
	if (msg.flags & RSSL_RFMF_DO_NOT_CACHE) {
		if (!flags.str().empty()) flags << '|';
		flags << "RSSL_RFMF_DO_NOT_CACHE";
	}
	if (msg.flags & RSSL_RFMF_PRIVATE_STREAM) {
		if (!flags.str().empty()) flags << '|';
		flags << "RSSL_RFMF_PRIVATE_STREAM";
	}
	if (msg.flags & RSSL_RFMF_HAS_POST_USER_INFO) {
		if (!flags.str().empty()) flags << '|';
		flags << "RSSL_RFMF_HAS_POST_USER_INFO";
	}
	if (msg.flags & RSSL_RFMF_HAS_PART_NUM) {
		if (!flags.str().empty()) flags << '|';
		flags << "RSSL_RFMF_HAS_PART_NUM";
	}
	o << "\"RsslRefreshMsg\": { "
		  "\"msgClass\": \"RSSL_MC_REFRESH\""
		", \"domainType\": \"" << internal::domain_type_string (static_cast<RsslDomainTypes> (msg.msgBase.domainType)) << "\""
		", \"containerType\": \"" << internal::data_type_string (static_cast<RsslDataTypes> (msg.msgBase.containerType)) << "\""
		", \"streamId\": " << msg.msgBase.streamId << ""
		", " << msg.msgBase.msgKey << ""
		", \"flags\": \"" << flags.str() << "\""
	" }";
	return o;
}

inline
std::ostream& operator<< (std::ostream& o, const RsslStatusMsg& msg) {
	std::ostringstream flags;
	if (msg.flags & RSSL_STMF_HAS_EXTENDED_HEADER) {
		flags << "RSSL_STMF_HAS_EXTENDED_HEADER";
	}
	if (msg.flags & RSSL_STMF_HAS_PERM_DATA) {
		if (!flags.str().empty()) flags << '|';
		flags << "RSSL_STMF_HAS_PERM_DATA";
	}
	if (msg.flags & RSSL_STMF_HAS_MSG_KEY) {
		if (!flags.str().empty()) flags << '|';
		flags << "RSSL_STMF_HAS_MSG_KEY";
	}
	if (msg.flags & RSSL_STMF_HAS_GROUP_ID) {
		if (!flags.str().empty()) flags << '|';
		flags << "RSSL_STMF_HAS_GROUP_ID";
	}
	if (msg.flags & RSSL_STMF_HAS_STATE) {
		if (!flags.str().empty()) flags << '|';
		flags << "RSSL_STMF_HAS_STATE";
	}
	if (msg.flags & RSSL_STMF_CLEAR_CACHE) {
		if (!flags.str().empty()) flags << '|';
		flags << "RSSL_STMF_CLEAR_CACHE";
	}
	if (msg.flags & RSSL_STMF_PRIVATE_STREAM) {
		if (!flags.str().empty()) flags << '|';
		flags << "RSSL_STMF_PRIVATE_STREAM";
	}
	if (msg.flags & RSSL_STMF_HAS_POST_USER_INFO) {
		if (!flags.str().empty()) flags << '|';
		flags << "RSSL_STMF_HAS_POST_USER_INFO";
	}
	o << "\"RsslStatusMsg\": { "
		  "\"msgClass\": \"RSSL_MC_STATUS\""
		", \"domainType\": \"" << internal::domain_type_string (static_cast<RsslDomainTypes> (msg.msgBase.domainType)) << "\""
		", \"containerType\": \"" << internal::data_type_string (static_cast<RsslDataTypes> (msg.msgBase.containerType)) << "\""
		", \"streamId\": " << msg.msgBase.streamId << ""
		", " << msg.msgBase.msgKey << ""
		", \"flags\": \"" << flags.str() << "\""
	" }";
	return o;
}

inline
std::ostream& operator<< (std::ostream& o, const RsslUpdateMsg& msg) {
	std::ostringstream flags;
	if (msg.flags & RSSL_UPMF_HAS_EXTENDED_HEADER) {
		flags << "RSSL_UPMF_HAS_EXTENDED_HEADER";
	}
	if (msg.flags & RSSL_UPMF_HAS_PERM_DATA) {
		if (!flags.str().empty()) flags << '|';
		flags << "RSSL_UPMF_HAS_PERM_DATA";
	}
	if (msg.flags & RSSL_UPMF_HAS_MSG_KEY) {
		if (!flags.str().empty()) flags << '|';
		flags << "RSSL_UPMF_HAS_MSG_KEY";
	}
	if (msg.flags & RSSL_UPMF_HAS_SEQ_NUM) {
		if (!flags.str().empty()) flags << '|';
		flags << "RSSL_UPMF_HAS_SEQ_NUM";
	}
	if (msg.flags & RSSL_UPMF_HAS_CONF_INFO) {
		if (!flags.str().empty()) flags << '|';
		flags << "RSSL_UPMF_HAS_CONF_INFO";
	}
	if (msg.flags & RSSL_UPMF_DO_NOT_CACHE) {
		if (!flags.str().empty()) flags << '|';
		flags << "RSSL_UPMF_DO_NOT_CACHE";
	}
	if (msg.flags & RSSL_UPMF_DO_NOT_CONFLATE) {
		if (!flags.str().empty()) flags << '|';
		flags << "RSSL_UPMF_DO_NOT_CONFLATE";
	}
	if (msg.flags & RSSL_UPMF_DO_NOT_RIPPLE) {
		if (!flags.str().empty()) flags << '|';
		flags << "RSSL_UPMF_DO_NOT_RIPPLE";
	}
	if (msg.flags & RSSL_UPMF_HAS_POST_USER_INFO) {
		if (!flags.str().empty()) flags << '|';
		flags << "RSSL_UPMF_HAS_POST_USER_INFO";
	}
	if (msg.flags & RSSL_UPMF_DISCARDABLE) {
		if (!flags.str().empty()) flags << '|';
		flags << "RSSL_UPMF_DISCARDABLE";
	}
	o << "\"RsslUpdateMsg\": { "
		  "\"msgClass\": \"RSSL_MC_UPDATE\""
		", \"domainType\": \"" << internal::domain_type_string (static_cast<RsslDomainTypes> (msg.msgBase.domainType)) << "\""
		", \"containerType\": \"" << internal::data_type_string (static_cast<RsslDataTypes> (msg.msgBase.containerType)) << "\""
		", \"streamId\": " << msg.msgBase.streamId << ""
		", " << msg.msgBase.msgKey << ""
		", \"flags\": \"" << flags.str() << "\""
	" }";
	return o;
}

inline
std::ostream& operator<< (std::ostream& o, const RsslCloseMsg& msg) {
	std::ostringstream flags;
	if (msg.flags & RSSL_CLMF_HAS_EXTENDED_HEADER) {
		flags << "RSSL_CLMF_HAS_EXTENDED_HEADER";
	}
	if (msg.flags & RSSL_CLMF_ACK) {
		if (!flags.str().empty()) flags << '|';
		flags << "RSSL_CLMF_ACK";
	}
	o << "\"RsslCloseMsg\": { "
		  "\"msgClass\": \"RSSL_MC_CLOSE\""
		", \"domainType\": \"" << internal::domain_type_string (static_cast<RsslDomainTypes> (msg.msgBase.domainType)) << "\""
		", \"containerType\": \"" << internal::data_type_string (static_cast<RsslDataTypes> (msg.msgBase.containerType)) << "\""
		", \"streamId\": " << msg.msgBase.streamId << ""
		", " << msg.msgBase.msgKey << ""
		", \"flags\": \"" << flags.str() << "\""
	" }";
	return o;
}

inline
std::ostream& operator<< (std::ostream& o, const RsslAckMsg& msg) {
	std::ostringstream flags;
	if (msg.flags & RSSL_AKMF_HAS_EXTENDED_HEADER) {
		flags << "RSSL_AKMF_HAS_EXTENDED_HEADER";
	}
	if (msg.flags & RSSL_AKMF_HAS_TEXT) {
		if (!flags.str().empty()) flags << '|';
		flags << "RSSL_AKMF_HAS_TEXT";
	}
	if (msg.flags & RSSL_AKMF_PRIVATE_STREAM) {
		if (!flags.str().empty()) flags << '|';
		flags << "RSSL_AKMF_PRIVATE_STREAM";
	}
	if (msg.flags & RSSL_AKMF_HAS_SEQ_NUM) {
		if (!flags.str().empty()) flags << '|';
		flags << "RSSL_AKMF_HAS_SEQ_NUM";
	}
	if (msg.flags & RSSL_AKMF_HAS_MSG_KEY) {
		if (!flags.str().empty()) flags << '|';
		flags << "RSSL_AKMF_HAS_MSG_KEY";
	}
	if (msg.flags & RSSL_AKMF_HAS_NAK_CODE) {
		if (!flags.str().empty()) flags << '|';
		flags << "RSSL_AKMF_HAS_NAK_CODE";
	}
	o << "\"RsslAckMsg\": { "
		  "\"msgClass\": \"RSSL_MC_ACK\""
		", \"domainType\": \"" << internal::domain_type_string (static_cast<RsslDomainTypes> (msg.msgBase.domainType)) << "\""
		", \"containerType\": \"" << internal::data_type_string (static_cast<RsslDataTypes> (msg.msgBase.containerType)) << "\""
		", \"streamId\": " << msg.msgBase.streamId << ""
		", " << msg.msgBase.msgKey << ""
		", \"flags\": \"" << flags.str() << "\""
	" }";
	return o;
}

inline
std::ostream& operator<< (std::ostream& o, const RsslGenericMsg& msg) {
	std::ostringstream flags;
	if (msg.flags & RSSL_GNMF_HAS_EXTENDED_HEADER) {
		flags << "RSSL_GNMF_HAS_EXTENDED_HEADER";
	}
	if (msg.flags & RSSL_GNMF_HAS_PERM_DATA) {
		if (!flags.str().empty()) flags << '|';
		flags << "RSSL_GNMF_HAS_PERM_DATA";
	}
	if (msg.flags & RSSL_GNMF_HAS_MSG_KEY) {
		if (!flags.str().empty()) flags << '|';
		flags << "RSSL_GNMF_HAS_MSG_KEY";
	}
	if (msg.flags & RSSL_GNMF_HAS_SEQ_NUM) {
		if (!flags.str().empty()) flags << '|';
		flags << "RSSL_GNMF_HAS_SEQ_NUM";
	}
	if (msg.flags & RSSL_GNMF_MESSAGE_COMPLETE) {
		if (!flags.str().empty()) flags << '|';
		flags << "RSSL_GNMF_MESSAGE_COMPLETE";
	}
	if (msg.flags & RSSL_GNMF_HAS_SECONDARY_SEQ_NUM) {
		if (!flags.str().empty()) flags << '|';
		flags << "RSSL_GNMF_HAS_SECONDARY_SEQ_NUM";
	}
	if (msg.flags & RSSL_GNMF_HAS_PART_NUM) {
		if (!flags.str().empty()) flags << '|';
		flags << "RSSL_GNMF_HAS_PART_NUM";
	}
	o << "\"RsslGenericMsg\": { "
		  "\"msgClass\": \"RSSL_MC_GENERIC\""
		", \"domainType\": \"" << internal::domain_type_string (static_cast<RsslDomainTypes> (msg.msgBase.domainType)) << "\""
		", \"containerType\": \"" << internal::data_type_string (static_cast<RsslDataTypes> (msg.msgBase.containerType)) << "\""
		", \"streamId\": " << msg.msgBase.streamId << ""
		", " << msg.msgBase.msgKey << ""
		", \"flags\": \"" << flags.str() << "\""
	" }";
	return o;
}

inline
std::ostream& operator<< (std::ostream& o, const RsslPostMsg& msg) {
	std::ostringstream flags;
	if (msg.flags & RSSL_PSMF_HAS_EXTENDED_HEADER) {
		flags << "RSSL_PSMF_HAS_EXTENDED_HEADER";
	}
	if (msg.flags & RSSL_PSMF_HAS_POST_ID) {
		if (!flags.str().empty()) flags << '|';
		flags << "RSSL_PSMF_HAS_POST_ID";
	}
	if (msg.flags & RSSL_PSMF_HAS_MSG_KEY) {
		if (!flags.str().empty()) flags << '|';
		flags << "RSSL_PSMF_HAS_MSG_KEY";
	}
	if (msg.flags & RSSL_PSMF_HAS_SEQ_NUM) {
		if (!flags.str().empty()) flags << '|';
		flags << "RSSL_PSMF_HAS_SEQ_NUM";
	}
	if (msg.flags & RSSL_PSMF_POST_COMPLETE) {
		if (!flags.str().empty()) flags << '|';
		flags << "RSSL_PSMF_POST_COMPLETE";
	}
	if (msg.flags & RSSL_PSMF_ACK) {
		if (!flags.str().empty()) flags << '|';
		flags << "RSSL_PSMF_ACK";
	}
	if (msg.flags & RSSL_PSMF_HAS_PERM_DATA) {
		if (!flags.str().empty()) flags << '|';
		flags << "RSSL_PSMF_HAS_PERM_DATA";
	}
	if (msg.flags & RSSL_PSMF_HAS_PART_NUM) {
		if (!flags.str().empty()) flags << '|';
		flags << "RSSL_PSMF_HAS_PART_NUM";
	}
	if (msg.flags & RSSL_PSMF_HAS_POST_USER_RIGHTS) {
		if (!flags.str().empty()) flags << '|';
		flags << "RSSL_PSMF_HAS_POST_USER_RIGHTS";
	}
	o << "\"RsslPostMsg\": { "
		  "\"msgClass\": \"RSSL_MC_POST\""
		", \"domainType\": \"" << internal::domain_type_string (static_cast<RsslDomainTypes> (msg.msgBase.domainType)) << "\""
		", \"containerType\": \"" << internal::data_type_string (static_cast<RsslDataTypes> (msg.msgBase.containerType)) << "\""
		", \"streamId\": " << msg.msgBase.streamId << ""
		", " << msg.msgBase.msgKey << ""
		", \"flags\": \"" << flags.str() << "\""
	" }";
	return o;
}

inline
std::ostream& operator<< (std::ostream& o, const RsslMsg& msg) {
	switch (msg.msgBase.msgClass) {
	case RSSL_MC_REQUEST:
		o << *reinterpret_cast<const RsslRequestMsg*>(&msg);
		break;
	case RSSL_MC_REFRESH:
		o << *reinterpret_cast<const RsslRefreshMsg*>(&msg);
		break;
	case RSSL_MC_STATUS:
		o << *reinterpret_cast<const RsslStatusMsg*>(&msg);
		break;
	case RSSL_MC_UPDATE:
		o << *reinterpret_cast<const RsslUpdateMsg*>(&msg);
		break;
	case RSSL_MC_CLOSE:
		o << *reinterpret_cast<const RsslCloseMsg*>(&msg);
		break;
	case RSSL_MC_ACK:
		o << *reinterpret_cast<const RsslAckMsg*>(&msg);
		break;
	case RSSL_MC_GENERIC:
		o << *reinterpret_cast<const RsslGenericMsg*>(&msg);
		break;
	case RSSL_MC_POST:
		o << *reinterpret_cast<const RsslPostMsg*>(&msg);
		break;
	default:
		o << "\"RsslMsg\": { "
			  "\"msgClass\": \"" << internal::message_class_string (static_cast<RsslMsgClasses> (msg.msgBase.msgClass)) << "\""
			", \"domainType\": \"" << internal::domain_type_string (static_cast<RsslDomainTypes> (msg.msgBase.domainType)) << "\""
			", \"containerType\": \"" << internal::data_type_string (static_cast<RsslDataTypes> (msg.msgBase.containerType)) << "\""
			", \"streamId\": " << msg.msgBase.streamId << ""
			", " << msg.msgBase.msgKey << ""
		" }";
		break;
	}
	return o;
}

inline
std::ostream& operator<< (std::ostream& o, const RsslSegmentedNetwork& segmented) {
	o << "\"RsslSegmentedNetwork\": { "
		  "\"recvAddress\": \"" << segmented.recvAddress << "\""
		", \"recvServiceName\": \"" << segmented.recvServiceName << "\""
		", \"unicastServiceName\": \"" << segmented.unicastServiceName << "\""
		", \"interfaceName\": \"" << segmented.interfaceName << "\""
		", \"sendAddress\": \"" << segmented.sendAddress << "\""
		", \"sendServiceName\": \"" << segmented.sendServiceName << "\""
	" }";
	return o;
}

inline
std::ostream& operator<< (std::ostream& o, const RsslUnifiedNetwork& unified) {
	o << "\"RsslUnifiedNetwork\": { "
		  "\"address\": \"" << unified.address << "\""
		", \"serviceName\": \"" << unified.serviceName << "\""
		", \"unicastServiceName\": \"" << unified.unicastServiceName << "\""
		", \"interfaceName\": \"" << unified.interfaceName << "\""
	" }";
	return o;
}

inline
std::ostream& operator<< (std::ostream& o, const RsslConnectionInfo& connectionInfo) {
	o << "\"RsslConnectionInfo\": { "
		  "\"segmented\": " << connectionInfo.segmented << ""
		", \"unified\": " << connectionInfo.unified << ""
	" }";
	return o;
}

#endif /* UPA_OSTREAM_HH_ */

/* eof */
