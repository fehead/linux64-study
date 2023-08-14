#include <stdio.h>
#include <stdlib.h>

typedef unsigned long long U64;

#define	OUT
#define	IN
#define	IN_OUT

#define	IDMAP_PGD_ORDER			9		// 48-39
#define	PAGE_SIZE		 		4096	// 4k
#define	PGDIR_SHIFT  	 		39
#define	PMD_TYPE_TABLE	 		3
#define	SWAPPER_BLOCK_SIZE		0x200000	//     SZ_2M; 
#define	MAX_FDT_SIZE			0x200000	//     SZ_2M; 
#define	SWAPPER_RX_MMUFLAGS		0x781
#define	SWAPPER_BLOCK_SHIFT		21
#define	SWAPPER_TABLE_SHIFT		30
#define	PAGE_SHIFT				12


U64 phys_to_pte(U64 phys)
{
	return phys;
}

/*
 * Macro to populate page table entries, these entries can be pointers to the next level
 * or last level entries pointing to physical memory.
 *
 *      tbl:    page table address
 *      rtbl:   pointer to page table or physical memory
 *      index:  start index to write
 *      eindex: end index to write - [index, eindex] written to
 *      flags:  flags for pagetable entry to or in
 *      inc:    increment to rtbl between each entry
 *      tmp1:   temporary variable
 *
 * Preserves:   tbl, eindex, flags, inc
 * Corrupts:    index, tmp1
 * Returns:     rtbl
 */
void populate_entries(IN U64 * tbl, IN_OUT U64 ** rtbl, U64 index, U64 eindex,
	U64 flags, U64 inc)
{
	printf("tbl base address %p\n", tbl);
	U64 tmp1;
	do {
		tmp1 = phys_to_pte((U64)*rtbl);
		tmp1 |= flags;					// tmp1 = table entry
		// tbl[index] = tmp1;				// str tmp1, [tbl, index, lsl #3]
		printf("[%03llu]-%p: %p\n", index, &tbl[index], tmp1);
		*rtbl += (inc/sizeof(U64));		// rtbl = pa next level 
		++index;
	} while(index < eindex);
}

/*
 * Compute indices of table entries from virtual address range. If multiple entries
 * were needed in the previous page table level then the next page table level is assumed
 * to be composed of multiple pages. (This effectively scales the end index).
 *
 *      vstart: virtual address of start of range
 *      vend:   virtual address of end of range - we map [vstart, vend]
 *      shift:  shift used to transform virtual address into index
 *      order:  #imm 2log(number of entries in page table)
 *      istart: index in table corresponding to vstart
 *      iend:   index in table corresponding to vend
 *      count:  On entry: how many extra entries were required in previous level, scales
 *                        our end index.
 *              On exit: returns how many extra entries required for next page table level
 *
 * Preserves:   vstart, vend
 * Returns:     istart, iend, count
 */
void compute_indices(const U64 vstart, const U64 vend, const int shift,
	const int order, OUT U64 * istart, OUT U64 * iend, IN_OUT U64 * count)
{
	/*
	 *	shift = 39, order = 9
	 * *istart = (vstart >> 39 & 0x1ff;
	 * *iend = (vend >> 39) & 0x1ff;
	 * *iend += (*count << 9);
	 */
	*istart = (vstart >> shift) & ((1 << order) -1);	// 48 기준으로 48~39 비트
	*iend = (vend >> shift) & ((1 << order) -1);
	*iend += (*count << order);
	*count = *iend - *istart;

	printf("[%#llx~%#llx] s:o[%d:%d] i[%llu~%llu] count[%llu]\n", vstart, vend,
		shift, order, *istart, *iend, *count);
	printf("istart ==> %p >> %d = %p, %p & %#x = %d[%#x]\n", vstart, shift,
		(vstart >> shift), (vstart >> shift), ((1 << order) -1), *istart, *istart);
	printf("iend ==> %p >> %d = %p, %p & %#x = %d[%#x]\n", vend, shift,
		(vend >> shift), (vend >> shift), ((1 << order) -1), *iend, *iend);
	printf("===============================================================\n");
}

/*
 * Map memory for specified virtual address range. Each level of page table needed supports
 * multiple entries. If a level requires n entries the next page table level is assumed to be
 * formed from n pages.
 *
 *      tbl:    location of page table
 *      rtbl:   address to be used for first level page table entry (typically tbl + PAGE_SIZE)
 *      vstart: virtual address of start of range
 *      vend:   virtual address of end of range - we map [vstart, vend - 1]
 *      flags:  flags to use to map last level entries
 *      phys:   physical address corresponding to vstart - physical memory is contiguous
 *      order:  #imm 2log(number of entries in PGD table)
 *
 * If extra_shift is set, an extra level will be populated if the end address does
 * not fit in 'extra_shift' bits. This assumes vend is in the TTBR0 range.
 *
 * Temporaries: istart, iend, tmp, count, sv - these need to be different registers
 * Preserves:   vstart, flags
 * Corrupts:    tbl, rtbl, vend, istart, iend, tmp, count, sv
 */
U64 * map_memory(U64 * tbl, const U64 vstart, U64 vend, U64 flags, const U64 phys, int order)
{
	vend = vend - 1;
	U64 * rtbl = tbl + (PAGE_SIZE / sizeof(U64));	// ((u8*)tbl) + PAGE_SIZE

	U64 istart = 0;
	U64 iend = 0;
	U64	count = 0;

	printf("LV1==========================================================\n");
	/*
	 * compute_indices(vstart, vend, 39, 9, &istart, &iend, &count);
	 */
	compute_indices(vstart, vend, PGDIR_SHIFT, order, &istart, &iend, &count);
	U64 *sv = rtbl;

	populate_entries(tbl, &rtbl, istart, iend, PMD_TYPE_TABLE, PAGE_SIZE);
	tbl = sv;

	printf("LV2==========================================================\n");
	/*
	 * compute_indices(vstart, vend, 30, 9, &istart, &iend, &count)
	 */
	// istart = iend = 0;
	compute_indices(vstart, vend, SWAPPER_TABLE_SHIFT, PAGE_SHIFT - 3, &istart, &iend, &count);
	sv = rtbl;
	populate_entries(tbl, &rtbl, istart, iend, PMD_TYPE_TABLE, PAGE_SIZE);
	tbl = sv;

	printf("LV3==========================================================\n");
	/*
	 * compute_indices(vstart, vend, 29, 9, &istart, &iend, &count)
	 */
	compute_indices(vstart, vend, SWAPPER_BLOCK_SHIFT, PAGE_SHIFT - 3, &istart, &iend, &count);

	/*
	 * bic rtbl, phys, 0x1f_ffff 하위 12비트 클리어.
	 */
	rtbl = (U64 *)(phys & ~((U64)SWAPPER_BLOCK_SIZE - 1));// bic rtbl, phys, SWAPPER_BLOCK_SIZE - 1
	populate_entries(tbl, &rtbl, istart, iend, flags, SWAPPER_BLOCK_SIZE);

	return tbl;
}


int main(void)
{
	// const U64 INIT_IDMAP_DIR_SIZE = (INIT_IDMAP_DIR_PAGES * PAGE_SIZE);
	U64 * init_idmap_pg_dir = (U64 *)0;

	U64 * x0 = init_idmap_pg_dir;
	// U64	x3 = 0xffff800008000000;	// _text = 0xffff800008000000
	// U64 x6 = 0xffff80000aaf0000;	// _end + MAX_FDT_SIZE + SWAPPER_BLOCK_SIZE;

	U64 x3 = 0x40200000;			//  <_text>
	U64	x6 = 0x42cf0000;			// _end + MAX_FDT_SIZE + SWAPPER_BLOCK_SIZE;
	U64 x7 = SWAPPER_RX_MMUFLAGS;	// 0x781
	/*
	 * map_memory(init_idmap_pg_dir, _text, _end + MAX_FDT_SIZE + SWAPPER_BLOCK_SIZE,
	 *	SWAPPER_RX_MMUFLAGS, _text, IDMAP_PGD_ORDER)
	 */
	map_memory(x0, x3, x6, x7, x3, IDMAP_PGD_ORDER);

	return 0;
}
