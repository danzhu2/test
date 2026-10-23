#include "serial.h"
#include "menu.h"			/* 变量 a 存在 menu.c 里，这里通过接口读写 */
#include "util.h"			/* Util_IntToStr */
#include "tick.h"			/* 用于"整行超时"判断 */
#include "Remote.h"			/* 遥控指令路由（拓展 6.3） */

/* ============================================================================
 *  USART1 串口接收 + 指令解析
 *
 *
 *  整体结构：
 *     串口中断       只搬字节进环形缓冲区，立刻退出
 *       ↓
 *     Serial_Task()  主循环里取字节、按行切分
 *       ↓
 *     Serial_ExecLine()  分发：GET A 查询 / 遥控指令 / 数字设值
 * ==========================================================================*/

#define RX_BUF_SIZE		128

volatile uint8_t  s_rxBuf[RX_BUF_SIZE];
volatile uint16_t s_rxHead = 0;
volatile uint16_t s_rxTail = 0;

/* ---------------------------------------------------------------------- */
/*  初始化                                                                */
/* ---------------------------------------------------------------------- */
void Serial_Init(void)
{
	GPIO_InitTypeDef  GPIO_InitStructure;
	USART_InitTypeDef USART_InitStructure;
	NVIC_InitTypeDef  NVIC_InitStructure;

	RCC_APB2PeriphClockCmd(RCC_APB2Periph_USART1 | RCC_APB2Periph_GPIOA, ENABLE);

	/* PA9 = TX（复用推挽），PA10 = RX（上拉输入） */
	GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_AF_PP;
	GPIO_InitStructure.GPIO_Pin   = GPIO_Pin_9;
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
	GPIO_Init(GPIOA, &GPIO_InitStructure);

	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IPU;
	GPIO_InitStructure.GPIO_Pin  = GPIO_Pin_10;
	GPIO_Init(GPIOA, &GPIO_InitStructure);

	USART_InitStructure.USART_BaudRate            = SERIAL_BAUD;
	USART_InitStructure.USART_WordLength          = USART_WordLength_8b;
	USART_InitStructure.USART_StopBits            = USART_StopBits_1;
	USART_InitStructure.USART_Parity              = USART_Parity_No;
	USART_InitStructure.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
	USART_InitStructure.USART_Mode                = USART_Mode_Tx | USART_Mode_Rx;
	USART_Init(USART1, &USART_InitStructure);

	NVIC_PriorityGroupConfig(NVIC_PriorityGroup_2);
	NVIC_InitStructure.NVIC_IRQChannel                   = USART1_IRQn;
	NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 1;
	NVIC_InitStructure.NVIC_IRQChannelSubPriority        = 1;
	NVIC_InitStructure.NVIC_IRQChannelCmd                = ENABLE;
	NVIC_Init(&NVIC_InitStructure);

	USART_ITConfig(USART1, USART_IT_RXNE, ENABLE);
	USART_Cmd(USART1, ENABLE);
}

/* 中断：只做"搬字节"这一件事 */
void USART1_IRQHandler(void)
{
	if (USART_GetITStatus(USART1, USART_IT_RXNE) == SET)    //判断是否中断
	{
		uint8_t  data = (uint8_t)USART_ReceiveData(USART1);     //中断后收数据
		uint16_t next = (uint16_t)((s_rxHead + 1) % RX_BUF_SIZE);    //计算尾部

		if (next != s_rxTail)    //判断缓冲区满了没有
		{
			s_rxBuf[s_rxHead] = data;       //缓冲区
			s_rxHead = next;
		}
	}
}

/* ---------------------------------------------------------------------- */
/*  发送                                                                  */
/* ---------------------------------------------------------------------- */

void Serial_SendByte(uint8_t byte)
{
	USART_SendData(USART1, byte);
	while (USART_GetFlagStatus(USART1, USART_FLAG_TXE) == RESET);    //确认当前自己是否移位，等flag置0
}

void Serial_SendString(const char *str)
{
	while (*str != '\0')
	{
		Serial_SendByte((uint8_t)(*str));
		str++;
	}
}

void Serial_SendInt(int32_t value)     //发十进制
{
	char buf[12];
	Util_IntToStr(value, buf);
	Serial_SendString(buf);
}

/* ---------------------------------------------------------------------- */
/*  指令解析（主循环里调用，非阻塞）                                       */
/* ---------------------------------------------------------------------- */

#define LINE_MAX		32
#define LINE_IDLE_MS	100

char     s_line[LINE_MAX];
uint8_t  s_len        = 0;
uint32_t s_lastRxTick = 0;

static char ToUpper(char c)
{
	return (c >= 'a' && c <= 'z') ? (char)(c - 32) : c;
}

static const char * SkipSpace(const char *s)
{
	while (*s == ' ' || *s == '\t')
	{
		s++;
	}
	return s;
}

static uint8_t IsGetCommand(const char *s)
{
	s = SkipSpace(s);
	return (ToUpper(s[0]) == 'G' && ToUpper(s[1]) == 'E' && ToUpper(s[2]) == 'T') ? 1 : 0;
}

static uint8_t ParseInt(const char *s, int32_t *out)
{
	int32_t v      = 0;
	uint8_t neg    = 0;
	uint8_t digits = 0;

	s = SkipSpace(s);    //跳过空格

	if (*s == '+' || *s == '-')
	{
		neg = (*s == '-') ? 1 : 0;    //符号标志
		s++;
	}

	while (*s >= '0' && *s <= '9')
	{
		v = v * 10 + (int32_t)(*s - '0');
		digits++;
		s++;
	}

	s = SkipSpace(s);

	if (digits == 0 || *s != '\0')
	{
		return 0;
	}

	*out = neg ? -v : v;    //输出带符号
	return 1;
}

/* 判断是不是遥控指令（整行已转大写后比较） */
static uint8_t IsRemoteCommand(const char *s)
{
	s = SkipSpace(s);

	if (0) { }
	else if (s[0]=='F' && s[1]=='W' && s[2]=='D' && s[3]=='\0')  return 1;	/* FWD   */
	else if (s[0]=='B' && s[1]=='A' && s[2]=='C' && s[3]=='K' && s[4]=='\0') return 1;	/* BACK  */
	else if (s[0]=='L' && s[1]=='E' && s[2]=='F' && s[3]=='T' && s[4]=='\0') return 1;	/* LEFT  */
	else if (s[0]=='R' && s[1]=='I' && s[2]=='G' && s[3]=='H' && s[4]=='T' && s[5]=='\0') return 1;	/* RIGHT */
	else if (s[0]=='S' && s[1]=='T' && s[2]=='O' && s[3]=='P' && s[4]=='\0') return 1;	/* STOP  */

	return 0;
}

/* 执行一条完整指令 */
static void Serial_ExecLine(char *line)
{
	int32_t v;
	uint8_t i;

	if (IsGetCommand(line))
	{
		/* 第 3 项 c：读变量 a */
		Serial_SendInt(Menu_GetA());
		Serial_SendString("\r\n");
		return;
	}

	/* 遥控指令：先整行转大写再匹配（容忍 fwd / Fwd 等写法） */
	for (i = 0; line[i] != '\0'; i++)
	{
		line[i] = ToUpper(line[i]);
	}

	if (IsRemoteCommand(line))
	{
		Remote_Handle(line);

		/* 拓展 6.3 评分点："上位机显示当前小车的运动状态"——
		 * 每次收到遥控指令就回传一次状态，手机/PC 端显示出来即可拿分 */
		Serial_SendString("STATE:");
		Serial_SendString(Remote_GetState());
		Serial_SendString("\r\n");
		return;
	}

	if (ParseInt(line, &v))
	{
		/* 第 3 项 b：写变量 a（0、正数、负数都能解析） */
		Menu_SetA(v);
	}
	/* 其他内容一律忽略，不回复，避免污染 GET A 的返回值 */
}

void Serial_Task(void)     //写入缓存区line【】，然后交给Serial_ExecLine传给menu
{
	while (s_rxTail != s_rxHead)    //缓冲区不空
	{
		uint8_t ch = s_rxBuf[s_rxTail];
		s_rxTail = (uint16_t)((s_rxTail + 1) % RX_BUF_SIZE);

		s_lastRxTick = Tick_Get();     //收到最后一个字节的时间

		if (ch == '\r' || ch == '\n')
		{
			if (s_len > 0)
			{
				s_line[s_len] = '\0';
				Serial_ExecLine(s_line);
				s_len = 0;
			}
		}
		else if (s_len < LINE_MAX - 1)
		{
			s_line[s_len] = (char)ch;
			s_len++;
		}
		else    //普通字符且超出max，等于0直接重来
		{
			s_len = 0;
		}
	}

	/* 兜底：串口助手没勾"发送新行"也能执行指令 */
	if (s_len > 0 && (uint32_t)(Tick_Get() - s_lastRxTick) >= LINE_IDLE_MS)
	{
		s_line[s_len] = '\0';
		Serial_ExecLine(s_line);
		s_len = 0;
	}
}
