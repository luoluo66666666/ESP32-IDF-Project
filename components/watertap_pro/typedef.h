/*******************************Copyright (c)***********************************
**                          佛山三俊电子有限公司
**				    	       开   发   部
**--------------文件信息--------------------------------------------------------
**文   件   名: types.h
**使用编译器：  GCCAVR,或 ICCAVR
**使用CPU:      AVR 8bit MCU
**创   建   人: AderWei
**创 建 日 期 ：2007-8-16 
**描        述: 重定义数据类型，项目中统一使用下面重定义过的类型。
**--------------当前版本修订----------------------------------------------------
** 修改人: 
** 日　期: 
** 描　述: 
** 
*******************************************************************************/
#ifndef _TYPES_H
#define _TYPES_H

typedef unsigned char      BOOLEAN;  /* 布尔变量             */
typedef unsigned char      Int8;    /* 无符号8位整形变量   */
typedef char               Int8s;   /* 有符号8位整形变量   */
typedef unsigned short     Int16;
typedef short              Int16s;
typedef unsigned int       Int32;   /* 无符号32位整形变量  */
typedef int                int32s;  /* 有符号32位整形变量  */
typedef float              FP32;    /*  单精度浮点数（32位）*/
typedef double             FP64;    /*  双精度浮点数（64位）*/

typedef unsigned char      INT8U;    /* 无符号8位整形变量   */
typedef char               INT8S;   /* 有符号8位整形变量   */
typedef unsigned short     INT16U;
typedef short              INT16S;
typedef unsigned int       INT32U;   /* 无符号32位整形变量  */
typedef int                INT32S;  /* 有符号32位整形变量  */

typedef unsigned char      uchar;
/*******************************************************************************
**                            End Of File
*******************************************************************************/

struct	bit_def {
	unsigned char	b0:1;
	unsigned char	b1:1;
	unsigned char	b2:1;
	unsigned char	b3:1;
	unsigned char	b4:1;
	unsigned char	b5:1;
	unsigned char	b6:1;
	unsigned char	b7:1;
};
union	byte_def{
	struct	bit_def bit;
	unsigned char	byte;
};

typedef struct
{
   Int8 byte0 : 8;
   Int8 byte1 : 8;
}doubyte_tag;

typedef union
{
	doubyte_tag twoByte;
	Int16       word;
}word_byte_tag;

#endif
