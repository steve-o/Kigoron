/* HTTP embedded server.
 */

#ifndef HTTPD_HH_
#define HTTPD_HH_

namespace kigoron
{
	class httpd_t
	{
	public:
		explicit httpd_t();
		~httpd_t();

		bool Initialize();
		void Close();

	private:
		SOCKET http_sock_;
	};

} /* namespace kigoron */

#endif /* HTTPD_HH_ */

/* eof */
