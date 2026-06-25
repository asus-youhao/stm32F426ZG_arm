/**
 * @file    kinematics.c
 * @brief   7-DoF 運動學實作
 */
#include "kinematics.h"
#include <math.h>
#include <string.h>

/* 4x4 齊次矩陣 (row-major)。 */
static void mat4_identity(float T[16])
{
    memset(T, 0, sizeof(float)*16);
    T[0]=T[5]=T[10]=T[15]=1.0f;
}

static void mat4_mul(const float A[16], const float B[16], float C[16])
{
    for (int i=0;i<4;i++)
        for (int j=0;j<4;j++){
            float s=0;
            for (int k=0;k<4;k++) s+=A[i*4+k]*B[k*4+j];
            C[i*4+j]=s;
        }
}

/* 單軸 DH 變換 */
static void dh_transform(const dh_t *p, float theta, float T[16])
{
    float ct=cosf(theta+p->theta_off), st=sinf(theta+p->theta_off);
    float ca=cosf(p->alpha), sa=sinf(p->alpha);
    T[0]=ct;     T[1]=-st*ca;  T[2]= st*sa;  T[3]= p->a*ct;
    T[4]=st;     T[5]= ct*ca;  T[6]=-ct*sa;  T[7]= p->a*st;
    T[8]=0.0f;   T[9]= sa;     T[10]=ca;     T[11]=p->d;
    T[12]=0;     T[13]=0;      T[14]=0;      T[15]=1.0f;
}

void kin_fk(const arm_kin_t *k, const float q[ARM_DOF], pose_t *out)
{
    float T[16], Ti[16], Tmp[16];
    mat4_identity(T);
    for (int i=0;i<ARM_DOF;i++){
        dh_transform(&k->dh[i], q[i], Ti);
        mat4_mul(T, Ti, Tmp);
        memcpy(T, Tmp, sizeof(T));
    }
    out->p[0]=T[3]; out->p[1]=T[7]; out->p[2]=T[11];
    out->R[0]=T[0]; out->R[1]=T[1]; out->R[2]=T[2];
    out->R[3]=T[4]; out->R[4]=T[5]; out->R[5]=T[6];
    out->R[6]=T[8]; out->R[7]=T[9]; out->R[8]=T[10];
}

void kin_jacobian(const arm_kin_t *k, const float q[ARM_DOF], float J[6*ARM_DOF])
{
    /* 逐軸累積變換,取每軸的 z 軸與原點,末端位置 pe。 */
    float T[16], Ti[16], Tmp[16];
    float z[ARM_DOF][3], o[ARM_DOF][3];
    float o0[3]={0,0,0}, z0[3]={0,0,1};

    mat4_identity(T);
    /* 先求末端位置 */
    pose_t pe; kin_fk(k, q, &pe);

    /* base frame */
    float zprev[3]={z0[0],z0[1],z0[2]};
    float oprev[3]={o0[0],o0[1],o0[2]};

    mat4_identity(T);
    for (int i=0;i<ARM_DOF;i++){
        /* J 第 i 欄使用「第 i 關節前的座標系」之 z, o（即目前 T 的 z 與 origin） */
        /* zprev/oprev 為 0..i-1 累積後的 z/origin */
        float pe_minus_o[3]={ pe.p[0]-oprev[0], pe.p[1]-oprev[1], pe.p[2]-oprev[2] };
        /* 線速度 = z × (pe - o) */
        J[0*ARM_DOF+i] = zprev[1]*pe_minus_o[2]-zprev[2]*pe_minus_o[1];
        J[1*ARM_DOF+i] = zprev[2]*pe_minus_o[0]-zprev[0]*pe_minus_o[2];
        J[2*ARM_DOF+i] = zprev[0]*pe_minus_o[1]-zprev[1]*pe_minus_o[0];
        /* 角速度 = z */
        J[3*ARM_DOF+i] = zprev[0];
        J[4*ARM_DOF+i] = zprev[1];
        J[5*ARM_DOF+i] = zprev[2];

        /* 推進到下一個座標系 */
        dh_transform(&k->dh[i], q[i], Ti);
        mat4_mul(T, Ti, Tmp);
        memcpy(T, Tmp, sizeof(T));
        zprev[0]=T[2]; zprev[1]=T[6]; zprev[2]=T[10];
        oprev[0]=T[3]; oprev[1]=T[7]; oprev[2]=T[11];
        (void)z; (void)o;
    }
}

void kin_pose_error(const pose_t *des, const pose_t *cur, float err6[6])
{
    err6[0]=des->p[0]-cur->p[0];
    err6[1]=des->p[1]-cur->p[1];
    err6[2]=des->p[2]-cur->p[2];

    /* 旋轉誤差：Re = Rd * Rc^T,取其軸角向量近似（小角度）。 */
    float Re[9];
    /* Re = Rd * Rc^T */
    for (int i=0;i<3;i++)
        for (int j=0;j<3;j++){
            float s=0;
            for (int k=0;k<3;k++) s+=des->R[i*3+k]*cur->R[j*3+k];
            Re[i*3+j]=s;
        }
    /* 軸角：theta*axis ≈ 0.5*[Re32-Re23, Re13-Re31, Re21-Re12] */
    err6[3]=0.5f*(Re[7]-Re[5]);
    err6[4]=0.5f*(Re[2]-Re[6]);
    err6[5]=0.5f*(Re[3]-Re[1]);
}
