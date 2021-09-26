
#include <stdio.h>
#include <stdint.h>
#include <assert.h>
#include <time.h>

#include "dircache.h"

double curtimems()
{
	timespec tp;
	clock_gettime(CLOCK_MONOTONIC, &tp);
	return (tp.tv_sec * 1000.0) + (tp.tv_nsec / 1e6);
}

int main() {
	auto* tree = dircache_opendir("tree");
	assert(tree);

	{	
		auto start = curtimems();
		
		dirent* d = nullptr;
		while((d = dircache_readdir(tree)))
		{
			//printf("Entry name: %s\n", d->d_name);
		}
		
		auto end = curtimems();
		printf("Initially uncached read took %f ms\n", end-start);

	}
	
	dircache_rewinddir(tree);
	{
		auto start = curtimems();
		
		dirent* d = nullptr;
		while((d = dircache_readdir(tree)))
		{
		//	printf("Entry name: %s\n", d->d_name);
		}
		
		auto end = curtimems();
		printf("Cached readdir(3) took %f ms\n", end-start);
	}
	
	// Scandir 
	{
		auto start = curtimems();
		
		dirent** entlist = nullptr;
		auto num = dircache_scandir("tree", &entlist, nullptr, nullptr);
		for(int i = 0; i < num; i++)
		{
		//	printf("Entry: %s\n", entlist[i]->d_name);
		}
		
		auto end = curtimems();
		printf("Cached scandir(3) took %f ms\n", end-start);
	}
	
	// Scandir uncached 
	dircache_invalidate();
	{
		auto start = curtimems();
		
		dirent** entlist = nullptr;
		auto num = dircache_scandir("tree", &entlist, nullptr, nullptr);
		for(int i = 0; i < num; i++)
		{
		//	printf("Entry: %s\n", entlist[i]->d_name);
		}
		
		auto end = curtimems();
		printf("Uncached scandir(3) took %f ms\n", end-start);
	}
	
	return 0;
}