#ifndef __SERIAL_H
#define __SERIAL_H

#include "stm32f10x.h"

/* ============================================================================
 *  【新增文件】USART1 串口通信 —— 对应赛题第 3 项 b / c
 *
 *  接线：
 *    PA9  (USART1_TX) → USB-TTL 模块的 RXD
 *    PA10 (USART1_RX) → USB-TTL 模块的 TXD
 *    GND              → USB-TTL 模块的 GND（必须共地，否则收发乱码）
 *
 *  上位机指令格式（一行一条，以回车/换行结尾）：
 *    111        → 把变量 a 改成 111（支持负数，例如 -45）
 *    GET A      → 单片机回传当前 a 的值
 *
 *  用的知识点就是江协教程里的"USART 串口 + 接收数据包状态机"，
 *  只是把复杂的包头包尾协议换成了最简单的"按行分隔"。
 * ==========================================================================*/

/* 波特率：必须和串口助手里设置的完全一致，否则收到的全是乱码 */
#define SERIAL_BAUD		115200

void Serial_Init(void);

/* 主循环里反复调用：把收到的数据按行切分、解析并执行。非阻塞。 */
void Serial_Task(void);

/* 主动发送（第 3 项 c 的回传就是这三条的组合） */
void Serial_SendByte(uint8_t byte);
void Serial_SendString(const char *str);
void Serial_SendInt(int32_t value);

#endif
