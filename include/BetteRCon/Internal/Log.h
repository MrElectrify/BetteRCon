#ifndef BETTERCON_STDOUTLOG_H_
#define BETTERCON_STDOUTLOG_H_

/*
 *	Log
 *	11/29/19 23:59
 */

// STL
#include <ctime>
#include <iomanip>
#include <iostream>
#include <mutex>

namespace BetteRCon
{
	namespace Internal
	{
		// Log wraps around an ostream with timestamps
		/// TODO: Synchronization between threads
		class Log
		{
		public:
			// Creates a log wrapped around an ostream
			Log(std::ostream& os) : m_os(os) {}
			
			// Writes an argument to the stream, returns the synchronized argument
			template<typename T>
			std::ostream& write(const T& arg)
			{
				auto tm = std::time(nullptr);
				auto msTime = std::chrono::duration_cast<std::chrono::milliseconds>
					(std::chrono::system_clock::now().time_since_epoch()).count()
					% 1000;

				return m_os << std::put_time(std::localtime(&tm), "[%Y-%m-%d %H:%M:%S") << '.' << std::setw(3) << std::setfill('0') << msTime << "]: " << arg;
			}
		private:
			std::ostream& m_os;
		};

		static Log g_stdOutLog(std::cout);
		static Log g_stdErrLog(std::cerr);

		// Returns the stream in a synchronized manner, after outputting the timestamp
		template<typename T>
		std::ostream& operator<<(Log& log, const T& arg)
		{
			return log.write(arg);
		}
	}
}

#endif