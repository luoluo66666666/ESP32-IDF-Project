#include "mode_ctrl.h"
#include "driver/gpio.h"
#include "soc/gpio_reg.h"
#include "esp_log.h"
#include <stdio.h>

int do_pin[] = {
    GPIO_NUM_1,
    GPIO_NUM_2,
    GPIO_NUM_42,
    GPIO_NUM_41,
    GPIO_NUM_40,
    GPIO_NUM_39,
    GPIO_NUM_38,
    GPIO_NUM_37,
    GPIO_NUM_36,
    GPIO_NUM_35,
    GPIO_NUM_0,
    GPIO_NUM_45,
    GPIO_NUM_48,
    GPIO_NUM_47,
    GPIO_NUM_21,
    GPIO_NUM_14,
    GPIO_NUM_13,
    GPIO_NUM_12,
    GPIO_NUM_11,
    GPIO_NUM_10,
    GPIO_NUM_9,
    GPIO_NUM_46,
    GPIO_NUM_3,
    GPIO_NUM_8,
    GPIO_NUM_18,
    GPIO_NUM_17,
};

int di_pin[] = {
    GPIO_NUM_4,
    GPIO_NUM_5,
    GPIO_NUM_6,
    GPIO_NUM_7,
    GPIO_NUM_15,
    GPIO_NUM_16,
};

static const char *TAG = "MODE_CTRL";

esp_err_t pin_init(void)
{
    esp_err_t ret = ESP_OK;

    for (size_t i = 0; i < sizeof(do_pin) / sizeof(do_pin[0]); i++)
    {
        ret = gpio_reset_pin(do_pin[i]);
        if (ret != ESP_OK)
        {
            ESP_LOGE(TAG, "Failed to reset pin %d: %s", do_pin[i],
                     esp_err_to_name(ret));
            return ret; // Skip to the next pin if reset fails
        }

        ret = gpio_set_direction(do_pin[i], GPIO_MODE_OUTPUT);
        if (ret != ESP_OK)
        {
            ESP_LOGE(TAG, "Failed to set direction for pin %d: %s", do_pin[i],
                     esp_err_to_name(ret));
            return ret; // Skip to the next pin if setting direction fails
        }
    }

    for (size_t i = 0; i < sizeof(di_pin) / sizeof(di_pin[0]); i++)
    {
        ret = gpio_reset_pin(di_pin[i]);
        if (ret != ESP_OK)
        {
            ESP_LOGE(TAG, "Failed to reset pin %d: %s", di_pin[i],
                     esp_err_to_name(ret));
            return ret; // Skip to the next pin if reset fails
        }

        ret = gpio_set_direction(di_pin[i], GPIO_MODE_INPUT);
        if (ret != ESP_OK)
        {
            ESP_LOGE(TAG, "Failed to set direction for pin %d: %s", di_pin[i],
                     esp_err_to_name(ret));
            return ret; // Skip to the next pin if setting direction fails
        }
        ret = gpio_set_pull_mode(di_pin[i], GPIO_PULLUP_ENABLE);
        if (ret != ESP_OK)
        {
            ESP_LOGE(TAG, "Failed to set pull mode for pin %d: %s", di_pin[i],
                     esp_err_to_name(ret));
            return ret; // Skip to the next pin if setting pull mode fails
        }
    }
    return ret;
}

int set_do_pin(int index, int level)
{
    if (index < 0 || index >= sizeof(do_pin) / sizeof(do_pin[0]))
    {
        ESP_LOGE(TAG, "Index out of bounds: %d", index);
        return -1; // Return an error codeF
    }
    esp_err_t ret = gpio_set_level(do_pin[index], level);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to set DO pin %d to level %d: %s", do_pin[index], level,
                 esp_err_to_name(ret));
        return ret; // Return an error code
    }
    ESP_LOGI(TAG, "Set DO%d pin: %d to level: %d", index, do_pin[index], level);
    return 0; // Success
}

uint32_t get_output_reg_level(gpio_num_t gpio_num)
{
    // from GPIO_OUT_REG read current output level of the GPIO pin
    uint32_t reg_value = REG_READ(GPIO_OUT_REG);
    return (reg_value >> gpio_num) & 0x1;
}

int get_do_pin(int index)
{
    if (index < 0 || index >= sizeof(do_pin) / sizeof(do_pin[0]))
    {
        ESP_LOGE(TAG, "Index out of bounds: %d", index);
        return -1; // Return an invalid pin number
    }
    int level = get_output_reg_level(do_pin[index]); // Ensure the pin is initialized
    ESP_LOGI(TAG, "Returning DO%d pin: %d, level: %d", index, do_pin[index], level);
    return level;
}

int get_di_pin(int index)
{
    if (index < 0 || index >= sizeof(di_pin) / sizeof(di_pin[0]))
    {
        ESP_LOGE(TAG, "Index out of bounds: %d", index);
        return -1; // Return an invalid pin number
    }
    int level = gpio_get_level(di_pin[index]); // Ensure the pin is initialized
    ESP_LOGI(TAG, "Returning DI%d pin: %d, level: %d", index, di_pin[index], level);
    return level;
}

