/**
 * @file  osal_defs.h（F746 bare-metal port）
 * @brief SOEM OSAL 型別——無 RTOS：執行緒/互斥鎖皆為佔位（單執行緒 loop engine）
 */
#ifndef _osal_defs_
#define _osal_defs_

#ifdef __cplusplus
extern "C" {
#endif

#include <time.h>   /* newlib struct timespec */

#ifdef EC_DEBUG
#include <stdio.h>
#define EC_PRINT printf
#else
#define EC_PRINT(...) \
   do                 \
   {                  \
   } while (0)
#endif

#ifndef OSAL_PACKED
#define OSAL_PACKED_BEGIN
#define OSAL_PACKED __attribute__((__packed__))
#define OSAL_PACKED_END
#endif

#define ec_timet            struct timespec

#define OSAL_THREAD_HANDLE  void *
#define OSAL_THREAD_FUNC    void
#define OSAL_THREAD_FUNC_RT void

#define osal_mutext         void *

#ifdef __cplusplus
}
#endif

#endif
