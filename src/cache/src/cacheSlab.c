/*
 * Copyright (c) 2019 TAOS Data, Inc. <cli@taosdata.com>
 *
 * This program is free software: you can use, redistribute, and/or modify
 * it under the terms of the GNU Affero General Public License, version 3
 * or later ("AGPL"), as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include "cacheint.h"
#include "cacheItem.h"
#include "cacheDefine.h"
#include "cacheSlab.h"
#include "osTime.h"

static cacheItem* cacheSlabDoAllocItem(cache_t *cache, size_t size, unsigned int id);
static int  cacheNewSlab(cache_t *cache, cacheSlabClass *pSlab);
static bool cacheIsReachMemoryLimit(cache_t *cache, int len);
static int  cacheSlabGrowArray(cache_t *cache, cacheSlabClass *pSlab);
static void *cacheAllocMemory(cache_t *cache, size_t size);
static void cacheSplitSlabPageInfoFreelist(cache_t *cache,char *ptr, uint32_t id);
static int cacheMoveItemFromLru(cache_t *cache, int id, int curLru, uint64_t totalBytes, 
                                uint32_t* moveToLru,  cacheItem* search, cacheItem** pItem);
static int cacheLruItemPull(cache_t *cache, int origId, int curLru, uint64_t total_bytes);

cache_code_t cacheSlabInit(cache_t *cache) {
  // init slab class
  int i = 0;
  size_t size = sizeof(cacheItem) + CHUNK_SIZE;
  for (i = 0; i < MAX_NUMBER_OF_SLAB_CLASSES; i++) {
    cacheSlabClass *slab = calloc(1, sizeof(cacheSlabClass));
    if (slab == NULL) {
      goto error;
    }

    if (cacheMutexInit(&(slab->mutex)) != 0) {
      goto error;
    }

    if (size % CHUNK_ALIGN_BYTES) {
      size += CHUNK_ALIGN_BYTES - (size % CHUNK_ALIGN_BYTES);
    }
    slab->size = size;
    slab->perSlab = SLAB_PAGE_SIZE / size;
    slab->id = i;
    slab->nAllocSlabs = 0;
    slab->slabArray = NULL;
    slab->nArray = 0;

    cache->slabs[i] = slab;

    size *= cache->options.factor;
  }
  cache->powerLargest = i;

  // init slab lru class
  for (i = 0; i < POWER_LARGEST; i++) {
    cacheSlabLruClass* lru = &(cache->lruArray[i]);
    lru->tail = NULL;
    lru->bytes = lru->num = 0;
    lru->id = i;
    if (cacheMutexInit(&(lru->mutex)) != 0) {
      goto error;
    }
  }

  return CACHE_OK;

error:
  for (i = 0; i < MAX_NUMBER_OF_SLAB_CLASSES; ++i) {
    if (cache->slabs[i] == NULL) {
      continue;
    }
    free(cache->slabs[i]);
  }
  
  return CACHE_FAIL;
}

uint32_t slabClsId(cache_t *cache, size_t size) {
  int i = 0;
  while (size > cache->slabs[i]->size) {
    if (i++ > cache->powerLargest) {
      return cache->powerLargest;
    }
  }

  return i;
}

cacheItem* cacheSlabAllocItem(cache_t *cache, size_t ntotal, uint32_t id) {
  cacheItem *item = NULL;
  int i;

  for (i = 0; i < 10; ++i) {
    item = cacheSlabDoAllocItem(cache, ntotal, id);
    if (item) {
      break;
    }

    if (cacheLruItemPull(cache, id, CACHE_LRU_COLD, 0) <= 0) {  /* try to pull item fom cold list */
      /* pull item from cold list failed, try to pull item from hot list */
      if (cacheLruItemPull(cache, id, CACHE_LRU_HOT, 0) <= 0) {
        break;
      }
    }
  }

  return item;
}

void cacheSlabFreeItem(cache_t *cache, cacheItem* item) {  
  //size_t ntotal = cacheItemTotalBytes(item->nkey, item->nbytes);
  uint32_t id = item_clsid(item);

  if (cacheItemNeverExpired(item)) {
    if (item->next) item->next->prev = item->prev;
    if (item->prev) item->prev->next = item->next;
    if (item == cache->neverExpireItemHead) {
      cache->neverExpireItemHead = item->next;
    }
  }

  cacheSlabClass* pSlab = cache->slabs[id];
  cacheMutexLock(&pSlab->mutex);

  if (!item_is_chunked(item)) {
    item->flags = ITEM_SLABBED;
    item->slabClsId = id;
    item->prev = NULL;
    item->next = pSlab->freeItem;
    if (pSlab->freeItem) pSlab->freeItem->prev = item;
    pSlab->freeItem = item;
    pSlab->nFree += 1;
  } else {

  }

  cacheMutexUnlock(&pSlab->mutex);
}

static bool cacheIsReachMemoryLimit(cache_t *cache, int len) {
  if (cache->alloced + len >= cache->options.limit) {
    return true;
  }

  return false;
}

static void *cacheAllocMemory(cache_t *cache, size_t size) {
  cache->alloced += size;
  return malloc(size);
}

static int cacheSlabGrowArray(cache_t *cache, cacheSlabClass *pSlab) {
  if (pSlab->nAllocSlabs == pSlab->nArray) {
    size_t new_size =  (pSlab->nArray != 0) ? pSlab->nArray * 2 : 16;
    void *new_array = realloc(pSlab->slabArray, new_size * sizeof(void *));
    if (new_array == NULL) return 0;
    pSlab->nArray = new_size;
    pSlab->slabArray = new_array;
  }

  return 1;
}

static void cacheSplitSlabPageInfoFreelist(cache_t *cache, char *ptr, uint32_t id) {
  cacheSlabClass *p = cache->slabs[id];
  int i = 0;
  for (i = 0; i < p->perSlab; i++) {
    cacheItem* item = (cacheItem*)ptr;
    item->slabClsId = id;
    cacheSlabFreeItem(cache, item);
    ptr += p->size;
  }
}

static int cacheNewSlab(cache_t *cache, cacheSlabClass *pSlab) {
  char *ptr;
  uint32_t id = pSlab->id;
  int len = pSlab->size * pSlab->perSlab;

  if (cacheIsReachMemoryLimit(cache, len)) { 
    return CACHE_REACH_LIMIT;
  }

  if (cacheSlabGrowArray(cache, pSlab) == 0 || (ptr = cacheAllocMemory(cache, len)) == NULL) {
    return CACHE_ALLOC_FAIL;
  }

  memset(ptr, 0, (size_t)len);
  cacheSplitSlabPageInfoFreelist(cache, ptr, id);

  pSlab->slabArray[pSlab->nArray++] = ptr;

  return CACHE_OK;
}

static cacheItem* cacheSlabDoAllocItem(cache_t *cache, size_t size, unsigned int id) {
  cacheSlabClass *pSlab = cache->slabs[id];
  cacheItem *item = NULL;

  cacheMutexLock(&pSlab->mutex);

  /* no free item, try to alloc new slab page */
  if (pSlab->nFree == 0) {
    cacheNewSlab(cache, pSlab);
  }

  /* if there is free items, free it from free list */
  if (pSlab->nFree > 0) {
    item = pSlab->freeItem;
    pSlab->freeItem = item->next;
    if (item->next) item->next->prev = NULL;
    item_unslabbed(item);
    pSlab->nFree -= 1;
  }

  cacheMutexUnlock(&pSlab->mutex);

  return item;
}

static int cacheMoveItemFromLru(cache_t *cache, int lruId, int curLru, uint64_t totalBytes, 
                                uint32_t* moveToLru,  cacheItem* search, cacheItem** pItem) {
  int removed = 0;
  uint64_t limit = 0;
  cache_option_t* opt = &(cache->options);
  cacheSlabLruClass* lru = &(cache->lruArray[lruId]);

  switch (curLru) {
    case CACHE_LRU_HOT:
      limit = totalBytes * opt->hotPercent / 100;
      // no break here, go through to next case
    case CACHE_LRU_WARM:
      if (limit == 0) {
        limit = totalBytes * opt->warmPercent / 100;
      }
      if (item_is_active(search)) { /* is item active? */
        item_unactive(search);      /* mark as unactive */
        removed++;
        if (curLru == CACHE_LRU_WARM) { /* is warm lru list? */          
          cacheItemMoveToLruHead(cache, search);  /* move to lru head */
          cacheItemRemove(cache, search);
        } else {                        /* else is hot lru list */
          *moveToLru = CACHE_LRU_WARM;    /* move to warm lru list */          
          cacheItemUnlinkFromLru(cache, search, true);
          *pItem = search;
        }
      } else if (lru->bytes > limit) {
        *moveToLru = CACHE_LRU_COLD;
        cacheItemUnlinkFromLru(cache, search, true);
        *pItem = search;
        removed++;
      } else {
        *pItem = search;
      }
      break;
    case CACHE_LRU_COLD:
      *pItem = search;
      break;
  }

  return removed;
}

static int cacheLruItemPull(cache_t *cache, int origId, int curLru, uint64_t totalBytes) {
  cacheItem* item = NULL;
  cacheItem* search;
  cacheItem* next;
  cacheSlabLruClass* lru;
  int lruId = origId;
  int removed = 0;
  int tries = 5;
  uint32_t moveToLru = 0;
  uint64_t now = taosGetTimestamp(TSDB_TIME_PRECISION_MILLI);

  lruId |= curLru;
  lru = &(cache->lruArray[lruId]);
  assert(lru->id == lruId);
  search = lru->tail;
  for (; tries > 0 && search != NULL; tries--, search = next) {
    assert(item_lruid(search) == lruId);

    cacheMutexLock(&(lru->mutex));

    next = search->prev;

    // is item expired?
    if (search->expireTime != 0 && now - search->lastTime >= search->expireTime) {
      cacheItemUnlinkNolock(search->pTable, search);
      cacheItemRemove(cache, search);
      removed++;
      continue;
    }

    cacheMutexUnlock(&(lru->mutex));

    removed += cacheMoveItemFromLru(cache, lruId, curLru, totalBytes, &moveToLru, search, &item);

    if (item != NULL) {
      break;
    }   
  }

  if (item != NULL) {
    if (moveToLru) {
      item->slabClsId = item_clsid(item);
      item->slabClsId|= moveToLru;
      cacheItemLinkToLru(cache, item, true);
    }
  }

  return removed;
}