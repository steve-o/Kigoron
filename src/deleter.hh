/* unique_ptr deleters
 */

#ifndef DELETER_HH_
#define DELETER_HH_

#include <memory>

namespace internal {

	struct release_deleter {
		template <class T> void operator()(T* ptr) {
			ptr->release();
		};
	};

	struct destroy_deleter {
		template <class T> void operator()(T* ptr) {
			ptr->destroy();
		};
	};

} /* namespace internal */

#endif /* DELETER_HH_ */

/* eof */