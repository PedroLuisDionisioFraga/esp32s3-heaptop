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

/* Minimum free size since the last clear, per region. IDF's own local-minimum
 * monitor is not used: its first start allocates inside a spinlock, and with
 * heap task tracking that allocation can wait on a mutex. */
typedef struct heaptop_heap_window
{
  bool rebase;   /* a clear ran: take the next sample as the new baseline */
  bool cleared;  /* min_free reports the window, not IDF's since-boot value */
  uint32_t base; /* IDF's minimum at the clear */
  uint32_t low;  /* lowest sampled free size since the clear */
} heaptop_heap_window_t;

static heaptop_heap_window_t s_win[HEAPTOP_REGION_COUNT]; /* sampler-owned */

void heaptop_heap_init(void)
{
  memset(s_win, 0, sizeof(s_win));
}

void heaptop_heap_clear(void)
{
  for (int r = 0; r < HEAPTOP_REGION_COUNT; r++) s_win[r].rebase = true;
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

    heaptop_heap_window_t *w = &s_win[r];
    if (w->rebase)
    {
      w->base = rs->min_free;
      w->low = rs->free;
      w->cleared = true;
      w->rebase = false;
    }
    if (w->cleared)
    {
      if (rs->free < w->low)
        w->low = rs->free;
      rs->min_free = heaptop_calc_min_since(w->base, w->low, rs->min_free);
    }
  }
}
