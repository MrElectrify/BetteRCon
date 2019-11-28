#include <BetteRCon/Internal/ErrorCode.h>

using BetteRCon::Internal::errc;
using BetteRCon::Internal::bettercon_category_impl;

const char* bettercon_category_impl::name() const noexcept
{
	return "bettercon";
}

std::string bettercon_category_impl::message(int ev) const noexcept
{
	switch (ev)
	{
	case errc::packet_too_small:
		return "The packet was too small and was missing crucial members";
	case errc::packet_malformed:
		return "The packet was malformed";
	default:
		return "Unknown";
	}
}

bool bettercon_category_impl::equivalent(const std::error_code& ec, int condition) const noexcept
{
	return false;
}

const std::error_category& BetteRCon::Internal::bettercon_category()
{
	static bettercon_category_impl instance;
	return instance;
}

std::error_condition BetteRCon::Internal::make_error_condition(errc e)
{
	return std::error_condition(
		static_cast<int>(e),
		bettercon_category());
}