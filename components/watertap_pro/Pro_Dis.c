#define _PRO_C

#include "Pro_Dis.h"
#include "driver/gpio.h"

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_timer.h"

#include "esp_log.h"

static const char *TAG = "ProDis";

#define Temp_CLK_GPIO GPIO_NUM_2
#define Temp_DATA_GPIO GPIO_NUM_4

// Clk取反
void Anti_ProClk()
{
	bClk = !bClk;
	if (bClk)
	{
		Set_Pro_CLK;
	}
	else
	{
		Clr_Pro_CLK;
	}
}

//--------------------------------------------------------------------//
// 端口初始化程序
//--------------------------------------------------------------------//
void ProDis_Init(void)
{
	// 配置时钟和数据引脚为输出
	gpio_config_t io_conf = {
		.pin_bit_mask = (1ULL << Temp_CLK_GPIO) | (1ULL << Temp_DATA_GPIO),
		.mode = GPIO_MODE_OUTPUT,
		.pull_up_en = GPIO_PULLUP_DISABLE,
		.pull_down_en = GPIO_PULLDOWN_DISABLE,
		.intr_type = GPIO_INTR_DISABLE};
	gpio_config(&io_conf);

	// 清零时钟和数据引脚
	Clr_Pro_CLK;
	Clr_Pro_DATA;
	bClk = false;

	SysInfounion.bit.b7 = 0;
	EmcOp = OpNone;
	bPro_Init = 1;
}

void com_delay(unsigned char t)
{
	unsigned char i;
	for (i = 0; i < t; i++)
		;
}

//--------------------------------------------------------------------//
// 设置数据端口输入/输出
//--------------------------------------------------------------------//
void ProDis_DataIOSet(unsigned char kType)
{
#ifdef exchangcom
	gpio_num_t pin = Temp_CLK_GPIO; // 替换原来的 GPIO_Pin_9
#else
	gpio_num_t pin = Temp_DATA_GPIO; // 替换原来的 GPIO_Pin_10
#endif

	gpio_config_t io_conf = {
		.pin_bit_mask = (1ULL << pin),
		.pull_up_en = GPIO_PULLUP_DISABLE,
		.pull_down_en = GPIO_PULLDOWN_DISABLE,
		.intr_type = GPIO_INTR_DISABLE};

	if (kType == 0) // 输出模式
	{
		io_conf.mode = GPIO_MODE_OUTPUT;
	}
	else if (kType == 1) // 输入模式，上拉输入
	{
		io_conf.mode = GPIO_MODE_INPUT;
		io_conf.pull_up_en = GPIO_PULLUP_ENABLE;
	}

	gpio_config(&io_conf);
}
//--------------------------------------------------------------------//
// 通信程序,1ms中断调用
//--------------------------------------------------------------------//
void ProDis_Run1ms(void)
{
	static Int8 ctrbyte;
	static Int8 rdctrbyte;
	Int8 tmp, levelck[3], proda_tmp;
	if (Idle_Time) // 空闲
	{
		// 设置时钟高电平
		Set_Pro_CLK;
		bClk = 1;

		if (Idle_Time < TIME_IDLE)
		{
			// 设置数据脚为输出，并置高电平
			ProDis_DataIOSet(0); // 输出模式
			Set_Pro_DATA;
		}

		Idle_Time--;
		return; // 空闲阶段结束
	}
	else
	{
		if (OpCnt)
		{
			switch (OpCnt)
			{
			case 40:
			case 38:
			case 36:
			case 34:
				ProDis_DataIOSet(0);
				if (ctrbyte & 0x08)
				{
					Set_Pro_DATA;
				}
				else
				{
					Clr_Pro_DATA;
				}
				ctrbyte <<= 1;
				break;

			case 32:
			case 30:
			case 28:
			case 26:
			case 24:
			case 22:
			case 20:
			case 18:
			case 16:
			case 14:
			case 12:
			case 10:
			case 8:
			case 6:
			case 4:
			case 2:
				if (bWrite)
				{
					ProDis_DataIOSet(0);
					if (ProSend & 0x8000)
					{
						Set_Pro_DATA;
					}
					else
					{
						Clr_Pro_DATA;
					}
					ProSend <<= 1;
				}
				else
				{
					ProDis_DataIOSet(1);
					ProRead <<= 1;
				}
				break;

			case 33:
			case 31:
			case 29:
			case 27:
			case 25:
			case 23:
			case 21:
			case 19:
			case 17:
			case 15:
			case 13:
			case 11:
			case 9:
			case 7:
			case 5:
			case 3:
			case 1:
				if (!bWrite)
				{
					levelck[0] = get_ProData;
					levelck[1] = 0;
					levelck[2] = 0;

					com_delay(50);
					levelck[1] = get_ProData;

					com_delay(50);
					levelck[2] = get_ProData;

					if (levelck[0] != levelck[1])
					{
						proda_tmp = levelck[2];
					}
					else if (levelck[0] != levelck[2])
					{
						proda_tmp = levelck[1];
					}
					else if (levelck[1] != levelck[2])
					{
						proda_tmp = levelck[0];
					}
					else
					{
						proda_tmp = levelck[0];
					}

					if (proda_tmp)
					{
						ProRead |= 1;
					}
				}
				break;

			default:
				break;
			}

			Anti_ProClk();
			OpCnt--;

			if (OpCnt == 0)
			{
				Idle_Time = TIME_IDLE;
				if (!bWrite)
				{
					if (((ProRead & 0x00ff) ^ ((ProRead >> 8) & 0xff)) == 0xff)
					{
						ErrTime = 50;
						switch (rdctrbyte)
						{
						case 8:
							ReadTemp = (Int8)((ProRead >> 8) & 0xff);
							break;

						case 9:
							ReadFlowL = (INT8U)((ProRead >> 8) & 0xff);
							bRd_FlowL = 1;
							break;

						case 0x0c:
							ReadFlowH = (INT8U)((ProRead >> 8) & 0xff);
							bRd_FlowH = 1;
							break;

						case 0x0b:
							Read_DevID = (INT8U)((ProRead >> 8) & 0xff);
							break;

						case 10:
							ReadErrCode = (Int8)((ProRead >> 8) & 0xff);
							break;

						default:
							break;
						}
					}
					ProRead = 0;
					rdctrbyte = 0;
				}
			}
		}
		else
		{
			OpCnt = 40;
			Clr_Pro_CLK;
			Clr_Pro_DATA;
			bClk = FALSE;

			if (EmcOp)
			{
				tmp = EmcOp;
				EmcOp = OpNone;
			}
			else if (FlowDataSetBck != Flwo_SendData)
			{
				FlowDataSetBck = Flwo_SendData;
				tmp = wFlow;
			}
			else if (SysInfoBK != SysInfounion.byte)
			{
				SysInfoBK = SysInfounion.byte;
				tmp = wSysInfo;
			}
			else if (TempDataSetBck != TempDataSet)
			{
				TempDataSetBck = TempDataSet;
				tmp = wTemp;
			}
			else
			{
				tmp = NextOp;
				NextOp++;
				if (NextOp > rErr)
				{
					NextOp = wTemp;
				}
			}

			switch (tmp)
			{
			case OpIdle:
			case OpNone:
				OpCnt = 0;
				Idle_Time = TIME_IDLE;
				bWrite = 0;
				ProDis_DataIOSet(0);
				break;

			case wTemp:
				ctrbyte = 0x00;
				ProSend = TempDataSet;
				ProSend = (ProSend << 8) | (TempDataSet ^ 0xff);
				bWrite = 1;
				ProDis_DataIOSet(0);
				break;

			case wFlow:
				ctrbyte = 0x01;
				ProSend = FlowDataSet;
				ProSend = (ProSend << 8) | (FlowDataSet ^ 0xff);
				bWrite = 1;
				ProDis_DataIOSet(0);
				break;

			case wSysInfo:
				ctrbyte = 0x02;
				ProSend = SysInfoSet;
				ProSend = (ProSend << 8) | (SysInfoSet ^ 0xff);
				bWrite = 1;
				ProDis_DataIOSet(0);
				break;

			case rTemp:
			case rTemp1:
				ctrbyte = 0x08;
				ProRead = 0;
				bWrite = 0;
				rdctrbyte = 8;
				ProDis_DataIOSet(0);
				break;

			case rFlow:
				ctrbyte = 0x09;
				ProRead = 0;
				bWrite = 0;
				rdctrbyte = 9;
				ProDis_DataIOSet(0);
				break;

			case rFlowH:
				ctrbyte = 0x0c;
				ProRead = 0;
				bWrite = 0;
				rdctrbyte = 0x0c;
				ProDis_DataIOSet(0);
				break;

			case rDevID:
				ctrbyte = 0x0b;
				ProRead = 0;
				bWrite = 0;
				rdctrbyte = 0x0b;
				ProDis_DataIOSet(0);
				break;

			case rErr:
				ctrbyte = 0x0a;
				ProRead = 0;
				bWrite = 0;
				rdctrbyte = 10;
				ProDis_DataIOSet(0);
				break;

			default:
				break;
			}
		}
	}
}

//--------------------------------------------------------------------//
// 通信是否正常判断,10ms定时调用
//--------------------------------------------------------------------//
void Pro_Run100ms(void)
{
	if (ErrTime)
	{
		ErrTime--;
		bCOMErr = 0;
		Tmepe_CurVal = ReadTemp;

		if ((SysInfoSet & 0x37) == 0 && bOTHERSPRAYC == 0)
		{
			ReadFlow = 0;
		}
		else
		{
			if (Read_DevID == DEV_TYPE_HZ20)
			{
				if (bRd_FlowL == 1 && bRd_FlowH == 1)
				{
					bRd_FlowL = 0;
					bRd_FlowH = 0;
					ReadFlow = ReadFlowL + ReadFlowH;
				}
			}
			else
			{
				ReadFlow = ReadFlowL;
			}
		}

		Flow_CurVal = ReadFlow;

		if ((ReadErrCode & 0x10) == 0)
		{
			Get_PwType = Power_Bell;
		}
		else
		{
			Get_PwType = Power_DC12;
		}

		Get_BellLV = (ReadErrCode & 0xe0) >> 5;
		bCOMErr = 0;

		if (pwon == 0)
		{
			Flow_SendDelay = 0;
			Flwo_SendData = 0;
		}
		else
		{
			if (Flow_SendDelay > 0)
				Flow_SendDelay--;
			if (Flow_SendDelay == 0)
			{
				Flow_SendDelay = 3;
				if (Flwo_SendData == 0)
				{
					Flwo_SendData = FlowDataSet;
				}
				else
				{
					if (FlowDataSet < FLOW_MIN_SET || FlowDataSet > FLOW_MAX_SET)
					{
						Flwo_SendData = FlowDataSet;
					}
					else
					{
						if (Flwo_SendData < FlowDataSet)
						{
							Flwo_SendData++;
						}
						else if (Flwo_SendData > FlowDataSet)
						{
							Flwo_SendData--;
						}

						if (Flwo_SendData > FLOW_MAX_SET)
							Flwo_SendData = FLOW_MAX_SET;
						else if (Flwo_SendData < FLOW_MIN_SET)
							Flwo_SendData = FLOW_MIN_SET;
					}
				}
			}
		}
	}
	else
	{
		ReadTemp = 1;
		ReadFlow = 2;
		ReadFlowH = 3;
		ReadErrCode = 1;
		Flwo_SendData = 1;
		bRd_FlowL = 1;
		bRd_FlowH = 0;
		ReadFlowL = 0;
		ReadFlowH = 0;
		Flow_CurVal = 0;
		bCOMErr = 1;
	}
}

//------------置设置的恒温温度--------------//
void Post_SetTempe(INT8U set_data)
{
	TempDataSet = set_data;
}

//------------置设置的流量级数--------------//
void Post_SetFlow(INT8U set_data)
{
	FlowDataSet = set_data;
}

//------------置龙头开关标------------------//
void Post_SetPowerOn(INT8U set_data)
{
	pwon = (set_data == 0) ? 0 : 1;
}

//----------------------------------------------------------
// 设置温度标志位
//----------------------------------------------------------
void Post_Set_TempeFlag(INT8U set_flag)
{
	bSet_TempeEn = (set_flag == 0) ? 0 : 1;
}

//----------------------------------------------------------
// 设置恒温速度标志位
//----------------------------------------------------------
void Post_Set_SpeedFlag(INT8U set_flag, INT8U set_speed)
{
	bSet_SpeedEn = (set_flag == 0) ? 0 : 1;

	if (set_speed > 5)
		set_speed = 5;
	SysInfoSet &= 0xf8;
	SysInfoSet |= set_speed;
}


/* 用于从 ISR 唤醒任务的二值信号量 */
static SemaphoreHandle_t prodis_sem = NULL;
static void IRAM_ATTR prodis_timer_isr(void *arg)
{
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    if (prodis_sem) {
        xSemaphoreGiveFromISR(prodis_sem, &xHigherPriorityTaskWoken);
    }
    if (xHigherPriorityTaskWoken == pdTRUE) {
        portYIELD_FROM_ISR();
    }
}

void ProDis_TimerInit(void)
{
    // 创建信号量（先创建）
    prodis_sem = xSemaphoreCreateBinary();
    if (prodis_sem == NULL) {
        ESP_LOGE(TAG, "create sem fail");
        return;
    }

    const esp_timer_create_args_t timer_args = {
        .callback = &prodis_timer_isr,
        .name = "prodis_timer",
#ifdef CONFIG_ESP_TIMER_SUPPORTS_ISR_DISPATCH_METHOD
        .dispatch_method = ESP_TIMER_ISR, // 如果 enabled，会在 ISR 中运行
#else
        .dispatch_method = ESP_TIMER_TASK, // 否则在 esp_timer 任务中运行
#endif
    };

    esp_timer_handle_t timer_handle;
    ESP_ERROR_CHECK(esp_timer_create(&timer_args, &timer_handle));
    ESP_ERROR_CHECK(esp_timer_start_periodic(timer_handle, 1000)); // 1000 us = 1ms

    ESP_LOGI(TAG, "ProDis timer started (1ms)");
}


// void ProDis_Run1ms(void)
// {
// 	static bool flag = false;
// 	if (flag)
// 	{
// 		Set_Pro_CLK;
// 	}
// 	else
// 	{
// 		Clr_Pro_CLK;
// 	}
// 	flag = !flag;
// }


void ProDis_Task(void *pvParameters)
{
    // 优先级可以设置较高，栈视 ProDis_Run1ms 的复杂程度调整
    while (1) {
        // 等待信号量（阻塞），超时可设置为 e.g. 5 ms 以便处理其它逻辑
        if (xSemaphoreTake(prodis_sem, portMAX_DELAY) == pdTRUE) {
            // 在任务上下文安全执行复杂逻辑
            ProDis_Run1ms();
        }
    }
}


// ----------------- 1ms 调用任务 -----------------
void Temp_Task1ms(void *arg)
{
    while (1) {
        ProDis_Run1ms();
        vTaskDelay(pdMS_TO_TICKS(1));  // 1ms
    }
}

// ----------------- 100ms 调用任务 -----------------
void Temp_Task100ms(void *arg)
{
    while (1)
    {
        // 调用获取数据函数
        Pro_Run100ms();

        // 打印出水温度、流量、电源类型、电池电量、通信状态
        ESP_LOGI(TAG, "Temperature: %d°C, Flow: %d, Power: %s, BatteryLV: %d, COMErr: %s",
                 ReadTemp,
                 Flow_CurVal,
                 (Get_PwType == 0 ? "Battery" : "DC12V"),
                 Get_BellLV,
                 (bCOMErr ? "Error" : "OK"));

        // 延时100ms
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}


void Temp_task(void)
{
	ProDis_Init();
	ProDis_TimerInit();
	    // 创建 1ms 通信任务
	xTaskCreate(Temp_Task100ms, "Temp_Task100ms", 4096, NULL, 5, NULL);
    // xTaskCreate(Temp_Task1ms, "Temp_Task1ms", 4096, NULL, 10, NULL);
	xTaskCreate(ProDis_Task, "ProDis_Task", 4096, NULL, 3, NULL);
    ESP_LOGI(TAG, "ProDis Task started");

    // 创建 100ms 数据读取任务
    

}