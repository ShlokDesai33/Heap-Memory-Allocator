#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <stdio.h>
#include <string.h>
#include "p3Heap.h"
 
/*
 * This structure serves as the header for each allocated and free block.
 * It also serves as the footer for each free block but only containing size.
 */
typedef struct blockHeader {

    int size_status;

    /*
     * Size of the block is always a multiple of 8.
     * Size is stored in all block headers and in free block footers.
     *
     * Status is stored only in headers using the two least significant bits.
     *   Bit0 => least significant bit, last bit
     *   Bit0 == 0 => free block
     *   Bit0 == 1 => allocated block
     *
     *   Bit1 => second last bit
     *   Bit1 == 0 => previous block is free
     *   Bit1 == 1 => previous block is allocated
     * 
     * End Mark:
     *  The end of the available memory is indicated using a size_status of 1.
     * 
     */
} blockHeader;

/* Global variable. It should always point to the first block,
 * i.e., the block at the lowest address.
 */
blockHeader *heap_start = NULL;

/* Size of heap allocation padded to round to nearest page size.
 */
int alloc_size;

 
/* 
 * Function for allocating 'size' bytes of heap memory.
 * Argument size: requested size for the payload
 * Returns address of allocated block (payload) on success.
 * Returns NULL on failure.
 *
 * - Uses BEST-FIT PLACEMENT POLICY to chose a free block
 */
void* balloc(int size) {
    if (size <= 0 || size > alloc_size) {
        return NULL;
    }
    // calculate block size to be allocated
    int block_size = size + sizeof(blockHeader);
    // add padding
    if (block_size % 8 != 0) {
        block_size += 8 - (block_size % 8);
    }

    // frontier traverses through the heap
    blockHeader *frontier = heap_start;
    // best_fit points to the best fit block
    blockHeader *best_fit = NULL;
    // raw size of best fit block
    int bf_size;

    while (frontier->size_status != 1) {
        int rem = frontier->size_status % 8;
        // fr_size is the frontier's block size minus the status bits
        int fr_size = (frontier->size_status) - rem;

        if (rem == 0 || rem == 2) {
            if (fr_size >= block_size) {
                // check for exact match
                if (fr_size == block_size) {
                    // update header
                    frontier->size_status += 1;
                    // next points to the next block after frontier
                    blockHeader *next = (blockHeader *)((char *)frontier + fr_size);
                    // if next block is not end mark, update its p-bit
                    if (next->size_status != 1) {
                        next->size_status += 2;
                    }
                    return (void *) (frontier + 1);
                }
                // assign best fit
                else if (best_fit == NULL || fr_size < bf_size) {
                    best_fit = frontier;
                    bf_size = fr_size;
                }
            }
        }

        // move to next block
        frontier = (blockHeader *)((char *)frontier + fr_size);
    }

    // if best fit is null, all blocks are allocated
    if (best_fit == NULL) {
        return NULL;
    }

    // exact match was not found
    // split best fit
    blockHeader *free_block = (blockHeader *)((char *)best_fit + block_size);
    free_block->size_status = bf_size - block_size + 2;

    // update best fit header
    best_fit->size_status = block_size + (best_fit->size_status % 8) + 1;
    
    return (void *) (best_fit + 1);
}
 
/* 
 * Function for freeing up a previously allocated block.
 * Argument ptr: address of the block to be freed up.
 * Returns 0 on success.
 * Returns -1 on failure.
 */                    
int bfree(void *ptr) {
    if (ptr == NULL) {
        return -1;
    }
    if ((int)ptr % 8 != 0) {
        return -1;
    }

    if (ptr < (void *)heap_start || ptr > (void *)((char *)heap_start + alloc_size)) {
        return -1;
    }

    // get pointer to the block header
    blockHeader *block = (blockHeader *)((char *)ptr - sizeof(blockHeader));
    int rem = block->size_status % 8;

    if (rem == 0 || rem == 2) {
        return -1;
    }

    // update next block's header
    blockHeader *next = (blockHeader *)((char *)block + ((block->size_status) - rem));

    if (next->size_status != 1) {
        next->size_status -= 2;
    }

    // update header
    block->size_status -= 1;

    return 0;
}

/*
 * Function for traversing heap block list and coalescing all adjacent
 * free blocks.
 *
 * This function is used for delayed coalescing.
 */
int coalesce() {
    // frontier traverses through the heap
    blockHeader *frontier = heap_start;
    int count = 0;

    while (frontier->size_status != 1) {
        int rem = frontier->size_status % 8;
        // fr_size is the frontier's block size minus the status bits
        int fr_size = (frontier->size_status) - rem;

        if (rem == 0 || rem == 2) {
            // next points to the next block after frontier
            blockHeader *next = (blockHeader *)((char *)frontier + fr_size);

            while (next->size_status != 1 && next->size_status % 8 == 0) {
                fr_size += (next->size_status);
                next = (blockHeader *)((char *)next + (next->size_status));
                count = 1;
            }

            frontier->size_status = fr_size + rem;
        }

        frontier = (blockHeader *)((char *)frontier + fr_size);
    }
    // count is 1 if coalescing was done
    return count;
}

 
/* 
 * Function used to initialize the memory allocator.
 * Argument sizeOfRegion: the size of the heap space to be allocated.
 * Returns 0 on success.
 * Returns -1 on failure.
 */                    
int init_heap(int sizeOfRegion) {
 
    static int allocated_once = 0; //prevent multiple myInit calls
 
    int pagesize;   // page size
    int padsize;    // size of padding when heap size not a multiple of page size
    void* mmap_ptr; // pointer to memory mapped area
    int fd;

    blockHeader* end_mark;
  
    if (0 != allocated_once) {
        fprintf(stderr, 
        "Error:mem.c: InitHeap has allocated space during a previous call\n");
        return -1;
    }

    if (sizeOfRegion <= 0) {
        fprintf(stderr, "Error:mem.c: Requested block size is not positive\n");
        return -1;
    }

    // Get the pagesize
    pagesize = getpagesize();

    // Calculate padsize as the padding required to round up sizeOfRegion 
    // to a multiple of pagesize
    padsize = sizeOfRegion % pagesize;
    padsize = (pagesize - padsize) % pagesize;

    alloc_size = sizeOfRegion + padsize;

    // Using mmap to allocate memory
    fd = open("/dev/zero", O_RDWR);
    if (-1 == fd) {
        fprintf(stderr, "Error:mem.c: Cannot open /dev/zero\n");
        return -1;
    }
    mmap_ptr = mmap(NULL, alloc_size, PROT_READ | PROT_WRITE, MAP_PRIVATE, fd, 0);
    if (MAP_FAILED == mmap_ptr) {
        fprintf(stderr, "Error:mem.c: mmap cannot allocate space\n");
        allocated_once = 0;
        return -1;
    }
  
    allocated_once = 1;

    // for double word alignment and end mark
    alloc_size -= 8;

    // Initially there is only one big free block in the heap.
    // Skip first 4 bytes for double word alignment requirement.
    heap_start = (blockHeader*) mmap_ptr + 1;

    // Set the end mark
    end_mark = (blockHeader*)((void*)heap_start + alloc_size);
    end_mark->size_status = 1;

    // Set size in header
    heap_start->size_status = alloc_size;

    // Set p-bit as allocated in header
    // note a-bit left at 0 for free
    heap_start->size_status += 2;

    // Set the footer
    blockHeader *footer = (blockHeader*) ((void*)heap_start + alloc_size - 4);
    footer->size_status = alloc_size;
  
    return 0;
} 
                  
/* 
 * Function to be used for DEBUGGING to help you visualize your heap structure.
 * Prints out a list of all the blocks including this information:
 * No.      : serial number of the block 
 * Status   : free/used (allocated)
 * Prev     : status of previous block free/used (allocated)
 * t_Begin  : address of the first byte in the block (where the header starts) 
 * t_End    : address of the last byte in the block 
 * t_Size   : size of the block as stored in the block header
 */                     
void disp_heap() {
 
    int counter;
    char status[6];
    char p_status[6];
    char *t_begin = NULL;
    char *t_end   = NULL;
    int t_size;

    blockHeader *current = heap_start;
    counter = 1;

    int used_size = 0;
    int free_size = 0;
    int is_used   = -1;

    fprintf(stdout, 
	"*********************************** Block List **********************************\n");
    fprintf(stdout, "No.\tStatus\tPrev\tt_Begin\t\tt_End\t\tt_Size\n");
    fprintf(stdout, 
	"---------------------------------------------------------------------------------\n");
  
    while (current->size_status != 1) {
        t_begin = (char*)current;
        t_size = current->size_status;
    
        if (t_size & 1) {
            // LSB = 1 => used block
            strcpy(status, "alloc");
            is_used = 1;
            t_size = t_size - 1;
        } else {
            strcpy(status, "FREE ");
            is_used = 0;
        }

        if (t_size & 2) {
            strcpy(p_status, "alloc");
            t_size = t_size - 2;
        } else {
            strcpy(p_status, "FREE ");
        }

        if (is_used) 
            used_size += t_size;
        else 
            free_size += t_size;

        t_end = t_begin + t_size - 1;
    
        fprintf(stdout, "%d\t%s\t%s\t0x%08lx\t0x%08lx\t%4i\n", counter, status, 
        p_status, (unsigned long int)t_begin, (unsigned long int)t_end, t_size);
    
        current = (blockHeader*)((char*)current + t_size);
        counter = counter + 1;
    }

    fprintf(stdout, 
	"---------------------------------------------------------------------------------\n");
    fprintf(stdout, 
	"*********************************************************************************\n");
    fprintf(stdout, "Total used size = %4d\n", used_size);
    fprintf(stdout, "Total free size = %4d\n", free_size);
    fprintf(stdout, "Total size      = %4d\n", used_size + free_size);
    fprintf(stdout, 
	"*********************************************************************************\n");
    fflush(stdout);

    return;  
} 
