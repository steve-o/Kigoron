/* UPA context.
 */

#ifndef UPA_HH_
#define UPA_HH_

#include <memory>

#include "config.hh"

namespace kigoron
{

	class upa_t
	{
	public:
		explicit upa_t (const config_t& config);
		~upa_t();

		bool Initialize();
		bool VerifyVersion();

	private:
		const config_t& config_;		
	};

} /* namespace kigoron */

#endif /* UPA_HH_ */

/* eof */
