/* This is free and unencumbered software released into the public domain. */
/* Check the bottom of this file for the remainder of the unlicense */

/* wb_alloc.h
 * 
 * Three custom allocators that can (hopefully) safely allocate a very large
 * amount of memory for you. Check the warnings below for an explanation
 * 
 * Version 0.0.1 Testing Alpha
 */

/* Author: William Bundy 
 * Written in November of 2017
 *
 * williambundy.xyz
 * github.com/williambundy
 * Twitter: @William_Bundy
 */ 

/* ===========================================================================
 * WARNING -- ALPHA SOFTWARE
 * ===========================================================================
 * This is ALPHA software, and has not been tested thoroughly!
 * Please report all bugs to the github, and expect that there will be plenty,
 * (especially with the tagged heap, I suspect)
 * ===========================================================================
 *
 * Also: Warning -- Virtual Memory abuse
 *
 * The way the core allocator here works, the MemoryArena, is by reserving
 * a ton of virtual memory up front and committing out of it as necessary.
 *
 * By "a ton" I mean an amount equal to the physical amount of ram on your
 * machine. I have 16 gb in my tower, so I reserve 16 gb. 
 *
 * This is probably fine, but...
 * 
 * This might be a bad idea!
 *
 * It's also a windows-centric design, so it may work poorly on POSIX systems
 * that only have mmap. 
 *
 * I've used this quite a bit in my own code, and it's worked fine there, 
 * but you might find you have problems.
 */

/* To use this library, include it as
 * #define WB_ALLOC_IMPLEMENTATION
 * #include "wb_alloc.h"
 *
 * It probably only works from C right now, sorry!
 * 
 * In a single translation unit, otherwise it behaves as a header
 *
 * Only minimal documentation is available right now, but check line 317
 * for some details.
 *
 * There is also a short demo at the bottom of the file.
 *
 * Options:
 *
 * #define WB_ALLOC_API
 * #define WB_ALLOC_BACKEND_API
 * This will apply to all functions in the library. By default, without
 * WB_ALLOC_IMPLEMENTATION, it is extern, and with it, it is blank
 * The internal virtual memory wrapping functions are defined with 
 * WB_ALLOC_BACKEND_API, which defaults to WB_ALLOC_API.
 *
 * #ifndef WB_ALLOC_CUSTOM_INTEGER_TYPES
 * The library uses wb_usize, wb_isize, and wb_flags. These are typedef'd
 * size_t, ptrdiff_t, and int, respectively. Typedef these on your own and 
 * define WB_ALLOC_CUSTOM_INTEGERTYPES to ignore them.
 *
 * #define WB_ALLOC_STACK_PTR wb_usize
 * #define WB_ALLOC_EXTENDED_INFO wb_isize
 * These are options for modes of wb_MemoryArena. By default they are 8 byte
 * integers.
 *
 * #define WB_ALLOC_MEMSET memset
 * #define WB_ALLOC_MEMCPY memcpy
 * wb_alloc uses memset and memcpy. If neither of these are present, the 
 * library includes string.h. You may define your own, and it will not.
 *
 * #define WB_ALLOC_TAGGEDHEAP_MAX_TAG_COUNT 64
 * This defines the total number of tags available to a tagged heap. If you 
 * need more than 64, or far fewer, redefine it as you need.
 *
 * #define WB_ALLOC_NO_ZERO_ON_INIT
 * Whenever you call wb_allocatorInit(wb_allocator*, ...) we zero the pointer 
 * you give, unless this flag is set.
 *
 * #define WB_ALLOC_CPLUSPLUS_FEATURES
 * If you are using C++, there are some "features" of C that are not available,
 * first and foremost, automatic void* coercion to other pointer types. To save 
 * your fingers, I've added templated versions of the regular allocation 
 * functions, which use the provided type to determine the size and return
 * type. Too, you can use arenaPush<MyType, 100>(arena) to allocate room for
 * 100 objects (this exists on a few, search for WB_ALLOC_CPLUSPLUS_FEATURES
 * to find the actual prototypes).
 */

/* Things I've borrowed that influence this:
 * http://blog.nervus.org/managing-virtual-address-spaces-with-mmap/
 * Jason Gregory's trick where you embed the free list into the pool slots!
 * That Naughty Dog GDC talk about fibers where he mentions the tagged heap
 */

/*
 * Roadmap:
 * 	- Testing
 * 	- C++ integration features
 * 	- Fixed-size only version that doesn't define anything to do 
 * 		with virtual memory
 * 	- A simple malloc-backed memory arena for projects that really don't
 * 		need anything special at all
 */

/* These are my personal notes; don't worry too much about them
 *
 * FUTURE(will): write comprehensive test suite for everything
 * 		- So far we've tested each individual thing for "wow look how bad this
 * 		crashes" bugs, but we haven't exactly performed anything rigorous 
 * 		that'd prove that it works under weird edge cases
 *
 * TODO(will): create and standardize on types
 * TODO(will): rename everything, reorganize struct definitions
 * TODO(will): add utility functions for wb_MemoryPool: 
 * 		- some are already implemented but commented out
 * 		- poolClear
 * 		- poolCompact: invalidate pool, but might be good for serialize?
 * 		- poolIterate
 * TODO(will): add utility functions for wb_TaggedHeap
 * 		- Combine arenas? 
 * TODO(will): consider adding ways for wb_MemoryPools to read their elements'
 *	data to make smarter decisions
 *		- Write a flag that denotes an object as "dead" or "alive"
 *		- Invoke a function that reinitializes an object on retrieve
 * TODO(will): scrub those warnings!
 * TODO(will): wb_TaggedHeapZeroOnAllocate //right now we zero on free
 *
 * Memory Arena
 * 		In the Handmade Hero sense, these are linear or "bump'n push" allocators
 * 		Allocate a bunch of memory up front; free the entire arena at once.
 * 		Options allow it to become a stack allocator too, allowing for push/pop
 * 		arenaPush(wb_MemoryArena*, wb_usize) is the main driver
 * 	- Normal: uses virtual memory to expand
 *  - FixedSize: allocates out of a fixed-size buffer 
 * 	- Stack: stores extra information allowing pop operations
 * 	- Extended: stores user-provided information alongside allocations
 * Memory Pool (sits atop an arena)
 * 		"Pools" allocations of same-size objects, with no external fragmentation
 * 		poolRetrieve(wb_MemoryPool*) and poolRelease(wb_MemoryPool*, void*) are 
 * 		the drivers
 *  - Normal: uses a normal arena to expand
 *  - FixedSize: allocates out of a fixed size buffer
 *  - Compacting: moves entries into empty slots
 *
 * Tagged Heap
 * 		Inspired by the Naughty Dog talk, these behave like a pool of fixed-size
 * 		memory arenas. By tagging your allocations, you can then free allocations 
 * 		by that tag.
 * 	- Normal: a memory pool of fixed-size arenas
 * 	- FixedSize: uses a fixed size pooa
 * 	- BestFit
 * 	- NoZeroMemory
 * 	- Clean up NoSetCommitSize
 *
 * BONUS:
 *	- WB_ALLOC_MALLOC_ARENA_ONLY 
 *		- Adds an expanding arena that allocates through malloc.
 * 	- WB_ALLOC_FIXED_SIZE_ONLY if virtual memory sends shivers down your spine.
 * 		- These are different from the other ones:
 * 			- Arena, Stack, Pool, wb_TaggedHeap as individual types.
 * 			- Designed to be as simple and compact as possible.
 * 	- WB_ALLOC_CPLUSPLUS_FEATURES C++isms, because why not? 
 * 		- Templated versions to be more idiomatic C++
 * 		- Member functions! arena->allocate<MyThing>();
 *		- Placement new stuff???
 **/
#ifndef WB_ALLOC_NO_DISABLE_STUPID_MSVC_WARNINGS
#pragma warning(push)
#pragma warning(disable:201 204 28 244 706)
#endif


#ifndef WB_ALLOC_ERROR_HANDLER
#define WB_ALLOC_ERROR_HANDLER(message, object, name) fprintf(stderr, \
		"wbAlloc error: [%s] %s\n", name, message)
#endif

#ifdef WB_ALLOC_CPLUSPLUS_FEATURES
#ifndef __cplusplus
#undef WB_ALLOC_CPLUSPLUS_FEATURES
#endif
#endif

#if !(defined(WB_ALLOC_POSIX) || defined(WB_ALLOC_WINDOWS))
#ifdef _MSC_VER
#define WB_ALLOC_WINDOWS 
#else
#ifdef __unix__
#define WB_ALLOC_POSIX
#endif
#endif
#endif

#ifndef WB_ALLOC_API
#ifdef WB_ALLOC_IMPLEMENTATION
#define WB_ALLOC_API 
#else
#define WB_ALLOC_API extern
#endif
#endif

#ifndef WB_ALLOC_BACKEND_API
#define WB_ALLOC_BACKEND_API WB_ALLOC_API
#endif


#ifndef WB_ALLOC_CUSTOM_INTEGER_TYPES
typedef size_t wb_usize;
typedef ptrdiff_t wb_isize;
typedef wb_isize wb_flags;
#endif


#ifndef WB_ALLOC_STACK_PTR
#define WB_ALLOC_STACK_PTR wb_usize
#endif

#ifndef WB_ALLOC_EXTENDED_INFO
#define WB_ALLOC_EXTENDED_INFO wb_isize
#endif

#if !defined(WB_ALLOC_MEMSET) && !defined(WB_ALLOC_MEMCPY)
/* NOTE(will): if the user hasn't provided their own functions, we want
 * 	to use the CRT ones automatically, even if they don't have string.h
 * 	included
 */
#include <string.h>
#endif

#ifndef WB_ALLOC_MEMSET
#define WB_ALLOC_MEMSET memset
#endif

#ifndef WB_ALLOC_MEMCPY
#define WB_ALLOC_MEMCPY memcpy
#endif

#ifndef WB_ALLOC_TAGGEDHEAP_MAX_TAG_COUNT
/* NOTE(will): if you listen to the Naughty Dog talk the tagged heap is based 
 * on, it seems like they only have ~4 tags? Something like "game", "render",
 * "physics", "anim", kinda thing. 64 should be way more than enough if that
 * is the average use case
 */
#define WB_ALLOC_TAGGEDHEAP_MAX_TAG_COUNT 64
#endif

#define wb_CalcKilobytes(x) (((wb_usize)x) * 1024)
#define wb_CalcMegabytes(x) (wb_CalcKilobytes((wb_usize)x) * 1024)
#define wb_CalcGigabytes(x) (wb_CalcMegabytes((wb_usize)x) * 1024)

#define wb_None 0
#define wb_Read 1
#define wb_Write 2
#define wb_Execute 4

#define wb_FlagArenaNormal 0
#define wb_FlagArenaFixedSize 1
#define wb_FlagArenaStack 2
#define wb_FlagArenaExtended 4
#define wb_FlagArenaNoZeroMemory 8
#define wb_FlagArenaNoRecommit 16 

#define wb_FlagPoolNormal 0
#define wb_FlagPoolFixedSize 1
#define wb_FlagPoolCompacting 2
#define wb_FlagPoolNoZeroMemory 4
#define wb_FlagPoolNoDoubleFreeCheck 8

#define wb_FlagTaggedHeapNormal 0
#define wb_FlagTaggedHeapFixedSize 1
#define wb_FlagTaggedHeapNoZeroMemory 2
#define wb_FlagTaggedHeapNoSetCommitSize 4
#define wb_FlagTaggedHeapSearchForBestFit 8
#define wbi__TaggedHeapSearchSize 8

/* Struct Definitions */

typedef struct wb_MemoryInfo wb_MemoryInfo;
struct wb_MemoryInfo
{
	wb_usize totalMemory, commitSize, pageSize;
	wb_flags commitFlags;
};

typedef struct wb_MemoryArena wb_MemoryArena;
struct wb_MemoryArena
{
	const char* name;
	void *start, *head, *end;
	void *tempStart, *tempHead;
	wb_MemoryInfo info;
	wb_isize align;
	wb_flags flags;
};

typedef struct wb_MemoryPool wb_MemoryPool;
struct wb_MemoryPool
{
	wb_usize elementSize; 
	wb_isize count, capacity;
	void* slots;
	const char* name;
	void** freeList;
	wb_MemoryArena* alloc;
	wb_isize lastFilled;
	wb_flags flags;
};

typedef struct wbi__TaggedHeapArena wbi__TaggedHeapArena;
struct wbi__TaggedHeapArena
{
	wb_isize tag;
	wbi__TaggedHeapArena *next;
	void *head, *end;
	char buffer;
};

typedef struct wb_TaggedHeap wb_TaggedHeap;
struct wb_TaggedHeap
{
	const char* name;
	wb_MemoryPool pool;
	wbi__TaggedHeapArena* arenas[WB_ALLOC_TAGGEDHEAP_MAX_TAG_COUNT];
	wb_MemoryInfo info;
	wb_usize arenaSize, align;
	wb_flags flags;
};

/* Function Prototypes */

/* arenaPush and arenaPushEx increment the head pointer of the provided
 * arena and return the old one. It is safe to write information within
 * the size provided to the function.
 *
 * The Ex version also takes a WB_ALLOC_EXTENDED_INFO (defaults to a ptrdiff_t-
 * sized int; you may wish to redefine it before including the file), which, 
 * if you're using an arena with the ArenaExtended flag enabled, will store
 * that information before your allocation.
 */ 

WB_ALLOC_API 
void* wb_arenaPushEx(wb_MemoryArena* arena, 
		wb_isize size, 
		WB_ALLOC_EXTENDED_INFO extended);

WB_ALLOC_API 
void* wb_arenaPush(wb_MemoryArena* arena, wb_isize size);

/* poolRetrieve gets the next element out of the pool. If the pool hasn't
 * been used yet, it simply pulls the next item out at the correct location.
 * Otherwise, it checks a free list of empty slots.
 *
 * poolRelease attaches the pointer to the free list. By default, it also
 * checks whether the pointer is already on the free list, to help prevent
 * double-free bugs. Use the PoolNoDoubleFreeCheck flag to disable this.
 *
 * If your pool is compacting (PoolCompacting flag), poolRelease will instead
 * copy the last element of the pool into the slot pointed at by ptr.
 * TODO(will): uh, maybe do some safety checks there...
 * This means that you can treat the pool's slots field as an array of your
 * struct/union and iterate over it without expecting holes; however, any 
 * retrieve/release operations can invalidate your pointers
 */
WB_ALLOC_API 
void* wb_poolRetrieve(wb_MemoryPool* pool);
WB_ALLOC_API 
void wb_poolRelease(wb_MemoryPool* pool, void* ptr);

/* taggedAlloc behaves much like arenaPush, returning a pointer to a segment
 * of memory that is safe to write to. However, you cannot allocate more than
 * the arenaSize field of the heap at once; eg: if arenaSize is 1 megabyte, 
 * you cannot allocate anything larger than that in that heap, ever. 
 *
 * Internally, a tagged heap is a memory pool of arenas (simlified ones rather
 * than a full MemoryArena). It, by default, selects the first arena in the
 * list, but you can set the TaggedHeapSearchForBestFit flag, which will search
 * for (the first eight or so) arenas that can fit the object, then put it into
 * the one with the smallest space remaining. 
 *
 * taggedFree allows you to free all allocations on a single tag at once. 
 * If you do not specify TaggedHeapNoZeroMemory, it will also memset everything
 * to zero.
 *
 */
WB_ALLOC_API 
void* wb_taggedAlloc(wb_TaggedHeap* heap, wb_isize tag, wb_usize size);
WB_ALLOC_API 
void wb_taggedFree(wb_TaggedHeap* heap, wb_isize tag);

#ifdef WB_ALLOC_CPLUSPLUS_FEATURES
template<typename T, int n = 1>
WB_ALLOC_API 
T* wb_arenaPushEx(wb_MemoryArena* arena, 
		WB_ALLOC_EXTENDED_INFO extended);

template<typename T, int n = 1>
WB_ALLOC_API 
T* wb_arenaPush(wb_MemoryArena* arena);

template<typename T>
WB_ALLOC_API 
T* wb_poolRetrieve(wb_MemoryPool* pool);

template<typename T>
WB_ALLOC_API 
void wb_poolRelease(wb_MemoryPool* pool, T* ptr);


template<typename T>
WB_ALLOC_API 
void wb_poolInit(wb_MemoryPool* pool, wb_MemoryArena* alloc, 
		wb_flags flags);

template<typename T>
WB_ALLOC_API 
wb_MemoryPool* wb_poolBootstrap(wb_MemoryInfo info,
		wb_flags flags);

template<typename T>
WB_ALLOC_API 
wb_MemoryPool* wb_poolFixedSizeBootstrap(
		void* buffer, wb_usize size,
		wb_flags flags);


template<typename T, int n = 1>
WB_ALLOC_API 
T* wb_taggedAlloc(wb_TaggedHeap* heap, wb_isize tag);
#endif


/* TODO(will): Write per-function documentation */

WB_ALLOC_BACKEND_API void* wbi__allocateVirtualSpace(wb_usize size);
WB_ALLOC_BACKEND_API void* wbi__commitMemory(void* addr, wb_usize size, 
		wb_flags flags);
WB_ALLOC_BACKEND_API void wbi__decommitMemory(void* addr, wb_usize size);
WB_ALLOC_BACKEND_API void wbi__freeAddressSpace(void* addr, wb_usize size);
WB_ALLOC_BACKEND_API wb_MemoryInfo wb_getMemoryInfo();

WB_ALLOC_API 
wb_isize wb_alignTo(wb_usize x, wb_usize align);

WB_ALLOC_API 
void wb_arenaInit(wb_MemoryArena* arena, wb_MemoryInfo info, wb_flags flags);
WB_ALLOC_API 
wb_MemoryArena* wb_arenaBootstrap(wb_MemoryInfo info, wb_flags flags);

WB_ALLOC_API 
void wb_arenaFixedSizeInit(wb_MemoryArena* arena, 
		void* buffer, wb_isize size,
		wb_flags flags);
WB_ALLOC_API 
wb_MemoryArena* arenaFixedSizeBootstrap(
		void* buffer, wb_usize size,
		wb_flags flags);


WB_ALLOC_API 
void wb_arenaPop(wb_MemoryArena* arena);

WB_ALLOC_API 
void wb_arenaStartTemp(wb_MemoryArena* arena);
WB_ALLOC_API 
void wb_arenaEndTemp(wb_MemoryArena* arena);

WB_ALLOC_API 
void wb_arenaClear(wb_MemoryArena* arena);
WB_ALLOC_API 
void wb_arenaDestroy(wb_MemoryArena* arena);


WB_ALLOC_API 
void wb_poolInit(
		wb_MemoryPool* pool,
		wb_MemoryArena* alloc, 
		wb_usize elementSize, 
		wb_flags flags);

WB_ALLOC_API 
wb_MemoryPool* wb_poolBootstrap(
		wb_MemoryInfo info,
		wb_isize elementSize, 
		wb_flags flags);

WB_ALLOC_API 
wb_MemoryPool* wb_poolFixedSizeBootstrap(
		wb_isize elementSize, 
		void* buffer, wb_usize size,
		wb_flags flags);


WB_ALLOC_API wb_isize wb_calcTaggedHeapSize(
		wb_isize arenaSize, wb_isize arenaCount, 
		wb_flags bootstrapped);

WB_ALLOC_API 
void wb_taggedInit(
		wb_TaggedHeap* heap,
		wb_MemoryArena* arena,
		wb_isize internalArenaSize, 
		wb_flags flags);

WB_ALLOC_API 
wb_TaggedHeap* wb_taggedBootstrap(wb_MemoryInfo info, 
		wb_isize arenaSize, 
		wb_flags flags);

WB_ALLOC_API 
wb_TaggedHeap* wb_taggedFixedSizeBootstrap(wb_isize arenaSize, 
		void* buffer, wb_isize bufferSize, 
		wb_flags flags);

WB_ALLOC_API 
void wbi__taggedArenaInit(wb_TaggedHeap* heap, 
		wbi__TaggedHeapArena* arena, 
		wb_isize tag);

WB_ALLOC_API 
void wbi__taggedArenaSortBySize(wbi__TaggedHeapArena** array, wb_isize count);


/* Platform-Specific Code */

#ifndef WB_ALLOC_CUSTOM_BACKEND
#ifdef WB_ALLOC_WINDOWS
#ifndef _WINDOWS_
#ifndef WINAPI
#define WINAPI __stdcall 
#endif
#ifdef __cplusplus
#define wbi__WindowsExtern extern "C"
#else
#define wbi__WindowsExtern extern
#endif
#ifndef _In_
#define _In_
#define _Out_
#define _In_opt_
#endif
typedef unsigned int DWORD;
typedef unsigned short WORD;
typedef DWORD* DWORD_PTR;
typedef void* LPVOID;
typedef int BOOL;
typedef unsigned long long* PULONGLONG;

typedef struct _SYSTEM_INFO {
  union {
    DWORD  dwOemId;
    struct {
      WORD wProcessorArchitecture;
      WORD wReserved;
    };
  };
  DWORD     dwPageSize;
  LPVOID    lpMinimumApplicationAddress;
  LPVOID    lpMaximumApplicationAddress;
  DWORD_PTR dwActiveProcessorMask;
  DWORD     dwNumberOfProcessors;
  DWORD     dwProcessorType;
  DWORD     dwAllocationGranularity;
  WORD      wProcessorLevel;
  WORD      wProcessorRevision;
} SYSTEM_INFO, *LPSYSTEM_INFO;

wbi__WindowsExtern
DWORD WINAPI GetLastError(void);

wbi__WindowsExtern
void WINAPI GetSystemInfo(
  _Out_ LPSYSTEM_INFO lpSystemInfo
);

wbi__WindowsExtern
BOOL WINAPI GetPhysicallyInstalledSystemMemory(
  _Out_ PULONGLONG TotalMemoryInKilobytes
);

wbi__WindowsExtern
LPVOID WINAPI VirtualAlloc(
  _In_opt_ LPVOID lpAddress,
  _In_     wb_usize dwSize,
  _In_     DWORD  flAllocationType,
  _In_     DWORD  flProtect
);

wbi__WindowsExtern
BOOL WINAPI VirtualFree(
  _In_ LPVOID lpAddress,
  _In_ wb_usize dwSize,
  _In_ DWORD  dwFreeType
);

#define MEM_COMMIT 0x1000
#define MEM_RESERVE 0x2000
#define PAGE_EXECUTE 0x10
#define PAGE_EXECUTE_READ 0x20
#define PAGE_EXECUTE_READWRITE 0x40
#define PAGE_EXECUTE_WRITECOPY 0x80
#define PAGE_NOACCESS 0x1
#define PAGE_READONLY 0x2
#define PAGE_READWRITE 0x4
#define PAGE_WRITECOPY 0x8
#define MEM_DECOMMIT 0x4000
#define MEM_RELEASE 0x8000
#endif

#ifdef WB_ALLOC_IMPLEMENTATION

WB_ALLOC_BACKEND_API
void* wbi__allocateVirtualSpace(wb_usize size)
{
    return VirtualAlloc(NULL, size, MEM_RESERVE, PAGE_NOACCESS);
}
 
WB_ALLOC_BACKEND_API
void* wbi__commitMemory(void* addr, wb_usize size, wb_flags flags)
{
	DWORD newFlags = 0;
	if(flags & wb_Read) {
		if(flags & wb_Write) {
			if(flags & wb_Execute) {
				newFlags = PAGE_EXECUTE_READWRITE;
			}
			newFlags = PAGE_READWRITE;
		} else if(flags & wb_Execute) {
			newFlags = PAGE_EXECUTE_READ;
		} else {
			newFlags = PAGE_READONLY;
		}
	} else if(flags & wb_Write) {
		if(flags & wb_Execute) {
			newFlags = PAGE_EXECUTE_READWRITE;
		} else {
			newFlags = PAGE_READWRITE;
		}
	} else if(flags & wb_Execute) {
		newFlags = PAGE_EXECUTE;
	} else {
		newFlags = PAGE_NOACCESS;
	}

    return VirtualAlloc(addr, size, MEM_COMMIT, newFlags);
}
 
WB_ALLOC_BACKEND_API
void wbi__decommitMemory(void* addr, wb_usize size)
{
    VirtualFree((void*)addr, size, MEM_DECOMMIT);
}
 
WB_ALLOC_BACKEND_API
void wbi__freeAddressSpace(void* addr, wb_usize size)
{
	/* This is a stupid hack, but blame clang/gcc with -Wall; not me */
	/* In any kind of optimized code, this should just get removed. */
	wb_usize clangWouldWarnYouAboutThis = size;
	clangWouldWarnYouAboutThis++;
    VirtualFree((void*)addr, 0, MEM_RELEASE);
}

WB_ALLOC_BACKEND_API
wb_MemoryInfo wb_getMemoryInfo()
{
	SYSTEM_INFO systemInfo;
	GetSystemInfo(&systemInfo);
	wb_usize pageSize = systemInfo.dwPageSize;

	wb_usize localMem = 0;
	wb_usize totalMem = 0;
	wb_usize ret = GetPhysicallyInstalledSystemMemory(&localMem);
	if(ret) {
		totalMem = localMem * 1024;
	}

	wb_MemoryInfo info; 
	/*{
		totalMem, wb_CalcMegabytes(1), pageSize,
		wb_Read | wb_Write
	};*/

	info.totalMemory = totalMem;
	info.commitSize = wb_CalcMegabytes(1);
	info.pageSize = pageSize;
	info.commitFlags = wb_Read | wb_Write;
	return info;

}
#endif
#endif

#ifdef WB_ALLOC_POSIX
#ifndef PROT_NONE
#define PROT_NONE 0
#define PROT_READ 0x1
#define PROT_WRITE 0x2
#define PROT_EXECUTE 0x4
#define MAP_SHARED 0x1
#define MAP_PRIVATE 0x2
#define MAP_FIXED 0x10
#define MAP_ANON 0x20
#define MS_SYNC 1
#define MS_ASYNC 4
#define MS_INVALIDATE 2
#endif

#ifndef _SC_PAGESIZE
#define _SC_PAGESIZE 30
#endif

#ifndef __off_t_defined
typedef wb_usize off_t;
#endif
 
#ifndef _SYS_SYSINFO_H
typedef short unsigned int wbi__u16;
typedef unsigned int wbi__u32;
typedef long int wbi__i64;
typedef unsigned long int wbi__u64;
struct sysinfo 
{
	wbi__i64 uptime;
	wbi__u64 loads[3];
	wbi__u64 totalram;
	wbi__u64 freeMemory;
	wbi__u64 sharedMemory;
	wbi__u64 bufferMemory;
	wbi__u64 totalSwap;
	wbi__u64 freeSwap;
	wbi__u16 procs;
	wbi__u16 pad;
	wbi__u64 totalHigh;
	wbi__u64 freeHigh;
	wbi__u32 memUnit;
	char _f[20-2*sizeof(long)-sizeof(int)];
};
extern int sysinfo(struct sysinfo* info);
#endif
extern void* mmap(void* addr, wb_usize len, int prot,
		int flags, int fd, off_t offset);
extern int munmap(void* addr, wb_usize len);
extern int msync(void* addr, wb_usize len, int flags);
extern long sysconf(int name);

#ifdef WB_ALLOC_IMPLEMENTATION
WB_ALLOC_BACKEND_API
void* wbi__allocateVirtualSpace(wb_usize size)
{
    void * ptr = mmap((void*)0, size, PROT_NONE, MAP_PRIVATE|MAP_ANON, -1, 0);
    msync(ptr, size, MS_SYNC|MS_INVALIDATE);
    return ptr;
}
 
WB_ALLOC_BACKEND_API
void* wbi__commitMemory(void* addr, wb_usize size, wb_flags flags)
{
    void * ptr = mmap(addr, size, flags, MAP_FIXED|MAP_SHARED|MAP_ANON, -1, 0);
    msync(addr, size, MS_SYNC|MS_INVALIDATE);
    return ptr;
}
 
WB_ALLOC_BACKEND_API
void wbi__decommitMemory(void* addr, wb_usize size)
{
    mmap(addr, size, PROT_NONE, MAP_FIXED|MAP_PRIVATE|MAP_ANON, -1, 0);
    msync(addr, size, MS_SYNC|MS_INVALIDATE);
}
 
WB_ALLOC_BACKEND_API
void wbi__freeAddressSpace(void* addr, wb_usize size)
{
    msync(addr, size, MS_SYNC);
    munmap(addr, size);
}

WB_ALLOC_BACKEND_API
wb_MemoryInfo wb_getMemoryInfo()
{
	struct sysinfo si;
	sysinfo(&si);
	wb_usize totalMem = si.totalram;
	wb_usize pageSize = sysconf(_SC_PAGESIZE);


	wb_MemoryInfo info = {
		totalMem, wb_CalcMegabytes(1), pageSize, 8,
		wb_Read | wb_Write
	};
	return info;

}
#endif
#endif
#endif

/* ===========================================================================
 * 		Main library -- Platform non-specific code
 * ===========================================================================
 */

#ifdef WB_ALLOC_IMPLEMENTATION
WB_ALLOC_API
wb_isize wb_alignTo(wb_usize x, wb_usize align)
{
	wb_usize mod = x & (align - 1);
	return mod ? x + (align - mod) : x;
}

/* Memory Arena */

WB_ALLOC_API 
void wb_arenaFixedSizeInit(wb_MemoryArena* arena, 
		void* buffer, wb_isize size, 
		wb_flags flags)
{
#ifndef WB_ALLOC_NO_ZERO_ON_INIT
	WB_ALLOC_MEMSET(arena, 0, sizeof(wb_MemoryArena));
#endif

	arena->name = "arena";

	arena->flags = flags | wb_FlagArenaFixedSize;
	arena->align = 8;

	arena->start = buffer;
	arena->head = buffer;
	arena->end = (void*)((wb_isize)arena->start + size);
	arena->tempStart = NULL;
	arena->tempHead = NULL;
}


WB_ALLOC_API 
void wb_arenaInit(wb_MemoryArena* arena, wb_MemoryInfo info, wb_flags flags)
{
#ifndef WB_ALLOC_NO_ZERO_ON_INIT
	WB_ALLOC_MEMSET(arena, 0, sizeof(wb_MemoryArena));
#endif

#ifndef WB_ALLOC_NO_FLAG_CORRECTNESS_CHECKS
	if(flags & wb_FlagArenaFixedSize) {
		WB_ALLOC_ERROR_HANDLER(
				"can't create a fixed-size arena with arenaInit\n"
				"use arenaFixedSizeInit instead.",
				arena, "arena");
		return;
	}
#endif

	arena->flags = flags;
	arena->name = "arena";
	arena->info = info;
	arena->start = wbi__allocateVirtualSpace(info.totalMemory);
	void* ret = wbi__commitMemory(arena->start,
			info.commitSize,
			info.commitFlags);
	if(!ret) {
		WB_ALLOC_ERROR_HANDLER("failed to commit inital memory", 
				arena, arena->name);
		return;
	}
	arena->head = arena->start;
	arena->end = (char*)arena->start + info.commitSize;
	arena->tempStart = NULL;
	arena->tempHead = NULL;
	arena->align = 8;
}

WB_ALLOC_API 
void* wb_arenaPushEx(wb_MemoryArena* arena, wb_isize size, 
		WB_ALLOC_EXTENDED_INFO extended)
{
	if(arena->flags & wb_FlagArenaStack) {
		size += sizeof(WB_ALLOC_STACK_PTR);
	}

	if(arena->flags & wb_FlagArenaExtended) {
		size += sizeof(WB_ALLOC_EXTENDED_INFO);
	}

	void *oldHead, *ret;
	wb_usize newHead, toExpand;
	oldHead = arena->head;
	newHead = wb_alignTo((wb_isize)arena->head + size, arena->align);

	if(newHead > (wb_usize)arena->end) {
		if(arena->flags & wb_FlagArenaFixedSize) {
			WB_ALLOC_ERROR_HANDLER(
					"ran out of memory",
					arena, arena->name);
			return NULL;
		}

		toExpand = wb_alignTo(size, arena->info.commitSize);
		ret = wbi__commitMemory(arena->end, toExpand, arena->info.commitFlags);
		if(!ret) {
			WB_ALLOC_ERROR_HANDLER("failed to commit memory in arenaPush",
					arena, arena->name);
			return NULL;
		}
		arena->end = (char*)arena->end + toExpand;
	}

	if(arena->flags & wb_FlagArenaStack) {
		WB_ALLOC_STACK_PTR* head = (WB_ALLOC_STACK_PTR*)newHead;
		head--;
		*head = (WB_ALLOC_STACK_PTR)oldHead;
	}
	
	if(arena->flags & wb_FlagArenaExtended) {
		WB_ALLOC_EXTENDED_INFO* head = (WB_ALLOC_EXTENDED_INFO*)oldHead;
		*head = extended;
		head++;
		oldHead = (void*)head;
	}

	arena->head = (void*)newHead;

	return oldHead;
}

WB_ALLOC_API
void* wb_arenaPush(wb_MemoryArena* arena, wb_isize size)
{
	return wb_arenaPushEx(arena, size, 0);
}

WB_ALLOC_API 
void wb_arenaPop(wb_MemoryArena* arena)
{
#ifndef WB_ALLOC_NO_FLAG_CORRECTNESS_CHECKS
	if(!(arena->flags & wb_FlagArenaStack)) {
		WB_ALLOC_ERROR_HANDLER(
				"can't use arenaPop with non-stack arenas",
				arena, arena->name);
		return;
	}
#endif

	
	wb_usize prevHeadPtr = (wb_isize)arena->head - sizeof(WB_ALLOC_STACK_PTR);
	void* newHead = (void*)(*(WB_ALLOC_STACK_PTR*)prevHeadPtr);
	if((wb_isize)newHead <= (wb_isize)arena->start) {
		arena->head = arena->start;
		return;
	}

	if(!(arena->flags & wb_FlagArenaNoZeroMemory)) {
		wb_usize size = (wb_isize)arena->head - (wb_isize)newHead;
		if(size > 0) {
			WB_ALLOC_MEMSET(newHead, 0, size);
		}
	}

	arena->head = newHead;
}

WB_ALLOC_API 
wb_MemoryArena* wb_arenaBootstrap(wb_MemoryInfo info, wb_flags flags)
{
#ifndef WB_ALLOC_NO_FLAG_CORRECTNESS_CHECKS
	if(flags & wb_FlagArenaFixedSize) {
		WB_ALLOC_ERROR_HANDLER(
				"can't create a fixed-size arena with arenaBootstrap\n"
				"use arenaFixedSizeBootstrap instead.",
				NULL, "arena");
		return NULL;
	}
#endif

	wb_MemoryArena arena;
	wb_arenaInit(&arena, info, flags);
	wb_MemoryArena* strapped = (wb_MemoryArena*)
		wb_arenaPush(&arena, sizeof(wb_MemoryArena) + 16);
	*strapped = arena;
	if(flags & wb_FlagArenaStack) {
		wb_arenaPushEx(strapped, 0, 0);
		*((WB_ALLOC_STACK_PTR*)(strapped->head) - 1) = 
			(WB_ALLOC_STACK_PTR)strapped->head;
	}

	
	return strapped;
}

WB_ALLOC_API 
wb_MemoryArena* arenaFixedSizeBootstrap(void* buffer, wb_usize size,
		wb_flags flags)
{
	wb_MemoryArena arena;
	wb_arenaFixedSizeInit(&arena, buffer, size, flags | wb_FlagArenaFixedSize);
	wb_MemoryArena* strapped = (wb_MemoryArena*)
		wb_arenaPush(&arena, sizeof(wb_MemoryArena) + 16);
	*strapped = arena;
	if(flags & wb_FlagArenaStack) {
		wb_arenaPushEx(strapped, 0, 0);
		*((WB_ALLOC_STACK_PTR*)(strapped->head) - 1) = 
			(WB_ALLOC_STACK_PTR)strapped->head;
	}
	return strapped;
}

WB_ALLOC_API 
void wb_arenaStartTemp(wb_MemoryArena* arena)
{
	if(arena->tempStart) return;
	arena->tempStart = (void*)wb_alignTo((wb_isize)arena->head, 
			arena->info.pageSize);
	arena->tempHead = arena->head;
	arena->head = arena->tempStart;
}

WB_ALLOC_API 
void wb_arenaEndTemp(wb_MemoryArena* arena)
{
	if(!arena->tempStart) return;
	arena->head = (void*)wb_alignTo((wb_isize)arena->head, arena->info.pageSize);
	wb_isize size = (wb_isize)arena->head - (wb_isize)arena->tempStart;

	/* NOTE(will): if you have an arena with flags 
	 * 	ArenaNoRecommit | ArenaNoZeroMemory
	 * This just moves the pointer, which might be something you want to do.
	 */
	if(!(arena->flags & wb_FlagArenaNoRecommit)) {
		wbi__decommitMemory(arena->tempStart, size);
		wbi__commitMemory(arena->tempStart, size, arena->info.commitFlags);
	} else if(!(arena->flags & wb_FlagArenaNoZeroMemory)) {
		WB_ALLOC_MEMSET(arena->tempStart, 0,
				(wb_isize)arena->head - (wb_isize)arena->tempStart);
	}

	arena->head = arena->tempHead;
	arena->tempHead = NULL;
	arena->tempStart = NULL;
}

WB_ALLOC_API 
void wb_arenaClear(wb_MemoryArena* arena)
{
	wb_MemoryArena local = *arena;
	wb_isize size = (wb_isize)arena->end - (wb_isize)arena->start;
	wbi__decommitMemory(local.start, size);
	wbi__commitMemory(local.start, size, local.info.commitFlags);
	*arena = local;
}

WB_ALLOC_API
void wb_arenaDestroy(wb_MemoryArena* arena)
{
	wbi__freeAddressSpace(arena->start, 
			(wb_isize)arena->end - (wb_isize)arena->start);
}

/* Memory Pool */
WB_ALLOC_API
void wb_poolInit(wb_MemoryPool* pool, wb_MemoryArena* alloc, 
		wb_usize elementSize,
		wb_flags flags)
{
#ifndef WB_ALLOC_NO_ZERO_ON_INIT
	WB_ALLOC_MEMSET(pool, 0, sizeof(wb_MemoryPool));
#endif

	pool->alloc = alloc;
	pool->flags = flags;
	pool->name = "pool";
	pool->elementSize = elementSize < sizeof(void*) ?
		sizeof(void*) :
		elementSize;
	pool->count = 0;
	pool->lastFilled = -1;
	pool->capacity = (wb_isize)
		((char*)alloc->end - (char*)alloc->head) / elementSize;

	pool->slots = alloc->head;
	pool->freeList = NULL;
}

WB_ALLOC_API
wb_MemoryPool* wb_poolBootstrap(wb_MemoryInfo info, 
		wb_isize elementSize,
		wb_flags flags)
{
	wb_MemoryArena* alloc;
	wb_MemoryPool* pool;

	wb_flags arenaFlags = 0;
	if(flags & wb_FlagPoolFixedSize) {
		arenaFlags = wb_FlagArenaFixedSize;
	}
	
	alloc = wb_arenaBootstrap(info, arenaFlags);
	pool = (wb_MemoryPool*)wb_arenaPush(alloc, sizeof(wb_MemoryPool));

	wb_poolInit(pool, alloc, elementSize, flags);
	return pool;
}

WB_ALLOC_API
wb_MemoryPool* wb_poolFixedSizeBootstrap(
		wb_isize elementSize, 
		void* buffer, wb_usize size, 
		wb_flags flags)
{
	wb_MemoryArena* alloc;
	wb_MemoryPool* pool;
	flags |= wb_FlagPoolFixedSize;
	
	alloc = arenaFixedSizeBootstrap(buffer, size, wb_FlagArenaFixedSize);
	pool = (wb_MemoryPool*)wb_arenaPush(alloc, sizeof(wb_MemoryPool));

	wb_poolInit(pool, alloc, elementSize, flags);
	return pool;
}

/* Utility functions not used
wb_isize poolIndex(wb_MemoryPool* pool, void* ptr)
{
	wb_isize diff = (wb_isize)ptr - (wb_isize)pool->slots;
	return diff / pool->elementSize;
}
void* poolFromIndex(wb_MemoryPool* pool, wb_isize index)
{
	return (char*)pool->slots + index * pool->elementSize;
}
*/

WB_ALLOC_API
void* wb_poolRetrieve(wb_MemoryPool* pool)
{
	void *ptr, *ret;
	ptr = NULL;
	if((!(pool->flags & wb_FlagPoolCompacting)) && pool->freeList) {
		ptr = pool->freeList;
		pool->freeList = (void**)*pool->freeList;
		pool->count++;

		if(!(pool->flags & wb_FlagPoolNoZeroMemory)) {
			WB_ALLOC_MEMSET(ptr, 0, pool->elementSize);
		}

		return ptr;
	} 

	if(pool->lastFilled >= pool->capacity - 1) {
		if(pool->flags & wb_FlagPoolFixedSize) {
			WB_ALLOC_ERROR_HANDLER("pool ran out of memory",
					pool, pool->name);
			return NULL;
		}

		ret = wb_arenaPush(pool->alloc, pool->alloc->info.commitSize);
		if(!ret) {
			WB_ALLOC_ERROR_HANDLER("arenaPush failed in poolRetrieve", 
					pool, pool->name);
			return NULL;
		}
		pool->capacity = (wb_isize)
			((char*)pool->alloc->end - (char*)pool->slots) / pool->elementSize;
	}

	ptr = (char*)pool->slots + ++pool->lastFilled * pool->elementSize;
	pool->count++;
	if(!(pool->flags & wb_FlagPoolNoZeroMemory)) {
		WB_ALLOC_MEMSET(ptr, 0, pool->elementSize);
	}
	return ptr;
}

WB_ALLOC_API
void wb_poolRelease(wb_MemoryPool* pool, void* ptr)
{
	pool->count--;

	if(pool->freeList && !(pool->flags & wb_FlagPoolNoDoubleFreeCheck)) {
		void** localList = pool->freeList;
		do {
			if(ptr == localList) {
				WB_ALLOC_ERROR_HANDLER("caught attempting to free previously "
						"freed memory in poolRelease", 
						pool, pool->name);
				return;
			}
		} while((localList = (void**)*localList));
	}

	if(pool->flags & wb_FlagPoolCompacting) {
		WB_ALLOC_MEMCPY(ptr, 
				(char*)pool->slots + pool->count * pool->elementSize,
				pool->elementSize);
		return;
	}

	*(void**)ptr = pool->freeList;
	pool->freeList = (void**)ptr;
}

/*
 * TODO(will): Maybe, someday, have a tagged heap that uses real memoryArenas
 * 	behind the scenes, so that you get to benefit from stack and extended 
 * 	mode for ~free; maybe as a preprocessor flag?
 */ 
WB_ALLOC_API
wb_isize wb_calcTaggedHeapSize(wb_isize arenaSize, wb_isize arenaCount,
		wb_flags bootstrapped)
{
	return arenaCount * (arenaSize + sizeof(wbi__TaggedHeapArena))
		+ sizeof(wb_TaggedHeap) * bootstrapped;
}

WB_ALLOC_API
void wb_taggedInit(wb_TaggedHeap* heap, wb_MemoryArena* arena, 
		wb_isize internalArenaSize, wb_flags flags)
{
#ifndef WB_ALLOC_NO_ZERO_ON_INIT
	WB_ALLOC_MEMSET(heap, 0, sizeof(wb_TaggedHeap));
#endif

	heap->name = "taggedHeap";
	heap->flags = flags;
	heap->align = 8;
	heap->arenaSize = internalArenaSize;
	wb_poolInit(&heap->pool, arena, 
			internalArenaSize + sizeof(wbi__TaggedHeapArena), 
			wb_FlagPoolNormal | wb_FlagPoolNoDoubleFreeCheck | 
			((flags & wb_FlagTaggedHeapNoZeroMemory) ? 
			wb_FlagPoolNoZeroMemory : 
			0));
}

WB_ALLOC_API
wb_TaggedHeap* wb_taggedBootstrap(wb_MemoryInfo info, 
		wb_isize arenaSize,
		wb_flags flags)
{
	wb_TaggedHeap* strapped;
	wb_TaggedHeap heap;
	info.commitSize = wb_calcTaggedHeapSize(arenaSize, 8, 1);
	wb_MemoryArena* arena = wb_arenaBootstrap(info, 
			((flags & wb_FlagTaggedHeapNoZeroMemory) ? 
			wb_FlagArenaNoZeroMemory :
			wb_FlagArenaNormal));
	strapped = (wb_TaggedHeap*)wb_arenaPush(arena, sizeof(wb_TaggedHeap) + 16);
	wb_taggedInit(&heap, arena, arenaSize, flags);
	*strapped = heap;
	return strapped;
}

WB_ALLOC_API
wb_TaggedHeap* wb_taggedFixedSizeBootstrap(
		wb_isize arenaSize, 
		void* buffer, wb_isize bufferSize, 
		wb_flags flags)
{
	wb_MemoryArena* alloc;
	wb_TaggedHeap* heap;
	flags |= wb_FlagTaggedHeapFixedSize;
	
	alloc = arenaFixedSizeBootstrap(buffer, bufferSize, 
			wb_FlagArenaFixedSize |
			((flags & wb_FlagTaggedHeapNoZeroMemory) ? 
			wb_FlagArenaNoZeroMemory : 0));
	heap = (wb_TaggedHeap*)wb_arenaPush(alloc, sizeof(wb_TaggedHeap));

	wb_taggedInit(heap, alloc, arenaSize, flags);
	return heap;
	
}

WB_ALLOC_API
void wbi__taggedArenaInit(wb_TaggedHeap* heap, 
		wbi__TaggedHeapArena* arena,
		wb_isize tag)
{
#ifndef WB_ALLOC_NO_ZERO_ON_INIT
	WB_ALLOC_MEMSET(arena, 0, sizeof(wbi__TaggedHeapArena));
#endif

	arena->tag = tag;
	arena->head = &arena->buffer;
	arena->end = (void*)((char*)arena->head + heap->arenaSize);
}

WB_ALLOC_API
void wbi__taggedArenaSortBySize(wbi__TaggedHeapArena** array, wb_isize count)
{
#define wbi__arenaSize(arena) ((wb_isize)arena->head - (wb_isize)arena->head)
	for(wb_isize i = 1; i < count; ++i) {
		wb_isize j = i - 1;

		wb_isize minSize = wbi__arenaSize(array[i]);
		if(wbi__arenaSize(array[j]) > minSize) {
			wbi__TaggedHeapArena* temp = array[i];
			while((j >= 0) && (wbi__arenaSize(array[j]) > minSize)) {
				array[j + 1] = array[j];
				j--;
			}
			array[j + 1] = temp;
		}
	}
#undef wbi__arenaSize
}


WB_ALLOC_API
void* wb_taggedAlloc(wb_TaggedHeap* heap, wb_isize tag, wb_usize size)
{
	wbi__TaggedHeapArena *arena, *newArena;
	void* oldHead;
	wbi__TaggedHeapArena* canFit[wbi__TaggedHeapSearchSize];
	wb_isize canFitCount = 0;

	if(size > heap->arenaSize) {
		WB_ALLOC_ERROR_HANDLER("cannot allocate an object larger than the "
				"size of a tagged heap arena.",
				heap, heap->name);
		return NULL;
	}

	if(!heap->arenas[tag]) {
		heap->arenas[tag] = (wbi__TaggedHeapArena*)wb_poolRetrieve(&heap->pool);
		if(!heap->arenas[tag]) {
			WB_ALLOC_ERROR_HANDLER("tagged heap arena retrieve returned null "
					"when creating a new tag",
				heap, heap->name);
			return NULL;
		}
		wbi__taggedArenaInit(heap, heap->arenas[tag], tag);
	}

	arena = heap->arenas[tag];

	if((char*)arena->head + size > (char*)arena->end) {
		/* TODO(will) add a find-better-fit option rather than
		 * allocating new arenas whenever */

		if(heap->flags & wb_FlagTaggedHeapSearchForBestFit) {
			while((arena = arena->next)) {
				if((char*)arena->head + size < (char*)arena->end) {
					canFit[canFitCount++] = arena;
					if(canFitCount > (wbi__TaggedHeapSearchSize - 1)) {
						break;
					}
				}
			}

			if(canFitCount > 0) {
				wbi__taggedArenaSortBySize(canFit, canFitCount);
				arena = canFit[0];
			}
		}

		if(canFitCount == 0) {
			newArena = (wbi__TaggedHeapArena*)wb_poolRetrieve(&heap->pool);
			if(!newArena) {
				WB_ALLOC_ERROR_HANDLER(
						"tagged heap arena retrieve returned null",
						heap, heap->name);
				return NULL;
			}
			wbi__taggedArenaInit(heap, newArena, tag);
			newArena->next = arena;
			arena = newArena;
		}
	}

	oldHead = arena->head;
	arena->head = (void*)wb_alignTo((wb_isize)arena->head + size, heap->align);
	return oldHead;
}

WB_ALLOC_API
void wb_taggedFree(wb_TaggedHeap* heap, wb_isize tag)
{
	wbi__TaggedHeapArena *head;
	if((head = heap->arenas[tag])) do {
		wb_poolRelease(&heap->pool, head);
	} while((head = head->next));
	heap->arenas[tag] = NULL;
}

#ifdef WB_ALLOC_CPLUSPLUS_FEATURES
template<typename T, int n>
WB_ALLOC_API 
T* wb_arenaPushEx(wb_MemoryArena* arena, 
		WB_ALLOC_EXTENDED_INFO extended)
{
	return reinterpret_cast<T*>wb_arenaPushEx(arena, sizeof(T) * n, extended);
}

template<typename T, int n>
WB_ALLOC_API 
T* wb_arenaPush(wb_MemoryArena* arena)
{
	return reinterpret_cast<T*>(wb_arenaPush(arena, sizeof(T) * n));
}

template<typename T>
WB_ALLOC_API 
T* wb_poolRetrieve(wb_MemoryPool* pool)
{
	return reinterpret_cast<T*>(wb_poolRetrieve(pool));
}

template<typename T>
WB_ALLOC_API 
void wb_poolRelease(wb_MemoryPool* pool, T* ptr)
{
	wb_poolRelease(pool, reinterpret_cast<void*>(ptr));
}

template<typename T>
WB_ALLOC_API 
void wb_poolInit(
		wb_MemoryPool* pool,
		wb_MemoryArena* alloc, 
		wb_flags flags)
{
	wb_poolInit(pool, alloc, sizeof(T), flags);
}

template<typename T>
WB_ALLOC_API 
wb_MemoryPool* wb_poolBootstrap(wb_MemoryInfo info,
		wb_flags flags)
{
	return wb_poolBootstrap(info, sizeof(T), flags);
}

template<typename T>
WB_ALLOC_API 
wb_MemoryPool* wb_poolFixedSizeBootstrap(
		void* buffer, wb_usize size,
		wb_flags flags)
{
	return wb_poolFixedSizeBootstrap(sizeof(T), buffer, size, flags);
}

template<typename T, int n>
WB_ALLOC_API 
T* wb_taggedAlloc(wb_TaggedHeap* heap, wb_isize tag)
{
	return reinterpret_cast<T*>(wb_taggedAlloc(heap, tag, sizeof(T) * n));
}
#endif
#endif

#ifndef WB_ALLOC_NO_DISABLE_STUPID_MSVC_WARNINGS
#pragma warning(pop)
#endif

/*
Anyone is free to copy, modify, publish, use, compile, sell, or
distribute this software, either in source code form or as a compiled
binary, for any purpose, commercial or non-commercial, and by any
means.

In jurisdictions that recognize copyright laws, the author or authors
of this software dedicate any and all copyright interest in the
software to the public domain. We make this dedication for the benefit
of the public at large and to the detriment of our heirs and
successors. We intend this dedication to be an overt act of
relinquishment in perpetuity of all present and future rights to this
software under copyright law.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR
OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
OTHER DEALINGS IN THE SOFTWARE.

For more information, please refer to <http://unlicense.org/>
*/
