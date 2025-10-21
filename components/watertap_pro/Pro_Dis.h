
#ifndef _PRO_DIS_H
#define _PRO_DIS_H

#include "typedef.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"


#ifndef TRUE
#define TRUE  1
#endif
#ifndef FALSE
#define FALSE 0
#endif

/********************************************************
*  declare SFR bit                                      *
********************************************************/
/*
struct	bit_def {
	unsigned char	b0:1;
	unsigned char	b1:1;
	unsigned char	b2:1;
	unsigned char	b3:1;s
	unsigned char	b4:1;
	unsigned char	b5:1;
	unsigned char	b6:1;
	unsigned char	b7:1;
};
union	byte_def{
	struct	bit_def bit;
	unsigned char	byte;
};
*/
//----------------------------------------------------------//

#define 			DEV_TYPE_HZ20				1

#define				FLOW_MAX_SET				218/*最大流量级数*/
#define				FLOW_MIN_SET				211/*最小流量级数*/
#define				FLOW_DEF_SET				218/*默认流量级数*/
#define				WT_FLOW_OV_MIN			210
#define				WT_FLOW_OV_MAX			219

#define				Power_Bell					0	//电池供电
#define				Power_DC12					1	//市电供电

#define				SPRAY_ML_TOP				1	//顶喷
#define				SPRAY_ML_SHOWER			2	//花洒
#define				SPRAY_ML_BACK				3 //背喷
#define				SPRAY_ML_LBACK			4 //第4路出水
#define				SPRAY_ML_FALLS			5 //第5路出水
#define				SPRAY_ML_WTAP				6 //第6路出水
#define				SPRAY_ML_DEF				2	//默认出水

//----------------------------------------------------------//

typedef enum 
{
	OpNone,
	OpIdle,
	wTemp,
	wFlow,
	wSysInfo,
	rTemp,
	rFlow,
	rFlowH,
	rDevID,
	rErr,
	rTemp1,
}OperationType_Tag;

//----------------------------------------------------------//
#ifdef _PRO_C
#define PRO_EXTERN
#else
#define PRO_EXTERN extern
#endif


PRO_EXTERN			union byte_def  ProDisTAG;

PRO_EXTERN			OperationType_Tag NextOp, EmcOp;

PRO_EXTERN			INT8U   	ReadTemp;
PRO_EXTERN			INT16U   	ReadFlow;
PRO_EXTERN			INT8U   	ReadErrCode;

PRO_EXTERN			INT8U   	Idle_Time;
PRO_EXTERN			INT8U   	OpCnt;
PRO_EXTERN			INT16U  	ProSend;
PRO_EXTERN			INT16U  	ProRead;
PRO_EXTERN			INT8U   	ErrTime;
PRO_EXTERN			INT8U  		SysInfoBK;
PRO_EXTERN			INT8U 		SysInfoBKII;

PRO_EXTERN			INT8U 		TempDataSet;
PRO_EXTERN			INT8U 		FlowDataSet;
PRO_EXTERN			INT8U 		TempDataSetBck;
PRO_EXTERN			INT8U 		FlowDataSetBck;

PRO_EXTERN			INT8U 		Tmepe_CurVal;					//保存获取的当前龙头温度
PRO_EXTERN			INT16U 		Flow_CurVal;					//保存获取的当前龙头流量
PRO_EXTERN			INT8U 		Get_PwType;					  //保存获取的当前龙头的供电方式,0:电池供电; 1:市电供电
PRO_EXTERN			INT8U 		Get_BellLV;						//保存获取的当前龙头电量,0~5级,0为电池快没电,低电量; 5为满电

PRO_EXTERN			INT8U 		bPro_Init;
PRO_EXTERN			INT8U 		ReadFlowL;
PRO_EXTERN			INT8U 		ReadFlowH;
PRO_EXTERN			INT8U 		Read_DevID;
PRO_EXTERN			INT8U 		Flow_SendDelay;
PRO_EXTERN			INT8U 		Flwo_SendData;

PRO_EXTERN			BOOLEAN 	bClk;

//----------------------------------------------------------//
/*
extern Int8 TempDataSet;
extern Int8 FlowDataSet;
extern Int8 TempDataSetBck;
extern Int8 FlowDataSetBck;
extern union byte_def SysInfounion;
extern Int8  SysInfoBK;
*/
PRO_EXTERN			union byte_def 	SysInfounion;
#define SysInfoSet 							SysInfounion.byte
#define bSpeedMode_B0						SysInfounion.bit.b0       //F102调温速度等级b0位 b2~b0值对应1~5级调温速度，值越太，调温速度越慢
#define bSpeedMode_B1						SysInfounion.bit.b1    		//F102调温速度等级b1位
#define bSpeedMode_B2						SysInfounion.bit.b2				//F102调温速度等级b2位

#define bSet_SpeedEn						SysInfounion.bit.b5				//设置温度速度允许标志
#define bSet_TempeEn						SysInfounion.bit.b6				//设置温度允许标志
#define pwon  									SysInfounion.bit.b7				//开关机控制标志,0:关机; 1:开机

PRO_EXTERN				union byte_def  SysInfounionII;
#define bSysInfoSetII 						SysInfounionII.byte  
#define bOTHERSPRAYC  						SysInfounionII.bit.b0		//第6路出水开关控制

//extern union byte_def  ProDisTAG;
#define bWrite  				ProDisTAG.bit.b0
#define bCOMErr  				ProDisTAG.bit.b1
#define bRd_FlowL  			ProDisTAG.bit.b2
#define bRd_FlowH  			ProDisTAG.bit.b3


#define Set_Pro_CLK     gpio_set_level(Temp_CLK_GPIO, 1)
#define Clr_Pro_CLK     gpio_set_level(Temp_CLK_GPIO, 0)
#define Set_Pro_DATA    gpio_set_level(Temp_DATA_GPIO, 1)
#define Clr_Pro_DATA    gpio_set_level(Temp_DATA_GPIO, 0)
#define get_ProData     gpio_get_level(Temp_DATA_GPIO)  // 读取 PRO_DATA 引脚电平


#define TIME_IDLE  5

extern unsigned char bPro_Init;

void ProDis_Init(void);
void ProDis_Run1ms(void);
void Pro_Run100ms(void);

//------------置设置的恒温温度--------------//
void Post_SetTempe(INT8U setData);
//------------置设置的流量级数--------------//
void Post_SetFlow(INT8U setData);
//------------置龙头开关标------------------//
void Post_SetPowerOn(INT8U setData);
//----------------------------------------------------------
//------------设置温度时置设置温度标志位--------------------
//当设置温度时置1，退出设置温度模式置0
//----------------------------------------------------------
void Post_Set_TempeFlag(INT8U setFlag);
//----------------------------------------------------------
//------------设置恒温速度标志位----------------------------
//setFlag:当设置时置1，退出设置模式置0
//set_Speed:恒温速度值，1~5;
//----------------------------------------------------------
void Post_Set_SpeedFlag(INT8U setFlag,INT8U set_Speed);

void Temp_task(void);

#endif


