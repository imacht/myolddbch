#include <dbch.h>
#include <em_msc.h>


#ifndef NV_PHY_PER_PG
#define NV_PHY_PER_PG		2
#endif
#ifndef PAGES
#define PAGES			4
#endif

#if PAGES < 2
#error Need more PAGES.
#endif

#define PAGE_SIZE		(NV_PHY_PER_PG * FLASH_PAGE_SIZE)

//#define DBG(f, ...)		uartf("NV:" f, ##__VA_ARGS__)
#define DBG(f, ...)


struct item {
	uint16_t id;	///< NV item id code (0xFFFF = not active)
	uint16_t len;	///< Length of NV item data bytes
	uint16_t chk;	///< Byte-wise checksum of the 'len' data bytes of the item.
	uint16_t pad1;	///< Padding ("don't care") for 32-bit flash writes
	uint16_t stat;	///< Item status
	uint16_t pad2;	///< Padding ("don't care") for 32-bit flash writes
	uint16_t live;	///< NV item is 'live' if  and id !=0xFFFF
	uint16_t pad3;	///< Padding ("don't care") for 32-bit flash writes
	uint8_t value[0];
};

struct page {
	uint16_t active;
	uint16_t pad1;	///< Padding ("don't care") for 32-bit flash writes
	uint16_t xfer;
	uint16_t pad2;	///< Padding ("don't care") for 32-bit flash writes
	struct item items[0];
};


VAR_AT_SEGMENT(NO_STRIPPING uint8_t _store[PAGES * PAGE_SIZE], __PSSTORE__);


static uint16_t tails[PAGES]; ///< Offset into the page of the first available erased space.
static uint16_t losts[PAGES]; ///< Count of the bytes lost for the zeroed-out items.
static uint8_t compacto; ///< Page reserved for item compacting transfer.


// flash

static int write_long(const void *addr, const void *data)
{
	MSC_Init();
	return MSC_WriteWord((uint32_t*)addr, data, 4);
}

static int write_word(const void *addr, uint16_t word)
{
	uint16_t data[] = {word, 0xFFFF};
	return write_long(addr, data);
}

static int wipe_word(const void *addr)
{
	return write_word(addr, 0);
}

static void write_buf(const uint8_t *addr, const uint8_t *src, int len)
{
	uint8_t i, rem, tmp[4];
	if ((rem = (addr - _store) % 4)) {
		addr -= rem;
		for (i = 0; i < rem; i++)
			tmp[i] = addr[i];
		for (; i < 4 && len; len--)
			tmp[i++] = *src++;
		while (i < 4)
			tmp[i++] = 255;
		write_long(addr, tmp);
		addr += 4;
	}

	rem = len % 4;
	MSC_WriteWord((uint32_t*)addr, src, len -= rem);

	if (rem) {
		addr += len;
		src += len;
		for (i = 0; i < rem; i++)
			tmp[i] = *src++;
		while (i < 4)
			tmp[i++] = 255;
		write_long(addr, tmp);
	}
}

static const void* address(int page, int offset)
{
	return _store + page * PAGE_SIZE + offset;
}


// helpers

static const struct item* init_page(int page, uint16_t id, int findDups);

/** Find an item Id in NV and return the address of its data.
 \param id Valid NV item Id.
 \return Offset of data corresponding to item Id, if found, otherwise 0.
 \note The page containing the item, if found; otherwise no valid assignment made - left equal to item Id. */
static const struct item* find_item(uint16_t id)
{
	int page;
	for (page = 0; page < PAGES; page++) {
		const struct item *i = init_page(page, id, FALSE);
		if (i)
			return i;
	}
	// Now attempt to find the item as the "old" item of a failed/interrupted NV write.
	if (~id & 0x8000)
		return find_item(id | 0x8000);
	return 0;
}

/** Zero an item, aka set live = 0.
 \param i Item header address */
static void zero_item(const struct item *i)
{
	int page = ((uint8_t*)i - _store) / PAGE_SIZE;
	losts[page] += (sizeof(*i) + i->len + 3) & ~3;

	wipe_word(&i->live);
}

/** Transfer an item, aka set stat = 0.
 \param i Item header address */
static void xfer_item(const struct item *i)
{
	wipe_word(&i->stat);
}

static uint16_t calc_chk(const struct item *h)
{
	const uint8_t *p = h->value;
	uint16_t chk = 0, len = (h->len + 3) & ~3;
	while (len--)
		chk += *p++;
	return chk;
}

/** Walk the page items; calculate checksums, lost bytes & page offset.
 \param page Valid NV page to verify and init.
 \param id Valid NV item Id to use function as a "find_item". If set to NULL then just perform the page initialization.
 \param findDups TRUE on recursive call from initNV() to find and zero-out duplicate items left from a write that is interrupted by a reset/power-cycle. FALSE otherwise.
 \return If 'id' is non-NULL and good checksums are found, return the offset of the data corresponding to item Id; else 0. */
static const struct item* init_page(int page, uint16_t id, int findDups)
{
	uint16_t tail = sizeof(struct page), lost = 0;
	do {
		const struct item *i = address(page, tail);
		if (i->id == 0xFFFF)
			break; // No more NV items

		uint16_t sz = (sizeof(*i) + i->len + 3) & ~3; // Get the actual size in bytes which is the ceiling(hdr.len)
		if (tail + sz > PAGE_SIZE) { // A bad 'len' write has blown away the rest of the page.
			lost += PAGE_SIZE - tail;
			tail = PAGE_SIZE;
			break;
		}

		if (i->live != 0xFFFF)
			lost += sz;
		else if (id != 0) { // This trick allows function to do double duty for find_item() without compromising its essential functionality at powerup initialization.
			// This trick allows asking to find the old/transferred item in case of a successful new item write that gets interrupted before the old item can be zeroed out.
			if ((id & 0x7fff) == i->id) if ((~id & 0x8000 && i->stat == 0xFFFF) || (id & 0x8000 && i->stat != 0xFFFF))
				return i;
		} else if (i->chk != calc_chk(i)) { // When invoked from the nv_init(), verify checksums and find & zero any duplicates.
			zero_item(i);  // Mark bad checksum as invalid.
			lost += sz;
		} else if (findDups) {
			// The trick of setting the MSB of the item Id causes the logic immediately above to return a valid page only if the header 'stat' indicates that it was the older item being transferred.
			if (i->stat == 0xFFFF && (i = find_item(i->id | 0x8000)))
				zero_item(i);  // Mark old duplicate as invalid.
		} else if (i->stat != 0xFFFF) // Any "old" item immediately exits and triggers the N^2 exhaustive initialization.
			return i; // signal error to nv_init
		tail += sz;
	} while (tail + sizeof(struct item) < PAGE_SIZE);

	tails[page] = tail;
	losts[page] = lost;
	return 0;
}

/** Erases a page in Flash.
 \param page Valid NV page to erase. */
static void erase_page(uint8_t page)
{
DBG("erase_page(%d)\n", page);
	MSC_Init();
	const uint8_t *addr = address(page, 0), *end = address(page + 1, 0);
	for (; addr < end; addr += FLASH_PAGE_SIZE)
		MSC_ErasePage((uint32_t*)addr);
//		halInternalFlashErase(MFB_PAGE_ERASE, (uint32_t)addr);

	tails[page] = sizeof(struct page);
	losts[page] = 0;
}

/** Writes an item header/data combo to the specified NV page.
 \param page Valid NV Flash page.
 \param id Valid NV item Id.
 \param len Byte count of the data to write.
 \param buf The data to write. If NULL, no data/checksum write.
 \param wrChk TRUE if the checksum should be written, FALSE otherwise.
 \return Output address if header/data to write matches header/data read back, else 0. */
static const struct item* write_item(int page, uint16_t id, int len, const uint8_t *buf, uint8_t wrChk)
{
DBG("write_item(%d,%x,%d,%d)\n", page, id, len, wrChk);
	const struct item *h = address(page, tails[page]), *r = 0;
	struct item hdr = {.id = id, .len = len};
	write_long(&h->id, &hdr);

	len = (sizeof(struct item) + hdr.len + 3) & ~3;
	if (h->id != id || h->len != hdr.len) {
		if (len > PAGE_SIZE - tails[page])
			len = PAGE_SIZE - tails[page];
		losts[page] += len;
	} else if (wrChk) {
		uint16_t chk, cnt;
		if (buf) {
			write_buf(h->value, buf, cnt = hdr.len);
			for (chk = 0; cnt--; )
				chk += *buf++;
		} else
			chk = hdr.len * 255;
		chk += 255 * (len - sizeof(struct item) - hdr.len);
		if (chk == calc_chk(h)) {
			write_word(&h->chk, chk);
			if (chk == h->chk)
				r = h;
		}
	} else
		r = h;

	tails[page] += len;
	return r;
}

static void compact_page_cleanup(int page)
{
DBG("compact_page_cleanup(%d)\n", page);
	// In order to recover from a page compaction that is interrupted, the logic in nv_init() depends upon the following order:
	// 1. State of the target of compaction is changed to ePgInUse.
	// 2. Compacted page is erased.
	const struct page *p = address(compacto, 0);
	wipe_word(&p->active); // Mark reserve page as being active.
	erase_page(compacto = page); // Set the reserve page to be the newly erased page.
}

/** Compacts the page specified.
 \param page Source page
 \param skipId Item Id to not compact.
 \return TRUE if valid items from 'page' are successully compacted onto the 'compacto'; FALSE otherwise.
 \note that on a failure, this could loop, re-erasing the 'compacto' and re-compacting with the risk of infinitely looping on HAL flash failure.
 Worst case scenario: HAL flash starts failing in general, perhaps low Vdd?
 All page compactions will fail which will cause all nv_write() calls to return NV_OPER_FAILED.
 Eventually, all pages in use may also be in the state of "pending compaction" where the page header member offsetof(struct page,xfer) is zeroed out.
 During this "HAL flash brown-out", the code will run and OTA should work (until low Vdd causes an actual chip brown-out, of course.)
 Although no new NV items will be created or written, the last value written with a return value of SUCCESS can continue to be read successfully.
 If eventually HAL flash starts working again, all of the pages marked as "pending compaction" may or may not be eventually compacted.
 But, initNV() will deterministically clean-up one page pending compaction per power-cycle (if HAL flash is working.)
 Nevertheless, one erased reserve page will be maintained through such a scenario. */
static int compact_page(uint8_t page, uint16_t skipId)
{
	uint16_t tail = sizeof(struct page), rtrn = 1;
	while (tail + sizeof(struct item) < PAGE_SIZE) {
		const struct item *i = address(page, tail), *o;
		if (i->id == 0xFFFF)
			break;

		uint16_t sz = (sizeof(struct item) + i->len + 3) & ~3;
		if (tail + sz > PAGE_SIZE || tails[compacto] + sz > PAGE_SIZE)
			break;

		if (i->live != 0x0000 && i->id != skipId && i->chk == calc_chk(i)) {
			// Prevent excessive re-writes to item header caused by numerous, rapid, & successive Nv interruptions caused by resets.
			if (i->stat == 0xFFFF)
				xfer_item(i);
			if ((o = write_item(compacto, i->id, i->len, 0, 0))) {
				write_buf(o->value, i->value, i->len);
				uint16_t chk = calc_chk(o); // Calculate and write the new checksum.
				write_word(&o->chk, chk);
				if (o->chk != chk) {
					rtrn = 0;
					break;
				}
			} else {
				rtrn = 0;
				break;
			}
		}
		tail += sz;
	}

	if (rtrn == FALSE)
		erase_page(compacto);
	else if (skipId == 0)
		compact_page_cleanup(page);

	return rtrn; // else invoking function must cleanup.
}

/** An NV item is created and initialized with the data passed to the function, if any.
 \param flag TRUE if the 'buf' parameter contains data for the call to write_item(). (i.e. if invoked from nv_item_init()). FALSE if write_item() should just write the header and the 'buf' parameter is ok to use as a return value of the page number to be cleaned with compact_page_cleanup(). (i.e. if invoked from nv_write()).
 \param id Valid NV item Id.
 \param len Item data length.
 \param buf Pointer to item initalization data. Set to NULL if none.
 \return Output address if header/data to write matches header/data read back, else 0. */
static const struct item* init_item(int flag, uint16_t id, int len, void *buf)
{
DBG("init_item(%d,%x,%d)\n", flag, id, len);
	int sz = (len + sizeof(struct item) + 3) & ~3, page = compacto;
	do {
		if (++page == PAGES)
			page = 0;
		if (page == compacto)
			return 0;
	} while (sz + tails[page] > PAGE_SIZE + losts[page]);

	if (sz + tails[page] <= PAGE_SIZE) // Item fits without compacting
		return write_item(page, id, len, buf, flag);

	const struct page *p = address(page, 0);
	wipe_word(&p->xfer); // Mark the old page as being in process of compaction.

	if (!compact_page(page, id)) // First the old page is compacted, then the new item will be the last one written to what had been the reserved page.
		return 0;

	const struct item *r = write_item(compacto, id, len, buf, flag);
	if (flag == FALSE)
		*(uint8_t*)buf = page; // Overload 'buf' as an OUT parameter to pass back to the calling function the old page to be cleaned up.
	else // Safe to do the compacted page cleanup even if write_item() above failed because the item does not yet exist since this call with flag==TRUE is from nv_item_init().
		compact_page_cleanup(page);
	return r;
}


// exports

void nv_init(void)
{
	compacto = PAGES;

	int old = PAGES, pg;
	for (pg = 0; pg < PAGES; pg++) {
		const struct page *p = address(pg, 0);
		if (p->active == 0xFFFF) {
			if (compacto == PAGES)
				compacto = pg;
			else // mark page active
				wipe_word(&p->active);
		} else if (p->xfer != 0xFFFF) // transfer was in progress
			old = pg;
	}

	if (old != PAGES) { // If a page compaction was interrupted before the old page was erased.
		if (compacto != PAGES) { // Interrupted compaction before the target of compaction was put in use; so erase the target of compaction and start again.
			erase_page(compacto);
			compact_page(old, 0);
		} else // Interrupted compaction after the target of compaction was put in use, but before the old page was erased; so erase it now and create a new reserve page.
			erase_page(compacto = old);
	} else if (compacto != PAGES)
		erase_page(compacto);  // The last page erase could have been interrupted by a power-cycle.
	// else if there is no reserve page, compact_page_cleanup() must have succeeded to put the old reserve page (i.e. the target of the compacted items) into use but got interrupted by a reset while trying to erase the page to be compacted.
	// Such a page should only contain duplicate items (i.e. all items will be marked 'Xfer') and thus should have the lost count equal to the page size less the page header.

	int findDups = 0;
	// Calculate page tails and lost bytes - any "old" item triggers an N^2 re-scan from start.
	for (pg = 0; pg < PAGES; pg++) {
		if (init_page(pg, 0, findDups)) {
			findDups = 1;
			pg = -1; // Pre-decrement so that loop increment will start over at zero.
		}
else DBG("page %d: tail=%d lost=%d\n", pg, tails[pg], losts[pg]);
	}

	// Final pass to calculate page lost after invalidating duplicate items.
	if (findDups) for (pg = 0; pg < PAGES; pg++)
		init_page(pg, 0, 0);

	if (compacto == PAGES) {
		int highestLoss = 0;
		for (pg = 0; pg < PAGES; pg++) {
			if (losts[pg] == PAGE_SIZE - sizeof(struct page)) { // Is this the page that was compacted?
				highestLoss = pg;
				break;
			} else if (losts[highestLoss] < losts[pg]) // This check is not expected to be necessary because the above test should always succeed with an early loop exit.
				highestLoss = pg;
		}
		erase_page(compacto = highestLoss); // The last page erase had been interrupted by a power-cycle.
	}
}

int nv_item_init(uint16_t id, uint16_t len, void *buf)
{
	const struct item *i = find_item(id);
	if (i)
		return 0;

	if (init_item(TRUE, id, len, buf))
		return 'u';
	return 'f';
}

int nv_write(uint16_t id, uint16_t ndx, uint16_t len, void *buf)
{
	const struct item *i = find_item(id), *n;
	if (!i)
		return 'u';
	if (i->len < ndx + len)
		return 'f';

	uint16_t chk = i->chk, diff = 0;
	const uint8_t *p = i->value + ndx, *q = buf, *end = q + len;
	for (; q < end; p++, q++) {
		if (*p != *q) {
			diff++; // Count number of different bytes and calculate expected checksum after transferring old data and writing new data.
			chk -= *p; // subtract old byte
			chk += *q; // add new
		}
	}
	if (diff == 0) // If the buffer to write isn't different in one or more bytes.
		return 0;

	int page = ((uint8_t*)i - _store) / PAGE_SIZE, compacted = PAGES, rtrn = 0;
	if ((n = init_item(0, id, i->len, &compacted))) {
		if (i->stat == 0xFFFF) // Prevent excessive re-writes to item header caused by numerous, rapid, and successive Nv interruptions caused by resets.
			xfer_item(i);
		write_buf(n->value, i->value, ndx);
		write_buf(n->value + ndx, buf, len);
		write_buf(n->value + ndx + len, i->value + ndx + len, i->len - ndx - len);
		write_word(&n->chk, calc_chk(n));
		if (n->chk != chk)
			rtrn = 'f';
	} else
		rtrn = 'f';

	if (compacted != PAGES) { // Even though the page compaction succeeded, if the new item is coming from the compacted page and writing the new value failed, then the compaction must be aborted.
		if (page == compacted && rtrn)
			erase_page(compacto);
		else
			compact_page_cleanup(compacted);
	}

	// Zero of the old item must wait until after compact page cleanup has finished - if the item is zeroed before and cleanup is interrupted by a power-cycle, the new item can be lost.
	if (page != compacted && rtrn != 'f')
		zero_item(i);

	return rtrn;
}

int nv_drop(uint16_t id)
{
	const struct item *i = find_item(id);
	if (!i)
		return 'u'; // NV item does not exist

	zero_item(i); // Set item header ID to zero to 'delete' the item

	// Verify that item has been removed
	if (find_item(id))
		return 'f'; // Still there
	return 0; // Yes, it's gone
}

const void* nv_find(uint16_t id)
{
	const struct item *i = find_item(id);
	return i ? i->value : 0;
}

int nv_size(const void *p)
{
	struct item *h = (struct item*)((char*)p - sizeof(*h));
	return h->len;
}

int nv_id(const void *p)
{
	struct item *h = (struct item*)((char*)p - sizeof(*h));
	return h->id;
}

int nv_save(uint16_t id, uint16_t len, void *buf)
{
	int r = nv_item_init(id, len, buf);
	if (r == 0)
		r = nv_write(id, 0, len, buf);
	return r;
}


// TODO SIMEE wrappers
/*
EmberStatus halInternalSimEeStartup(bool forceRebuildAll)
{
	return 0;
}

void halInternalSimEeGetData(void *vdata, uint8_t compileId, uint8_t index, uint8_t len)
{
}

void halInternalSimEeSetData(uint8_t compileId, void *vdata, uint8_t index, uint8_t len)
{
}

void halInternalSimEeIncrementCounter(uint8_t compileId)
{
}
*/
