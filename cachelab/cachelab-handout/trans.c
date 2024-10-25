/*
岳镝 2100012961

(M,N)=(32,32):采用8*8分块，对每一个8*8的块，采用A逐行复制到B的逐列。

(M,N)=(64,64):采用8*8分块，对每一个8*8的块A(8*8)，将其分块成4个4*4，记作A11, A12, A21, A22.
相应的，B也同理。首先，操作：B11 = A11^T, B12 = A12^T. 对A而言，上述转置是逐行完成的。
然后，对A21逐列同时完成以下操作:B21 = B12, B12 = A21^T.
最后，完成B22 = A22^T

(M,N)=(60,68):采用16*8分块，采用A的逐行复制到B的逐列
*/

/*
 * trans.c - Matrix transpose B = A^T
 *
 * Each transpose function must have a prototype of the form:
 * void trans(int M, int N, int A[N][M], int B[M][N]);
 *
 * A transpose function is evaluated by counting the number of misses
 * on a 1KB direct mapped cache with a block size of 32 bytes.
 */
#include <stdio.h>
#include "cachelab.h"
#include "contracts.h"
#define BLOCK1 8
#define BLOCK2 4
#define BLOCK3 16

int is_transpose(int M, int N, int A[N][M], int B[M][N]);

/*
 * transpose_submit - This is the solution transpose function that you
 *     will be graded on for Part B of the assignment. Do not change
 *     the description string "Transpose submission", as the driver
 *     searches for that string to identify the transpose function to
 *     be graded. The REQUIRES and ENSURES from 15-122 are included
 *     for your convenience. They can be removed if you like.
 */
char transpose_submit_desc[] = "Transpose submission";
void transpose_submit(int M, int N, int A[N][M], int B[M][N])
{
    REQUIRES(M > 0);
    REQUIRES(N > 0);

    int tmp0, tmp1, tmp2, tmp3, tmp4, tmp5, tmp6, tmp7;
    int I, J, i, j;
    if (N == 32)
    {
        // 8*8分块处理
        for (int I = 0; I < N; I += BLOCK1)
        {
            for (J = 0; J < M; J += BLOCK1)
            {
                for (i = I; i < I + BLOCK1; i++)
                {
                    tmp0 = A[i][J];
                    tmp1 = A[i][J + 1];
                    tmp2 = A[i][J + 2];
                    tmp3 = A[i][J + 3];
                    tmp4 = A[i][J + 4];
                    tmp5 = A[i][J + 5];
                    tmp6 = A[i][J + 6];
                    tmp7 = A[i][J + 7];

                    B[J][i] = tmp0;
                    B[J + 1][i] = tmp1;
                    B[J + 2][i] = tmp2;
                    B[J + 3][i] = tmp3;
                    B[J + 4][i] = tmp4;
                    B[J + 5][i] = tmp5;
                    B[J + 6][i] = tmp6;
                    B[J + 7][i] = tmp7;
                }
            }
        }
    }

    if (N == 64)
    {
        for (I = 0; I < N; I += BLOCK1)
        {
            for (J = 0; J < M; J += BLOCK1)
            {
                // B11 = A11^T完成处理，A12^T暂时保存在B12
                for (i = I; i < I + BLOCK2; i++)
                {
                    tmp0 = A[i][J];
                    tmp1 = A[i][J + 1];
                    tmp2 = A[i][J + 2];
                    tmp3 = A[i][J + 3];
                    tmp4 = A[i][J + 4];
                    tmp5 = A[i][J + 5];
                    tmp6 = A[i][J + 6];
                    tmp7 = A[i][J + 7];
                    B[J][i] = tmp0;
                    B[J + 1][i] = tmp1;
                    B[J + 2][i] = tmp2;
                    B[J + 3][i] = tmp3;
                    B[J][i + BLOCK2] = tmp4;
                    B[J + 1][i + BLOCK2] = tmp5;
                    B[J + 2][i + BLOCK2] = tmp6;
                    B[J + 3][i + BLOCK2] = tmp7;
                }

                // 同时做B12 = A21^T, B21 = B12
                for (j = J; j < J + BLOCK2; j++)
                {
                    tmp0 = B[j][I + BLOCK2];
                    tmp1 = B[j][I + BLOCK2 + 1];
                    tmp2 = B[j][I + BLOCK2 + 2];
                    tmp3 = B[j][I + BLOCK2 + 3];

                    B[j][I + BLOCK2] = A[I + BLOCK2][j];
                    B[j][I + BLOCK2 + 1] = A[I + BLOCK2 + 1][j];
                    B[j][I + BLOCK2 + 2] = A[I + BLOCK2 + 2][j];
                    B[j][I + BLOCK2 + 3] = A[I + BLOCK2 + 3][j];

                    B[j + BLOCK2][I] = tmp0;
                    B[j + BLOCK2][I + 1] = tmp1;
                    B[j + BLOCK2][I + 2] = tmp2;
                    B[j + BLOCK2][I + 3] = tmp3;
                }

                // B22 = A22^T
                for (i = I + BLOCK2; i < I + BLOCK1; i++)
                {
                    tmp0 = A[i][J + BLOCK2];
                    tmp1 = A[i][J + BLOCK2 + 1];
                    tmp2 = A[i][J + BLOCK2 + 2];
                    tmp3 = A[i][J + BLOCK2 + 3];
                    B[J + BLOCK2][i] = tmp0;
                    B[J + BLOCK2 + 1][i] = tmp1;
                    B[J + BLOCK2 + 2][i] = tmp2;
                    B[J + BLOCK2 + 3][i] = tmp3;
                }
            }
        }
    }

    if (N == 68)
    {
        // 16*8分块处理
        for (I = 0; I < N; I += BLOCK3)
        {
            for (J = 0; J < M; J += BLOCK1)
            {
                for (i = I; i < I + BLOCK3 && i < N; i++)
                {
                    tmp0 = A[i][J];
                    tmp1 = A[i][J + 1];
                    tmp2 = A[i][J + 2];
                    tmp3 = A[i][J + 3];

                    if (J == M - BLOCK2)
                    {
                        B[J][i] = tmp0;
                        B[J + 1][i] = tmp1;
                        B[J + 2][i] = tmp2;
                        B[J + 3][i] = tmp3;
                        continue;
                    }
                    tmp4 = A[i][J + 4];
                    tmp5 = A[i][J + 5];
                    tmp6 = A[i][J + 6];
                    tmp7 = A[i][J + 7];

                    B[J][i] = tmp0;
                    B[J + 1][i] = tmp1;
                    B[J + 2][i] = tmp2;
                    B[J + 3][i] = tmp3;
                    B[J + 4][i] = tmp4;
                    B[J + 5][i] = tmp5;
                    B[J + 6][i] = tmp6;
                    B[J + 7][i] = tmp7;
                }
            }
        }
    }

    ENSURES(is_transpose(M, N, A, B));
}

/*
 * You can define additional transpose functions below. We've defined
 * a simple one below to help you get started.
 */

char test1_trans_desc[] = "test1";
void test1_trans(int M, int N, int A[N][M], int B[M][N])
{
    int I, J, i, tmp0, tmp1, tmp2, tmp3, tmp4, tmp5, tmp6, tmp7;
    if (N == 68)
    {
        for (I = 0; I < N; I += BLOCK1)
        {
            for (J = 0; J < M; J += BLOCK1)
            {
                for (i = I; i < I + BLOCK1 && i < N; i++)
                {
                    tmp0 = A[i][J];
                    tmp1 = A[i][J + 1];
                    tmp2 = A[i][J + 2];
                    tmp3 = A[i][J + 3];

                    if (J == M - BLOCK2)
                    {
                        tmp4 = A[i + 1][J];
                        tmp5 = A[i + 1][J + 1];
                        tmp6 = A[i + 1][J + 2];
                        tmp7 = A[i + 1][J + 3];
                        B[J][i] = tmp0;
                        B[J + 1][i] = tmp1;
                        B[J + 2][i] = tmp2;
                        B[J + 3][i] = tmp3;
                        B[J][i + 1] = tmp4;
                        B[J + 1][i + 1] = tmp5;
                        B[J + 2][i + 1] = tmp6;
                        B[J + 3][i + 1] = tmp7;
                        i++;
                        continue;
                    }
                    tmp4 = A[i][J + 4];
                    tmp5 = A[i][J + 5];
                    tmp6 = A[i][J + 6];
                    tmp7 = A[i][J + 7];

                    B[J][i] = tmp0;
                    B[J + 1][i] = tmp1;
                    B[J + 2][i] = tmp2;
                    B[J + 3][i] = tmp3;
                    B[J + 4][i] = tmp4;
                    B[J + 5][i] = tmp5;
                    B[J + 6][i] = tmp6;
                    B[J + 7][i] = tmp7;
                }
            }
        }
    }
}

char test2_trans_desc[] = "test2";
void test2_trans(int M, int N, int A[N][M], int B[M][N])
{
    int I, J, i, tmp0, tmp1, tmp2, tmp3, tmp4, tmp5, tmp6, tmp7;
    if (N == 68)
    {
        for (I = 0; I < N; I += 14)
        {
            for (J = 0; J < M; J += BLOCK1)
            {
                for (i = I; i < I + 14 && i < N; i++)
                {
                    tmp0 = A[i][J];
                    tmp1 = A[i][J + 1];
                    tmp2 = A[i][J + 2];
                    tmp3 = A[i][J + 3];

                    if (J == M - BLOCK2)
                    {
                        B[J][i] = tmp0;
                        B[J + 1][i] = tmp1;
                        B[J + 2][i] = tmp2;
                        B[J + 3][i] = tmp3;
                        continue;
                    }
                    tmp4 = A[i][J + 4];
                    tmp5 = A[i][J + 5];
                    tmp6 = A[i][J + 6];
                    tmp7 = A[i][J + 7];

                    B[J][i] = tmp0;
                    B[J + 1][i] = tmp1;
                    B[J + 2][i] = tmp2;
                    B[J + 3][i] = tmp3;
                    B[J + 4][i] = tmp4;
                    B[J + 5][i] = tmp5;
                    B[J + 6][i] = tmp6;
                    B[J + 7][i] = tmp7;
                }
            }
        }
    }
}

/*
 * trans - A simple baseline transpose function, not optimized for the cache.
 */
char trans_desc[] = "Simple row-wise scan transpose";
void trans(int M, int N, int A[N][M], int B[M][N])
{
    int i, j, tmp;

    REQUIRES(M > 0);
    REQUIRES(N > 0);

    for (i = 0; i < N; i++)
    {
        for (j = 0; j < M; j++)
        {
            tmp = A[i][j];
            B[j][i] = tmp;
        }
    }

    ENSURES(is_transpose(M, N, A, B));
}

/*
 * registerFunctions - This function registers your transpose
 *     functions with the driver.  At runtime, the driver will
 *     evaluate each of the registered functions and summarize their
 *     performance. This is a handy way to experiment with different
 *     transpose strategies.
 */
void registerFunctions()
{
    /* Register your solution function */
    registerTransFunction(transpose_submit, transpose_submit_desc);

    registerTransFunction(test1_trans, test1_trans_desc);

    registerTransFunction(test2_trans, test2_trans_desc);

    /* Register any additional transpose functions */
    registerTransFunction(trans, trans_desc);
}

/*
 * is_transpose - This helper function checks if B is the transpose of
 *     A. You can check the correctness of your transpose by calling
 *     it before returning from the transpose function.
 */
int is_transpose(int M, int N, int A[N][M], int B[M][N])
{
    int i, j;

    for (i = 0; i < N; i++)
    {
        for (j = 0; j < M; ++j)
        {
            if (A[i][j] != B[j][i])
            {
                return 0;
            }
        }
    }
    return 1;
}
