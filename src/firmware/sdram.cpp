#include "sdram.h"

#include <cstring>

SDRAM_DATA volatile uint32_t g_sdram_data_probe_words[16];
SDRAM_DATA volatile uint32_t g_sdram_test_scratch_words[SDRAM_TEST_SCRATCH_WORDS];

SDRAM_DATA alignas(8) uint8_t g_sdram_json_pool[SDRAM_JSON_POOL_SIZE];
SdramJsonAllocator g_sdram_json_allocator;

static_assert((sizeof(SdramJsonAllocator::BlockHeader) % SdramJsonAllocator::kAlignment) == 0u,
	"SdramJsonAllocator::BlockHeader must preserve payload alignment");

SdramJsonAllocator::SdramJsonAllocator()
	: pool_base_(nullptr),
	  pool_size_(0u),
	  head_(nullptr),
	  initialized_(false) {}

size_t SdramJsonAllocator::alignUp(size_t value, size_t alignment)
{
	return (value + (alignment - 1u)) & ~(alignment - 1u);
}

SdramJsonAllocator::BlockHeader* SdramJsonAllocator::blockFromPayload(void* payload)
{
	return reinterpret_cast<BlockHeader*>(reinterpret_cast<uint8_t*>(payload) - sizeof(BlockHeader));
}

void* SdramJsonAllocator::payloadFromBlock(BlockHeader* block)
{
	return reinterpret_cast<void*>(reinterpret_cast<uint8_t*>(block) + sizeof(BlockHeader));
}

void SdramJsonAllocator::init(void* pool, size_t pool_size)
{
	pool_base_ = pool;
	pool_size_ = alignUp(pool_size, kAlignment);
	initialized_ = false;
	head_ = nullptr;

	if (pool_base_ == nullptr || pool_size_ <= sizeof(BlockHeader) ||
		((reinterpret_cast<uintptr_t>(pool_base_) & (kAlignment - 1u)) != 0u)) {
		return;
	}

	head_ = reinterpret_cast<BlockHeader*>(pool_base_);
	head_->size = pool_size_ - sizeof(BlockHeader);
	head_->next = nullptr;
	head_->prev = nullptr;
	head_->used = kFreeTag;
	initialized_ = true;
}

bool SdramJsonAllocator::isInitialized() const
{
	return initialized_;
}

void SdramJsonAllocator::splitBlock(BlockHeader* block, size_t wanted_size)
{
	const size_t aligned_wanted = alignUp(wanted_size, kAlignment);
	if (block == nullptr || block->size <= aligned_wanted + sizeof(BlockHeader) + kAlignment) {
		return;
	}

	uint8_t* split_addr = reinterpret_cast<uint8_t*>(payloadFromBlock(block)) + aligned_wanted;
	auto* split = reinterpret_cast<BlockHeader*>(split_addr);
	split->size = block->size - aligned_wanted - sizeof(BlockHeader);
	split->used = kFreeTag;
	split->next = block->next;
	split->prev = block;

	if (split->next) {
		split->next->prev = split;
	}

	block->next = split;
	block->size = aligned_wanted;
}

void SdramJsonAllocator::coalesce(BlockHeader* block)
{
	if (block == nullptr || block->used != kFreeTag) {
		return;
	}

	if (block->next && block->next->used == kFreeTag) {
		BlockHeader* next = block->next;
		block->size += sizeof(BlockHeader) + next->size;
		block->next = next->next;
		if (block->next) {
			block->next->prev = block;
		}
	}

	if (block->prev && block->prev->used == kFreeTag) {
		BlockHeader* prev = block->prev;
		prev->size += sizeof(BlockHeader) + block->size;
		prev->next = block->next;
		if (prev->next) {
			prev->next->prev = prev;
		}
	}
}

void* SdramJsonAllocator::allocate(size_t size)
{
	if (!initialized_ || size == 0u) {
		return nullptr;
	}

	const size_t wanted = alignUp(size, kAlignment);
	BlockHeader* cur = head_;
	while (cur) {
		if (cur->used == kFreeTag && cur->size >= wanted) {
			splitBlock(cur, wanted);
			cur->used = kUsedTag;
			return payloadFromBlock(cur);
		}
		cur = cur->next;
	}
	return nullptr;
}

void SdramJsonAllocator::deallocate(void* ptr)
{
	if (!initialized_ || ptr == nullptr) {
		return;
	}

	BlockHeader* block = blockFromPayload(ptr);
	if (block->used != kUsedTag) {
		return;
	}

	block->used = kFreeTag;
	coalesce(block);
}

void* SdramJsonAllocator::reallocate(void* ptr, size_t new_size)
{
	if (!initialized_) {
		return nullptr;
	}

	if (ptr == nullptr) {
		return allocate(new_size);
	}
	if (new_size == 0u) {
		deallocate(ptr);
		return nullptr;
	}

	BlockHeader* block = blockFromPayload(ptr);
	if (block->used != kUsedTag) {
		return nullptr;
	}

	const size_t wanted = alignUp(new_size, kAlignment);
	if (block->size >= wanted) {
		splitBlock(block, wanted);
		return ptr;
	}

	if (block->next && block->next->used == kFreeTag &&
		(block->size + sizeof(BlockHeader) + block->next->size) >= wanted) {
		BlockHeader* next = block->next;
		block->size += sizeof(BlockHeader) + next->size;
		block->next = next->next;
		if (block->next) {
			block->next->prev = block;
		}
		splitBlock(block, wanted);
		return ptr;
	}

	void* new_ptr = allocate(wanted);
	if (new_ptr == nullptr) {
		return nullptr;
	}

	std::memcpy(new_ptr, ptr, block->size);
	deallocate(ptr);
	return new_ptr;
}

bool sdram_json_allocator_init(void)
{
	if (!sdram_wait_ready()) {
		return false;
	}

	g_sdram_json_allocator.init(g_sdram_json_pool, sizeof(g_sdram_json_pool));
	return g_sdram_json_allocator.isInitialized();
}
