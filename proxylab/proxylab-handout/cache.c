#include "csapp.h"

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400
#define CACHE_BLK_NUM 10

typedef struct 
{
    int cnt;    //计数器，用于LRU
    char key[MAXLINE];  //用URL唯一标识每次请求
    char value[MAX_OBJECT_SIZE];    //实际存放的数据
    int datasize;   //数据大小
    int valid;  //有效位
} cache_blk;

cache_blk cache[CACHE_BLK_NUM];
int readcnt, writecnt, visitcnt;
sem_t rmutex, wmutex, r, w;

void cache_init();
void writer(int index, char *key, char *value, int length);
int cache_find(int fd, char *dstkey);
void cache_insert(char *key, char *value, int datasize);

/*
 * cache_init
 * 初始化cache和信号量
 */
void cache_init()
{
    int i;
    for(i = 0; i < CACHE_BLK_NUM; i++)
    {
        cache[i].cnt = 0;
        cache[i].datasize = 0;
        cache[i].valid = 0;
        cache[i].key[0] = '\0';
        cache[i].value[0] = '\0';
    }
    visitcnt = 0;
    readcnt = writecnt = 0;
    Sem_init(&rmutex, 0, 1);
    Sem_init(&wmutex, 0, 1);
    Sem_init(&r, 0, 1);
    Sem_init(&w, 0, 1);
}

/*
 * writer
 * 用key和value修改cache内容
 */
void writer(int index, char *key, char *value, int length)
{
    strcpy(cache[index].key, key);
    cache[index].datasize = length;
    memcpy(cache[index].value, value, length);
    cache[index].valid = 1;
    cache[index].cnt = visitcnt;
        
}

/*
 * cache_find
 * 在cache中寻找dstkey，如果命中，直接将对应的value写到fd中
 * 采用第一类读者-写者模型
 * 第二类读者-写者导致timeout？
 */
int cache_find(int fd, char *dstkey)
{
    int datasize, valid, cnt, i, hit=0;

//        P(&r);
//        V(&r);
    P(&rmutex);
    readcnt++;
    if(readcnt == 1)
        P(&w);
    V(&rmutex);
        
    visitcnt++;
    for(i = 0; i < CACHE_BLK_NUM; i++)
    {
        //reader(i, key, value, &datasize, &cnt, &valid);
        /* 命中 */
        if(cache[i].valid && !strcmp(cache[i].key, dstkey))
        {
            cache[i].cnt = visitcnt;
            Rio_writen(fd, cache[i].value, cache[i].datasize);
            hit = 1;
            break;
        }
    }

    P(&rmutex);
    readcnt--;
    if(readcnt == 0)
            V(&w);
    V(&rmutex);

    return hit;

}

/*
 * cache_insert
 * 将(key, value)对写入cache
 * 策略：LRU
 * 采用第一类读者-写者模型
 * 第二类读者-写者导致timeout？
 */
void cache_insert(char *key, char *value, int datasize)
{
    
    int cnt, valid, i, mini, hit=0, pos;
/*
        P(&wmutex);
        writecnt++;
        if(writecnt == 1)
            P(&r);
        V(&wmutex);
*/
    P(&w);
    visitcnt++;
    mini = visitcnt;
    for(i = 0; i < CACHE_BLK_NUM; i++)
    {
        if(!cache[i].valid && !hit)
        {
            hit = 1;
            writer(i, key, value, datasize);
            break;
        }
        else
        {
            if(cache[i].cnt < mini)
            {
                mini = cache[i].cnt;
                pos = i;
            }
        }
    }
    if(!hit)
    {
        writer(pos, key, value, datasize);
    }
    V(&w);
/*
        P(&wmutex);
        writecnt--;
        if(writecnt == 0)
            V(&r);
        V(&wmutex);
*/
    
}