#include <dbch.h>
//#include PLATFORM_HEADER


//#define DBG(fmt,...)	dbgf(fmt,##__VA_ARGS__)
#define DBG(fmt,...)


static int *hend = (int*)_GUARD_REGION_SEGMENT_END;


// helpers

static void* alloc_hend(int words)
{
	int *r = hend + 1;
	if (words) {
		r[-1] = words;
		hend = r + words;
	}
	return r;
}

static void* reuse_freed(int words)
{
	int *p = (int*)_GUARD_REGION_SEGMENT_END;
	while (p < hend) {
		int hdr = *p++;
		if (hdr > 0) // used
			p += hdr;
		else if (-hdr < words) // not big enough
			p -= hdr;
		else if (-hdr > words + 1) { // split in twain
			p[words] = hdr + words + 1;
			p[-1] = words;
			return p;
		} else { // exact size or lose a word
			p[-1] = -hdr;
			return p;
		}
	}
	return alloc_hend(words);
}

static void mark_freed(int *p)
{
	p[-1] = -p[-1];
}

static void consolidate_heap(void)
{
	int *p = (int*)_GUARD_REGION_SEGMENT_END;
	while (p < hend) {
		int hdr = *p++;
		if (hdr > 0) // used
			p += hdr;
		else if (p - hdr == hend) { // last slab is free
			hend = --p; // reclaim some heap
			return;
		} else if ((hdr & p[-hdr]) < 0) { // next is also free
			int next = p[-hdr];
			*--p = (hdr + next - 1);
		} else
			p -= hdr;
	}
}


// exports

void* qalloc(size_t bytes)
{
	void *r = alloc_hend((bytes + 3) / 4);
	DBG("qalloc(%d) %x\n", bytes, r);
	return r;
}

void* malloc(size_t bytes)
{
	void *r = reuse_freed((bytes + 3) / 4);
	DBG("malloc(%d) %x\n", bytes, r);
	return r;
}

void* zalloc(size_t bytes)
{
	void *r = malloc(bytes);
	MEMSET(r, 0, bytes);
	return r;
}

void free(void *ptr)
{
	if (ptr) {
		mark_freed(ptr);
		consolidate_heap();
	}
}
#if 0
void heap_dump(void)
{
	int *p = (int*)_GUARD_REGION_SEGMENT_END;
	while (p < hend) {
		int hdr = *p++, words = hdr < 0 ? -hdr : hdr;
		dbgf("%4x %d %s bytes\n", p, words * 4, hdr < 0 ? "free" : "used");
		p += words;
	}
}
#endif
