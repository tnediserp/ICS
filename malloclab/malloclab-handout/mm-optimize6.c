/*
 * mm.c
 *
 * 岳镝 2100012961
 * 
 * 使用分离的空闲链表，分离适配。对于每个空闲链表，用双链表来组织(prev, next指针域)。对每个大小类，维护某种程度上
 * 的单调性，以达到接近于best_fit的效果。
 * 细节上，还有如下一些主要的优化措施：
 * 1.   去脚部。对于已分配块，无需维护footer。对每个块，在它的header中增加一个prev_alloc位，用于指示它的上一个
 *      块是否被分配。
 * 2.   压缩prev, next指针的大小。利用堆的大小不超过2^32字节这一事实，prev, next指针可以被压缩成4字节int，
 *      表示相对heap_listp的偏移量。要计算绝对地址时只需用这个偏移量加上heap_listp即可。
 * 3.   尝试维护大小类中空闲块大小的某种程度上的单调性。理想情况下应该直接使之单调，但考虑到会降低thru，故限制
 *      在插入时比较的次数。
 * 4.   place优化。第偶数次malloc在空闲块前部分配，第奇数次malloc在空闲块后部分配。这项优化专门针对binary2-bal.rep
 * 5.   realloc优化。realloc时观察当前块大小。如果足够大，直接返回即可。否则，考察当前块后面的块，若其空闲，
 *      试图将其吸纳进来，此时若大小足够就直接返回。上述均不成立时再用naive算法。（事实证明这个优化的实验性能一般）
 * 6.   尝试改变大小类的划分方法。（实验效果不佳）
 *     
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
//# define dbg_printf(...) printf(__VA_ARGS__)
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
#define CHUNKSIZE 4096

/* 空闲链表数及其所占的总空间 */
#define LINK_NUM 16
#define LINK_HDRS_SIZE LINK_NUM*WSIZE 

#define UNSIGNED_MAX 0xffffffff

/* 限制在插入过程中比较的总次数，保证thru和best_fit的折中 */
#define MAX_TRAV 5

/* 大小类划分临界值 */
#define CRITICAL_0  32
#define CRITICAL_1  64
#define CRITICAL_2  96
#define CRITICAL_3  160
#define CRITICAL_4  224
#define CRITICAL_5  352
#define CRITICAL_6  480
#define CRITICAL_7  1<<9
#define CRITICAL_8  1<<10
#define CRITICAL_9  1<<11
#define CRITICAL_10 1<<12
#define CRITICAL_11 1<<13
#define CRITICAL_12 1<<14
#define CRITICAL_13 1<<15
#define CRITICAL_14 1<<16
#define CRITICAL_15 1<<16
#define CRITICAL_16 1<<17
#define CRITICAL_17 1<<18
#define CRITICAL_18 1<<19
#define CRITICAL_19 1<<20
#define CRITICAL_20 1<<21
#define CRITICAL_21 1<<22
#define CRITICAL_22 1<<23
#define CRITICAL_23 1<<24
#define CRITICAL_24 1<<25
#define CRITICAL_25 1<<26
#define CRITICAL_26 1<<27
#define CRITICAL_27 1<<28
#define CRITICAL_28 1<<29
#define CRITICAL_29 1<<30
#define CRITICAL_30 2147483648U 

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



static char *heap_listp = 0;  /* Pointer to first block */  
static int malloc_times = 0;  /* 记录这是第几次malloc，用于place的策略 */

/* Function prototypes for internal helper routines */
static void *extend_heap(size_t words);
static void *place(void *bp, size_t asize);
static void anti_place(void *bp, size_t asize);
static void *find_fit(size_t asize);
static void *coalesce(void *bp);
static void *anti_coalesce(void *bp, size_t asize);
inline static void *insert_blkp(void *bp, void *loc);
static void *up_insert_blkp(void *bp, int index);
inline static void remove_blkp(void *bp);
inline static int find_link(unsigned int x);
static int travheap(int lineno);
static int travlist(int lineno);

/*
 * Initialize: return -1 on error, 0 on success.
 */
int mm_init(void) 
{
    /* Create the initial empty heap 
     * Link head pointer    4 * 16 = 64 Bytes
     * Prologue header      4 Bytes
     * Epilogue header      4 Bytes
     */
    if ((heap_listp = mem_sbrk((LINK_NUM + 2)*WSIZE)) == (void *)-1) 
        return -1;

    malloc_times = 0;
    int i;
    /* 注意：序言块可以去脚部 */                                 
    for(i=0; i < LINK_NUM; i++)
    {
        PUT(heap_listp + i*WSIZE, UNSIGNED_MAX);
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
    malloc_times++;    /* malloc次数增加 */
    size_t asize;      /* Adjusted block size */
    size_t extendsize; /* Amount to extend heap if no fit */
    char *bp;      

    if (heap_listp == 0){
        mm_init();
    }
    /* Ignore spurious requests */
    if (size == 0)
        return NULL;

    /* 
     * Adjust block size to include overhead and alignment reqs. 
     * 一个块至少16字节，其中head, foot各4字节，prev, next各4字节，
     * 但是因为非空闲块不需要维护prev, next指针，所以可以暂时把它们
     * 的空间当成有效负载，回收的时候再维护这两个指针。
     */
    if (size <= DSIZE + WSIZE)                                          
        asize = 2*DSIZE;                                      
    else
        asize = DSIZE * ((size + (WSIZE) + (DSIZE-1)) / DSIZE);  /* 非空闲块可以去脚部 */


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

    /* alloc域变成0，维护prev_alloc域 */
    PUT(HDRP(ptr), PACK(size, prev_alloc << 1));
    PUT(FTRP(ptr), PACK(size, prev_alloc << 1));
    
    coalesce(ptr);
}

/*
 * realloc - you may want to look at mm-naive.c
 * realloc时观察当前块大小。如果足够大，直接返回即可。否则，考察当前块后面的块，若其空闲，
 * 试图将其吸纳进来，此时若大小足够就直接返回。上述均不成立时再用naive算法。（事实证明这个优化的实验性能一般）
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
        return oldptr;
    }
        
    else
    {
        newptr = anti_coalesce(oldptr, asize);
        if(GET_SIZE(HDRP(newptr)) >= asize)
        {
            anti_place(newptr, asize);
            return newptr;
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
    int freenum_heap = travheap(lineno);
    int freenum_link = travlist(lineno);

    /* 检查两种方式统计的空闲块个数是否匹配 */
    if(freenum_heap != freenum_link)
    {
        printf("free block num dosen't match\n");
        printf("caculate by heap: %d\n", freenum_heap);
        printf("caculate by link: %d\n", freenum_link);
        exit(0);
    }
}

static int travheap(int lineno)
{
    char *bp;
    int freenum = 0;
    /* 检查序言块 */
    bp = heap_listp + LINK_HDRS_SIZE + WSIZE;
    if(!GET_ALLOC(HDRP(bp)))
    {
        printf("addr= 0x%lx, Prologue header error\n", (size_t)bp);
        exit(0);
    }
    for(bp = heap_listp + LINK_HDRS_SIZE + DSIZE; GET_SIZE(HDRP(bp)) > 0; bp = NEXT_BLKP(bp))
    {
        /* 检查是否是合法地址（在堆中） */
        if(!in_heap(bp))
        {
            printf("addr= 0x%lx, exceed heap bundary\n", (size_t)bp);
            exit(0);
        }

        /* 检查8字节对齐 */
        if(!aligned(bp))
        {
            printf("addr= 0x%lx, bad alignment\n", (size_t)bp);
            exit(0);
        }
        
        /* 检查size */
        int size = GET_SIZE(HDRP(bp));
        if(size < 2*DSIZE)
        {
            printf("addr= 0x%lx, too small size\n", (size_t)bp);
            exit(0);
        }
        if(size % DSIZE != 0)
        {
            printf("addr= 0x%lx, bad size alignment\n", (size_t)bp);
            exit(0);
        }

        /* 检查前后两个块的alloc一致性 */
        char *next_blkp = NEXT_BLKP(bp);
        if(GET_ALLOC(HDRP(bp)) != GET_PREV_ALLOC(HDRP(next_blkp)))
        {
            printf("addr= 0x%lx, bad alloc consistency\n", (size_t)bp);
            exit(0);
        }

        /* 对于空闲块，检查header, footer是否匹配，以及判断是否出现连续空闲的情况 */
        if(!GET_ALLOC((HDRP(bp))))
        {
            freenum++;
            int bp_header = GET(HDRP(bp));
            int bp_footer = GET(FTRP(bp));
            int next_alloc = GET_ALLOC(HDRP(NEXT_BLKP(bp)));
            if(bp_header != bp_footer)
            {
                printf("sizeof bp= %d\n", GET_SIZE(HDRP(bp)));
                printf("addr= 0x%lx, header dosen't match footer\nheader= %d, footer= %d\n", 
                (size_t)bp, bp_header, bp_footer);
                exit(0);
            }
            if(!next_alloc)
            {
                printf("addr= 0x%lx, consecutive free block\n", (size_t)bp);
                exit(0);
            }
        }
    }
    /* 检查结尾块 */
    if(!GET_ALLOC(HDRP(bp)))
    {
        printf("addr= 0x%lx, Epilogue header error\n", (size_t)bp);
        exit(0);
    }
    return freenum;
}

static int travlist(int lineno)
{
    int i, freenum = 0;
    char *bp, *bp0, *next;
    for(i=0; i < LINK_NUM; i++)
    {
        bp0 = NEXT_FREE_BLKP(heap_listp + i*WSIZE);
        for(bp = bp0; bp; bp = NEXT_FREE_BLKP(bp))
        {
            freenum++;
        
            /* 判断是否是合法地址（在堆中） */
            if(!in_heap(bp))
            {
                printf("addr= 0x%lx, exceed heap bundary\n", (size_t)bp);
                exit(0);
            }

            /* 检查是否位于正确的大小类中 */
            int index = find_link(GET_SIZE(HDRP(bp)));
            if(index != i)
            {
                printf("addr= 0x%lx, in wrong segregated list\nsize= %d, now in list %d, should in list %d\n", 
                (size_t)bp, GET_SIZE(HDRP(bp)), i, index);
                exit(0);
            }

            /* 判断prev, next指针的一致性 */
            next = NEXT_FREE_BLKP(bp);
            if(next != NULL)
            {
                if(PREV_FREE_BLKP(next) != bp)
                {
                    printf("addr= 0x%lx, prev pointer doesn't match next pointer\n", (size_t)bp);
                    exit(0);
                }
            }
        }
    }
    return freenum;
}

/*
 * extend_heap
 * 堆空间不够时扩充堆。
 */
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
    /* 注意维护prev_alloc域 */
    PUT(HDRP(bp), PACK(size, prev_alloc << 1));         /* Free block header */   
    PUT(FTRP(bp), PACK(size, prev_alloc << 1));         /* Free block footer */   
    PUT(HDRP(NEXT_BLKP(bp)), PACK(0, 1));               /* New epilogue header */ 

    /* Coalesce if the previous block was free */
    return coalesce(bp);                                          
}

/*
 * find_link
 * 返回大小为x的空闲块应该位于哪个大小类
 */
inline static int find_link(unsigned int x)
{
   if(x < CRITICAL_0) return 0;
   if(x < CRITICAL_1) return 1;
   if(x < CRITICAL_2) return 2;
   if(x < CRITICAL_3) return 3;
   if(x < CRITICAL_4) return 4;
   if(x < CRITICAL_5) return 5;
   if(x < CRITICAL_6) return 6;
   if(x < CRITICAL_7) return 7;
   if(x < CRITICAL_8) return 8;
   if(x < CRITICAL_9) return 9;
   if(x < CRITICAL_10) return 10;
   if(x < CRITICAL_11) return 11;
   if(x < CRITICAL_12) return 12;
   if(x < CRITICAL_13) return 13;
   if(x < CRITICAL_14) return 14;
   return 15;
}

/*
 * insert_blkp
 * 在空闲链表表项loc后面插入空闲块
 */
inline static void *insert_blkp(void *bp, void *loc)
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
 * up_insert_blkp
 * 在某种程度上维护单调性的意义下，在第index个空闲链表中插入空闲块bp
 */
static void *up_insert_blkp(void *bp, int index)
{
    char *ptr;
    char *ptr0 = heap_listp + index*WSIZE;
    char *pos = ptr0;
    int i = 0;
    size_t size = GET_SIZE(HDRP(bp));
    /* 注意：比较次数不超过MAX_TRAV次 */
    for(ptr = NEXT_FREE_BLKP(ptr0); ptr != NULL && i < MAX_TRAV; ptr = NEXT_FREE_BLKP(ptr), i++)
    {
        if(GET_SIZE(HDRP(ptr)) < size)
        {
            pos = ptr;
        }
        else break;
    }
    insert_blkp(bp, pos);
    return bp;
}

/*
 * remove_blkp
 * 从空闲链表中删除bp指向的空闲块
 */
inline static void remove_blkp(void *bp)
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

        index = find_link(size);
        up_insert_blkp(bp, index);       

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
        size += GET_SIZE(HDRP(NEXT_BLKP(bp)));
        PUT(HDRP(bp), PACK(size, prev_alloc << 1));
        PUT(FTRP(bp), PACK(size, prev_alloc << 1));

        remove_blkp(next_blkp);
        index = find_link(size);
        up_insert_blkp(bp, index);
    }

    /* Case 3 
     * prev_alloc=0, next_alloc=1
     * PREV_BLKP大小增加；
     * 原链表删除PREV_BLKP
     * 插入适当的空闲链表
     */
    else if (!prev_alloc && next_alloc) 
    {      
        char *next_blkp = NEXT_BLKP(bp);
        SET_PREV_FREE(HDRP(next_blkp));

        /* 注意维护prev_prev_alloc */
        size_t prev_prev_alloc = GET_PREV_ALLOC(HDRP(PREV_BLKP(bp)));
        size += GET_SIZE(HDRP(PREV_BLKP(bp)));
        PUT(FTRP(bp), PACK(size, prev_prev_alloc << 1));
        PUT(HDRP(PREV_BLKP(bp)), PACK(size, prev_prev_alloc << 1));
        
        bp = PREV_BLKP(bp);
        
        remove_blkp(bp);
        index = find_link(size);
        up_insert_blkp(bp, index);
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
        char *next_blkp = NEXT_BLKP(bp);                              
        size += GET_SIZE(HDRP(PREV_BLKP(bp))) + 
            GET_SIZE(FTRP(NEXT_BLKP(bp)));
        PUT(HDRP(PREV_BLKP(bp)), PACK(size, prev_prev_alloc << 1));
        PUT(FTRP(NEXT_BLKP(bp)), PACK(size, prev_prev_alloc << 1));
        bp = PREV_BLKP(bp);
        
        remove_blkp(next_blkp);
        
        remove_blkp(bp);
        index = find_link(size);
        up_insert_blkp(bp, index);
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
 * 块指针bp, 想要realloc出asize大小。查看bp后面的块，如果
 * 有空闲，并且合并后总大小达到asize, 就合并，并且做相应处理，
 * 返回新的块指针。
 */
static void *anti_coalesce(void *bp, size_t asize)
{
    size_t prev_alloc = GET_PREV_ALLOC(HDRP(bp));
    size_t next_alloc = GET_ALLOC(HDRP(NEXT_BLKP(bp)));
    size_t size;


    /* Case 1 
     * next_alloc=1
     * 此时失败。
     */
    if (next_alloc) 
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
    else 
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
     * 空闲块 -> 非空闲块流程
     * 维护当前块的prev_alloc域
     * 改变下一个块的prev_alloc域
     */

    if((csize - asize) < (2*DSIZE))
    { 
        char *next_blkp = NEXT_BLKP(bp);

        /* 维护当前块 */
        PUT(HDRP(bp), PACK(csize, (prev_alloc << 1) | 1));
        PUT(FTRP(bp), PACK(csize, (prev_alloc << 1) | 1));

        /* 下一个块置prev_alloc为1 */
        SET_PREV_ALLOC(HDRP(next_blkp));

        remove_blkp(bp);
        return bp;
    }

    /* 如果是第偶数次malloc，放在空闲块的头部 */
    else if (malloc_times%2 == 0)
    { 
        remove_blkp(bp);

        /* 维护待分配的块 */
        PUT(HDRP(bp), PACK(asize, (prev_alloc << 1) | 1));
        PUT(FTRP(bp), PACK(asize, (prev_alloc << 1) | 1));

        /* 维护后面的块 */
        bp = NEXT_BLKP(bp);
        PUT(HDRP(bp), PACK(csize-asize, 2));
        PUT(FTRP(bp), PACK(csize-asize, 2));

        int index = find_link(csize - asize);
        up_insert_blkp(bp, index);

        return PREV_BLKP(bp);
    }

    /* 如果是第奇数次malloc，放在空闲块的尾部 */
    else
    {
        remove_blkp(bp);
        char *next_blkp = NEXT_BLKP(bp);
        
        /* 维护前部的块 */
        PUT(HDRP(bp), PACK(csize-asize, (prev_alloc << 1)));
        PUT(FTRP(bp), PACK(csize-asize, (prev_alloc << 1)));
        
        int index = find_link(csize - asize);
        up_insert_blkp(bp, index);

        /* 维护待分配的块 */
        bp = NEXT_BLKP(bp);
        PUT(HDRP(bp), PACK(asize, 1));

        /* 待分配块后面的块的prev_alloc域置为1 */
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
     * 空闲块 -> 非空闲块流程
     * 维护当前块的prev_alloc域
     * 改变下一个块的prev_alloc域
     */
    if ((csize - asize) >= (8*DSIZE)) 
    { 
        char *next_blkp = NEXT_BLKP(bp);

        PUT(HDRP(bp), PACK(asize, (prev_alloc << 1) | 1));

        bp = NEXT_BLKP(bp);
        PUT(HDRP(bp), PACK(csize-asize, 2));
        PUT(FTRP(bp), PACK(csize-asize, 2));
        SET_PREV_FREE(HDRP(next_blkp));

        int index = find_link(csize - asize);
        up_insert_blkp(bp, index);
    }
    else 
    { 
        PUT(HDRP(bp), PACK(csize, (prev_alloc << 1) | 1));
    }
}

/* 
 * find_fit - Find a fit for a block with asize bytes 
 * 分离空闲链表的find_fit
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

    index = find_link(asize);
    for(i = index; i < LINK_NUM; i++)
    {
        bp0 = NEXT_FREE_BLKP(heap_listp + i*WSIZE);
        for (bp = bp0; bp != NULL; bp = NEXT_FREE_BLKP(bp)) 
        {
            size_t blksize = GET_SIZE(HDRP(bp));
            if (!GET_ALLOC(HDRP(bp)) && (asize <= blksize)) 
            {       
                return bp;
            }
        }
    }
    
    return NULL; /* No fit */
#endif
}

