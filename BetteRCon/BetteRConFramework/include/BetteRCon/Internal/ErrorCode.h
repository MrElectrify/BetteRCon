#ifndef BETTERCON_INTERNAL_ERRORCODE_H_
#define BETTERCON_INTERNAL_ERRORCODE_H_

/*
 *	Error Code
 *	10/11/19 12:06
 */

// STL
#include <system_error>

/*
 *	Note: Naming convention is snake_case here to conform
 *		  to c++ standards
 */

namespace BetteRCon
{
	namespace Internal
	{
		enum errc
		{
			packet_too_small = 1,	// The packet is too small and did not include all members
			packet_malformed,		// The packet was malformed
		};

		/*
		 *	ErrorCode inherits from std::error_condition, and provides our own tracking of error codes
		 */
		class bettercon_category_impl :
			public std::error_category
		{
		public:
			virtual const char* name() const;
			virtual std::string message(int ev) const;
			virtual bool equivalent(const std::error_code& ec, int condition) const;
		};

		// error code's category
		const std::error_category& bettercon_category();

		std::error_condition make_error_condition(errc e);

		using error_code = std::error_condition;
	}
}

namespace std
{
	template <>
	struct is_error_condition_enum<BetteRCon::Internal::errc>
		: public true_type {};
}

#endif