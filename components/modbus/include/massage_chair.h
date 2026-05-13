#ifndef __MASSAGE_CHAIR_H__
#define __MASSAGE_CHAIR_H__

#include "esp_err.h"

/* 按摩椅模式 */
typedef enum {
    MASSAGE_MODE_STOP = 0,
    MASSAGE_MODE_1 = 1,
    MASSAGE_MODE_2 = 2,
    MASSAGE_MODE_3 = 3
} massage_mode_t;

/* 按摩椅力度 */
typedef enum {
    MASSAGE_STRENGTH_LOW = 0,
    MASSAGE_STRENGTH_MEDIUM = 1,
    MASSAGE_STRENGTH_HIGH = 2
} massage_strength_t;

/* 停止按摩椅 */
esp_err_t massage_chair_stop(void);

/* 设置按摩椅模式 */
esp_err_t massage_chair_set_mode(massage_mode_t mode);

/* 设置按摩椅力度 */
esp_err_t massage_chair_set_strength(massage_strength_t strength);

/* 按摩椅测试任务 */
void massage_chair_test_task(void *arg);

#endif
