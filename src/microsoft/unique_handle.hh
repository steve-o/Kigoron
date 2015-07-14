/* http://msdn.microsoft.com/en-gb/magazine/hh288076.aspx
 */

#ifndef __MS_UNIQUE_HANDLE_HH__
#define __MS_UNIQUE_HANDLE_HH__

#include <winsock2.h>

namespace ms
{

	struct handle_traits
	{
		static HANDLE invalid() throw()
		{
			return nullptr;
		}
 
		static void close(HANDLE value) throw()
		{
			CloseHandle (value);
		}
	};

	template <typename Type, typename Traits>
	class unique_handle
	{
		unique_handle (unique_handle const &);
		unique_handle& operator= (unique_handle const &);
 
		void close() throw()
		{
			if (*this)
			{
				Traits::close (m_value);
			}
		}
 
		Type m_value;
 
	public:
 
		explicit unique_handle (Type value = Traits::invalid()) throw() :
			m_value(value)
		{
		}
 
		~unique_handle() throw()
		{
			close();
		}

	private:
 
		struct boolean_struct { int member; };
		typedef int boolean_struct::* boolean_type;
 
		bool operator== (unique_handle const &);
		bool operator!= (unique_handle const &);
 
	public:
 
		operator boolean_type() const throw()
		{
			return Traits::invalid() != m_value ? &boolean_struct::member : nullptr;
		}

		Type get() const throw()
		{
			return m_value;
		}

		bool reset (Type value = Traits::invalid()) throw()
		{
			if (m_value != value)
			{
				close();
				m_value = value;
			}
 
			return *this;
		}

		Type release() throw()
		{
			auto value = m_value;
			m_value = Traits::invalid();
			return value;
		}

		unique_handle (unique_handle&& other) throw() :
			m_value (other.release())
		{
		}
 
		unique_handle& operator= (unique_handle&& other) throw()
		{
			reset (other.release());
			return *this;
		}
	};

/* Example usage:
 *
 * handle h (CreateEvent (...));
 */
	typedef unique_handle<HANDLE, handle_traits> handle;

} /* namespace ms */

#endif /* __MS_UNIQUE_HANDLE_HH__ */

/* eof */
