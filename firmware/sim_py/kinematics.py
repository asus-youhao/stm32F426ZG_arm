"""
kinematics.py — 7-DoF DH 正運動學 + 幾何 Jacobian + DLS 逆運動學（純 Python）
與韌體 control/kinematics.c、ik.c 一致。
"""
import math


def _mat4_mul(A, B):
    C = [0.0] * 16
    for i in range(4):
        for j in range(4):
            s = 0.0
            for k in range(4):
                s += A[i*4+k] * B[k*4+j]
            C[i*4+j] = s
    return C


def _dh(a, alpha, d, theta):
    ct, st = math.cos(theta), math.sin(theta)
    ca, sa = math.cos(alpha), math.sin(alpha)
    return [ct, -st*ca,  st*sa, a*ct,
            st,  ct*ca, -ct*sa, a*st,
            0.0, sa,     ca,    d,
            0.0, 0.0,    0.0,   1.0]


class ArmKin:
    def __init__(self, dh_params, base_p=(0.0, 0.0, 0.0)):
        # dh_params: list of (a, alpha, d, theta_off)
        self.dh = dh_params
        self.base_p = list(base_p)

    def fk(self, q):
        T = [1,0,0,self.base_p[0], 0,1,0,self.base_p[1], 0,0,1,self.base_p[2], 0,0,0,1]
        for i, (a, alpha, d, toff) in enumerate(self.dh):
            T = _mat4_mul(T, _dh(a, alpha, d, q[i] + toff))
        p = [T[3], T[7], T[11]]
        R = [T[0],T[1],T[2], T[4],T[5],T[6], T[8],T[9],T[10]]
        return p, R

    def jacobian(self, q):
        n = len(self.dh)
        pe, _ = self.fk(q)
        J = [[0.0]*n for _ in range(6)]
        z = [0.0, 0.0, 1.0]
        o = list(self.base_p)
        T = [1,0,0,self.base_p[0], 0,1,0,self.base_p[1], 0,0,1,self.base_p[2], 0,0,0,1]
        for i, (a, alpha, d, toff) in enumerate(self.dh):
            r = [pe[0]-o[0], pe[1]-o[1], pe[2]-o[2]]
            J[0][i] = z[1]*r[2]-z[2]*r[1]
            J[1][i] = z[2]*r[0]-z[0]*r[2]
            J[2][i] = z[0]*r[1]-z[1]*r[0]
            J[3][i] = z[0]; J[4][i] = z[1]; J[5][i] = z[2]
            T = _mat4_mul(T, _dh(a, alpha, d, q[i] + toff))
            z = [T[2], T[6], T[10]]
            o = [T[3], T[7], T[11]]
        return J


def pose_error(p_des, R_des, p_cur, R_cur):
    e = [p_des[0]-p_cur[0], p_des[1]-p_cur[1], p_des[2]-p_cur[2]]
    # Re = Rd * Rc^T
    Re = [0.0]*9
    for i in range(3):
        for j in range(3):
            s = 0.0
            for k in range(3):
                s += R_des[i*3+k]*R_cur[j*3+k]
            Re[i*3+j] = s
    e += [0.5*(Re[7]-Re[5]), 0.5*(Re[2]-Re[6]), 0.5*(Re[3]-Re[1])]
    return e


def _mat_inverse(M, n):
    # Gauss-Jordan;M 為 n x n list-of-lists,回傳反矩陣或 None
    a = [row[:] + [1.0 if i == j else 0.0 for j in range(n)] for i, row in enumerate(M)]
    for col in range(n):
        piv = max(range(col, n), key=lambda r: abs(a[r][col]))
        if abs(a[piv][col]) < 1e-9:
            return None
        a[col], a[piv] = a[piv], a[col]
        d = a[col][col]
        a[col] = [x/d for x in a[col]]
        for r in range(n):
            if r == col:
                continue
            f = a[r][col]
            a[r] = [a[r][k] - f*a[col][k] for k in range(2*n)]
    return [row[n:] for row in a]


def ik_step(kin, q, p_des, R_des, lam=0.05, gain=0.5):
    """DLS 單步：dq = J^T (J J^T + λ²I)^-1 e。就地更新 q,回傳誤差範數。"""
    p_cur, R_cur = kin.fk(q)
    e = pose_error(p_des, R_des, p_cur, R_cur)
    J = kin.jacobian(q)
    n = len(q)
    # JJt (6x6)
    JJt = [[sum(J[i][k]*J[j][k] for k in range(n)) for j in range(6)] for i in range(6)]
    for i in range(6):
        JJt[i][i] += lam*lam
    inv = _mat_inverse(JJt, 6)
    if inv is None:
        return math.sqrt(sum(x*x for x in e))
    tmp = [sum(inv[i][j]*e[j] for j in range(6)) for i in range(6)]   # 6
    dq = [sum(J[j][i]*tmp[j] for j in range(6)) for i in range(n)]    # n (J^T tmp)
    for i in range(n):
        q[i] += gain * dq[i]
    return math.sqrt(sum(x*x for x in e))
