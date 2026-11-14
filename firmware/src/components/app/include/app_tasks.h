#pragma once

#include <stdint.h>

typedef struct {
    const char *name;
    uint32_t stack;
    uint32_t prio;
} app_task_cfg_t;

/*
 * Every task the firmware creates. Stack sizes are checked against the
 * high-water marks reported by the console command `tasks`.
 */
#define APP_TASK_APP      ((app_task_cfg_t){"app", 6144, 5})
#define APP_TASK_UI       ((app_task_cfg_t){"ui", 8192, 4})
#define APP_TASK_NET      ((app_task_cfg_t){"net", 8192, 4})
#define APP_TASK_WS       ((app_task_cfg_t){"ws", 8192, 5})
#define APP_TASK_PWR      ((app_task_cfg_t){"pwr", 3072, 2})
#define APP_TASK_CONSOLE  ((app_task_cfg_t){"console", 4096, 3})
