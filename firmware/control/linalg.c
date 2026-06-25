/**
 * @file    linalg.c
 * @brief   小型浮點線性代數實作
 */
#include "linalg.h"
#include <string.h>
#include <math.h>

void la_matmul(const float *A, const float *B, float *C,
               int ra, int ca, int cb)
{
    for (int i = 0; i < ra; i++)
        for (int j = 0; j < cb; j++) {
            float s = 0.0f;
            for (int k = 0; k < ca; k++) s += A[i*ca + k] * B[k*cb + j];
            C[i*cb + j] = s;
        }
}

void la_transpose(const float *A, float *AT, int r, int c)
{
    for (int i = 0; i < r; i++)
        for (int j = 0; j < c; j++)
            AT[j*r + i] = A[i*c + j];
}

void la_addscaled(const float *A, const float *B, float s, float *C, int r, int c)
{
    int n = r*c;
    for (int i = 0; i < n; i++) C[i] = A[i] + s*B[i];
}

void la_matvec(const float *A, const float *x, float *y, int r, int c)
{
    for (int i = 0; i < r; i++) {
        float s = 0.0f;
        for (int k = 0; k < c; k++) s += A[i*c + k] * x[k];
        y[i] = s;
    }
}

void la_eye(float *A, int n)
{
    memset(A, 0, sizeof(float)*n*n);
    for (int i = 0; i < n; i++) A[i*n + i] = 1.0f;
}

bool la_inverse(const float *A, float *Ainv, int n)
{
    /* 增廣 [A | I],Gauss-Jordan 部分主元 */
    float m[LA_MAX][2*LA_MAX];
    if (n > LA_MAX) return false;

    for (int i = 0; i < n; i++) {
        for (int j = 0; j < n; j++) m[i][j] = A[i*n + j];
        for (int j = 0; j < n; j++) m[i][n + j] = (i == j) ? 1.0f : 0.0f;
    }

    for (int col = 0; col < n; col++) {
        /* 部分主元 */
        int piv = col;
        float best = fabsf(m[col][col]);
        for (int r = col+1; r < n; r++) {
            float v = fabsf(m[r][col]);
            if (v > best) { best = v; piv = r; }
        }
        if (best < 1e-9f) return false;             /* 奇異 */
        if (piv != col)
            for (int j = 0; j < 2*n; j++) {
                float t = m[col][j]; m[col][j] = m[piv][j]; m[piv][j] = t;
            }

        float d = m[col][col];
        for (int j = 0; j < 2*n; j++) m[col][j] /= d;

        for (int r = 0; r < n; r++) {
            if (r == col) continue;
            float f = m[r][col];
            for (int j = 0; j < 2*n; j++) m[r][j] -= f * m[col][j];
        }
    }

    for (int i = 0; i < n; i++)
        for (int j = 0; j < n; j++)
            Ainv[i*n + j] = m[i][n + j];
    return true;
}
