/*
岳镝 2100012961

给定cache参数(s,E,b)，给定目标访存序列，输出命中、不命中、替换次数(hitcount,misscount,evictioncount)

把cache看成一个由Block组成的S*E二维数组。每次访存，就根据地址给出的s域去cache的第2^s行寻找，比较tag域，
确定是hit,miss,eviction.
*/
#include <getopt.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include "cachelab.h"

#define m 64

//将16进制表示（字符）转化为整数，遇到字符c时停止
long long hex2i(char str[], char c)
{
    long long x = 0;
    int i = 0, n;
    n = strlen(str);
    while (i < n && str[i] != c)
    {
        if (str[i] >= 0 && str[i] <= '9')
            x = (x << 4) + str[i++] - '0';
        else
            x = (x << 4) + str[i++] - 'a' + 10;
    }
    return x;
}

//表示一个Block
struct Block
{
    int vcount;        //用于LRU策略的计数器
    long long address; //记录Block中的（一个）地址
};

//初始化cache，所有位置都为空
void init(int size, struct Block *cache)
{
    for (int i = 0; i < size; i++)
    {
        cache[i].address = 0;
        cache[i].vcount = -1;
    }
}

int main(int argc, char *argv[])
{
    int opt, readfile;
    int s, E, b, S;
    long long address;
    int setindex, setpos;
    int hitflag;
    int putpos, evictionpos, maxvcount;
    char filename[40], content[255];
    char operation[1];
    struct Block *cache;
    int hitcount = 0, misscount = 0, evictioncount = 0;

    //处理命令行信息，得到s,E,b的值
    while ((opt = getopt(argc, argv, ":vs:E:b:t:")) != -1)
    {
        switch (opt)
        {
        case 's':
            s = atoi(optarg);
            break;

        case 'E':
            E = atoi(optarg);
            break;

        case 'b':
            b = atoi(optarg);
            break;

        case 't':
            strcpy(filename, optarg);
            break;

        case ':':
            /* missing option argument 参数缺失*/
            fprintf(stderr, "%s: option '-%c' requires an argument\n",
                    argv[0], optopt);
            break;

        default:
            printf("error!\n");
            break;
        }
    }

    S = 1 << s;
    cache = (struct Block *)malloc(S * E * sizeof(struct Block)); // cache看成是一个S*E的Block类型数组
    init(S * E, cache);

    FILE *fp = NULL;
    fp = fopen(filename, "r");

    //读入trace访存序列，模拟cache行为
    while ((readfile = fscanf(fp, "%s", operation)) != EOF)
    {
        fscanf(fp, "%s", content);
        address = hex2i(content, ',');
        setindex = (address >> b) & ((1 << s) - 1); //即将访问的地址所在的Set
        setpos = setindex * E;
        putpos = -1;      // address放入哪个block(miss情况下)
        evictionpos = -1; // address替换哪个block
        maxvcount = -1;   //记录最大的vcount(用于LRU)
        hitflag = 0;      //是否命中

        //遍历当前Set
        for (int i = setpos; i < setpos + E; i++)
        {
            //命中
            if (cache[i].vcount >= 0 && (address ^ cache[i].address) >> (s + b) == 0)
            {
                cache[i].vcount = 0;
                hitflag = 1;
                hitcount++;
                // printf("%d hit\n",address);
            }

            //如果遇到空的block
            else if (cache[i].vcount == -1)
            {
                putpos = i;
            }

            //遇到非空block
            else
            {
                cache[i].vcount++;
                if (cache[i].vcount > maxvcount)
                {
                    maxvcount = cache[i].vcount;
                    evictionpos = i;
                }
            }
        }

        //如果没有命中
        if (!hitflag)
        {
            // printf("%d miss\n",address);
            misscount++;

            //存在空的block，就不会发生替换
            if (putpos >= 0)
            {
                cache[putpos].vcount = 0;
                cache[putpos].address = address;
            }

            //否则，会发生替换
            else
            {
                // printf("%d eviction\n",address);
                evictioncount++;
                cache[evictionpos].vcount = 0;
                cache[evictionpos].address = address;
            }
        }

        //如果是modify操作，store时必然命中
        if (operation[0] == 'M')
            hitcount++;
    }

    fclose(fp);
    printSummary(hitcount, misscount, evictioncount);
    return 0;
}