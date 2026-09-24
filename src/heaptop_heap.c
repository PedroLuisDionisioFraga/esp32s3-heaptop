#include <string.h>

#include "esp_heap_caps.h"
#include "heaptop_calc.h"
#include "heaptop_priv.h"

/* Internal DMA only: on some targets PSRAM regions also carry MALLOC_CAP_DMA. */
static const uint32_t s_region_caps[HEAPTOP_REGION_COUNT] = {
  [HEAPTOP_REGION_INTERNAL] = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT,
  [HEAPTOP_REGION_DMA] = MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA,
  [HEAPTOP_REGION_PSRAM] = MALLOC_CAP_SPIRAM,
};

/* Runs inside the heap's critical section: count only, never allocate or print. */
static bool _walk_cb(walker_heap_into_t heap, walker_block_info_t block, void *user_data)
{
  (void)heap;
  if (!block.used)
    heaptop_calc_hist_add((heaptop_frag_hist_t *)user_data, (uint32_t)block.size);
  return true;
}

void heaptop_heap_histogram(heaptop_region_t region, heaptop_frag_hist_t *h)
{
  memset(h, 0, sizeof(*h));
  if (region >= HEAPTOP_REGION_COUNT)
    return;
  /* Raw TLSF blocks: sizes include block metadata, so buckets are approximate. */
  heap_caps_walk(s_region_caps[region], _walk_cb, h);
}

void heaptop_heap_sample(heaptop_snapshot_t *s)
{
  for (int r = 0; r < HEAPTOP_REGION_COUNT; r++)
  {
    multi_heap_info_t info;
    /* Walks every block of the matching heaps under their spinlocks. */
    heap_caps_get_info(&info, s_region_caps[r]);

    heaptop_region_stats_t *rs = &s->region[r];
    rs->total = (uint32_t)(info.total_free_bytes + info.total_allocated_bytes);
    rs->present = rs->total > 0;
    rs->free = (uint32_t)info.total_free_bytes;
    rs->min_free = (uint32_t)info.minimum_free_bytes;
    rs->largest = (uint32_t)info.largest_free_block;
    rs->used_blocks = (uint32_t)info.allocated_blocks;
    rs->free_blocks = (uint32_t)info.free_blocks;
    rs->frag_pct10 = heaptop_calc_frag_pct10(rs->free, rs->largest);
  }
}
