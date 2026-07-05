/**
 * @file test_config.c — 設定檔驗收（項目 7）
 *
 * 逐鍵 apply（含萬用/越界/未知鍵）、檔案載入（註解/空白/行內註解）、
 * 壞檔整檔放棄（不半套用）、reset 還原預設。
 * 注意：本測試會動全域設定,結尾必 reset（其他測試吃預設值）。
 */
#include "test_framework.h"
#include "robot_config.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

static int feq(float a, float b) { return fabsf(a - b) < 1e-4f; }

void test_config(void)
{
    robot_config_reset();

    /* ---- 逐鍵 apply ---- */
    {
        CHECK(robot_config_apply("joint.3.vmax", 1.25f) == 0);
        CHECK(feq(robot_js_cfg()[3].vmax, 1.25f));
        CHECK(feq(robot_js_cfg()[4].vmax, 2.0f));       /* 他軸不動 */

        CHECK(robot_config_apply("joint.*.amax", 5.0f) == 0);   /* 萬用 */
        CHECK(feq(robot_js_cfg()[0].amax, 5.0f) &&
              feq(robot_js_cfg()[13].amax, 5.0f));

        CHECK(robot_config_apply("ik.max_iters", 42.0f) == 0);
        CHECK(robot_ik_cfg()->max_iters == 42);
        CHECK(robot_config_apply("q_init.3", 0.9f) == 0);
        CHECK(feq(robot_q_init()[3], 0.9f));
        CHECK(robot_config_apply("base.right.y", -0.25f) == 0);
        CHECK(feq(robot_right_kin()->base_p[1], -0.25f));
        CHECK(robot_config_apply("dh.left.2.d", 0.31f) == 0);
        CHECK(feq(robot_left_kin()->dh[2].d, 0.31f));
        CHECK(feq(robot_right_kin()->dh[2].d, 0.30f));  /* 右臂不受左鍵影響 */

        CHECK(robot_config_apply("joint.14.vmax", 1.0f) == -1); /* 越界 */
        CHECK(robot_config_apply("joint.3.nope", 1.0f) == -1);  /* 未知欄 */
        CHECK(robot_config_apply("joint.3", 1.0f) == -1);       /* 段不足 */
        CHECK(robot_config_apply("joint.3.vmax.x", 1.0f) == -1);/* 段過多 */
        CHECK(robot_config_apply("what.ever", 1.0f) == -1);
        CHECK(robot_config_apply("q_init.7", 1.0f) == -1);
        CHECK(robot_config_apply("base.left.w", 1.0f) == -1);
    }

    /* ---- reset 還原 ---- */
    {
        robot_config_reset();
        CHECK(feq(robot_js_cfg()[3].vmax, 2.0f));
        CHECK(feq(robot_js_cfg()[0].amax, 8.0f));
        CHECK(robot_ik_cfg()->max_iters == 100);
        CHECK(feq(robot_right_kin()->base_p[1], -0.20f));
    }

    /* ---- 檔案載入 ---- */
    {
        const char *path = "test_config_tmp.cfg";
        FILE *f = fopen(path, "w");
        CHECK(f != NULL);
        fprintf(f, "# 註解行\n\n"
                   "  joint.0.vmax = 1.1   # 行內註解\n"
                   "joint.*.q_max=2.5\n"
                   "ik.lambda = 0.07\n");
        fclose(f);
        CHECK(robot_config_load(path) == 3);
        CHECK(feq(robot_js_cfg()[0].vmax, 1.1f));
        CHECK(feq(robot_js_cfg()[9].q_max, 2.5f));
        CHECK(feq(robot_ik_cfg()->lambda, 0.07f));

        /* 壞檔：第 2 鍵壞 → 整檔放棄,第 1 鍵也不得套用 */
        robot_config_reset();
        f = fopen(path, "w");
        fprintf(f, "joint.0.vmax = 1.1\njoint.0.bogus = 9\n");
        fclose(f);
        CHECK(robot_config_load(path) == -2);
        CHECK(feq(robot_js_cfg()[0].vmax, 2.0f));       /* 未半套用 */

        f = fopen(path, "w");                            /* 格式錯 */
        fprintf(f, "joint.0.vmax 1.1\n");
        fclose(f);
        CHECK(robot_config_load(path) == -2);
        f = fopen(path, "w");
        fprintf(f, "joint.0.vmax = abc\n");
        fclose(f);
        CHECK(robot_config_load(path) == -2);

        CHECK(robot_config_load("no_such_file.cfg") == -1);
        remove(path);
    }

    robot_config_reset();      /* 後續測試吃預設值 */
}
