#include <stdio.h>
#include <stdlib.h>

typedef unsigned long long U64;

#define	OUT
#define	IN
#define	IN_OUT

#define	IDMAP_PGD_ORDER			9	// 48-39
#define	PAGE_SIZE		 		4096	// 4k
#define	PGDIR_SHIFT  	 		39
#define	PMD_TYPE_TABLE	 		3
#define	SWAPPER_BLOCK_SIZE		0x200000	//     SZ_2M; 
#define	MAX_FDT_SIZE			0x200000   //     SZ_2M; 
#define	SWAPPER_RW_MMUFLAGS		0x701
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
 * Remap a subregion created with the map_memory macro with modified attributes
 * or output address. The entire remapped region must have been covered in the
 * invocation of map_memory.
 *
 * x0: last level table address (returned in first argument to map_memory)
 * x1: start VA of the existing mapping
 * x2: start VA of the region to update
 * x3: end VA of the region to update (exclusive)
 * x4: start PA associated with the region to update
 * x5: attributes to set on the updated region
 * x6: order of the last level mappings
 */
U64 remap_region(U64 x0_tbl, U64 x1_saddr, U64 x2_vstart, U64 x3_vend, U64 x4_phys, U64 x5_shift, U64 x6_order)
{
	printf("START\t0[%p] 1[%p], 2[%p], 3[%p], 4[%p], 5[%p], x6[%p]\n",
		x0_tbl, x1_saddr, x2_vstart, x3_vend, x4_phys, x5_shift, x6_order);

	/*
	 *x0             0x41ce2000
	 *x1             0x40200000	_text
	 *x2             0x428e8000	init_pg_dir
	 *x3             0x428ed000	init_pg_end
	 *x4             0x42800000	init_pg_end & ~(0xfffff)
	 *x5             0x701
	 *x6             21	
	 */
	x3_vend--;		// make end inclusive	init_pg_end - 1
	x1_saddr >>= x6_order;	// _text >> SWAPPER_BLOCK_SHIFT;

	// Get the index offset for the start of the last level table
	// bfi     x1, xzr, #0, PAGE_SHIFT - 3
	x1_saddr >>= (PAGE_SHIFT - 3);
	x1_saddr <<= (PAGE_SHIFT - 3);

	// Derive the start and end indexes into the last level table
	// associated with the provided region
	x2_vstart >>= x6_order;
	x3_vend >>= x6_order;
	x2_vstart -= x1_saddr;	// init_pg_dir - _text
	x3_vend -= x1_saddr;	// init_pg_end - _text;

	x1_saddr = 1;
	x6_order = x1_saddr << x6_order;		// block size at this level	SZ_2M

	printf("END \t0[%p] 1[%llu], 2[%llu], 3[%llu], 4[%p], 5[%p], x6[%p]\n",
		x0_tbl, x1_saddr, x2_vstart, x3_vend, x4_phys, x5_shift, x6_order);

	// populate_entries(U64 * tbl, IN_OUT U64 ** rtbl, U64 index, U64 eindex,
	populate_entries((U64 *)x0_tbl, (U64 **)&x4_phys, x2_vstart, x3_vend, x5_shift, x6_order);
}


int main()
{
	/*
	 *x0             0x41ce2000
	 *x1             0x40200000
	 *x2             0x428e8000
	 *x3             0x428ed000
	 *x4             0x42800000
	 *x5             0x701     
	 *x6             21
	 *x7             0x781
	 */
	// populate_entries(U64 * tbl, IN_OUT U64 ** rtbl, U64 index, U64 eindex,
	U64 x0_tbl = 0x2000;		// 0x41ce2000 - _text
	U64 x1_saddr = 0x41ce2000;	// _text
	U64 x2_vstart = 0x428e8000;	// init_pg_dir;
	U64 x3_vend = 0x428ed000;	// init_pg_end;
	U64 x4_phys = x2_vstart & ~(SWAPPER_BLOCK_SIZE - 1);
	printf("init_pg_dir~ init_pg_end remap_region=========================\n");
	remap_region(x0_tbl, x1_saddr, x2_vstart, x3_vend, x4_phys, SWAPPER_RW_MMUFLAGS, SWAPPER_BLOCK_SHIFT);

	U64 x21_fdt = 0x48000000;	// FDT address
	x1_saddr = 0x41ce2000;			// _text
	U64 x22_fdt_addr = 0x42ce0000;		// _end + SWAPPER_BLOCK_SIZE;
	x2_vstart = x22_fdt_addr & ~(0x1ff);			// bic x2, x22, #SWAPPER_BLOCK_SIZE - 1
	/* remapped FDT address
	*/
	x22_fdt_addr = x21_fdt & ~(0x1ff);		// bfi x22, x21, #0, #SWAPPER_BLOCK_SHIFT
	x3_vend = x2_vstart + MAX_FDT_SIZE + SWAPPER_BLOCK_SIZE;
	x4_phys = x21_fdt & ~(0x1ff);		// bic x4, x21, #SWAPPER_BLOCK_SIZE - 1
	printf("FDT remap_region==============================================\n");
	remap_region(x0_tbl, x1_saddr, x2_vstart, x3_vend, x4_phys, SWAPPER_RW_MMUFLAGS, SWAPPER_BLOCK_SHIFT);
	return 0;
}
