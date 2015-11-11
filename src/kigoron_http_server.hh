/* HTTP embedded server.
 */

#ifndef KIGORON_HTTP_SERVER_HH_
#define KIGORON_HTTP_SERVER_HH_

#ifdef _WIN32
#	include <winsock2.h>
#endif

#include <string>
#include <memory>
#include <vector>

#include "chromium/basictypes.hh"
#include "chromium/values.hh"
#include "net/server/http_server.hh"
#include "net/server/http_server_request_info.hh"

#ifdef _WIN32           
#	define in_port_t	uint16_t
#endif

class GURL;

namespace kigoron
{
// temporary integration until message loop is available.
	class provider_t;

	struct ProviderInfo {
		ProviderInfo();
		~ProviderInfo();

		std::string hostname;
		std::string username;
		int pid;
		std::vector<std::string> clients;
	};

	class KigoronHttpServer
		: public net::HttpServer::Delegate
	{
	public:

		class Delegate {
		public:
			virtual ~Delegate() {}

			virtual void CreateInfo(ProviderInfo* info) = 0;
		};

// Constructor doesn't start server.
		explicit KigoronHttpServer (chromium::MessageLoopForIO* message_loop_for_io, Delegate* delegate);

// Destroys the object.
		virtual ~KigoronHttpServer();

// Starts HTTP server: start listening port |port| for HTTP requests.
		bool Start (in_port_t port);

// Stops HTTP server.
		void Shutdown();

	private:
// net::HttpServer::Delegate methods:
		virtual void OnHttpRequest (int connection_id, const net::HttpServerRequestInfo& info) override;
		virtual void OnWebSocketRequest(int connection_id, const net::HttpServerRequestInfo& info) override;
		virtual void OnWebSocketMessage(int connection_id, const std::string& data) override;
		virtual void OnClose(int connection_id) override;

 // Sends error as response. Invoked when request method is invalid.
		void ReportInvalidMethod(int connection_id);

// Returns |true| if |request| should be done with correct |method|.
// Otherwise sends |Invalid method| error.
// Also checks support of |request| by this server.
		bool ValidateRequestMethod(int connection_id, const std::string& request, const std::string& method);

// Processes http request after all preparations.
		net::HttpStatusCode ProcessHttpRequest(const GURL& url, const net::HttpServerRequestInfo& info, std::string* response);

// Provider API methods. Return reference to NULL if output should be empty.
		void ProcessInfo(std::string* response, net::HttpStatusCode* status_code) const;

// Port for listening.
		in_port_t port_;

// Contains encapsulated object for listening for requests.
		std::shared_ptr<net::HttpServer> server_;

// Message loop to direct all tasks towards.
		chromium::MessageLoopForIO* message_loop_for_io_;

		Delegate* delegate_;
	};

} /* namespace kigoron */

#endif /* KIGORON_HTTP_SERVER_HH_ */

/* eof */
