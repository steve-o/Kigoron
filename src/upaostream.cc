/* UPA output streaming.
 */

#include "upaostream.hh"

#define RETURN_STRING_LITERAL(x) \
case x: \
	return #x;

const char*
internal::channel_state_string (
	const RsslChannelState state_
	)
{
	switch (state_) {
	RETURN_STRING_LITERAL (RSSL_CH_STATE_CLOSED);
	RETURN_STRING_LITERAL (RSSL_CH_STATE_INACTIVE);
	RETURN_STRING_LITERAL (RSSL_CH_STATE_INITIALIZING);
	RETURN_STRING_LITERAL (RSSL_CH_STATE_ACTIVE);
	default: return "(Unknown)";
	}
	return "";
}

const char*
internal::compression_type_string (
	const RsslCompTypes type_
	)
{
	switch (type_) {
	RETURN_STRING_LITERAL (RSSL_COMP_NONE);
	RETURN_STRING_LITERAL (RSSL_COMP_ZLIB);
	RETURN_STRING_LITERAL (RSSL_COMP_LZ4);
	default: return "(Unknown)";
	}
	return "";
}

const char*
internal::connection_type_string (
	const RsslConnectionTypes type_
	)
{
	switch (type_) {
	RETURN_STRING_LITERAL (RSSL_CONN_TYPE_INIT);
	RETURN_STRING_LITERAL (RSSL_CONN_TYPE_SOCKET);
	RETURN_STRING_LITERAL (RSSL_CONN_TYPE_ENCRYPTED);
	RETURN_STRING_LITERAL (RSSL_CONN_TYPE_HTTP);
	RETURN_STRING_LITERAL (RSSL_CONN_TYPE_UNIDIR_SHMEM);
	RETURN_STRING_LITERAL (RSSL_CONN_TYPE_RELIABLE_MCAST);
	default: return "(Unknown)";
	}
	return "";
}

/* Containers are a subset of RSSL data types. */
const char*
internal::container_type_string (
	const RsslContainerType type_
	)
{
	return rsslDataTypeToString (static_cast<RsslDataTypes> (type_));
}

const char*
internal::data_type_string (
	const RsslDataTypes type_
	)
{
	return rsslDataTypeToString (type_);
}

const char*
internal::domain_type_string (
	const RsslDomainTypes type_
	)
{
	return rsslDomainTypeToString (type_);
}

const char*
internal::filter_entry_id_string (
	const RDMDirectoryServiceFilterIds id_
	)
{
	switch (id_) {
	RETURN_STRING_LITERAL (RDM_DIRECTORY_SERVICE_INFO_ID);
	RETURN_STRING_LITERAL (RDM_DIRECTORY_SERVICE_STATE_ID);
	RETURN_STRING_LITERAL (RDM_DIRECTORY_SERVICE_GROUP_ID);
	RETURN_STRING_LITERAL (RDM_DIRECTORY_SERVICE_LOAD_ID);
	RETURN_STRING_LITERAL (RDM_DIRECTORY_SERVICE_DATA_ID);
	RETURN_STRING_LITERAL (RDM_DIRECTORY_SERVICE_LINK_ID);
	default: return "(Unknown)";
	}
	return "";
}

const char*
internal::filter_entry_action_string (
	const RsslFilterEntryActions action_
	)
{
	switch (action_) {
	RETURN_STRING_LITERAL (RSSL_FTEA_UPDATE_ENTRY);
	RETURN_STRING_LITERAL (RSSL_FTEA_SET_ENTRY);
	RETURN_STRING_LITERAL (RSSL_FTEA_CLEAR_ENTRY);
	default: return "(Unknown)";
	}
	return "";
}

const char*
internal::instrument_type_string (
	const RDMInstrumentNameTypes type_
	)
{
	switch (type_) {
	RETURN_STRING_LITERAL (RDM_INSTRUMENT_NAME_TYPE_UNSPECIFIED);
	RETURN_STRING_LITERAL (RDM_INSTRUMENT_NAME_TYPE_RIC);
	RETURN_STRING_LITERAL (RDM_INSTRUMENT_NAME_TYPE_CONTRIBUTOR);
	RETURN_STRING_LITERAL (RDM_INSTRUMENT_NAME_TYPE_MAX_RESERVED);
	default: return "(Unknown)";
	}
	return "";
}

const char*
internal::login_type_string (
	const RDMLoginUserIdTypes type_
	)
{
	switch (type_) {
	RETURN_STRING_LITERAL (RDM_LOGIN_USER_NAME);
	RETURN_STRING_LITERAL (RDM_LOGIN_USER_EMAIL_ADDRESS);
	RETURN_STRING_LITERAL (RDM_LOGIN_USER_TOKEN);
	RETURN_STRING_LITERAL (RDM_LOGIN_USER_COOKIE);
	default: return "(Unknown)";
	}
	return "";
}

const char*
internal::map_entry_action_string (
	const RsslMapEntryActions action_
	)
{
	switch (action_) {
	RETURN_STRING_LITERAL (RSSL_MPEA_UPDATE_ENTRY);
	RETURN_STRING_LITERAL (RSSL_MPEA_ADD_ENTRY);
	RETURN_STRING_LITERAL (RSSL_MPEA_DELETE_ENTRY);
	default: return "(Unknown)";
	}
	return "";
}

const char*
internal::message_class_string (
	const RsslMsgClasses class_
	)
{
	return rsslMsgClassToString (class_);
}

const char*
internal::primitive_type_string (
	const RsslPrimitiveType type_
	)
{
	return rsslDataTypeToString (static_cast<RsslDataTypes> (type_));
}

const char*
internal::protocol_type_string (
	const RsslUInt8 type_
	)
{
	const char* c;

	switch (type_) {
	case RSSL_RWF_PROTOCOL_TYPE:		c = "RWF"; break;
	default: c = "(Unknown)"; break;
	}

	return c;
}

const char*
internal::real_hint_string (
	const RsslRealHints hint_
	)
{
	switch (hint_) {
	RETURN_STRING_LITERAL (RSSL_RH_EXPONENT_14);
	RETURN_STRING_LITERAL (RSSL_RH_EXPONENT_13);
	RETURN_STRING_LITERAL (RSSL_RH_EXPONENT_12);
	RETURN_STRING_LITERAL (RSSL_RH_EXPONENT_11);
	RETURN_STRING_LITERAL (RSSL_RH_EXPONENT_10);
	RETURN_STRING_LITERAL (RSSL_RH_EXPONENT_9);
	RETURN_STRING_LITERAL (RSSL_RH_EXPONENT_8);
	RETURN_STRING_LITERAL (RSSL_RH_EXPONENT_7);
	RETURN_STRING_LITERAL (RSSL_RH_EXPONENT_6);
	RETURN_STRING_LITERAL (RSSL_RH_EXPONENT_5);
	RETURN_STRING_LITERAL (RSSL_RH_EXPONENT_4);
	RETURN_STRING_LITERAL (RSSL_RH_EXPONENT_3);
	RETURN_STRING_LITERAL (RSSL_RH_EXPONENT_2);
	RETURN_STRING_LITERAL (RSSL_RH_EXPONENT_1);
	RETURN_STRING_LITERAL (RSSL_RH_EXPONENT0);
	RETURN_STRING_LITERAL (RSSL_RH_EXPONENT1);
	RETURN_STRING_LITERAL (RSSL_RH_EXPONENT2);
	RETURN_STRING_LITERAL (RSSL_RH_EXPONENT3);
	RETURN_STRING_LITERAL (RSSL_RH_EXPONENT4);
	RETURN_STRING_LITERAL (RSSL_RH_EXPONENT5);
	RETURN_STRING_LITERAL (RSSL_RH_EXPONENT6);
	RETURN_STRING_LITERAL (RSSL_RH_EXPONENT7);
	RETURN_STRING_LITERAL (RSSL_RH_FRACTION_1);
	RETURN_STRING_LITERAL (RSSL_RH_FRACTION_2);
	RETURN_STRING_LITERAL (RSSL_RH_FRACTION_4);
	RETURN_STRING_LITERAL (RSSL_RH_FRACTION_8);
	RETURN_STRING_LITERAL (RSSL_RH_FRACTION_16);
	RETURN_STRING_LITERAL (RSSL_RH_FRACTION_32);
	RETURN_STRING_LITERAL (RSSL_RH_FRACTION_64);
	RETURN_STRING_LITERAL (RSSL_RH_FRACTION_128);
	RETURN_STRING_LITERAL (RSSL_RH_FRACTION_256);
	default: return "(Unknown)";
	}
	return "";
}

const char*
internal::return_code_string (
	const RsslReturnCodes rc
	)
{
	return rsslRetCodeToString (rc);
}

const char*
internal::qos_rate_string (
	const RsslQosRates rate_
	)
{
	return rsslQosRateToString (rate_);
}

const char*
internal::qos_timeliness_string (
	const RsslQosTimeliness timeliness_
	)
{
	return rsslQosTimelinessToString (timeliness_);
}

const char*
internal::error_info_code_string (
	const RsslErrorInfoCode code_
	)
{
	switch (code_) {
	RETURN_STRING_LITERAL (RSSL_EIC_SUCCESS);
	RETURN_STRING_LITERAL (RSSL_EIC_FAILURE);
	default: return "(Unknown)";
	}
	return "";
}

/* eof */
