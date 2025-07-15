#include "ctrl_protocol.h"
#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"

#include "esp_log.h"

#define RUN_BIT BIT10
#define FAULT_BIT BIT11

#define MODE0_BIT BIT0
#define MODE1_BIT BIT1
#define MODE2_BIT BIT2
#define MODE3_BIT BIT3
#define MODE4_BIT BIT4
#define MODE5_UPPER_BIT BIT5
#define MODE5_LOWER_BIT BIT6
#define MODE_CLEAR BIT7

static EventGroupHandle_t e_ctrl_protocol;

const char *TAG = "CTRL_PROTOCOL";

void ctrl_protocol_init(void)
{
    e_ctrl_protocol = xEventGroupCreate();
    if (e_ctrl_protocol == NULL)
    {
        ESP_LOGE(TAG, "Failed to create event group");
    }
    xEventGroupSetBits(e_ctrl_protocol, MODE0_BIT);

    xEventGroupClearBits(e_ctrl_protocol, MODE1_BIT |
                                              MODE2_BIT |
                                              MODE3_BIT |
                                              MODE4_BIT |
                                              MODE5_UPPER_BIT |
                                              MODE5_LOWER_BIT |
                                              MODE_CLEAR);

    xEventGroupClearBits(e_ctrl_protocol, FAULT_BIT);
    xEventGroupClearBits(e_ctrl_protocol, RUN_BIT);
}

int set_fault(void)
{
    xEventGroupSetBits(e_ctrl_protocol, FAULT_BIT);
    return 0;
}

int clear_fault(void)
{
    xEventGroupClearBits(e_ctrl_protocol, FAULT_BIT);
    return 0;
}

int get_fault(void)
{
    EventBits_t e_bits = xEventGroupGetBits(e_ctrl_protocol);

    if (e_bits & FAULT_BIT)
    {
        return 1;
    }
    return 0;
}

int set_run(void)
{
    EventBits_t e_bits = xEventGroupGetBits(e_ctrl_protocol);

    if (e_bits & FAULT_BIT)
    {
        ESP_LOGE(TAG, "Can't run while fault");
        ESP_LOGE(TAG, "Please clear fault first");
        return -1;
    }
    xEventGroupSetBits(e_ctrl_protocol, RUN_BIT);
    return 0;
}

int clear_run(void)
{
    xEventGroupClearBits(e_ctrl_protocol, RUN_BIT);
    return 0;
}

int get_run(void)
{
    EventBits_t e_bits = xEventGroupGetBits(e_ctrl_protocol);

    if (e_bits & RUN_BIT)
    {
        return 1;
    }
    return 0;
}

/**
 * @author   :wangwang105
 * @brief    :Set the mode object
 * @param    {mode}:0:MODE0,1:MODE1,2:MODE2,3:MODE3,4:MODE4,5:MODE5_UPPER,6:MODE5_LOWER
 * @return   {int}:return the mode object currently set
 * @note     :return -1 if the mode is invalid or the mode can't be changed while running or fault
 * @date     :2025-07-10 09:41:06
 */
int set_mode(int mode)
{
    EventBits_t e_bits = xEventGroupGetBits(e_ctrl_protocol);

    if (e_bits & RUN_BIT)
    {
        ESP_LOGE(TAG, "Can't change mode while running");
        return -1;
    }
    if (e_bits & FAULT_BIT)
    {
        ESP_LOGE(TAG, "Can't change mode while fault");
        ESP_LOGE(TAG, "Please clear fault first");
        return -1;
    }

    if (mode < 0 || mode > 6)
    {
        ESP_LOGE(TAG, "Invalid mode");
        return -1;
    }

    switch (mode)
    {
    case 0:
        xEventGroupClearBits(e_ctrl_protocol, MODE0_BIT | MODE1_BIT | MODE2_BIT | MODE3_BIT | MODE4_BIT | MODE5_UPPER_BIT | MODE5_LOWER_BIT | MODE_CLEAR);
        xEventGroupSetBits(e_ctrl_protocol, MODE0_BIT);
        break;
    case 1:
        xEventGroupClearBits(e_ctrl_protocol, MODE0_BIT | MODE1_BIT | MODE2_BIT | MODE3_BIT | MODE4_BIT | MODE5_UPPER_BIT | MODE5_LOWER_BIT | MODE_CLEAR);
        xEventGroupSetBits(e_ctrl_protocol, MODE1_BIT);
        break;
    case 2:
        xEventGroupClearBits(e_ctrl_protocol, MODE0_BIT | MODE1_BIT | MODE2_BIT | MODE3_BIT | MODE4_BIT | MODE5_UPPER_BIT | MODE5_LOWER_BIT | MODE_CLEAR);
        xEventGroupSetBits(e_ctrl_protocol, MODE2_BIT);
        break;
    case 3:
        xEventGroupClearBits(e_ctrl_protocol, MODE0_BIT | MODE1_BIT | MODE2_BIT | MODE3_BIT | MODE4_BIT | MODE5_UPPER_BIT | MODE5_LOWER_BIT | MODE_CLEAR);
        xEventGroupSetBits(e_ctrl_protocol, MODE3_BIT);
        break;
    case 4:
        xEventGroupClearBits(e_ctrl_protocol, MODE0_BIT | MODE1_BIT | MODE2_BIT | MODE3_BIT | MODE4_BIT | MODE5_UPPER_BIT | MODE5_LOWER_BIT | MODE_CLEAR);
        xEventGroupSetBits(e_ctrl_protocol, MODE4_BIT);
        break;
    case 5:
        xEventGroupClearBits(e_ctrl_protocol, MODE0_BIT | MODE1_BIT | MODE2_BIT | MODE3_BIT | MODE4_BIT | MODE5_UPPER_BIT | MODE5_LOWER_BIT | MODE_CLEAR);
        xEventGroupSetBits(e_ctrl_protocol, MODE5_UPPER_BIT);
        break;
    case 6:
        xEventGroupClearBits(e_ctrl_protocol, MODE0_BIT | MODE1_BIT | MODE2_BIT | MODE3_BIT | MODE4_BIT | MODE5_UPPER_BIT | MODE5_LOWER_BIT | MODE_CLEAR);
        xEventGroupSetBits(e_ctrl_protocol, MODE5_LOWER_BIT);
        break;
    case 7:
        xEventGroupClearBits(e_ctrl_protocol, MODE0_BIT | MODE1_BIT | MODE2_BIT | MODE3_BIT | MODE4_BIT | MODE5_UPPER_BIT | MODE5_LOWER_BIT | MODE_CLEAR);
        xEventGroupSetBits(e_ctrl_protocol, MODE_CLEAR);
        break;
    default:
        ESP_LOGE(TAG, "Invalid mode");
        return mode = -1;
        break;
    }
    return mode;
}

/**
 * @author   :wangwang105
 * @brief    :Get the mode object currently set
 * @return   {int}: return the mode object currently set
 * @note     :retrun: 0:MODE0,1:MODE1,2:MODE2,3:MODE3,4:MODE4,5:MODE5_UPPER,6:MODE5_LOWER -1:invalid mode
 * @date     :2025-07-10 09:40:00
 */
int get_mode(void)
{
    EventBits_t e_bits = xEventGroupGetBits(e_ctrl_protocol);

    if (e_bits & MODE0_BIT)
    {
        return 0;
    }
    if (e_bits & MODE1_BIT)
    {
        return 1;
    }
    if (e_bits & MODE2_BIT)
    {
        return 2;
    }
    if (e_bits & MODE3_BIT)
    {
        return 3;
    }
    if (e_bits & MODE4_BIT)
    {
        return 4;
    }
    if (e_bits & MODE5_UPPER_BIT)
    {
        return 5;
    }
    if (e_bits & MODE5_LOWER_BIT)
    {
        return 6;
    }
    if (e_bits & MODE_CLEAR)
    {
        return 7;
    }

    return -1;
}

void ctrl_protocol(const char *input, char *output, int max_len)
{
    ESP_LOGI(TAG, "Received len:%d command: %s", strnlen(input, max_len), input);
    
    if (strncmp(input, "CMD:MODE0\r\n", strnlen("CMD:MODE0\r\n",max_len)) == 0)
    {
        if (get_run() == 1)
        {
            ESP_LOGE(TAG, "Can't change mode while running");
            ESP_LOGE(TAG, "Please stop running first");
            goto err_;
        }
        if (get_fault() == 1)
        {
            ESP_LOGE(TAG, "Can't change mode while fault");
            ESP_LOGE(TAG, "Please clear fault first");
            goto err_;
        }

        set_mode(0);
        snprintf(output, max_len, "CMD:MODE1,OK\r\n");
        return;
    }
    else if (strncmp(input, "CMD:MODE1\r\n", strnlen("CMD:MODE1\r\n",max_len)) == 0)
    {
        if (get_run() == 1)
        {
            ESP_LOGE(TAG, "Can't change mode while running");
            ESP_LOGE(TAG, "Please stop running first");
            goto err_;
        }
        if (get_fault() == 1)
        {
            ESP_LOGE(TAG, "Can't change mode while fault");
            ESP_LOGE(TAG, "Please clear fault first");
            goto err_;
        }

        set_mode(1);
        snprintf(output, max_len, "CMD:MODE1,OK\r\n");
        return;
    }
    else if (strncmp(input, "CMD:MODE2\r\n", strnlen("CMD:MODE2\r\n",max_len)) == 0)
    {
        if (get_run() == 1)
        {
            ESP_LOGE(TAG, "Can't change mode while running");
            ESP_LOGE(TAG, "Please stop running first");
            goto err_;
        }
        if (get_fault() == 1)
        {
            ESP_LOGE(TAG, "Can't change mode while fault");
            ESP_LOGE(TAG, "Please clear fault first");
            goto err_;
        }

        set_mode(2);
        snprintf(output, max_len, "CMD:MODE2,OK\r\n");
        return;
    }
    else if (strncmp(input, "CMD:MODE3\r\n", strnlen("CMD:MODE3\r\n",max_len)) == 0)
    {
        if (get_run() == 1)
        {
            ESP_LOGE(TAG, "Can't change mode while running");
            ESP_LOGE(TAG, "Please stop running first");
            goto err_;
        }
        if (get_fault() == 1)
        {
            ESP_LOGE(TAG, "Can't change mode while fault");
            ESP_LOGE(TAG, "Please clear fault first");
            goto err_;
        }

        set_mode(3);
        snprintf(output, max_len, "CMD:MODE3,OK\r\n");
        return;
    }
    else if (strncmp(input, "CMD:MODE4\r\n", strnlen("CMD:MODE4\r\n",max_len)) == 0)
    {
        if (get_run() == 1)
        {
            ESP_LOGE(TAG, "Can't change mode while running");
            ESP_LOGE(TAG, "Please stop running first");
            goto err_;
        }
        if (get_fault() == 1)
        {
            ESP_LOGE(TAG, "Can't change mode while fault");
            ESP_LOGE(TAG, "Please clear fault first");
            goto err_;
        }

        set_mode(4);
        snprintf(output, max_len, "CMD:MODE4,OK\r\n");
        return;
    }
    else if (strncmp(input, "CMD:MODE5_UPPER\r\n", strnlen("CMD:MODE5_UPPER\r\n",max_len)) == 0)
    {
        if (get_run() == 1)
        {
            ESP_LOGE(TAG, "Can't change mode while running");
            ESP_LOGE(TAG, "Please stop running first");
            goto err_;
        }
        if (get_fault() == 1)
        {
            ESP_LOGE(TAG, "Can't change mode while fault");
            ESP_LOGE(TAG, "Please clear fault first");
            goto err_;
        }
        set_mode(5);
        snprintf(output, max_len, "CMD:MODE5_UPPER,OK\r\n");
        return;
    }
    else if (strncmp(input, "CMD:MODE5_LOWER\r\n", strnlen("CMD:MODE5_LOWER\r\n",max_len)) == 0)
    {
        if (get_run() == 1)
        {
            ESP_LOGE(TAG, "Can't change mode while running");
            ESP_LOGE(TAG, "Please stop running first");
            goto err_;
        }
        if (get_fault() == 1)
        {
            ESP_LOGE(TAG, "Can't change mode while fault");
            ESP_LOGE(TAG, "Please clear fault first");
            goto err_;
        }
        set_mode(6);
        snprintf(output, max_len, "CMD:MODE5_LOWER,OK\r\n");
        return;
    }
    else if (strncmp(input, "CMD:MODE_CLEAR\r\n", strnlen("CMD:MODE_CLEAR\r\n",max_len)) == 0)
    {
        if (get_run() == 1)
        {
            ESP_LOGE(TAG, "Can't change mode while running");
            ESP_LOGE(TAG, "Please stop running first");
            goto err_;
        }
        if (get_fault() == 1)
        {
            ESP_LOGE(TAG, "Can't change mode while fault");
            ESP_LOGE(TAG, "Please clear fault first");
            goto err_;
        }
        set_mode(7);
        snprintf(output, max_len, "CMD:MODE_CLEAR,OK\r\n");
        return;
    }
    else if (strncmp(input, "CMD:MODE_START\r\n", strnlen("CMD:MODE_START\r\n",max_len)) == 0)
    {
        if (get_run() == 1)
        {
            ESP_LOGE(TAG, "Can't change run mode while running");
            ESP_LOGE(TAG, "Please stop running first");
            goto err_;
        }
        if (get_fault() == 1)
        {
            ESP_LOGE(TAG, "Can't change run mode while fault");
            ESP_LOGE(TAG, "Please clear fault first");
            goto err_;
        }
        set_run();
        snprintf(output, max_len, "CMD:MODE_START,OK\r\n");
        return;
    }
    else if (strncmp(input, "CMD:MODE_STOP\r\n", strnlen("CMD:MODE_STOP\r\n",max_len)) == 0)
    {
        if (get_run() == 0)
        {
            ESP_LOGE(TAG, "Can't stop while not running");
            ESP_LOGE(TAG, "Please start running first");
            goto err_;
        }
        if (get_fault() == 1)
        {
            ESP_LOGE(TAG, "Can't stop while fault");
            ESP_LOGE(TAG, "Please clear fault first");
            goto err_;
        }
        clear_run();
        snprintf(output, max_len, "CMD:MODE_STOP,OK\r\n");
        return;
    }
    else if (strncmp(input, "CMD:GET_STATUS\r\n", strnlen("CMD:GET_STATUS\r\n",max_len)) == 0)
    {
        if (get_fault() == 1)
        {
            snprintf(output, max_len, "CMD:FAULT\r\n");
            return;
        }
        if (get_run() == 1)
        {
            snprintf(output, max_len, "CMD:RUN\r\n");
            return;
        }
        else
        {
            snprintf(output, max_len, "CMD:STOP\r\n");
            return;
        }
    }

err_:
    ESP_LOGE(TAG, "Invalid command");
    snprintf(output, max_len, "CMD:ERR\r\n");
    return;
}
