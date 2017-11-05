/* This is provided as a short demo of how the C++ features look when
 * used in the same tests as the C version. Comments have been removed,
 * read the C version first.
 */

/* This is free and unencumbered software released into the public domain. */

#include <stdio.h>

#define WB_ALLOC_IMPLEMENTATION
#define WB_ALLOC_CPLUSPLUS_FEATURES
#include "wb_alloc.h"

#ifdef _MSC_VER
#pragma warning(disable:189)
#endif
int main()
{
	int i;

	printf("wb_alloc: C++ test\n");
	wb_MemoryInfo info = wb_getMemoryInfo();
	printf("  Physical Memory: %zukb\n"
		   "  Page Size......: %zukb\n"
		   "  Commit Size____: %zukb\n\n", 
		   info.totalMemory / wb_CalcKilobytes(1), 
		   info.pageSize / wb_CalcKilobytes(1), 
		   info.commitSize / wb_CalcKilobytes(1));

	printf("Memory Arena test\n");
	wb_MemoryArena* arena = wb_arenaBootstrap(info, wb_FlagArenaNormal);

	int* numbers1 = wb_arenaPush<int, 10>(arena);
	int* numbers2 = wb_arenaPush<int, 20>(arena);
	int* numbers3 = wb_arenaPush<int, 40>(arena);
	int* numbers4 = wb_arenaPush<int, 80>(arena);

	for(i = 0; i < 150; ++i) {
		numbers1[i] = 150 - i;
	}

	for(i = 0; i < 150; ++i) {
		printf("%d ", numbers1[i]);
	}
	printf("\n\n");

	wb_arenaClear(arena);
	wb_arenaPush(arena, sizeof(int) * 150);

	for(i = 0; i < 150; ++i) {
		printf("%d ", numbers1[i]);
	}
	printf("\n\n");

	wb_arenaDestroy(arena);

	printf("Memory Pool test\n");
	printf("(can you see the free list?)\n");
	wb_MemoryPool* pool = wb_poolBootstrap<wb_usize>(info, wb_FlagPoolNormal);

	wb_usize* soManyNumbers[100];
	for(i = 0; i < 100; ++i) {
		soManyNumbers[i] = wb_poolRetrieve<wb_usize>(pool);
		*soManyNumbers[i] = 4096 - (i + 1) * 4;
	}

	for(i = 0; i < 50; ++i) {
		wb_poolRelease(pool, soManyNumbers[i * 2]);
	}

	for(i = 0; i < 100; ++i) {
		printf("%zx ", *soManyNumbers[i]);
	}
	printf("\n\n");

	for(i = 0; i < 50; ++i) {
		wb_usize* returned = wb_poolRetrieve<wb_usize>(pool);
		*returned = i;
	}

	for(i = 0; i < 100; ++i) {
		printf("%zx ", *soManyNumbers[i]);
	}
	printf("\n\n");

	wb_arenaDestroy(pool->alloc);

	printf("Tagged Heap test\n");

#define Tag_A 0
#define Tag_B 1
#define Tag_C 2

	wb_TaggedHeap* heap = wb_taggedBootstrap(
			info,
			sizeof(wb_usize) * 65, 
			/* normally the arena size would be 
			   something like wb_CalcMegabytes(2) */
			wb_FlagTaggedHeapNormal);
	wb_usize* memoryView = (wb_usize*)heap->pool.slots;

	wb_usize* aBlock = wb_taggedAlloc<wb_usize, 64>(heap, Tag_A);
	wb_usize* bBlock = wb_taggedAlloc<wb_usize, 64>(heap, Tag_B);
	wb_usize* cBlock = wb_taggedAlloc<wb_usize, 64>(heap, Tag_C);

	for(i = 0; i < 64; ++i) {
		aBlock[i] = i;
		bBlock[i] = 64 - i;
		cBlock[i] = 64 + i;
	}

	for(i = 0; i < 64*5; ++i) {
		printf("%zu ", memoryView[i]);
	}
	printf("\n\n");

	wb_taggedFree(heap, Tag_B);
	
	/* This ensures B is cleared */
	bBlock = wb_taggedAlloc<wb_usize>(heap, Tag_B);

	for(i = 0; i < 64*5; ++i) {
		printf("%zu ", memoryView[i]);
	}
	printf("\n\n");

	wb_arenaDestroy(heap->pool.alloc);

	return 0;
}


