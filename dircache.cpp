
#include "dircache.h"

#include <string>
#include <unordered_map>
#include <vector>
#include <cstring>

struct dircontext_t
{
	DIR* m_dir;
	std::vector<dirent> m_entries;
	size_t m_pos;
};

std::unordered_map<std::string, dircontext_t*> gDirMap;

// Kill off all internal cached data
void dircache_invalidate()
{
	for(auto& pair : gDirMap)
	{
		auto* dir = pair.second;
		if(dir->m_dir)
		{
			closedir(dir->m_dir);
			dir->m_dir = nullptr;
		}
		
		delete dir;
	}
	gDirMap.clear();
}

// readdir(3)
dirent* dircache_readdir(dircontext_t* dir)
{
	// Grab from internal list if it already exists
	if(!dir->m_entries.empty())
	{
		if(dir->m_entries.size() <= dir->m_pos)
			return nullptr;
		return &dir->m_entries[dir->m_pos++];
	}
	
	// Otherwise build the directory entry list and re-run ourselves 
	dirent* dent = nullptr;
	while((dent = readdir(dir->m_dir)))
	{
		dir->m_entries.push_back(*dent);
	}
	
	// Break out early if there still aren't any entries!
	if(dir->m_entries.empty())
		return nullptr;
	
	return dircache_readdir(dir);
}

// opendir(3)
dircontext_t* dircache_opendir(const char* path)
{
	auto it = gDirMap.find(path);
	
	// Create new entry if not found 
	if(it == gDirMap.end())
	{
		auto* dtree = new dircontext_t();
		dtree->m_dir = opendir(path);
		if(!dtree->m_dir)
		{
			delete dtree;
			return nullptr;
		}
		gDirMap.insert({path, dtree});
		return dtree;
	}
	
	// Check if the directory is open or not, open if not
	if(!it->second->m_dir)
	{
		it->second->m_dir = opendir(path);
	}
	
	return it->second;
}

// rewinddir(3)
void dircache_rewinddir(dircontext_t* dir)
{
	dir->m_pos = 0;
	rewinddir(dir->m_dir);
}

// telldir(3)
long dircache_telldir(dircontext_t* dir)
{
	return telldir(dir->m_dir);
}

// seekdir(3)
void dircache_seekdir(dircontext_t* dir, long loc)
{
	seekdir(dir->m_dir, loc);
	dir->m_pos = loc;
}

// closedir(3)
void dircache_closedir(dircontext_t* dir)
{
	closedir(dir->m_dir);
	dir->m_dir = nullptr;
}

// scandir(3)
int dircache_scandir(const char* dirp, struct dirent*** namelist,
	int(*filter)(const struct dirent*), 
	int(*compare)(const struct dirent**, const struct dirent**))
{
	// Lookup the dir first
	auto it = gDirMap.find(dirp);
	
	auto populateItems = [=](dircontext_t* dir) -> int {
		int num = scandir(dirp, namelist, filter, compare);
		if(num < 0)
			return num;
		
		for(int i = 0; i < num; i++) 
		{
			// TODO: This will affect readdir's ordering. readdir shouldn't have a defined order right?
			dir->m_entries.push_back(*(*namelist)[i]);
		}	
		return num;
	};
	
	if(it != gDirMap.end())
	{
		auto* dir = it->second;
		// If the list is already populated, just return that
		if(!dir->m_entries.empty())
		{
			const auto sz = dir->m_entries.size() * sizeof(dirent**);
			*namelist = static_cast<dirent**>(calloc(1, sizeof(dirent*) * dir->m_entries.size()));
			
			size_t idx = 0;
			for(auto& ent : dir->m_entries) 
			{
				if(filter && !filter(&ent))
					continue;
				(*namelist)[idx++] = &ent;
			}
			
			// Sort using qsort, as specified in the manpages
			if(compare)
				qsort(*namelist, idx, sizeof(dirent*), (comparison_fn_t)compare);
			
			return idx;
		}
		// Otherwise populate the list and cache the result for subsequent calls
		else
		{
			return populateItems(dir);
		}
	}
	
	// Create a new entry if not found yet, and populate it
	auto* dir = new dircontext_t();
	dir->m_dir = opendir(dirp);
	
	if(!dir->m_dir)
	{
		delete dir;
		return -1;
	}
	
	gDirMap.insert({dirp, dir});
	return populateItems(dir);
}