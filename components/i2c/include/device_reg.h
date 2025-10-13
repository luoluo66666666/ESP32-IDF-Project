#include <stdint.h>
#include "driver/i2c_master.h"
#include "esp_err.h"

typedef struct
{
    uint8_t D; // bit7~0 校验码（C段反码）
    uint8_t C; // bit15~8 数据段（不同地址意义不同）

    union
    {
        uint8_t addr; // bit18~16 地址段（S2~S0）
    };

    uint8_t Write_Flag : 1; // bit19 读写标志：0=写，1=读
} mlb_reg_t;

// ================= 写数据结构 =================
typedef struct
{
    // addr=000 温度数据 / 模式
    struct
    {
        uint8_t temp_set; // 温度值/模式值，例如 24=冷水, 49/56=热水, 25~48/25~55=设定温度
    } temperature;

    // addr=010 系统信息
    struct
    {
        uint8_t speed_mode : 3; // b2~b0 调温速度等级 (1~5)
        uint8_t reserved1 : 1;  // b3 预留
        uint8_t reserved2 : 1;  // b4 预留
        uint8_t speed_flag : 1; // b5 调温速度调整标志
        uint8_t temp_flag : 1;  // b6 温度设置标志
        uint8_t power : 1;      // b7 开关机
    } system_info;

    // addr=100 校准流量
    struct
    {
        uint8_t flow_calib; // b7~b0 流量校准值，用于校正流量,通过外设输入正常的流量值来校正所测量流量数据
    } flow;
} mlb_write_data_t;

// MLB 读数据结构
typedef struct
{
    // addr=000 温度数据
    struct
    {
        uint8_t temperature : 7; // b6~b0 主机温度 0~99, 温度探头故障时返回0
        uint8_t reserved : 1;    // b7 预留
    } temp_data;

    // addr=001 流量数据
    uint8_t flow; // b7~b0 0~255, 对应流量 *0.1L/min

    // addr=010 主机状态信息
    struct
    {
        uint8_t speed_mode : 3; // b2~b0 当前恒温速度模式 1~5
        uint8_t speed_set : 1;  // b3 恒温速度设置标志
        uint8_t mode_flag : 2;  // b4~b5 冷水/热水模式状态
        uint8_t work_flag : 1;  // b6 工作状态标志，0常规状态，1设置参数状态
        uint8_t power : 1;      // b7 开关机状态，0休眠，1工作
    } host_status;

    // addr=011 恒温设置信息
    uint8_t set_temp; // b7~b0 恒温设定/模式值

    // addr=100 流量值
    uint8_t flow_value; // b7~b0 当流量大于25.5L时，addr=01+此值为实际流量

    // addr=101 主机状态信息2
    struct
    {
        uint8_t temp_sensor_fault : 1; // bit0 温度探头故障 0正常，1故障
        uint8_t reserved : 7;          // 其他标志位
    } host_status2;

    // 原始 C/D 段，用于校验
    uint8_t C; // bit15~8 数据段
    uint8_t D; // bit7~0 校验码 (C段反码)

    // 数据有效标志
    bool valid; // true: 校验通过，false: 校验失败
} mlb_read_data_t;

typedef struct
{
    i2c_device_config_t i2c_dev_config_1;
    uint8_t addr_bits; // 3表示bit18~16
    uint8_t write_delay_ms; // 写后等待
} my_i2c_device_config_t;

typedef struct
{
    i2c_master_dev_handle_t i2c_dev_1;
    uint8_t addr_bits;       // 3表示bit18~16
    uint8_t data_bits;       // 8表示bit15~8
    uint8_t write_delay_ms; // 写后等待
    uint8_t buf[3];          // 协议缓冲区
} my_i2c_device_t;

typedef  my_i2c_device_t my_i2c_device_t;
typedef  my_i2c_device_t *my_i2c_device_handle_t;    /* i2c设备的句柄 */

static void i2c_master_init(void);
static void i2c_slave_init(void);
static void i2c_master_task(void *arg);
static void i2c_slave_task(void *arg);
void i2c_main(void);
