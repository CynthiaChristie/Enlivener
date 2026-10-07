

#define PIVOT_SORT_MIN	32

// These constants all have two pieces of information:
//
//   - If one of the values is a pivot, and if so, which, and
//   - Whether to keep the current order, or swap it
//
// SORT_SWAP is there so the intent appears lexically correct, and also because my current app has a LOT
// of sorters the use the original 3 SORT_ that don't reference a pivot.  However, swap will pivot regardless.
// If a swap is needed, the out-of-order pointer will be the the pivot unless given SORT_PIVOT_FIRST_SWAP
// The sorter function can't say 'swap but don't pivot' - there is not and should not be code for that

#define SORT_PIVOT_FIRST_SWAP	2	// Needs swap, use first pointer as pivot
#define SORT_PIVOT_SECOND_SWAP	3	// Needs swap, use second pointer as pivot
#define SORT_PIVOT_FIRST_KEEP	4	// Is in order, but use first pointer as pivot
#define SORT_PIVOT_SECOND_KEEP	5	// Is in order, but use second pointer as pivot
#define SORT_PIVOT_EQUAL	6	// Pointers are are equal, pick one to be pivot (in practice, 1st)

#define SORT_EQUAL		0	// Pointers are equal, no pivot here
#define SORT_KEEP		1	// Is in order, no pivot here
#define SORT_SWAP		SORT_PIVOT_SECOND_SWAP // Not in order, but of course pivot

