/* UPA interactive fake snapshot provider.
 */

#include "kigoron.hh"

#define __STDC_FORMAT_MACROS
#include <cstdint>
#include <inttypes.h>

#include <windows.h>

#include "chromium/command_line.hh"
#include "chromium/files/file_util.hh"
#include "chromium/logging.hh"
#include "chromium/strings/string_split.hh"
#include "upa.hh"
#include "unix_epoch.hh"


namespace switches {

//   Symbol map file.
const char kSymbolPath[]		= "symbol-path";

//   Maximum symbol age.
const char kMaxAge[]			= "max-age";

}  // namespace switches

namespace {

static const int kRdmRicId			= 4453;	// UN_RIC(8697), UNDERLYING(3814), PRIM_RIC(4453)

static const int kRdmClassId			= 3308; // UN_CLASS(4195), CLASS_CODE(3308), e.g. EQU
static const int kRdmExchangeId			= 4308;	// EXCH_SNAME(4308), RDN_EXCHID(4), e.g. NSQ
static const int kRdmNameId			= 3;	// UN_NAME(4197), DSPLY_NAME(3)
static const int kRdmCurrencyId			= 3591; // CCY_NAME(3591), PRIM_CCY(9018)
static const int kRdmSymbolId			= 3684; // UN_SYMBOL(4200), MNEMONIC(3684), UN_SYMB(8743), EXCHCODE(4058)
static const int kRdmIsinId			= 3655;	// UN_ISIN(4196), ISIN_1(5632), ISIN_CODE(3655)
static const int kRdmCusipId			= 4742;	// U_CUSIP(8543), CUSIP_CD(4742)
static const int kRdmSedolId			= 3756; // U_SEDOL(8544, SEDOL(3756)
static const int kRdmGicsId			= 8535; // GICS_CODE(8535)
static const int kRdmActivityTime1Id		= 1010;
static const int kRdmActivityDate1Id		= 875;

static const std::string kErrorMalformedRequest = "Malformed request.";
static const std::string kErrorNotFound = "Not found in Tick History.";
static const std::string kErrorPermData = "Unable to retrieve permission data for item.";
static const std::string kErrorInternal = "Internal error.";

}  // namespace anon

static std::weak_ptr<kigoron::kigoron_t> g_application;

kigoron::kigoron_t::kigoron_t()
	: mainloop_shutdown_ (false)
	, shutting_down_ (false)
{
}

kigoron::kigoron_t::~kigoron_t()
{
	LOG(INFO) << "fin.";
}

/* On a shutdown event set a global flag and force the event queue
 * to catch the event by submitting a log event.
 */
static
BOOL
CtrlHandler (
	DWORD	fdwCtrlType
	)
{
	const char* message;
	switch (fdwCtrlType) {
	case CTRL_C_EVENT:
		message = "Caught ctrl-c event";
		break;
	case CTRL_CLOSE_EVENT:
		message = "Caught close event";
		break;
	case CTRL_BREAK_EVENT:
		message = "Caught ctrl-break event";
		break;
	case CTRL_LOGOFF_EVENT:
		message = "Caught logoff event";
		break;
	case CTRL_SHUTDOWN_EVENT:
	default:
		message = "Caught shutdown event";
		break;
	}
	if (!g_application.expired()) {
		LOG(INFO) << message << "; closing provider.";
		auto sp = g_application.lock();
		sp->Quit();
	} else {
		LOG(WARNING) << message << "; provider already expired.";
	}
	return TRUE;
}

int
kigoron::kigoron_t::Run()
{
	int rc = EXIT_SUCCESS;
	VLOG(1) << "Run as application starting.";
/* Add shutdown handler. */
	g_application = shared_from_this();
	::SetConsoleCtrlHandler ((PHANDLER_ROUTINE)::CtrlHandler, TRUE);
	if (Start()) {
/* Wait for mainloop to quit */
		boost::unique_lock<boost::mutex> lock (mainloop_lock_);
		while (!mainloop_shutdown_)
			mainloop_cond_.wait (lock);
		Reset();
	} else {
		rc = EXIT_FAILURE;
	}
/* Remove shutdown handler. */
	::SetConsoleCtrlHandler ((PHANDLER_ROUTINE)::CtrlHandler, FALSE);
	VLOG(1) << "Run as application finished.";
	return rc;
}

void
kigoron::kigoron_t::Quit()
{
	shutting_down_ = true;
	if ((bool)provider_) {
		provider_->Quit();
	}
}

bool
kigoron::kigoron_t::Initialize ()
{
	try {
/* Configuration. */
		CommandLine* command_line = CommandLine::ForCurrentProcess();

/** Config file overrides **/
/* Maximum symbol age */
		if (command_line->HasSwitch (switches::kMaxAge)) {
			config_.max_age = command_line->GetSwitchValueASCII (switches::kMaxAge);
		}

		LOG(INFO) << "Kigoron: { "
			"\"config\": " << config_ <<
			" }";

/* Symbol list */
		if (command_line->HasSwitch (switches::kSymbolPath)) {
			boost::posix_time::time_duration reset_tod (boost::date_time::not_a_date_time);
			if (!config_.max_age.empty()) {
				reset_tod = boost::posix_time::duration_from_string (config_.max_age);
				LOG(INFO) << "Symbols set to expire when aged +" << reset_tod;
			} else {
				LOG(INFO) << "Symbols will not expire.";
			}
			std::vector<std::string> files;
			config_.symbol_path = command_line->GetSwitchValueASCII (switches::kSymbolPath);
/* Separate out multiple files if provided. */
			chromium::SplitString (config_.symbol_path, ',', &files);
			for (const auto& file : files) {
				if (!chromium::PathExists (file)) {
					LOG(WARNING) << "Symbol file '" << file << "' does not exist.";
					continue;
				}
/* Capture timestamp on file for age. */
				chromium::File::Info info;
				if (!chromium::GetFileInfo (file, &info)) {
					LOG(WARNING) << "Cannot stat file '" << file << "'.";
					continue;
				}
/* #RIC,ISIN,CUSIP,SEDOL,GICS,Domain,Description,Exchange, ...
 */
#define COLUMN_RIC	0
#define COLUMN_ISIN	1
#define COLUMN_CUSIP	2
#define COLUMN_SEDOL	3
#define COLUMN_GICS	4
#define COLUMN_CLASS	5
#define COLUMN_NAME	6
#define COLUMN_EXCHANGE	7
#define COLUMN_CURRENCY 10
				std::string contents;
				std::vector<std::string> instruments, columns;
				LOG(INFO) << "Sourcing instruments from file '" << file << "'.";
				file_util::ReadFileToString (file, &contents);
				chromium::SplitString (contents, '\n', &instruments);
				for (const auto& instrument : instruments) {
					DVLOG(2) << "[" << instrument << "]";
					if (instrument.empty() || instrument.front() == '#')
						continue;
					columns.clear();
					chromium::SplitString (instrument, ',', &columns);
					if (columns.empty() || columns.at (COLUMN_RIC).empty())
						continue;
					const boost::posix_time::ptime last_modified (boost::posix_time::from_time_t (info.last_modified));
					boost::posix_time::ptime max_age (boost::date_time::not_a_date_time);
					if (!reset_tod.is_not_a_date_time()) {
						max_age = last_modified + reset_tod;
					}
// requires _VARIADIC_MAX=7 or more to build.
					auto item = std::make_shared<item_t> (columns[COLUMN_RIC],
										columns[COLUMN_EXCHANGE],
										columns[COLUMN_CLASS],
										columns[COLUMN_NAME],
										columns[COLUMN_CURRENCY],
										last_modified,
										max_age);
					map_.emplace (std::string ("RIC=") + columns[COLUMN_RIC], item);
					if (!columns.at (COLUMN_ISIN).empty()) {
						item->isin_code.assign (columns[COLUMN_ISIN]);
						map_.emplace (std::string ("ISIN=") + columns[COLUMN_ISIN], item);
					}
					if (!columns.at (COLUMN_CUSIP).empty()) {
						item->cusip_code.assign (columns[COLUMN_CUSIP]);
						map_.emplace (std::string ("CUSIP=") + columns[COLUMN_CUSIP], item);
					}
					if (!columns.at (COLUMN_SEDOL).empty()) {
						item->sedol_code.assign (columns[COLUMN_SEDOL]);
						map_.emplace (std::string ("SEDOL=") + columns[COLUMN_SEDOL], item);
					}
					if (!columns.at (COLUMN_GICS).empty()) {
						item->gics_code.assign (columns[COLUMN_GICS]);
						map_.emplace (std::string ("GICS=") + columns[COLUMN_GICS], item);
					}
				}
			}
			LOG(INFO) << "Symbol map contains " << map_.size() << " entries.";
		}

/* UPA context. */
		upa_.reset (new upa_t (config_));
		if (!(bool)upa_ || !upa_->Initialize())
			goto cleanup;
/* UPA provider. */
		provider_.reset (new provider_t (config_, upa_, static_cast<client_t::Delegate*> (this)));
		if (!(bool)provider_ || !provider_->Initialize())
			goto cleanup;

	} catch (const std::exception& e) {
		LOG(ERROR) << "Upa::Initialisation exception: { "
			"\"What\": \"" << e.what() << "\" }";
	}

	LOG(INFO) << "Initialisation complete.";
	return true;
cleanup:
	Reset();
	LOG(INFO) << "Initialisation failed.";
	return false;
}

bool
kigoron::kigoron_t::OnRequest (
	const boost::posix_time::ptime& now,
	uintptr_t handle,
	uint16_t rwf_version, 
	int32_t token,
	uint16_t service_id,
	const std::string& item_name,
	bool use_attribinfo_in_updates
	)
{
	DVLOG(3) << "Request: { "
		  "\"now\": " << now << ""
		", \"handle\": " << handle << ""
		", \"rwf_version\": " << rwf_version << ""
		", \"token\": " << token << ""
		", \"service_id\": " << service_id << ""
		", \"item_name\": \"" << item_name << "\""
		", \"use_attribinfo_in_updates\": " << (use_attribinfo_in_updates ? "true" : "false") << ""
		" }";
/* Reset message buffer */
	rssl_length_ = sizeof (rssl_buf_);
/* Validate symbol */
	auto search = map_.find (item_name);
	if (search == map_.end()) {
		LOG(INFO) << "Closing resource not found for \"" << item_name << "\"";
		if (!provider_t::WriteRawClose (
				rwf_version,
				token,
				service_id,
				RSSL_DMT_MARKET_PRICE,
				item_name,
				use_attribinfo_in_updates,
				RSSL_STREAM_CLOSED, RSSL_SC_NOT_FOUND, kErrorNotFound,
				rssl_buf_,
				&rssl_length_
				))
		{
			return false;
		}
		goto send_reply;
	}

	if (!WriteRaw (now, rwf_version, token, service_id, item_name, nullptr, search->second, rssl_buf_, &rssl_length_)) {
/* Extremely unlikely situation that writing the response fails but writing a close will not */
		if (!provider_t::WriteRawClose (
				rwf_version,
				token,
				service_id,
				RSSL_DMT_MARKET_PRICE,
				item_name,
				use_attribinfo_in_updates,
				RSSL_STREAM_CLOSED_RECOVER, RSSL_SC_ERROR, kErrorInternal,
				rssl_buf_,
				&rssl_length_
				))
		{
			return false;
		}
		goto send_reply;
	}

send_reply:
	return provider_->SendReply (reinterpret_cast<RsslChannel*> (handle), token, rssl_buf_, rssl_length_);
}

bool
kigoron::kigoron_t::WriteRaw (
	const boost::posix_time::ptime& now,
	uint16_t rwf_version,
	int32_t token,
	uint16_t service_id,
	const chromium::StringPiece& item_name,
	const chromium::StringPiece& dacs_lock,	    /* ignore DACS lock */
	std::shared_ptr<item_t> item,
	void* data,
	size_t* length
	)
{
/* 7.4.8.1 Create a response message (4.2.2) */
	RsslRefreshMsg response = RSSL_INIT_REFRESH_MSG;
#ifndef NDEBUG
	RsslEncodeIterator it = RSSL_INIT_ENCODE_ITERATOR;
#else
	RsslEncodeIterator it;
	rsslClearEncodeIterator (&it);
#endif
	RsslBuffer buf = { static_cast<uint32_t> (*length), static_cast<char*> (data) };
	RsslRet rc;

	DCHECK(!item_name.empty());

/* 7.4.8.3 Set the message model type of the response. */
	response.msgBase.domainType = RSSL_DMT_MARKET_PRICE;
/* 7.4.8.4 Set response type, response type number, and indication mask. */
	response.msgBase.msgClass = RSSL_MC_REFRESH;
/* let infrastructure cache images to reduce latency on requests. */
	response.flags = RSSL_RFMF_SOLICITED	    |
			 RSSL_RFMF_REFRESH_COMPLETE;
/* RDM field list. */
	response.msgBase.containerType = RSSL_DT_FIELD_LIST;

/* 7.4.8.2 Create or re-use a request attribute object (4.2.4) */
	response.msgBase.msgKey.serviceId   = service_id;
	response.msgBase.msgKey.nameType    = RDM_INSTRUMENT_NAME_TYPE_RIC;
	response.msgBase.msgKey.name.data   = const_cast<char*> (item_name.data());
	response.msgBase.msgKey.name.length = static_cast<uint32_t> (item_name.size());
	response.msgBase.msgKey.flags = RSSL_MKF_HAS_SERVICE_ID | RSSL_MKF_HAS_NAME_TYPE | RSSL_MKF_HAS_NAME;
	response.flags |= RSSL_RFMF_HAS_MSG_KEY;
/* Set the request token. */
	response.msgBase.streamId = token;

/** Optional: but require to replace stale values in cache when stale values are supported. **/
/* Item interaction state: Open, Closed, ClosedRecover, Redirected, NonStreaming, or Unspecified. */
	response.state.streamState = RSSL_STREAM_NON_STREAMING;
/* Data quality state: Ok, Suspect, or Unspecified. */
	response.state.dataState = RSSL_DATA_OK;
/* Error code, e.g. NotFound, InvalidArgument, ... */
	response.state.code = RSSL_SC_NONE;

/* group per source file iff a max-age is provided. */
	if (!item->expiration_time.is_not_a_date_time()
		&& now >= item->expiration_time)
	{
		response.state.dataState = RSSL_DATA_SUSPECT;
	}

	rc = rsslSetEncodeIteratorBuffer (&it, &buf);
	if (RSSL_RET_SUCCESS != rc) {
		LOG(ERROR) << "rsslSetEncodeIteratorBuffer: { "
			  "\"returnCode\": " << static_cast<signed> (rc) << ""
			", \"enumeration\": \"" << rsslRetCodeToString (rc) << "\""
			", \"text\": \"" << rsslRetCodeInfo (rc) << "\""
			" }";
		return false;
	}
	rc = rsslSetEncodeIteratorRWFVersion (&it, provider_t::rwf_major_version (rwf_version), provider_t::rwf_major_version (rwf_version));
	if (RSSL_RET_SUCCESS != rc) {
		LOG(ERROR) << "rsslSetEncodeIteratorRWFVersion: { "
			  "\"returnCode\": " << static_cast<signed> (rc) << ""
			", \"enumeration\": \"" << rsslRetCodeToString (rc) << "\""
			", \"text\": \"" << rsslRetCodeInfo (rc) << "\""
			", \"majorVersion\": " << static_cast<unsigned> (provider_t::rwf_major_version (rwf_version)) << ""
			", \"minorVersion\": " << static_cast<unsigned> (provider_t::rwf_minor_version (rwf_version)) << ""
			" }";
		return false;
	}
	rc = rsslEncodeMsgInit (&it, reinterpret_cast<RsslMsg*> (&response), /* maximum size */ 0);
	if (RSSL_RET_ENCODE_CONTAINER != rc) {
		LOG(ERROR) << "rsslEncodeMsgInit: { "
			  "\"returnCode\": " << static_cast<signed> (rc) << ""
			", \"enumeration\": \"" << rsslRetCodeToString (rc) << "\""
			", \"text\": \"" << rsslRetCodeInfo (rc) << "\""
			" }";
		return false;
	}
	{
/* 4.3.1 RespMsg.Payload */
/* Clear required for SingleWriteIterator state machine. */
		RsslFieldList field_list;
		RsslFieldEntry field;
		RsslBuffer data_buffer;
		RsslTime rssl_time;
		RsslDate rssl_date;

		rsslClearFieldList (&field_list);
		rsslClearFieldEntry (&field);
		rsslClearTime (&rssl_time);
		rsslClearDate (&rssl_date);

		field_list.flags = RSSL_FLF_HAS_STANDARD_DATA;
		rc = rsslEncodeFieldListInit (&it, &field_list, 0 /* summary data */, 0 /* payload */);
		if (RSSL_RET_SUCCESS != rc) {
			LOG(ERROR) << "rsslEncodeFieldListInit: { "
				  "\"returnCode\": " << static_cast<signed> (rc) << ""
				", \"enumeration\": \"" << rsslRetCodeToString (rc) << "\""
				", \"text\": \"" << rsslRetCodeInfo (rc) << "\""
				", \"flags\": \"RSSL_FLF_HAS_STANDARD_DATA\""
				" }";
			return false;
		}

/* For each field set the Id via a FieldEntry bound to the iterator followed by setting the data.
 * The iterator API provides setters for common types excluding 32-bit floats, with fallback to 
 * a generic DataBuffer API for other types or support of pre-calculated values.
 */
/* PRIM_RIC */
		field.fieldId  = kRdmRicId;
		field.dataType = RSSL_DT_RMTES_STRING;
		data_buffer.data   = const_cast<char*> (item->primary_ric.c_str());
		data_buffer.length = static_cast<uint32_t> (item->primary_ric.size());
		rc = rsslEncodeFieldEntry (&it, &field, &data_buffer);
		if (RSSL_RET_SUCCESS != rc) {
			LOG(ERROR) << "rsslEncodeFieldEntry: { "
				  "\"returnCode\": " << static_cast<signed> (rc) << ""
				", \"enumeration\": \"" << rsslRetCodeToString (rc) << "\""
				", \"text\": \"" << rsslRetCodeInfo (rc) << "\""
				", \"fieldId\": " << field.fieldId << ""
				", \"dataType\": \"" << rsslDataTypeToString (field.dataType) << "\""
				", \"primaryRic\": \"" << item->primary_ric << "\""
				" }";
			return false;
		}

/* CLASS_CODE */
		field.fieldId  = kRdmClassId;
		field.dataType = RSSL_DT_RMTES_STRING;
		data_buffer.data   = const_cast<char*> (item->class_code.c_str());
		data_buffer.length = static_cast<uint32_t> (item->class_code.size());
		rc = rsslEncodeFieldEntry (&it, &field, &data_buffer);
		if (RSSL_RET_SUCCESS != rc) {
			LOG(ERROR) << "rsslEncodeFieldEntry: { "
				  "\"returnCode\": " << static_cast<signed> (rc) << ""
				", \"enumeration\": \"" << rsslRetCodeToString (rc) << "\""
				", \"text\": \"" << rsslRetCodeInfo (rc) << "\""
				", \"fieldId\": " << field.fieldId << ""
				", \"dataType\": \"" << rsslDataTypeToString (field.dataType) << "\""
				", \"classCode\": \"" << item->class_code << "\""
				" }";
			return false;
		}

/* EXCH_SNAME */
		field.fieldId  = kRdmExchangeId;
		field.dataType = RSSL_DT_RMTES_STRING;
		data_buffer.data   = const_cast<char*> (item->exchange_code.c_str());
		data_buffer.length = static_cast<uint32_t> (item->exchange_code.size());
		rc = rsslEncodeFieldEntry (&it, &field, &data_buffer);
		if (RSSL_RET_SUCCESS != rc) {
			LOG(ERROR) << "rsslEncodeFieldEntry: { "
				  "\"returnCode\": " << static_cast<signed> (rc) << ""
				", \"enumeration\": \"" << rsslRetCodeToString (rc) << "\""
				", \"text\": \"" << rsslRetCodeInfo (rc) << "\""
				", \"fieldId\": " << field.fieldId << ""
				", \"dataType\": \"" << rsslDataTypeToString (field.dataType) << "\""
				", \"exchangeShortName\": \"" << item->exchange_code << "\""
				" }";
			return false;
		}

/* CCY_NAME */
		field.fieldId  = kRdmCurrencyId;
		field.dataType = RSSL_DT_RMTES_STRING;
		data_buffer.data   = const_cast<char*> (item->currency_name.c_str());
		data_buffer.length = static_cast<uint32_t> (item->currency_name.size());
		rc = rsslEncodeFieldEntry (&it, &field, &data_buffer);
		if (RSSL_RET_SUCCESS != rc) {
			LOG(ERROR) << "rsslEncodeFieldEntry: { "
				  "\"returnCode\": " << static_cast<signed> (rc) << ""
				", \"enumeration\": \"" << rsslRetCodeToString (rc) << "\""
				", \"text\": \"" << rsslRetCodeInfo (rc) << "\""
				", \"fieldId\": " << field.fieldId << ""
				", \"dataType\": \"" << rsslDataTypeToString (field.dataType) << "\""
				", \"currencyName\": \"" << item->currency_name << "\""
				" }";
			return false;
		}

/* DSPLY_NAME */
		field.fieldId  = kRdmNameId;
		field.dataType = RSSL_DT_RMTES_STRING;
		data_buffer.data   = const_cast<char*> (item->display_name.c_str());
		data_buffer.length = static_cast<uint32_t> (item->display_name.size());
		rc = rsslEncodeFieldEntry (&it, &field, &data_buffer);
		if (RSSL_RET_SUCCESS != rc) {
			LOG(ERROR) << "rsslEncodeFieldEntry: { "
				  "\"returnCode\": " << static_cast<signed> (rc) << ""
				", \"enumeration\": \"" << rsslRetCodeToString (rc) << "\""
				", \"text\": \"" << rsslRetCodeInfo (rc) << "\""
				", \"fieldId\": " << field.fieldId << ""
				", \"dataType\": \"" << rsslDataTypeToString (field.dataType) << "\""
				", \"displayName\": \"" << item->display_name << "\""
				" }";
			return false;
		}

/* ISIN_CODE */
		field.fieldId  = kRdmIsinId;
		field.dataType = RSSL_DT_RMTES_STRING;
		data_buffer.data   = const_cast<char*> (item->isin_code.c_str());
		data_buffer.length = static_cast<uint32_t> (item->isin_code.size());
		rc = rsslEncodeFieldEntry (&it, &field, &data_buffer);
		if (RSSL_RET_SUCCESS != rc) {
			LOG(ERROR) << "rsslEncodeFieldEntry: { "
				  "\"returnCode\": " << static_cast<signed> (rc) << ""
				", \"enumeration\": \"" << rsslRetCodeToString (rc) << "\""
				", \"text\": \"" << rsslRetCodeInfo (rc) << "\""
				", \"fieldId\": " << field.fieldId << ""
				", \"dataType\": \"" << rsslDataTypeToString (field.dataType) << "\""
				", \"isinCode\": \"" << item->isin_code << "\""
				" }";
			return false;
		}

/* CUSIP_CD */
		field.fieldId  = kRdmCusipId;
		field.dataType = RSSL_DT_RMTES_STRING;
		data_buffer.data   = const_cast<char*> (item->cusip_code.c_str());
		data_buffer.length = static_cast<uint32_t> (item->cusip_code.size());
		rc = rsslEncodeFieldEntry (&it, &field, &data_buffer);
		if (RSSL_RET_SUCCESS != rc) {
			LOG(ERROR) << "rsslEncodeFieldEntry: { "
				  "\"returnCode\": " << static_cast<signed> (rc) << ""
				", \"enumeration\": \"" << rsslRetCodeToString (rc) << "\""
				", \"text\": \"" << rsslRetCodeInfo (rc) << "\""
				", \"fieldId\": " << field.fieldId << ""
				", \"dataType\": \"" << rsslDataTypeToString (field.dataType) << "\""
				", \"cusipCode\": \"" << item->cusip_code << "\""
				" }";
			return false;
		}

/* SEDOL */
		field.fieldId  = kRdmSedolId;
		field.dataType = RSSL_DT_RMTES_STRING;
		data_buffer.data   = const_cast<char*> (item->sedol_code.c_str());
		data_buffer.length = static_cast<uint32_t> (item->sedol_code.size());
		rc = rsslEncodeFieldEntry (&it, &field, &data_buffer);
		if (RSSL_RET_SUCCESS != rc) {
			LOG(ERROR) << "rsslEncodeFieldEntry: { "
				  "\"returnCode\": " << static_cast<signed> (rc) << ""
				", \"enumeration\": \"" << rsslRetCodeToString (rc) << "\""
				", \"text\": \"" << rsslRetCodeInfo (rc) << "\""
				", \"fieldId\": " << field.fieldId << ""
				", \"dataType\": \"" << rsslDataTypeToString (field.dataType) << "\""
				", \"sedolCode\": \"" << item->sedol_code << "\""
				" }";
			return false;
		}

/* GICS_CODE */
		field.fieldId  = kRdmGicsId;
		field.dataType = RSSL_DT_RMTES_STRING;
		data_buffer.data   = const_cast<char*> (item->gics_code.c_str());
		data_buffer.length = static_cast<uint32_t> (item->gics_code.size());
		rc = rsslEncodeFieldEntry (&it, &field, &data_buffer);
		if (RSSL_RET_SUCCESS != rc) {
			LOG(ERROR) << "rsslEncodeFieldEntry: { "
				  "\"returnCode\": " << static_cast<signed> (rc) << ""
				", \"enumeration\": \"" << rsslRetCodeToString (rc) << "\""
				", \"text\": \"" << rsslRetCodeInfo (rc) << "\""
				", \"fieldId\": " << field.fieldId << ""
				", \"dataType\": \"" << rsslDataTypeToString (field.dataType) << "\""
				", \"gicsCode\": \"" << item->gics_code << "\""
				" }";
			return false;
		}

/* VALUE_TS1 */
		field.fieldId  = kRdmActivityTime1Id;
		field.dataType = RSSL_DT_TIME;
		rssl_time.hour        = item->modification_time.time_of_day().hours();
		rssl_time.minute      = item->modification_time.time_of_day().minutes();
		rssl_time.second      = item->modification_time.time_of_day().seconds();
// ensure 96-bit resolution not in use, BOOST_DATE_TIME_POSIX_TIME_STD_CONFIG
		rssl_time.millisecond = static_cast<uint16_t> (item->modification_time.time_of_day().fractional_seconds() / 1000);
// microsecond resolution lost in conversions.
		rc = rsslEncodeFieldEntry (&it, &field, &rssl_time);
		if (RSSL_RET_SUCCESS != rc) {
			LOG(ERROR) << "rsslEncodeFieldEntry: { "
				  "\"returnCode\": " << static_cast<signed> (rc) << ""
				", \"enumeration\": \"" << rsslRetCodeToString (rc) << "\""
				", \"text\": \"" << rsslRetCodeInfo (rc) << "\""
				", \"fieldId\": " << field.fieldId << ""
				", \"dataType\": \"" << rsslDataTypeToString (field.dataType) << "\""
				", \"activityTime1\": { "
					  "\"hour\": " << rssl_time.hour << ""
					", \"minute\": " << rssl_time.minute << ""
					", \"second\": " << rssl_time.second << ""
					", \"millisecond\": " << rssl_time.millisecond << ""
				" }"
				" }";
			return false;
		}

/* VALUE_DT1 */
		field.fieldId  = kRdmActivityDate1Id;
		field.dataType = RSSL_DT_DATE;
		rssl_date.year  = /* upa(yyyy) */ item->modification_time.date().year();
		rssl_date.month = /* upa(1-12) */ static_cast<uint8_t> (item->modification_time.date().month());
		rssl_date.day	= /* upa(1-31) */ static_cast<uint8_t> (item->modification_time.date().day());
		rc = rsslEncodeFieldEntry (&it, &field, &rssl_date);
		if (RSSL_RET_SUCCESS != rc) {
			LOG(ERROR) << "rsslEncodeFieldEntry: { "
				  "\"returnCode\": " << static_cast<signed> (rc) << ""
				", \"enumeration\": \"" << rsslRetCodeToString (rc) << "\""
				", \"text\": \"" << rsslRetCodeInfo (rc) << "\""
				", \"fieldId\": " << field.fieldId << ""
				", \"dataType\": \"" << rsslDataTypeToString (field.dataType) << "\""
				", \"activityDate1\": { "
					  "\"year\": " << rssl_date.year << ""
					", \"month\": " << rssl_date.month << ""
					", \"day\": " << rssl_date.day << ""
				" }"
				" }";
			return false;
		}

		rc = rsslEncodeFieldListComplete (&it, RSSL_TRUE /* commit */);
		if (RSSL_RET_SUCCESS != rc) {
			LOG(ERROR) << "rsslEncodeFieldListComplete: { "
				  "\"returnCode\": " << static_cast<signed> (rc) << ""
				", \"enumeration\": \"" << rsslRetCodeToString (rc) << "\""
				", \"text\": \"" << rsslRetCodeInfo (rc) << "\""
				" }";
			return false;
		}
	}
/* finalize multi-step encoder */
	rc = rsslEncodeMsgComplete (&it, RSSL_TRUE /* commit */);
	if (RSSL_RET_SUCCESS != rc) {
		LOG(ERROR) << "rsslEncodeMsgComplete: { "
			  "\"returnCode\": " << static_cast<signed> (rc) << ""
			", \"enumeration\": \"" << rsslRetCodeToString (rc) << "\""
			", \"text\": \"" << rsslRetCodeInfo (rc) << "\""
			" }";
		return false;
	}
	buf.length = rsslGetEncodedBufferLength (&it);
	LOG_IF(WARNING, 0 == buf.length) << "rsslGetEncodedBufferLength returned 0.";

	if (DCHECK_IS_ON()) {
/* Message validation: must use ASSERT libraries for error description :/ */
		if (!rsslValidateMsg (reinterpret_cast<RsslMsg*> (&response))) {
			LOG(ERROR) << "rsslValidateMsg failed.";
			return false;
		} else {
			DVLOG(4) << "rsslValidateMsg succeeded.";
		}
	}
	*length = static_cast<size_t> (buf.length);
	return true;
}

bool
kigoron::kigoron_t::Start()
{
	LOG(INFO) << "Starting instance: { "
		" }";
	if (!shutting_down_ && Initialize()) {
/* Spawn new thread for message pump. */
		event_thread_.reset (new boost::thread ([this]() {
			MainLoop();
/* Raise condition loop is complete. */
			boost::lock_guard<boost::mutex> lock (mainloop_lock_);
			mainloop_shutdown_ = true;
			mainloop_cond_.notify_one();
		}));
		return true;
	}  else {
		return false;
	}
}

void
kigoron::kigoron_t::Stop()
{
	LOG(INFO) << "Shutting down instance: { "
		" }";
	shutting_down_ = true;
	if ((bool)provider_) {
		provider_->Quit();
/* Wait for mainloop to quit */
		boost::unique_lock<boost::mutex> lock (mainloop_lock_);
		while (!mainloop_shutdown_)
			mainloop_cond_.wait (lock);
		Reset();
	}
}

void
kigoron::kigoron_t::Reset()
{
/* Close client sockets with reference counts on provider. */
	if ((bool)provider_)
		provider_->Close();
/* Release everything with an UPA dependency. */
	CHECK_LE (provider_.use_count(), 1);
	provider_.reset();
/* Final tests before releasing UPA context */
	chromium::debug::LeakTracker<client_t>::CheckForLeaks();
	chromium::debug::LeakTracker<provider_t>::CheckForLeaks();
/* No more UPA sockets so close up context */
	CHECK_LE (upa_.use_count(), 1);
	upa_.reset();
	chromium::debug::LeakTracker<upa_t>::CheckForLeaks();
}

void
kigoron::kigoron_t::MainLoop()
{
	try {
		provider_->Run(); 
	} catch (const std::exception& e) {
		LOG(ERROR) << "Runtime exception: { "
			"\"What\": \"" << e.what() << "\" }";
	}
}

/* eof */
