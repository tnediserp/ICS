/*
 * mm-optimize2.c
 * 分离适配
 * 去脚部
 * realloc优化，吸收两侧空闲块
 * place优化，小块在前部分配，大块在后部分配
 *
 * NOTE TO STUDENTS: Replace this header comment with your own header
 * comment that gives a high level description of your solution.
 */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "mm.h"
#include "memlib.h"

/* If you want debugging output, use the following macro.  When you hand
 * in, remove the #define DEBUG line. */
#define DEBUG 1
#ifdef DEBUG
# define dbg_printf(...) printf(__VA_ARGS__)
#else
# define dbg_printf(...)
#endif

/* do not change the following! */
#ifdef DRIVER
/* create aliases for driver tests */
#define malloc mm_malloc
#define free mm_free
#define realloc mm_realloc
#define calloc mm_calloc
#endif /* def DRIVER */

/* single word (4) or double word (8) alignment */
#define ALIGNMENT 8
#define DSIZE 8
#define WSIZE 4
#define CHUNKSIZE 1<<12
#define LINK_NUM 32
#define LINK_HDRS_SIZE LINK_NUM*WSIZE 
#define UNSIGNED_MAX 0xffffffff
#define PLACE_BUNDARY 160

/* rounds up to the nearest multiple of ALIGNMENT */
#define ALIGN(p) (((size_t)(p) + (ALIGNMENT-1)) & ~0x7)

#define MAX(x, y) ((x) > (y)? (x) : (y))  

/* Pack a size and allocated bit into a word */
#define PACK(size, alloc)  ((size) | (alloc)) 

/* Read and write a word at address p */
#define GET(p)       (*(unsigned int *)(p))            
#define PUT(p, val)  (*(unsigned int *)(p) = (val))    

/* Read the size and allocated fields from address p */
#define GET_SIZE(p)  (GET(p) & ~0x7)                   
#define GET_ALLOC(p) (GET(p) & 0x1)            
#define GET_PREV_ALLOC(p) ((GET(p) >> 1) & 0x1)        

/* Given block ptr bp, compute address of its header and footer */
#define HDRP(bp)       ((char *)(bp) - WSIZE)                      
#define FTRP(bp)       ((char *)(bp) + GET_SIZE(HDRP(bp)) - DSIZE) 

/* 设置prev_alloc域 */
#define SET_PREV_ALLOC(bp) (PUT(bp, PACK(GET_SIZE(bp), GET_ALLOC(bp) | 2)))
#define SET_PREV_FREE(bp)  (PUT(bp, PACK(GET_SIZE(bp), GET_ALLOC(bp) & (~2))))

/* Given block ptr bp, compute address of next and previous blocks */
#define NEXT_BLKP(bp)  ((char *)(bp) + GET_SIZE(((char *)(bp) - WSIZE))) 
#define PREV_BLKP(bp)  ((char *)(bp) - GET_SIZE(((char *)(bp) - DSIZE))) 

/* 给定bp, 求出它的prev,next指针 */
#define NEXTP_DOMAIN(bp) ((char *)(bp))
#define PREVP_DOMAIN(bp) ((char *)(bp) + WSIZE)

/* 将地址偏移量转换为实际地址，UNSIGNED_MAX对应于NULL */
#define MEM_LOC(p) ((unsigned int)p == UNSIGNED_MAX ? NULL : (char *)(heap_listp + (size_t)(p)))
#define OFFSET(p) (p == NULL ? UNSIGNED_MAX : (unsigned int)((char *)p - heap_listp))

/* 寻找空闲块在空闲链表中的前驱和后继 */
#define NEXT_FREE_BLKP(bp)  ((char *) MEM_LOC(*(unsigned int *)(bp)))
#define PREV_FREE_BLKP(bp)  ((char *) MEM_LOC(*((unsigned int *)(bp) + 1)))

#define GET_ADDR(p)         ((char *) *(size_t *)(p))
#define PUT_ADDR(p, val)    (*(size_t *)(p) = (size_t)(val))


static char *heap_listp = 0;  /* Pointer to first block */  

/* Function prototypes for internal helper routines */
static void *extend_heap(size_t words);
static void *place(void *bp, size_t asize);
static void anti_place(void *bp, size_t asize);
static void *find_fit(size_t asize);
static void *coalesce(void *bp);
static void *anti_coalesce(void *bp, size_t asize);
static void *insert_blkp(void *bp, void *loc);
static void remove_blkp(void *bp);
static int log_2(unsigned int x);

/*
 * Initialize: return -1 on error, 0 on success.
 */
int mm_init(void) 
{
    /* Create the initial empty heap 
     * Link head pointer    4 * 32 = 128 Bytes
     * Alignment padding    4 Bytes
     * Prologue header      4 Bytes
     * Prologue next        4 Bytes
     * Prologue footer      4 Bytes
     * Epilogue header      4 Bytes
     * Epilogue prev        8 Bytes
     */
    if ((heap_listp = mem_sbrk(34*WSIZE)) == (void *)-1) 
        return -1;

    int i;
    /* 这部分可能还涉及到前驱和后继指针的初始化处理，应添加 
     * 序言块是否可以去脚部？
     * 应该可以，而且序言块似乎没有用，直接去掉
     */                                 
    for(i=0; i < LINK_NUM; i++)
    {
        PUT(heap_listp + WSIZE + i*WSIZE, UNSIGNED_MAX);
    }
    //PUT(heap_listp + LINK_HDRS_SIZE, UNSIGNED_MAX);                            /* Alignment padding */
    PUT(heap_listp + LINK_HDRS_SIZE, PACK(0, 1));                    /* Prologue header */ 
    //PUT_ADDR(heap_listp + (2*WSIZE) + LINK_HDRS_SIZE, NULL);                         /* 头结点next指针 */
    //PUT(heap_listp + (2*WSIZE) + LINK_HDRS_SIZE, PACK(DSIZE, 1));                    /* Prologue footer */ 
    PUT(heap_listp + WSIZE + LINK_HDRS_SIZE, PACK(0, 3));                        /* Epilogue header */
    //heap_listp += (2*WSIZE);                     
    
    /* Extend the empty heap with a free block of CHUNKSIZE bytes */
    char *bp;
    if ((bp = extend_heap(CHUNKSIZE/WSIZE)) == NULL) 
        return -1;

    return 0;
}

/*
 * malloc
 */
void *malloc (size_t size) 
{
    size_t asize;      /* Adjusted block size */
    size_t extendsize; /* Amount to extend heap if no fit */
    char *bp;      

    if (heap_listp == 0){
        mm_init();
    }
    /* Ignore spurious requests */
    if (size == 0)
        return NULL;

    /* Adjust block size to include overhead and alignment reqs. 
     * asize需要考虑修改
     * 一个块至少16字节，其中head, foot各4字节，prev, next各4字节，
     * 但是因为非空闲块不需要维护prev, next指针，所以可以暂时把它们
     * 的空间当成有效负载，回收的时候再维护这两个指针。
     */
    if (size <= DSIZE + WSIZE)                                          
        asize = 2*DSIZE;                                      
    else
        asize = DSIZE * ((size + (WSIZE) + (DSIZE-1)) / DSIZE); //非空闲块可以去脚部


    /* Search the free list for a fit */
    if ((bp = find_fit(asize)) != NULL) {  
        char *ptr = place(bp, asize);     
        return ptr;
    }

    /* No fit found. Get more memory and place the block */
    extendsize = MAX(asize,CHUNKSIZE);                 
    if ((bp = extend_heap(extendsize/WSIZE)) == NULL)  
        return NULL;                                  
    char *ptr = place(bp, asize);                                 
    return ptr;
}

/*
 * free
 * 非空闲块 -> 空闲块流程
 * 改变当前块的alloc域
 * 维护当前块的prev_alloc域
 * 改变下一个块的prev_alloc域
 */
void free (void *ptr) 
{
    if (ptr == 0) 
        return;

    size_t size = GET_SIZE(HDRP(ptr));
    size_t prev_alloc = GET_PREV_ALLOC(HDRP(ptr));
    if (heap_listp == 0){
        mm_init();
    }

    /* prev, next指针的问题，在coalesce中解决？ */
    PUT(HDRP(ptr), PACK(size, prev_alloc << 1));
    PUT(FTRP(ptr), PACK(size, prev_alloc << 1));
    
    coalesce(ptr);
}

/*
 * realloc - you may want to look at mm-naive.c
 */
void *realloc(void *oldptr, size_t size) 
{
    size_t oldsize, asize;
    void *newptr;

    /* If size == 0 then this is just free, and we return NULL. */
    if(size == 0) {
        mm_free(oldptr);
        return 0;
    }

    /* If oldptr is NULL, then this is just malloc. */
    if(oldptr == NULL) {
        return mm_malloc(size);
    }

    oldsize = GET_SIZE(HDRP(oldptr));

    if (size <= DSIZE + WSIZE)                                          
        asize = 2*DSIZE;                                      
    else
        asize = DSIZE * ((size + (WSIZE) + (DSIZE-1)) / DSIZE);

    if(asize <= oldsize)
    {
        //anti_place(oldptr, asize);
        return oldptr;
    }
        

    else
    {
        newptr = anti_coalesce(oldptr, asize);
        if(GET_SIZE(HDRP(newptr)) >= asize)
        {
            if(newptr == oldptr)
            {
                anti_place(newptr, asize);
                return newptr;
            }
                
            else
            {
                memcpy(newptr, oldptr, size);
                anti_place(newptr, asize);
                return newptr;
            }
        }

        else
        {
            newptr = mm_malloc(size);

            /* If realloc() fails the original block is left untouched  */
            if(!newptr) {
                return 0;
            }

            /* Copy the old data. */
            oldsize = GET_SIZE(HDRP(oldptr));
            if(size < oldsize) oldsize = size;
            memcpy(newptr, oldptr, oldsize);

            /* Free the old block. */
            mm_free(oldptr);

            return newptr;
        }
    }

    
}

/*
 * calloc - you may want to look at mm-naive.c
 * This function is not tested by mdriver, but it is
 * needed to run the traces.
 */
void *calloc (size_t nmemb, size_t size) 
{
    size_t bytes = nmemb * size;
    void *newptr;

    newptr = malloc(bytes);
    memset(newptr, 0, bytes);

    return newptr;
}


/*
 * Return whether the pointer is in the heap.
 * May be useful for debugging.
 */
static int in_heap(const void *p) 
{
    return p <= mem_heap_hi() && p >= mem_heap_lo();
}

/*
 * Return whether the pointer is aligned.
 * May be useful for debugging.
 */
static int aligned(const void *p) 
{
    return (size_t)ALIGN(p) == (size_t)p;
}

/*
 * mm_checkheap
 */
void mm_checkheap(int lineno) 
{
    printf("---cur line:%d empty blocks:\n",lineno);
    char *tmpP = heap_listp;
    int count_empty_block = 0;
    while(tmpP != NULL)
    {
        count_empty_block++;
        if(!GET_ALLOC(HDRP(tmpP)))
        {
            printf("address:%lx size:%d \n",(size_t)tmpP,GET_SIZE(HDRP(tmpP)));
        }
        
        tmpP = NEXT_FREE_BLKP(tmpP);
    }
    printf("empty_block num: %d\n",count_empty_block);
}

static void *extend_heap(size_t words) 
{
    char *bp;
    size_t size, prev_alloc;

    /* Allocate an even number of words to maintain alignment */
    size = (words % 2) ? (words+1) * WSIZE : words * WSIZE; 

    if ((long)(bp = mem_sbrk(size)) == -1)  
        return NULL;                                        

    prev_alloc = GET_PREV_ALLOC(HDRP(bp));

    /* Initialize free block header/footer and the epilogue header */
    PUT(HDRP(bp), PACK(size, prev_alloc << 1));         /* Free block header */   
    PUT(FTRP(bp), PACK(size, prev_alloc << 1));         /* Free block footer */   
    PUT(HDRP(NEXT_BLKP(bp)), PACK(0, 1)); /* New epilogue header */ 

    /* Coalesce if the previous block was free */
    return coalesce(bp);                                          
}

static int log_2(unsigned int x)
{
    int l = 0, r = 31, mid, ans = 0;
    while(l <= r)
    {
        mid = (l + r) >>1;
        if((((unsigned int) 1) << mid) <= x)
        {
            ans = mid;
            l = mid + 1;
        }
        else
        {
            r = mid - 1;
        }
    }
    return ans;
}

/*
 * insert_blkp
 * 在显式空闲链表表项loc后面插入空闲块
 * 还要考虑一下边界情况和非法情况
 */
static void *insert_blkp(void *bp, void *loc)
{
    char *next = NEXT_FREE_BLKP(loc);
    PUT(NEXTP_DOMAIN(bp), OFFSET(next));
    PUT(PREVP_DOMAIN(bp), OFFSET(loc));
    if(next != NULL)
    {
        PUT(PREVP_DOMAIN(next), OFFSET(bp)); 
    }
    PUT(NEXTP_DOMAIN(loc), OFFSET(bp));
    return bp;
}

/*
 * remove_blkp
 * 从空闲链表中删除bp指向的空闲块
 * 还要考虑一下边界情况和非法情况
 */
static void remove_blkp(void *bp)
{
    char *prev = PREV_FREE_BLKP(bp);
    char *next = NEXT_FREE_BLKP(bp);

    PUT(NEXTP_DOMAIN(prev), OFFSET(next));
    if(next != NULL)
    {
        PUT(PREVP_DOMAIN(next), OFFSET(prev));
    }
}

/*
 * coalesce - Boundary tag coalescing. Return ptr to coalesced block
 */
static void *coalesce(void *bp) 
{
    size_t prev_alloc = GET_PREV_ALLOC(HDRP(bp));
    size_t next_alloc = GET_ALLOC(HDRP(NEXT_BLKP(bp)));
    size_t size = GET_SIZE(HDRP(bp));
    int index;


    /* Case 1 
     * prev_alloc=1, next_alloc=1
     * 直接将当前块插入适当的显式空闲链表
     */
    if (prev_alloc && next_alloc) 
    {     
        char *next_blkp = NEXT_BLKP(bp);
        SET_PREV_FREE(HDRP(next_blkp));

        index = log_2(size);
        insert_blkp(bp, heap_listp + index*WSIZE);       

        return bp;
    }

    /* Case 2 
     * prev_alloc=1, next_alloc=0
     * 当前块大小增加；
     * 从空闲链表中删除NEXT_BLKP;
     * 当前块插入空闲链表
     */
    else if (prev_alloc && !next_alloc) 
    {      
        char *next_blkp = NEXT_BLKP(bp);
        remove_blkp(next_blkp);
        size += GET_SIZE(HDRP(NEXT_BLKP(bp)));
        PUT(HDRP(bp), PACK(size, prev_alloc << 1));
        PUT(FTRP(bp), PACK(size, prev_alloc << 1));

        index = log_2(size);
        insert_blkp(bp, heap_listp + index*WSIZE);
    }

    /* Case 3 
     * prev_alloc=0, next_alloc=1
     * PREV_BLKP大小增加；
     * 原链表删除PREV_BLKP
     * 插入适当的空闲链表
     */
    else if (!prev_alloc && next_alloc) 
    {      
        char *prev_blkp = PREV_BLKP(bp);
        char *next_blkp = NEXT_BLKP(bp);
        remove_blkp(prev_blkp);

        SET_PREV_FREE(HDRP(next_blkp));

        size_t prev_prev_alloc = GET_PREV_ALLOC(HDRP(PREV_BLKP(bp)));
        size += GET_SIZE(HDRP(PREV_BLKP(bp)));
        PUT(FTRP(bp), PACK(size, prev_prev_alloc << 1));
        PUT(HDRP(PREV_BLKP(bp)), PACK(size, prev_prev_alloc << 1));
        
        bp = PREV_BLKP(bp);
        index = log_2(size);
        insert_blkp(bp, heap_listp + index*WSIZE);
    }

    /* Case 4
     * prev_alloc=0, next_alloc=0
     * 大小全部加到PREV_BLKP上面
     * 从空闲链表删除NEXT_BLKP 
     * 原链表删除PREV_BLKP
     * 插入适当的空闲链表
     */
    else 
    {       
        size_t prev_prev_alloc = GET_PREV_ALLOC(HDRP(PREV_BLKP(bp)));
        char *prev_blkp = PREV_BLKP(bp);
        char *next_blkp = NEXT_BLKP(bp);
        remove_blkp(prev_blkp);
        remove_blkp(next_blkp);                              
        size += GET_SIZE(HDRP(PREV_BLKP(bp))) + 
            GET_SIZE(FTRP(NEXT_BLKP(bp)));
        PUT(HDRP(PREV_BLKP(bp)), PACK(size, prev_prev_alloc << 1));
        PUT(FTRP(NEXT_BLKP(bp)), PACK(size, prev_prev_alloc << 1));
        
        bp = PREV_BLKP(bp);
        index = log_2(size);
        insert_blkp(bp, heap_listp + index*WSIZE);
    }
#ifdef NEXT_FIT
    /* Make sure the rover isn't pointing into the free block */
    /* that we just coalesced */
    if ((rover > (char *)bp) && (rover < NEXT_BLKP(bp))) 
        rover = bp;
#endif
    return bp;
}

/*
 * anti_coalesce, 用于realloc.
 * 块指针bp, 想要realloc出asize大小。查看bp后面的块和前面的块，如果
 * 有空闲，并且合并后总大小达到asize, 就合并，并且做相应处理，
 * 返回新的块指针。
 */
static void *anti_coalesce(void *bp, size_t asize)
{
    size_t prev_alloc = GET_PREV_ALLOC(HDRP(bp));
    size_t next_alloc = GET_ALLOC(HDRP(NEXT_BLKP(bp)));
    size_t size;


    /* Case 1 
     * prev_alloc=1, next_alloc=1
     * 此时失败。
     */
    if (prev_alloc && next_alloc) 
    {            
        return bp;
    }

    /* Case 2 
     * prev_alloc=1, next_alloc=0
     * 判断，如果加上NEXT_BLKP之后大小达到要求，就合并。
     * 当前块大小增加；
     * 从空闲链表中删除NEXT_BLKP;
     * NEXT_NEXT_BLKP的prev_alloc域修改。
     */
    else if (prev_alloc && !next_alloc) 
    {      
        char *next_blkp = NEXT_BLKP(bp);
        size = GET_SIZE(HDRP(bp)) + GET_SIZE(HDRP(next_blkp));
        
        if(size >= asize)
        {
            remove_blkp(next_blkp);
            char *next_next_blkp = NEXT_BLKP(next_blkp);
            PUT(HDRP(bp), PACK(size, (prev_alloc << 1) | 1));
            SET_PREV_ALLOC(HDRP(next_next_blkp));
        }
    }

    /* Case 3 
     * prev_alloc=0, next_alloc=1
     * PREV_BLKP大小增加；
     * 原链表删除PREV_BLKP
     */
    else if (!prev_alloc && next_alloc) 
    {      
        char *prev_blkp = PREV_BLKP(bp);

        size_t prev_prev_alloc = GET_PREV_ALLOC(HDRP(prev_blkp));
        size = GET_SIZE(HDRP(bp)) + GET_SIZE(HDRP(prev_blkp));
        
        if(size >= asize)
        {
            remove_blkp(prev_blkp);
            PUT(HDRP(prev_blkp), PACK(size, (prev_prev_alloc << 1) | 1));
            bp = prev_blkp;
        }
    }

    /* Case 4
     * prev_alloc=0, next_alloc=0
     * 大小全部加到PREV_BLKP上面
     * 从空闲链表删除NEXT_BLKP 
     * 原链表删除PREV_BLKP
     * 修改NEXT_NEXT_BLKP的prev_alloc域
     */
    else 
    {       
        char *next_blkp = NEXT_BLKP(bp); 
        char *prev_blkp = PREV_BLKP(bp);
        size_t prev_prev_alloc = GET_PREV_ALLOC(HDRP(prev_blkp));
        
        size = GET_SIZE(HDRP(bp)) + GET_SIZE(HDRP(prev_blkp)) + GET_SIZE(FTRP(next_blkp));

        if(size >= asize)
        {
            remove_blkp(next_blkp);
            remove_blkp(prev_blkp);
            char *next_next_blkp = NEXT_BLKP(next_blkp);
            PUT(HDRP(prev_blkp), PACK(size, (prev_prev_alloc << 1) | 1));
            bp = prev_blkp;
            SET_PREV_ALLOC(HDRP(next_next_blkp));
        }
    }

#ifdef NEXT_FIT
    /* Make sure the rover isn't pointing into the free block */
    /* that we just coalesced */
    if ((rover > (char *)bp) && (rover < NEXT_BLKP(bp))) 
        rover = bp;
#endif
    return bp;
}

/* 
 * place - Place block of asize bytes at start of free block bp 
 *         and split if remainder would be at least minimum block size
 */
static void *place(void *bp, size_t asize)
{
    size_t csize = GET_SIZE(HDRP(bp));   
    size_t prev_alloc = GET_PREV_ALLOC(HDRP(bp));

    /* csize - asize >= 2*DSIZE
     * 分割
     *
     * 非空闲块 -> 空闲块流程
     * 维护当前块的prev_alloc域
     * 改变下一个块的prev_alloc域
     */

    if((csize - asize) < (2*DSIZE))
    { 
        char *next_blkp = NEXT_BLKP(bp);

        PUT(HDRP(bp), PACK(csize, (prev_alloc << 1) | 1));
        PUT(FTRP(bp), PACK(csize, (prev_alloc << 1) | 1));

        SET_PREV_ALLOC(HDRP(next_blkp));

        
        remove_blkp(bp);
        return bp;
    }

    /* 放在空闲块的头部 */
    else if (asize <= PLACE_BUNDARY) 
    { 
        //char *prev = PREV_FREE_BLKP(bp);
        remove_blkp(bp);

        PUT(HDRP(bp), PACK(asize, (prev_alloc << 1) | 1));
        PUT(FTRP(bp), PACK(asize, (prev_alloc << 1) | 1));

        bp = NEXT_BLKP(bp);
        PUT(HDRP(bp), PACK(csize-asize, 2));
        PUT(FTRP(bp), PACK(csize-asize, 2));
        int index = log_2(csize - asize);
        insert_blkp(bp, heap_listp + index*WSIZE);

        return PREV_BLKP(bp);
        //coalesce(bp);
    }

    /* 放在空闲块的尾部 */
    else
    {
        remove_blkp(bp);
        char *next_blkp = NEXT_BLKP(bp);
        
        
        PUT(HDRP(bp), PACK(csize-asize, (prev_alloc << 1)));
        PUT(FTRP(bp), PACK(csize-asize, (prev_alloc << 1)));
        
        int index = log_2(csize - asize);
        insert_blkp(bp, heap_listp + index*WSIZE);


        bp = NEXT_BLKP(bp);
        PUT(HDRP(bp), PACK(asize, 1));
        SET_PREV_ALLOC(HDRP(next_blkp));
        return bp;
    }
    
}

/*
 * anti_place: 用于realloc, 切割策略 
 */
static void anti_place(void *bp, size_t asize)
{
    size_t csize = GET_SIZE(HDRP(bp));   
    size_t prev_alloc = GET_PREV_ALLOC(HDRP(bp));

    /* csize - asize >= 2*DSIZE
     * 分割
     *
     * 非空闲块 -> 空闲块流程
     * 维护当前块的prev_alloc域
     * 改变下一个块的prev_alloc域
     */
    if ((csize - asize) >= (8*DSIZE)) 
    { 
        //char *prev = PREV_FREE_BLKP(bp);
        char *next_blkp = NEXT_BLKP(bp);

        PUT(HDRP(bp), PACK(asize, (prev_alloc << 1) | 1));

        bp = NEXT_BLKP(bp);
        PUT(HDRP(bp), PACK(csize-asize, 2));
        PUT(FTRP(bp), PACK(csize-asize, 2));
        SET_PREV_FREE(HDRP(next_blkp));

        int index = log_2(csize - asize);
        insert_blkp(bp, heap_listp + index*WSIZE);
        //coalesce(bp);
    }
    else 
    { 
        PUT(HDRP(bp), PACK(csize, (prev_alloc << 1) | 1));
    }
}

/* 
 * find_fit - Find a fit for a block with asize bytes 
 */
static void *find_fit(size_t asize)
{
#ifdef NEXT_FIT 
    /* Next fit search */
    char *oldrover = rover;

    /* Search from the rover to the end of list */
    for ( ; GET_SIZE(HDRP(rover)) > 0; rover = NEXT_BLKP(rover))
        if (!GET_ALLOC(HDRP(rover)) && (asize <= GET_SIZE(HDRP(rover))))
            return rover;

    /* search from start of list to old rover */
    for (rover = heap_listp; rover < oldrover; rover = NEXT_BLKP(rover))
        if (!GET_ALLOC(HDRP(rover)) && (asize <= GET_SIZE(HDRP(rover))))
            return rover;

    return NULL;  /* no fit found */
#else 
    /* First-fit search */
    int index, i;
    void *bp, *bp0;

    index = log_2(asize);
    for(i = index; i < LINK_NUM; i++)
    {
        bp0 = NEXT_FREE_BLKP(heap_listp + i*WSIZE);
        for (bp = bp0; bp != NULL; bp = NEXT_FREE_BLKP(bp)) 
        {
            if (!GET_ALLOC(HDRP(bp)) && (asize <= GET_SIZE(HDRP(bp)))) 
            {
                return bp;
            }
        }
    }
    
    return NULL; /* No fit */
#endif
}

