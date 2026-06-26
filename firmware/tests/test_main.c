/** @file test_main.c — 單元測試進入點 */
#include "test_framework.h"

int g_tests = 0, g_fails = 0;

void test_trajectory(void);
void test_kinematics(void);
void test_ik(void);
void test_cia402(void);
void test_hostif(void);
void test_canopen(void);

int main(void)
{
    printf("===== STM32F746 雙臂控制 單元測試 =====\n\n");
    RUN(test_trajectory);
    RUN(test_kinematics);
    RUN(test_ik);
    RUN(test_cia402);
    RUN(test_hostif);
    RUN(test_canopen);

    printf("\n----------------------------------------\n");
    printf("總計 %d 檢查, %d 失敗 → %s\n", g_tests, g_fails,
           g_fails == 0 ? "PASS" : "FAIL");
    return g_fails == 0 ? 0 : 1;
}
