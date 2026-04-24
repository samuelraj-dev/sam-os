#include "kmalloc.h"
#include "pmm.h"
#include "panic.h"

#define KMALLOC_ALIGN 16

typedef struct KBlock {
    uint64_t size;
    int free;
    uint32_t reserved;
    struct KBlock* next;
    uint64_t reserved2;
} KBlock;

static KBlock* heap_head = 0;
static int heap_ready = 0;

static uint64_t align_up(uint64_t value, uint64_t align)
{
    return (value + align - 1) & ~(align - 1);
}

static void split_block(KBlock* block, uint64_t size)
{
    uint64_t remaining = block->size - size;
    if (remaining <= sizeof(KBlock) + KMALLOC_ALIGN)
        return;

    KBlock* next = (KBlock*)((uint8_t*)(block + 1) + size);
    next->size = remaining - sizeof(KBlock);
    next->free = 1;
    next->next = block->next;

    block->size = size;
    block->next = next;
}

static void coalesce(void)
{
    for (KBlock* block = heap_head; block && block->next; block = block->next) {
        if (block->free && block->next->free) {
            block->size += sizeof(KBlock) + block->next->size;
            block->next = block->next->next;
        }
    }
}

static KBlock* expand_heap(uint64_t size)
{
    uint64_t total = align_up(size + sizeof(KBlock), PAGE_SIZE);
    uint64_t pages = total / PAGE_SIZE;
    KBlock* block = (KBlock*)pmm_alloc_pages(pages);
    if (!block)
        return 0;

    block->size = total - sizeof(KBlock);
    block->free = 1;
    block->next = 0;

    if (!heap_head) {
        heap_head = block;
        return block;
    }

    KBlock* tail = heap_head;
    while (tail->next)
        tail = tail->next;
    tail->next = block;
    return block;
}

void kmalloc_init(void)
{
    kassert((sizeof(KBlock) & (KMALLOC_ALIGN - 1)) == 0,
            "kmalloc: block header is not allocation-aligned");
    if (!heap_ready) {
        kassert(expand_heap(PAGE_SIZE) != 0, "kmalloc: initial heap allocation failed");
        heap_ready = 1;
    }
}

void* kmalloc(uint64_t size)
{
    if (size == 0)
        return 0;

    kassert(heap_ready, "kmalloc: heap used before kmalloc_init");
    size = align_up(size, KMALLOC_ALIGN);

    for (KBlock* block = heap_head; block; block = block->next) {
        if (block->free && block->size >= size) {
            split_block(block, size);
            block->free = 0;
            return block + 1;
        }
    }

    KBlock* block = expand_heap(size);
    if (!block)
        return 0;

    split_block(block, size);
    block->free = 0;
    return block + 1;
}

void* kzalloc(uint64_t size)
{
    uint8_t* ptr = (uint8_t*)kmalloc(size);
    if (!ptr)
        return 0;

    for (uint64_t i = 0; i < size; i++)
        ptr[i] = 0;
    return ptr;
}

void kfree(void* ptr)
{
    if (!ptr)
        return;

    KBlock* block = ((KBlock*)ptr) - 1;
    block->free = 1;
    coalesce();
}
