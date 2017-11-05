/* This is provided as a short demo on how each allocator in 
 * wb_alloc works and how to do a simple initialization for them
 */

/* This is free and unencumbered software released into the public domain. */
#include <stdio.h>

#define WB_ALLOC_IMPLEMENTATION
#include "wb_alloc.h"

#ifdef _MSC_VER
/* Disable unused variable warnings on MSVC */
#pragma warning(disable:189)
#endif
int main()
{
	int i;
	wb_MemoryInfo info;
	wb_MemoryArena* arena;
	wb_MemoryPool* pool;
	wb_TaggedHeap* heap;
	int *numbers1, *numbers2, *numbers3, *numbers4;
	wb_usize* soManyNumbers[100];
	wb_usize* memoryView;
	wb_usize* aBlock, *bBlock, *cBlock;

	printf("wb_alloc: C test\n");
	/* MemoryInfo contains the information about the system's 
	   total memory, page size, and sets some defaults about the 
	   amount of memory committed at once */
	info = wb_getMemoryInfo();
	printf("  Physical Memory: %zukb\n"
		   "  Page Size......: %zukb\n"
		   "  Commit Size____: %zukb\n\n", 
		   info.totalMemory / wb_CalcKilobytes(1), 
		   info.pageSize / wb_CalcKilobytes(1), 
		   info.commitSize / wb_CalcKilobytes(1));

	printf("Memory Arena test\n");
	/* Bootstrapping the arena means that it allocates the memory
	   for itself, then stores its own struct at the beginning */
	arena = wb_arenaBootstrap(info, wb_FlagArenaNormal);

	/* Make some room for numbers! */
	numbers1 = wb_arenaPush(arena, sizeof(int) * 10);
	numbers2 = wb_arenaPush(arena, sizeof(int) * 20);
	numbers3 = wb_arenaPush(arena, sizeof(int) * 40);
	numbers4 = wb_arenaPush(arena, sizeof(int) * 80);

	/* Warnings are dumb */
	numbers2[0] = 0;
	numbers3[0] = 0;
	numbers4[0] = 0;

	for(i = 0; i < 150; ++i) {
		numbers1[i] = 150 - i;
	}

	for(i = 0; i < 150; ++i) {
		printf("%d ", numbers1[i]);
	}
	printf("\n\n");

	/* Clearing the arena decommits all its committed pages then
	   recommits them, which is guaranteed on modern operating
	   systems to zero them */
	wb_arenaClear(arena);
	wb_arenaPush(arena, sizeof(int) * 150);

	for(i = 0; i < 150; ++i) {
		printf("%d ", numbers1[i]);
	}
	printf("\n\n");

	wb_arenaDestroy(arena);

	printf("Memory Pool test\n");
	printf("(can you see the free list?)\n");
	/* Internally, the pool creates a memory arena and does the same
	 * trick the arena does. */
	pool = wb_poolBootstrap(info, 8, wb_FlagPoolNormal);

	/* Get pointers to numbers out of the pool, give them some funky values */
	/* wb_usize* soManyNumbers[100]; */
	for(i = 0; i < 100; ++i) {
		soManyNumbers[i] = wb_poolRetrieve(pool);
		*soManyNumbers[i] = 4096 - (i + 1) * 4;
	}

	/* Return half of them to the pool */
	for(i = 0; i < 50; ++i) {
		wb_poolRelease(pool, soManyNumbers[i * 2]);
	}

	/* Print them all out; can you see where the pool has written its
	 * free list pointers? */
	for(i = 0; i < 100; ++i) {
		printf("%zx ", *soManyNumbers[i]);
	}
	printf("\n\n");

	/* Notice that they're retrieved in reverse order */
	for(i = 0; i < 50; ++i) {
		int* returned = wb_poolRetrieve(pool);
		*returned = i;
	}

	for(i = 0; i < 100; ++i) {
		printf("%zx ", *soManyNumbers[i]);
	}
	printf("\n\n");

	/* Because everything is stored on the internal memory arena, 
	 * destroying that cleans up the entire pool */
	wb_arenaDestroy(pool->alloc);

	printf("Tagged Heap test\n");

#define Tag_A 0
#define Tag_B 1
#define Tag_C 2

	/* This behaves very similarly to the other bootstrap functions */
	heap = wb_taggedBootstrap(
			info,
			sizeof(wb_usize) * 65, 
			/* normally the arena size would be 
			   something like wb_CalcMegabytes(2) */
			wb_FlagTaggedHeapNormal);

	/* This'll allow us to neatly print out the memory layout */
	memoryView = heap->pool.slots;


	/* Allocate some tagged blocks... */
	aBlock = wb_taggedAlloc(heap, Tag_A, sizeof(wb_usize) * 64);
	bBlock = wb_taggedAlloc(heap, Tag_B, sizeof(wb_usize) * 64);
	cBlock = wb_taggedAlloc(heap, Tag_C, sizeof(wb_usize) * 64);

	/* ...and put recognizable patterns in them */
	for(i = 0; i < 64; ++i) {
		aBlock[i] = i;
		bBlock[i] = 64 - i;
		cBlock[i] = 64 + i;
	}

	/* You get to see the memory layout of the TaggedHeapArena structs too
	 * struct TaggedHeapArnea {
	 *     wb_isize tag;
	 *     TaggedHeapArena* next;
	 *     void *head, *end;
	 *     char buffer;
	 * }
	 * Something to note: buffer gets padded out to 8 bytes, so this struct
	 * probably isn't safe to write to disk if you're building on multiple 
	 * compilers or achitectures, but hey, what's a few bytes to a good
	 * processor-friend?
	 * */
	for(i = 0; i < 64*5; ++i) {
		printf("%zu ", memoryView[i]);
	}
	printf("\n\n");

	wb_taggedFree(heap, Tag_B);
	
	/* This ensures B is cleared */
	bBlock = wb_taggedAlloc(heap, Tag_B, sizeof(wb_usize) * 64);

	/* And now we can see that its contents have been zeroed */
	for(i = 0; i < 64*5; ++i) {
		printf("%zu ", memoryView[i]);
	}
	printf("\n\n");

	/* The heap stores everything on its pool's internal arena, so, like 
	 * before, destroying that wipes the whole thing. */
	wb_arenaDestroy(heap->pool.alloc);

	return 0;
}

