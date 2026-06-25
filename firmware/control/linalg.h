/**
 * @file    linalg.h
 * @brief   小型浮點線性代數（給運動學/IK 用,STM32F7 FPU）
 *
 * 採 row-major,動態維度但有上限 LA_MAX。所有運算為 float（單精度）。
 */
#ifndef LINALG_H
#define LINALG_H

#include <stdint.h>
#include <stdbool.h>

#define LA_MAX 8   /* 支援到 8x8 / 長度 8 之內（7-DoF + margin） */

/* C = A(ra x ca) * B(ca x cb) */
void la_matmul(const float *A, const float *B, float *C,
               int ra, int ca, int cb);

/* AT(c x r) = transpose(A(r x c)) */
void la_transpose(const float *A, float *AT, int r, int c);

/* C = A + s*B （同維度 r x c） */
void la_addscaled(const float *A, const float *B, float s, float *C, int r, int c);

/* y(r) = A(r x c) * x(c) */
void la_matvec(const float *A, const float *x, float *y, int r, int c);

/* 設為單位矩陣 n x n */
void la_eye(float *A, int n);

/**
 * @brief n x n 反矩陣（Gauss-Jordan,含部分主元）。
 * @return true 成功;false 若奇異。
 */
bool la_inverse(const float *A, float *Ainv, int n);

#endif /* LINALG_H */
