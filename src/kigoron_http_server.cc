/* HTTP embedded server.
 */

#include "kigoron_http_server.hh"

#include "chromium/logging.hh"
#include "chromium/strings/stringprintf.hh"
#include "net/base/ip_endpoint.hh"
#include "net/base/net_errors.hh"
#include "net/socket/tcp_listen_socket.hh"
#include "url/gurl.hh"

namespace {

// Returns |true| if |request| should be GET method.
bool IsGetMethod(const std::string& request) {
  return true;
}

// Returns |true| if |request| should be POST method.
bool IsPostMethod(const std::string& request) {
  return false;
}

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
	GURL url("http://host" + info.path);

	if (!ValidateRequestMethod(connection_id, url.path(), info.method))
		return;

	if (info.path == "" || info.path == "/") {
		std::string response;
		net::HttpStatusCode status_code = ProcessHttpRequest(url, info, &response);
		server_->Send (connection_id, status_code, response, "text/html; charset=UTF-8");
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
	std::stringstream ss;

	ProviderInfo info;
	delegate_->CreateInfo (&info);

	ss << info.clients.size();
	server_->SendOverWebSocket(connection_id, ss.str());
}

void
kigoron::KigoronHttpServer::OnClose (
	int connection_id
	)
{
}

void
kigoron::KigoronHttpServer::ReportInvalidMethod (
	int connection_id
	)
{
	server_->Send(connection_id, net::HTTP_BAD_REQUEST, "Invalid method", "text/plain");
}

bool
kigoron::KigoronHttpServer::ValidateRequestMethod (
	int connection_id,
	const std::string& request,
	const std::string& method
	)
{
	DCHECK(!IsGetMethod(request) || !IsPostMethod(request));

	if (!IsGetMethod(request) && !IsPostMethod(request)) {
		server_->Send404(connection_id);
		return false;
	}

	if ((IsGetMethod(request) && method != "GET") ||
		(IsPostMethod(request) && method != "POST"))
	{
		ReportInvalidMethod(connection_id);
		return false;
	}

	return true;
}

net::HttpStatusCode
kigoron::KigoronHttpServer::ProcessHttpRequest (
	const GURL& url,
	const net::HttpServerRequestInfo& info,
	std::string* response
	)
{
	net::HttpStatusCode status_code = net::HTTP_OK;

	if (info.path == "" || info.path == "/") {
		ProcessInfo (response, &status_code);
	} else {
		NOTREACHED();
	}

	return status_code;
}

// Provider API methods:

void
kigoron::KigoronHttpServer::ProcessInfo (
	std::string* response,
	net::HttpStatusCode* status_code
	) const
{
	std::stringstream ss;

	ProviderInfo info;
	delegate_->CreateInfo (&info);
	
	ss   << "<!DOCTYPE html>"
		"<html>"
		"<head>"
			"<meta charset=\"UTF-8\">"
			"<script type=\"text/javascript\">"
			"(function() {"
				"var sock = new WebSocket(\"ws://\" + window.location.host + \"/ws\");"
				"var id = undefined;"
				"sock.onopen = function() {"
					"id = window.setInterval(function() {"
						"sock.send(\"!\");"
					"}, 100);"
				"};"
				"sock.onclose = function() {"
					"if (typeof id === \"number\") {"
						"window.clearInterval(id);"
						"id = undefined;"
					"}"
				"};"
				"sock.onmessage = function(msg) {"
					"document.getElementById(\"clients\").textContent = msg.data;"
				"};"
				"document.addEventListener(\"visibilitychange\", function() {"
					"switch (document.visibilityState) {"
					"case \"hidden\":"
					"case \"unloaded\":"
						"sock.onclose();"
						"break;"
					"case \"visible\":"
						"if (id === undefined && sock.readyState == WebSocket.OPEN) {"
							"sock.onopen();"
						"}"
						"break;"
					"}"
				"});"
			"})();"
			"</script>"
		"</head>"
		"<body>"
		"<table>"
		"<tr>"
			"<th>host name:</th><td>" << info.hostname << "</td>"
		"</tr><tr>"
			"<th>user name:</th><td>" << info.username << "</td>"
		"</tr><tr>"
			"<th>process ID:</th><td>" << info.pid << "</td>"
		"</tr><tr>"
			"<th>clients:</th><td id=\"clients\"></td>"
		"</tr>"
		"</table>"
		"</body>"
		"</html>"
		"\n";

	response->assign (ss.str());
	*status_code = net::HTTP_OK;
}

/* eof */
