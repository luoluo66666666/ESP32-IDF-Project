#include <stdint.h>
#include "driver/i2c_master.h"
#include "esp_err.h"

/************************************i2c定义*****************************************/
typedef struct
{
    i2c_device_config_t i2c_dev_config_1;
    uint8_t addr_bits;      // 3表示bit18~16
    uint8_t write_delay_ms; // 写后等待
} my_i2c_device_config_t;

typedef struct
{
    i2c_port_t i2c_num; // I2C端口号（主机 I2C_NUM_0）
    uint8_t addr_bits;      // bit18~16
    uint8_t data_bits;      // bit15~8
    uint8_t write_delay_ms; // 写延时
    uint8_t buf[3];         // 协议发送/接收缓存
} my_i2c_device_t;

typedef my_i2c_device_t my_i2c_device_t;
typedef my_i2c_device_t *my_i2c_device_handle_t; /* i2c设备的句柄 */
/*----------------------------------------------------------------------------------*/
/************************************设备的协议栈定义*****************************************/
typedef enum
{
    MLB_ADDR_TEMP_MODE = 0x00, // 温度数据 / 模式
    MLB_ADDR_FLOW = 0x01,      // 流量数据
    MLB_ADDR_SYS = 0x02,       // 主机状态信息
    MLB_ADDR_SET_TEMP = 0x03,  // 恒温设置信息
    MLB_ADDR_FLOW_VAL = 0x04,  // 流量值
    MLB_ADDR_SYS2 = 0x05,      // 主机状态信息2
    MLB_ADDR_CALIB_FLOW = 0x04 // 校准流量（写操作）
} mlb_reg_addr_t;

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
    my_i2c_device_handle_t dev; // I2C 设备句柄
    mlb_reg_addr_t reg;         // MLB 读写寄存器
    mlb_write_data_t wdata;     // 写数据
    mlb_read_data_t rdata;      // 读数据
} mlb_device_t;
/*----------------------------------------------------------------------------------*/

/************************************恒温系统定义*****************************************/
typedef struct
{
    mlb_device_t *dev;  // MLB 设备句柄
    int target_temp;    // 目标温度
    int temp_tolerance; // 温差容忍
    int adjust_speed;   // 调温速度
    int maintain_speed; // 恒温维持速度
    int current_temp;   // 当前温度
    int current_speed;  // 当前调温速度
    bool heating;       // 是否加热
} thermostat_t;

// // 指针类型别名
// typedef thermostat_t *thermo;

/*----------------------------------------------------------------------------------*/
extern thermostat_t thermo; // 静态实例
extern mlb_device_t dev;    // MLB 设备实例

void i2c_master_init(void);
esp_err_t my_i2c_master_init(my_i2c_device_handle_t *out_dev);

esp_err_t i2c_write_reg(my_i2c_device_handle_t dev, uint8_t addr, uint8_t data);
esp_err_t i2c_read_reg(my_i2c_device_handle_t dev, uint8_t addr, uint8_t *out);
esp_err_t mlb_set_temperature(mlb_device_t *mlb, uint8_t temp);
esp_err_t mlb_get_temperature(mlb_device_t *mlb, uint8_t *temp);
esp_err_t mlb_set_system_info(mlb_device_t *mlb, uint8_t data);
esp_err_t mlb_get_system_status(mlb_device_t *mlb, uint8_t *status);
esp_err_t mlb_set_flow_calib(mlb_device_t *mlb, uint8_t flow);
esp_err_t mlb_get_flow(mlb_device_t *mlb, uint8_t *flow);
void thermostat_handle_cmd(thermostat_t *thermo, const char *cmd, char *response, size_t resp_len);

void app_init_task(void);
