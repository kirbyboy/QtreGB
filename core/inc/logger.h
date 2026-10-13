#ifndef QTREGB_LOGGER_H
#define QTREGB_LOGGER_H

#include <string>
#include <vector>
#include <iostream>

#include "types.h"

namespace qtreGB
{

#ifdef ENABLE_LOG

struct LogEntry
{
    std::string source;
    std::string msg;
};

class Logger
{
	public:
		static Logger& instance()
		{
			static Logger logger;
			return logger;
		}

		void set_active(bool active)
		{
			m_active = active;
		}

    	void log(const std::string& source, const std::string& msg)
		{
			if(m_active)
			{
	        	m_entries.push_back({source, msg});
			}
		}

		const std::vector<LogEntry>& entries() const { return m_entries; }

		void dump() const
		{
			for (auto& e : m_entries)
			std::cout << e.source << "," << e.msg << "\n";
		}

	private:
		//Logger() = default;
		Logger() : m_active(false){};
		std::vector<LogEntry> m_entries;
		bool m_active;
};

inline void LOG(const std::string& source, const std::string& msg)
{
	Logger::instance().log(source, msg);
}

inline void active_LOG(bool active)
{
	Logger::instance().set_active(active);
}

#else
// Release version: empty inline function -> zero overhead
inline void LOG(const std::string& , const std::string& ) noexcept {}
inline void active_LOG(bool) noexcept {}
#endif


} // namespace qtreGB
#endif
