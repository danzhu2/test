#include "stm32f10x.h"
#include "Motor.h"

/* ============================================================================
 *  TB6612 电机驱动实现
 *
 *  PWM 频率选 20kHz 的原因：
 *    低于 16kHz 人耳能听到啸叫；过高则 MOS 开关损耗变大。
 *    72MHz / (ARR+1) = 20kHz → ARR = 3599，PSC = 0。
 * ==========================================================================*/

/* ⚠️【需修改】引脚定义区（接线不同只改这里）
 *
 *  全局引脚分配（已避开所有冲突，改线前先核对这张表）：
 *    PA0~PA7  8 路灰度传感器      PA8/PA11  电机 PWM（TIM1_CH1/CH4）
 *    PA9/PA10 USART1              PA12      TB6612 STBY
 *    PB0/1/10/11 按键             PB8/PB9   OLED        PB12 LED2
 *    PB13/PB15  右电机方向         PB14/PA15 左电机方向（PA15 需禁 JTAG）
 *    PB4/PB5    左编码器(TIM3重映射) PB6/PB7  右编码器(TIM4)
 *    PB3        蜂鸣器（需禁 JTAG） PC13      板载 LED1
 *    PA13/PA14  SWD 调试（不要占用）
 */
#define MOTOR_PWMA_PORT		GPIOA
#define MOTOR_PWMA_PIN		GPIO_Pin_8			/* TIM1_CH1  → TB6612 PWMA（左电机 PWM） */
#define MOTOR_PWMB_PORT		GPIOA
#define MOTOR_PWMB_PIN		GPIO_Pin_11			/* TIM1_CH4  → TB6612 PWMB（右电机 PWM） */

#define MOTOR_AIN1_PORT		GPIOB
#define MOTOR_AIN1_PIN		GPIO_Pin_14			/* 左电机方向 1（原 PA4 与传感器 S5 冲突，已改） */
#define MOTOR_AIN2_PORT		GPIOA
#define MOTOR_AIN2_PIN		GPIO_Pin_15			/* 左电机方向 2（⚠️ PA15 默认是 JTAG，依赖 main() 里的解禁） */
#define MOTOR_BIN1_PORT		GPIOB
#define MOTOR_BIN1_PIN		GPIO_Pin_13			/* 右电机方向 1 */
#define MOTOR_BIN2_PORT		GPIOB
#define MOTOR_BIN2_PIN		GPIO_Pin_15			/* 右电机方向 2 */
#define MOTOR_STBY_PORT		GPIOA
#define MOTOR_STBY_PIN		GPIO_Pin_12			/* TB6612 使能，接高才工作 */

/* ⚠️【需修改】如果某个电机装反了（给正速度却后退），不用改接线，
 *    把对应的方向宏取反即可：把下面 1 改成 0。 */
#define MOTOR_L_DIR_INVERT	0
#define MOTOR_R_DIR_INVERT	0

#define PWM_MAX				3599				/* = ARR，对应 100% 占空比 */

/* -------------------------------------------------------------------------- */

void MotorDirPinsInit(void)
{
	GPIO_InitTypeDef GPIO_InitStructure;

	RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA | RCC_APB2Periph_GPIOB, ENABLE);

	GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_Out_PP;
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;

	GPIO_InitStructure.GPIO_Pin = MOTOR_AIN1_PIN;
	GPIO_Init(MOTOR_AIN1_PORT, &GPIO_InitStructure);
	GPIO_InitStructure.GPIO_Pin = MOTOR_AIN2_PIN;
	GPIO_Init(MOTOR_AIN2_PORT, &GPIO_InitStructure);
	GPIO_InitStructure.GPIO_Pin = MOTOR_BIN1_PIN;
	GPIO_Init(MOTOR_BIN1_PORT, &GPIO_InitStructure);
	GPIO_InitStructure.GPIO_Pin = MOTOR_BIN2_PIN;
	GPIO_Init(MOTOR_BIN2_PORT, &GPIO_InitStructure);
	GPIO_InitStructure.GPIO_Pin = MOTOR_STBY_PIN;
	GPIO_Init(MOTOR_STBY_PORT, &GPIO_InitStructure);
}

void MotorPwmInit(void)
{
	GPIO_InitTypeDef      GPIO_InitStructure;
	TIM_TimeBaseInitTypeDef TIM_TimeBaseInitStructure;
	TIM_OCInitTypeDef     TIM_OCInitStructure;

	/* TIM1 挂在 APB2（⚠️ 换 TIM2/3/4 时要改成 RCC_APB1PeriphClockCmd） */
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_TIM1, ENABLE);
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);

	GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_AF_PP;		/* PWM 输出必须复用推挽 */
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
	GPIO_InitStructure.GPIO_Pin   = MOTOR_PWMA_PIN | MOTOR_PWMB_PIN;
	GPIO_Init(GPIOA, &GPIO_InitStructure);

	TIM_TimeBaseInitStructure.TIM_ClockDivision     = TIM_CKD_DIV1;
	TIM_TimeBaseInitStructure.TIM_CounterMode       = TIM_CounterMode_Up;
	TIM_TimeBaseInitStructure.TIM_Prescaler         = 0;			/* 72MHz 直接计数 */
	TIM_TimeBaseInitStructure.TIM_Period            = PWM_MAX;		/* 72M/3600 = 20kHz */
	TIM_TimeBaseInitStructure.TIM_RepetitionCounter = 0;
	TIM_TimeBaseInit(TIM1, &TIM_TimeBaseInitStructure);

	TIM_OCStructInit(&TIM_OCInitStructure);					/* 先填默认值再改需要的，避免成员遗漏 */
	TIM_OCInitStructure.TIM_OCMode      = TIM_OCMode_PWM1;	/* CNT<CCR 时输出有效电平 */
	TIM_OCInitStructure.TIM_OutputState = TIM_OutputState_Enable;
	TIM_OCInitStructure.TIM_OCPolarity  = TIM_OCPolarity_High;
	TIM_OC1Init(TIM1, &TIM_OCInitStructure);				/* CH1 = PWMA */
	TIM_OC4Init(TIM1, &TIM_OCInitStructure);				/* CH4 = PWMB */

	/* ⚠️ 关键：TIM1 是高级定时器，必须额外使能主输出 MOE，
	 *    否则引脚上永远没有波形，而且编译链接都不会报错——新手大坑 */
	TIM_CtrlPWMOutputs(TIM1, ENABLE);

	TIM_OC1PreloadConfig(TIM1, TIM_OCPreload_Enable);		/* CCR 带预装载，改值在更新事件生效 */
	TIM_OC4PreloadConfig(TIM1, TIM_OCPreload_Enable);
	TIM_ARRPreloadConfig(TIM1, ENABLE);

	TIM_Cmd(TIM1, ENABLE);
}

void Motor_Init(void)
{
	MotorDirPinsInit();
	MotorPwmInit();

	/* STBY 拉高使能 TB6612 */
	GPIO_SetBits(MOTOR_STBY_PORT, MOTOR_STBY_PIN);

	/* 上电先保证两个电机停转 */
	TIM_SetCompare1(TIM1, 0);
	TIM_SetCompare4(TIM1, 0);
	GPIO_ResetBits(MOTOR_AIN1_PORT, MOTOR_AIN1_PIN);
	GPIO_ResetBits(MOTOR_AIN2_PORT, MOTOR_AIN2_PIN);
	GPIO_ResetBits(MOTOR_BIN1_PORT, MOTOR_BIN1_PIN);
	GPIO_ResetBits(MOTOR_BIN2_PORT, MOTOR_BIN2_PIN);
}

/* 单轮方向脚设置。invert 处理电机安装方向反了的情况 */
void SetDir(GPIO_TypeDef *port1, uint16_t pin1,
                   GPIO_TypeDef *port2, uint16_t pin2,
                   int16_t speed, uint8_t invert)
{
	uint8_t fwd = (speed >= 0) ? 1u : 0u;

	if (invert)
	{
		fwd = (uint8_t)(!fwd);
	}

	/* 正转：pin1=1 pin2=0；反转：pin1=0 pin2=1（TB6612 真值表） */
	GPIO_WriteBit(port1, pin1, fwd ? Bit_SET : Bit_RESET);
	GPIO_WriteBit(port2, pin2, fwd ? Bit_RESET : Bit_SET);
}

void Motor_Set(int16_t left, int16_t right)
{
	uint16_t ccrL, ccrR;

	/* 限幅：上层 PID 输出没控好也不至于把 CCR 写爆 */
	if (left  >  1000) left  =  1000;
	if (left  < -1000) left  = -1000;
	if (right >  1000) right =  1000;
	if (right < -1000) right = -1000;

	ccrL = (uint16_t)(((int32_t)(left  < 0 ? -left  : left) * PWM_MAX) / 1000);       //设占空比数值
	ccrR = (uint16_t)(((int32_t)(right < 0 ? -right : right) * PWM_MAX) / 1000);

	SetDir(MOTOR_AIN1_PORT, MOTOR_AIN1_PIN, MOTOR_AIN2_PORT, MOTOR_AIN2_PIN, left,  MOTOR_L_DIR_INVERT);
	SetDir(MOTOR_BIN1_PORT, MOTOR_BIN1_PIN, MOTOR_BIN2_PORT, MOTOR_BIN2_PIN, right, MOTOR_R_DIR_INVERT);

	TIM_SetCompare1(TIM1, ccrL);   //占空比
	TIM_SetCompare4(TIM1, ccrR);
}

void Motor_Brake(void)
{
	/* 方向脚同时置 1 = 短路刹车；PWM 保持输出但电机两端被短接 */
	GPIO_SetBits(MOTOR_AIN1_PORT, MOTOR_AIN1_PIN);
	GPIO_SetBits(MOTOR_AIN2_PORT, MOTOR_AIN2_PIN);
	GPIO_SetBits(MOTOR_BIN1_PORT, MOTOR_BIN1_PIN);
	GPIO_SetBits(MOTOR_BIN2_PORT, MOTOR_BIN2_PIN);
	TIM_SetCompare1(TIM1, PWM_MAX);
	TIM_SetCompare4(TIM1, PWM_MAX);
}
