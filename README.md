# wb_alloc.h 

## Easy-to-use custom allocators in a public-domain C/C++ library.

- Allocate as much memory as you want\* with these simple custom
  allocators. (The amount of memory is limited to the physical memory in
  your machine.)
- Easy to use without the CRT or malloc.
- Doesn't include library headers if you don't need it to.
- Doesn't include operating system headers at all.
- Compiles as C89 and C++.
- Templatized versions of allocation functions for C++ for easier memory
  safety (use `WB_ALLOC_CPLUSPLUS_FEATURES` to enable these).

## Current Status

As of 11/4/17 or version 0.0.1, the library compiles with no warnings
under clang and MSVC, however, it is largely untested with regards to
runtime errors or performance. It should be considered **alpha software**.

There are also some convenience features missing, such as being able to
directly free/clear a memory pool and tagged heap.

## Demo

	#include <stdio.h> 

	#define WB_ALLOC_IMPLEMENTATION
	#include "wb_alloc.h"

	int main()
	{
		wb_MemoryInfo info;
		wb_MemoryArena* arena;
		int i;

		/* MemoryInfo contains the information about the system's 
		   total memory, page size, and sets some defaults about the 
		   amount of memory committed at once */
		info = wb_getMemoryInfo();

		/* Bootstrapping the arena means that it allocates the memory
		   for itself, then stores its own struct at the beginning */
		arena = wb_arenaBootstrap(info, wb_FlagArenaNormal);

		/* Make some room for numbers! */
		int* numbers1 = wb_arenaPush(arena, sizeof(int) * 100);
		int* numbers2 = wb_arenaPush(arena, sizeof(int) * 200);
		int* numbers3 = wb_arenaPush(arena, sizeof(int) * 400);
		int* numbers4 = wb_arenaPush(arena, sizeof(int) * 800);

		for(i = 0; i < 1500; ++i) {
			numbers1[i] = 1500 - i;
		}

		for(i = 0; i < 1500; ++i) {
			printf("%d ", numbers[i]);
		}
		printf("\n\n");

		/* Clearing the arena decommits all its committed pages then
		   recommits them, which is guaranteed on modern operating
		   systems to zero them */
		wb_arenaClear(arena);
		wb_arenaPush(arena, sizeof(int) * 1500);

		for(i = 0; i < 1500; ++i) {
			printf("%d\n", numbers[i]);
		}

		return 0;
	}

## Allocators Included

#### Memory Arena

	void* wb_arenaPush(
			wb_MemoryArena* arena, 
			wb_isize size);
	
	wb_MemoryArena* wb_arenaBootstrap(
			wb_MemoryInfo info, 
			wb_flags flags);

Inspired by the Handmade Hero memory management structure, my memory arena
is a variation on a "linear allocator" or a "bump-pointer allocator". It
starts by allocating a large region of memory, returning a pointer to it,
then incrementing the pointer by the amount of space requested. 

This kind of allocation is extremely fast, but does not allow for easy
deallocation of individual objects; however, in practice, this tends not
to be a problem. The entire arena is easily freed at once, which is often
fine for things like state transitions.

The memory arena has a couple variants that can be enabled via flag

1. `wb_FlagArenaStack` turns the arena from a linear allocator into
   a stack allocator. With this, you can always pop (or rather, free) the
   last allocation on the stack, which will let you pop the previous one,
   and so on, until the stack is empty.

2. `wb_FlagArenaExtended` stores extra information at the beginning of
   each allocation. While this is by default an 8-byte integer, it can be
   changed by defining `WB_ALLOC_EXTENDED_INFO` to the type of your
   choice. This is designed to aid with serialization. 

These flags may be used together.

#### Memory Pool

	void* wb_poolRetrieve(wb_MemoryPool* pool);
	void wb_poolRelease(wb_MemoryPool* pool, void* ptr);

	wb_MemoryPool* wb_poolBootstrap(
			wb_MemoryInfo info,
			wb_flags flags);

The memory pool is a simple fixed-size free-list allocator. It allows you
to freely allocate and free fixed-size objects with no external
fragmentation. This kind of allocation is good when you have many of the
same object that need to be created and destroyed frequently, such as
entities in a game world.

Using a memory arena under the hood, the memory pool can allocate until it
runs out of virtual memory.

#### Tagged Heap

	void* wb_taggedAlloc(
			wb_TaggedHeap* heap, 
			wb_isize tag,
			wb_usize size);

	void wb_taggedFree(wb_TaggedHeap* heap, wb_isize tag);

	wb_TaggedHeap* wb_taggedBoostrap(
			wb_MemoryInfo info, 
			wb_isize arenaSize, 
			wb_flags flags);

This one is inspired by the Naughty Dog GDC talk about using fibers to
multithread their engine. They mention this as their solution to the
overuse of wasteful memory arenas. The tagged heap behaves like a memory
pool of memory arenas; that is, when you allocate, you specify a tag, and
then you can free all the memory allocated under a single tag at once. It
does this efficiently by allocating fixed-size arenas under the hood and
allocating out of those as needed per-tag. 

The tagged heap is the most flexible allocator in the library, allowing
you to allocate almost as freely as with malloc and free if you find your
deallocations apply to many related objects at once. 

## The Magic

To put it bluntly: this library abuses virtual memory. 

Well, maybe not that strongly; however, this library makes contradictory
promises:

1. Allocators that are easy to use, ie, don't require you to do extra work
   when you need more memory.

2. The allocators are also better than what the standard libraries provide
   you with along any number of axes, such as performance, fragmentation,
   and understandability.

(I realize these aren't strictly contradictory, but they sound like they
should be)

To accomplish both of these I ~~sold my soul~~ decided to rely on the
virtual memory capabilities of the operating system. On Windows,
especially, it is easy to use VirtualAlloc and VirtualFree to create
a large contiguous memory space. This space has almost no real memory cost
until you start commiting pages out of it. By using this capability (which
is possible to do on posix systems with some fanangling), a memory arena
will happily reserve as much memory as you want it to. I figure a sane
default is an amount equal to the amount of physical memory in the
machine. Though I realize you could also go far beyond that (the commit
limit on windows is that + 3x the size of the page file I believe), on its
own, I fear this might not be the safest way to do business. However, the
benefits are real:

1. You get very large contiguous ranges of memory to work with. 

2. While the amount of memory you have isn't infinite, it might as well be
   for most projects

3. If you were ever to run out of memory, you'd be in trouble anyway.

With that said, we move on to...

## Implementation Details, Caveats, and Flags

These are mostly things I've thought of relating to the implementation,
which might catch you off-guard if you aren't familiar with how allocators
like these tend to work behind the scenes.

A general note: to match the behavior of VirtualAlloc and similar, all
memory returned by these allocators is zeroed by default. However, this is
not a free operation, and as such, there exists a family of flags
`wb_Flag<Allocator>NoZeroMemory` that disables the use of memset to zero
memory. However, memset is still used to zero the object's memory in the
initialization functions. This can be disabled by specifying
`WB_ALLOC_NO_ZERO_ON_INIT`.

#### Memory Arena

The memory arena will happily allocate memory until it runs out of virtual
space. When it about to run out of committed memory, it commits more,
which means you might end up calling an OS function on any allocation in 
the library. 

To alleviate this worry, I have added a `wb_Flag<Allocator>FixedSize` flag
to each allocator, which allows you to initialize them with a buffer of
memory that they aren't allowed to overrun. While this will not call
operating system functions at runtime, you have to pay for the memory up
front instead.

Memory arenas also have the capability to define a temporary head for
a simple, one-off stack-like behavior. When the temporary state is ended,
the arena, in addition to moving the head pointer to its original place,
decommits and recommits the temporary pages by default. This may be
disabled by using the `wb_FlagArenaNoRecommit` flag. However, to match the
behavior of memory from VirtualAlloc, it will instead memset those pages
to zero instead, which may also be disabled.

#### Memory Pool

Internally, the memory pool uses a free list of freed objects. To prevent
double free errors, it walks the list every time you attempt to free an
object. This can be disabled via the `wb_FlagPoolNoDoubleFreeCheck` flag.
I feel that this may be safely disabled if you know what you're doing when
you turn on release mode. 

Another possible gotcha of the memory pool is that, while it will not
encounter external fragmentation, which would possibly decrease the amount
of available memory over time, it can run into internal fragmentation,
which may lead to poor cache behavior if you iterate over its contained
objects in order. If, rather than holding onto individual pointers, you
plan on accessing the pool's contents as an array, you can specify
`wb_FlagPoolCompacting`, which will move the last element into the removed
element's slot when freeing. This keeps the array of elements contiguous,
but will invalidate pointers from the previous free. 

#### Tagged Heap

I mentioned earlier that the tagged heap behaves like a pool of arenas,
and the caveats that apply to both apply to it, to an extent. It too sits
upon a single, expanding memory arena to back itself and its memory pool.
Its internal arenas are fixed size, which means that you cannot allocate
an object larger than an arena. I'm not sure how the actual Naughty Dog
implementation works, but they mentioned their internal buffers were
2 megabytes each, which is probably enough for one-off allocations in
things like games. 

Another limitation of the tagged heap is that, for simplicity, it simply
stores an array of pointers to its arenas, and uses its numerical tags to
index into that. By default, only space for 64 arenas is created, but this
may be modified by changing `WB_ALLOC_TAGGED_HEAP_MAX_TAG_COUNT` to you
preferred number. It is intended that you simply create your tags in an
enum starting from zero. 

When allocating in a tagged heap, it is possible that the current arena
for the specific tag would run out of room. By default, the tagged heap
will retrieve another arena from its pool and use that. This is wasteful
if you are allocating objects large enough that only one fits in an arena
in addition to a bunch of smaller objects. To mitigate this, you can set
the flag `wb_FlagTaggedHeapSearchForBestFit`, which changes the behavior
to search for the best of the first 8 (by default) arenas to put the
object in.

## C++ Support

C++ adds a significant amount of friction when working with malloc and
similar because it removes automatic `void*` coercion to other pointer
types. The internals use casts to get around this, but this is especially
unpleasant in the calling code. To alleviate this, I have added
templatized overloads of every function that takes a size or returns
a pointer, a few of which are listed listed here:

	template<typename T, int n = 1>
	T* wb_arenaPush(wb_MemoryArena* arena);

	template<typename T>
	T* wb_poolRetrieve(wb_MemoryPool* pool);

	template<typename T>
	wb_MemoryPool* wb_poolBootstrap(wb_MemoryInfo info,
			wb_flags flags);

	template<typename T, int n = 1>
	T* wb_taggedAlloc(wb_TaggedHeap* heap, wb_isize tag);

	void example(wb_MemoryArena* arena, wb_MemoryInfo info) 
	{
		auto numbers = wb_arenaPush<int, 1000>(arena);
		auto thing10 = wb_arenaPush<Thing>(arena);
		//   numbers = (int*)wb_arenaPush(arena, sizeof(int) * 1000);
		//   thing10 = (Thing*)wb_arenaPush(arena, sizeof(Thing));

		auto thingPool = wb_poolBootstrap<Thing>(info);

		/* MemoryPools don't actually remember what type they were 
		   initialized with, so you have to specify on allocation too */
		auto thing2 = wb_poolRetrieve<Thing>(thingPool);
		auto thing3 = wb_poolRetrieve<Thing>(thingPool);
		auto thing4 = wb_poolRetrieve<Thing>(thingPool);
		//   thing5 = (Thing*)wb_poolRetrieve(thingPool);
	}




