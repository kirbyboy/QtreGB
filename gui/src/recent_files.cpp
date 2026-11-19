#include "recent_files.h"

#include <algorithm>

void RecentFiles::set(const std::vector<std::string>& paths)
{
	m_paths.clear();
	for(const std::string& path : paths)
	{
		if(path.empty())
		{
			continue;
		}
		if(std::find(m_paths.begin(), m_paths.end(), path) != m_paths.end())
		{
			continue;
		}
		m_paths.push_back(path);
		if(m_paths.size() == MaxEntries)
		{
			break;
		}
	}
}

void RecentFiles::remember(const std::string& path)
{
	if(path.empty())
	{
		return;
	}
	// Copy before touching m_paths: path may alias one of its elements.
	const std::string entry = path;
	m_paths.erase(std::remove(m_paths.begin(), m_paths.end(), entry),
		m_paths.end());
	m_paths.insert(m_paths.begin(), entry);
	if(m_paths.size() > MaxEntries)
	{
		m_paths.resize(MaxEntries);
	}
}

void RecentFiles::remove(const std::string& path)
{
	// Copy for the same aliasing reason as remember().
	const std::string entry = path;
	m_paths.erase(std::remove(m_paths.begin(), m_paths.end(), entry),
		m_paths.end());
}
