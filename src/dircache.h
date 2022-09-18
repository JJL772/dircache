#pragma once

#include <unistd.h>
#include <sys/stat.h>
#include <sys/dir.h>

struct dircontext_t;

/**
 * Invalidates all internal cache data
 * Call this when you want to force a refresh of the tree
 */
void dircache_invalidate();

/**
 * @brief Replacement for readdir. See readdir(3)
 */
dirent* dircache_readdir(dircontext_t* dir);

/**
 * @brief Replacement for opendir. See opendir(3)
 */
dircontext_t* dircache_opendir(const char* path);

/** 
 * @brief Reset dircontext to first entry
 */
void dircache_rewinddir(dircontext_t* dir);

/** 
 * @brief See telldir(3)
 */
long dircache_telldir(dircontext_t* dir);

/**
 * @brief See seekdir(3)
 */
void dircache_seekdir(dircontext_t* dir, long loc);

/**
 * @brief See closedir(3)
 */
void dircache_closedir(dircontext_t* dir);

/**
 * @brief See scandir(3)
 */
int dircache_scandir(const char* dirp, struct dirent*** namelist,
	int(*filter)(const struct dirent*), 
	int(*compare)(const struct dirent**, const struct dirent**));

