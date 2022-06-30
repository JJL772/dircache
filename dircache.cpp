#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>
#include <atomic>
#include <time.h>
#include <pthread.h>

#include "dircache.h"

////////////////////////////////////////////////////////////////////////////////
// Struct decls
////////////////////////////////////////////////////////////////////////////////

/**
 * dirent_t represents a directory entry on the disk
 * These have a vector of entries, an atomic ref count 
 * and an addedat field for the time in which it was added
 * Entries that are considered stale (configured by the user)
 * will be purged from the db and replaced with fresh entries.
 * Stale entries only get purged when their refcount reaches 0
 * Thus, it's important to dirclose 
 */
struct dirent_t {
	std::vector<dirent> entries;	// List of entries
	std::atomic_uint32_t nref;		// Ref count from dirdbcontext-s
	double addedat;					// When this entry was added to the db
};

/**
 * dircontext_t just contains a position in the read stream
 * and a pointer to the dirent_t that we're supposed to be 
 * reading from.
 * This is the definition of the details behind the 
 */
struct dircontext_t {
	size_t pos;
	dirent_t* ent;
};

////////////////////////////////////////////////////////////////////////////////
// Common threading utils and hash helpers
//  RW lock is based on the posix rwmutex, also autolock helpers
////////////////////////////////////////////////////////////////////////////////

struct ReadWriteLock {
	ReadWriteLock() {
		pthread_rwlockattr_init(&attr);
		pthread_rwlock_init(&lock, &attr);
	}
	ReadWriteLock(const ReadWriteLock&) = delete;
	ReadWriteLock(ReadWriteLock&&) = delete;
	
	~ReadWriteLock() {
		pthread_rwlock_destroy(&lock);
		pthread_rwlockattr_destroy(&attr);
	}
	
	void read_lock() {
		pthread_rwlock_rdlock(&lock);
	}
	
	void write_lock() {
		pthread_rwlock_wrlock(&lock);
	}
	
	void unlock() {
		pthread_rwlock_unlock(&lock);
	}
	
	pthread_rwlockattr_t attr;
	pthread_rwlock_t lock;
};

/**
 * Auto lock for reads on a RW mutex
 */
struct AutoReadLock {
	AutoReadLock(ReadWriteLock& lock) : lock_(lock) {
		lock_.read_lock();
	}
	~AutoReadLock() {
		lock_.unlock();
	}
	
	ReadWriteLock& lock_;
};

/**
 * Auto lock for writes on a RW mutex
 */
struct AutoWriteLock {
	AutoWriteLock(ReadWriteLock& lock) : lock_(lock) {
		lock_.write_lock();
	}
	~AutoWriteLock() {
		lock_.unlock();
	}
	
	ReadWriteLock& lock_;
};

////////////////////////////////////////////////////////////////////////////////
// Global db accessors
////////////////////////////////////////////////////////////////////////////////

// Returns the internal directory db
static auto& dir_db() {
	static std::unordered_map<std::string, dirent_t*> dirdb;
	return dirdb;
}

// Returns global db lock
static auto& dir_db_lock() {
	static ReadWriteLock lock;
	return lock;
}

static dircontext_t* dc_build_around_ent(dirent_t* dent) {
	auto* ctx = new dircontext_t;
	dent->nref.fetch_add(1); // Inc ref count
	ctx->ent = dent;
	ctx->pos = 0;
	return ctx;
}

////////////////////////////////////////////////////////////////////////////////
// Private helpers
////////////////////////////////////////////////////////////////////////////////

/**
 * Returns time in ms
 */
static double dc_get_time() {
	timespec tp;
	clock_gettime(CLOCK_MONOTONIC, &tp);
	return (tp.tv_sec * 1e3) + (tp.tv_nsec / 1e6);
}

/**
 * Find or populate the dir in the db
 * Calls readdir outright if the dir doesn't exist in the db yet,
 * then stores off those results.
 */
static dircontext_t* dc_find_or_populate(const char* path) {
	auto& db = dir_db();
	if (auto ent = db.find(path); ent != db.end()) {
		return dc_build_around_ent(ent->second);
	}
	
	// Open the dir using standard POSIX functions,
	// read contents and store into the db.
	auto* d = opendir(path);
	if (!d)
		return nullptr;
	dirent** namelist = nullptr;
	// Grab all dir entries
	int r = scandir(path, &namelist, 
		[](const dirent* d) -> int { return 1; },
		[](const dirent** a, const dirent** b) -> int {
			return strcmp((*a)->d_name, (*b)->d_name);
		});
	// Bail out on error
	if (r == -1) {
		return nullptr;
	}
	
	// Build a new directory entry
	auto* dent = new dirent_t;
	dent->addedat = dc_get_time();
	dent->nref.store(0);
	for (int i = 0; i < r; ++i)
		dent->entries.push_back(*namelist[i]);
	
	// Insert into the db
	dir_db_lock().write_lock();
	dir_db().insert({path, dent});
	dir_db_lock().unlock();
	
	// Finally build a returnable value
	return dc_build_around_ent(dent);
}

static void dc_close(dircontext_t* context) {
	context->ent->nref.fetch_sub(1); // Dec refcount
	memset(context, 0, sizeof(*context)); // For safety :)
	
	// @TODO: Evict stale entries
	
	
	delete context;
}

////////////////////////////////////////////////////////////////////////////////
// Public implementation
////////////////////////////////////////////////////////////////////////////////

// Invalidate all entries
void dircache_invalidate() {
	dir_db_lock().write_lock();
	dir_db().clear();
	dir_db_lock().unlock();
}

// readdir(3)
dirent* dircache_readdir(dircontext_t* dir) {
	if (dir->pos >= dir->ent->entries.size())
		return nullptr;
	return &dir->ent->entries[dir->pos++];
}

// opendir(3)
dircontext_t* dircache_opendir(const char* path) {
	return dc_find_or_populate(path);
}

// rewinddir(3)
void dircache_rewinddir(dircontext_t* dir) {
	dir->pos = 0;
}

// telldir(3)
long dircache_telldir(dircontext_t* dir) {
	return dir->pos;
}

// seekdir(3)
void dircache_seekdir(dircontext_t* dir, long loc) {
	if (loc >= dir->ent->entries.size())
		return;
	dir->pos = loc;
}

// closedir(3)
void dircache_closedir(dircontext_t* dir) {
	dc_close(dir);
}

// scandir(3)
int dircache_scandir(const char* dirp,
	struct dirent*** namelist,
	int (*filter)(const struct dirent*),
	int (*compare)(const struct dirent**,
		const struct dirent**)) {
	
	auto ctx = dc_find_or_populate(dirp);
	if (!ctx)
		return -1;
		
	// Accumulate entries into a list -- This is not quite optimal. Should determine the number of ents first
	*namelist = (dirent**)calloc(ctx->ent->entries.size(), sizeof(dirent*));
	int n = 0;
	for (auto& e : ctx->ent->entries) {
		if (filter && filter(&e))
			continue;
		(*namelist)[n++] = &e;
	}
	
	// Resultant list sorted with qsort, as specified by POSIX standard
	if (n && compare)
		qsort(*namelist, n, sizeof(dirent*), (comparison_fn_t)compare);
	
	return n;
}