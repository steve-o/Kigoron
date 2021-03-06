/* HTTP embedded server.
 */

#include "kigoron_http_server.hh"

#include "chromium/json/json_writer.hh"
#include "chromium/logging.hh"
#include "chromium/strings/stringprintf.hh"
#include "chromium/values.hh"
#include "net/base/ip_endpoint.hh"
#include "net/base/net_errors.hh"
#include "net/server/http_server_response_info.hh"
#include "net/socket/tcp_listen_socket.hh"
#include "url/gurl.hh"

namespace {

#include "index.html.h"
#include "poll.js.h"

}  // namespace

kigoron::ProviderInfo::ProviderInfo() : pid(0) {
}

kigoron::ProviderInfo::~ProviderInfo() {
}

kigoron::KigoronHttpServer::KigoronHttpServer (
	chromium::MessageLoopForIO* message_loop_for_io,
	kigoron::KigoronHttpServer::Delegate* delegate
	)
	: port_ (0)
	, message_loop_for_io_ (message_loop_for_io)
	, delegate_ (delegate)
{
}

kigoron::KigoronHttpServer::~KigoronHttpServer()
{
	Shutdown();
}

/* Open HTTP port and listen for incoming connection attempts.
 */
bool
kigoron::KigoronHttpServer::Start (
	in_port_t port
	)
{
	if ((bool)server_)
		return true;

	net::TCPListenSocketFactory factory (message_loop_for_io_, "::", port);
	server_ = std::make_shared<net::HttpServer> (factory, this);
	net::IPEndPoint address;

	if (net::OK != server_->GetLocalAddress (&address)) {
		NOTREACHED() << "Cannot start HTTP server";
		return false;
	}

	LOG(INFO) << "Address of HTTP server: " << address.ToString();
	return true;
}

void
kigoron::KigoronHttpServer::Shutdown()
{
	if (!(bool)server_)
		return;

	server_.reset();
}

void
kigoron::KigoronHttpServer::OnHttpRequest (
	int connection_id,
	const net::HttpServerRequestInfo& info
	)
{
	VLOG(1) << "Processing HTTP request: " << info.path;
	if (0 == info.path.find ("/json")) {
		OnJsonRequestUI (connection_id, info);
		return;
	}

	if (info.path == "" || info.path == "/") {
		OnDiscoveryPageRequestUI (connection_id);
		return;
	}
	if (info.path == "/poll.js") {
		OnPollScriptRequestUI (connection_id);
		return;
	}

	if (0 != info.path.find ("/provider/")) {
		server_->Send404 (connection_id);
		return;
	}

	server_->Send404 (connection_id);
}

void
kigoron::KigoronHttpServer::OnWebSocketRequest (
	int connection_id,
	const net::HttpServerRequestInfo& info
	)
{
	server_->AcceptWebSocket(connection_id, info);
}

void
kigoron::KigoronHttpServer::OnWebSocketMessage (
	int connection_id,
	const std::string& data
	)
{
	std::string response;

	std::shared_ptr<chromium::DictionaryValue> dict(new chromium::DictionaryValue);
	ProviderInfo info;
	delegate_->CreateInfo (&info);
	dict->SetString("hostname", info.hostname);
	dict->SetString("username", info.username);
	dict->SetInteger("pid", info.pid);
	dict->SetInteger("clients", info.client_count);
	dict->SetInteger("msgs", info.msgs_received);
	chromium::JSONWriter::Write(dict.get(), &response);

	server_->SendOverWebSocket(connection_id, response);
}

void
kigoron::KigoronHttpServer::OnClose (
	int connection_id
	)
{
}

static bool ParseJsonPath(
    const std::string& path,
    std::string* command,
    std::string* target_id) {
  // Fall back to list in case of empty query.
  if (path.empty()) {
    *command = "list";
    return true;
  }
  if (path.find("/") != 0) {
    // Malformed command.
    return false;
  }
  *command = path.substr(1);
  size_t separator_pos = command->find("/");
  if (separator_pos != std::string::npos) {
    *target_id = command->substr(separator_pos + 1);
    *command = command->substr(0, separator_pos);
  }
  return true;
}

void
kigoron::KigoronHttpServer::OnJsonRequestUI (
	int connection_id,
	const net::HttpServerRequestInfo& info
	)
{
// Trim /json
	std::string path = info.path.substr(5);

// Trim fragment and query
	std::string query;
	size_t query_pos = path.find("?");
	if (query_pos != std::string::npos) {
		query = path.substr(query_pos + 1);
		path = path.substr(0, query_pos);
	}
	size_t fragment_pos = path.find("#");
	if (fragment_pos != std::string::npos)
		path = path.substr(0, fragment_pos);

	std::string command;
	std::string target_id;
	if (!ParseJsonPath(path, &command, &target_id)) {
		SendJson(connection_id, net::HTTP_NOT_FOUND, nullptr, "Malformed query: " + info.path);
		return;
	}

	if ("info" == command) {
		chromium::DictionaryValue dict;
		ProviderInfo info;
		delegate_->CreateInfo (&info);
		dict.SetString("hostname", info.hostname);
		dict.SetString("username", info.username);
		dict.SetInteger("pid", info.pid);
		dict.SetInteger("clients", info.client_count);
		dict.SetInteger("msgs", info.msgs_received);
		SendJson(connection_id, net::HTTP_OK, &dict, std::string());
	}

	SendJson(connection_id, net::HTTP_NOT_FOUND, nullptr, "Unknown command: " + command);
}

void
kigoron::KigoronHttpServer::OnDiscoveryPageRequestUI (
	int connection_id
	)
{
	std::string response = GetDiscoveryPageHTML();
	server_->Send200(connection_id, response, "text/html; charset=UTF-8");
}

void
kigoron::KigoronHttpServer::OnPollScriptRequestUI (
	int connection_id
	)
{
	std::string response = GetPollScriptJS();
	server_->Send200(connection_id, response, "application/json; charset=UTF-8");
}

void
kigoron::KigoronHttpServer::SendJson (
	int connection_id,
	net::HttpStatusCode status_code,
	chromium::Value* value,
	const std::string& message
	)
{
// Serialize value and message.
	std::string json_value;
	if (value) {
		chromium::JSONWriter::WriteWithOptions(value,
			chromium::JSONWriter::OPTIONS_PRETTY_PRINT,
			&json_value);
	}
	std::string json_message;
	std::shared_ptr<chromium::Value> message_object(new chromium::StringValue(message));
	chromium::JSONWriter::Write(message_object.get(), &json_message);

	net::HttpServerResponseInfo response(status_code);
	response.SetBody(json_value + message, "application/json; charset=UTF-8");
	server_->SendResponse(connection_id, response);
}

#include "chromium/strings/string_util.hh"

std::string
kigoron::KigoronHttpServer::GetDiscoveryPageHTML() const
{
	ProviderInfo info;
	delegate_->CreateInfo (&info);

	std::string response (WWW_INDEX_HTML);
	ReplaceFirstSubstringAfterOffset (&response, 0, "%HOSTNAME%", info.hostname);
	ReplaceFirstSubstringAfterOffset (&response, 0, "%USERNAME%", info.username);
	ReplaceFirstSubstringAfterOffset (&response, 0, "%PID%", std::to_string (info.pid));
	ReplaceFirstSubstringAfterOffset (&response, 0, "%CLIENTS%", std::to_string (info.client_count));
	ReplaceFirstSubstringAfterOffset (&response, 0, "%MSGS%", std::to_string (info.msgs_received));

	return response;
}

std::string
kigoron::KigoronHttpServer::GetPollScriptJS() const
{
	return std::string (WWW_POLL_JS);
}

/* eof */
