#include "serial.h"
#include "menu.h"			/* 变量 a 存在 menu.c 里，这里通过接口读写 */
#include "util.h"			/* Util_IntToStr */
#include "tick.h"			/* 用于"整行超时"判断 */

/* ============================================================================
 *  【新增文件】USART1 串口接收 + 指令解析
 *
 *  整体结构（和江协教程的数据包接收是同一个套路）：
 *     串口中断          只干一件事：把收到的字节塞进环形缓冲区，立刻退出
 *       ↓
 *     环形缓冲区        s_rxBuf / s_rxHead / s_rxTail（中断和主循环共享）
 *       ↓
 *     Serial_Task()     主循环里把字节一个个取出来，遇到回车就当成一条完整指令
 *       ↓
 *     Serial_ExecLine() 判断这条指令是"设值"还是"读值"
 *
 *  为什么不直接在中断里解析指令？
 *     中断里做事越少越好。解析要比较字符串、可能要发送回传（发送是阻塞的），
 *     放在中断里会长时间占用 CPU，影响按键消抖和后续的巡线控制。
 * ==========================================================================*/

#define RX_BUF_SIZE		128		/* 环形缓冲区大小，收满后新数据丢弃 */

static volatile uint8_t  s_rxBuf[RX_BUF_SIZE];
static volatile uint16_t s_rxHead = 0;		/* 中断往里写的位置 */
static volatile uint16_t s_rxTail = 0;		/* 主循环往外读的位置 */

/* ---------------------------------------------------------------------- */
/*  初始化                                                                */
/* ---------------------------------------------------------------------- */
void Serial_Init(void)
{
	GPIO_InitTypeDef  GPIO_InitStructure;
	USART_InitTypeDef USART_InitStructure;
	NVIC_InitTypeDef  NVIC_InitStructure;

	/* USART1 和 GPIOA 都挂在 APB2 上（注意不是 APB1！） */
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_USART1 | RCC_APB2Periph_GPIOA, ENABLE);

	/* PA9 = USART1_TX，复用推挽输出（AF_PP，不能写 Out_PP） */
	GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_AF_PP;
	GPIO_InitStructure.GPIO_Pin   = GPIO_Pin_9;
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
	GPIO_Init(GPIOA, &GPIO_InitStructure);

	/* PA10 = USART1_RX，输入。江协例程用浮空输入，
	 * 这里用上拉输入：杜邦线拔了/悬空时读到的是高电平（空闲态），
	 * 不会因为悬空乱跳而误触发接收中断。 */
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IPU;
	GPIO_InitStructure.GPIO_Pin  = GPIO_Pin_10;
	GPIO_Init(GPIOA, &GPIO_InitStructure);

	USART_InitStructure.USART_BaudRate            = SERIAL_BAUD;
	USART_InitStructure.USART_WordLength          = USART_WordLength_8b;		/* 8 位数据 */
	USART_InitStructure.USART_StopBits            = USART_StopBits_1;		/* 1 位停止位 */
	USART_InitStructure.USART_Parity              = USART_Parity_No;		/* 无校验 */
	USART_InitStructure.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
	USART_InitStructure.USART_Mode                = USART_Mode_Tx | USART_Mode_Rx;	/* 收发都要 */
	USART_Init(USART1, &USART_InitStructure);

	NVIC_PriorityGroupConfig(NVIC_PriorityGroup_2);
	NVIC_InitStructure.NVIC_IRQChannel                   = USART1_IRQn;
	NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 1;
	NVIC_InitStructure.NVIC_IRQChannelSubPriority        = 1;
	NVIC_InitStructure.NVIC_IRQChannelCmd                = ENABLE;
	NVIC_Init(&NVIC_InitStructure);

	USART_ITConfig(USART1, USART_IT_RXNE, ENABLE);		/* 收到一个字节就进中断 */
	USART_Cmd(USART1, ENABLE);
}

/* ---------------------------------------------------------------------- */
/*  中断服务函数：只做"搬字节"这一件事                                     */
/*  名字必须和启动文件里的向量名完全一致：USART1_IRQHandler                 */
/* ---------------------------------------------------------------------- */
void USART1_IRQHandler(void)
{
	if (USART_GetITStatus(USART1, USART_IT_RXNE) == SET)
	{
		uint8_t  data = (uint8_t)USART_ReceiveData(USART1);		/* 读数据，同时自动清标志 */
		uint16_t next = (uint16_t)((s_rxHead + 1) % RX_BUF_SIZE);

		if (next != s_rxTail)		/* 缓冲区没满才存；满了就丢弃，避免覆盖没读走的数据 */
		{
			s_rxBuf[s_rxHead] = data;
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
	while (USART_GetFlagStatus(USART1, USART_FLAG_TXE) == RESET);	/* 等发送寄存器空 */
}

void Serial_SendString(const char *str)
{
	while (*str != '\0')
	{
		Serial_SendByte((uint8_t)(*str));
		str++;
	}
}

void Serial_SendInt(int32_t value)
{
	char buf[12];					/* "-2147483648" 最长 11 字符 + '\0' */
	Util_IntToStr(value, buf);
	Serial_SendString(buf);
}

/* ---------------------------------------------------------------------- */
/*  指令解析（主循环里调用，非阻塞）                                       */
/* ---------------------------------------------------------------------- */

#define LINE_MAX		32			/* 单条指令最长 31 个字符，超出就丢弃 */

/* 整行超时（毫秒）：如果一直没有收到回车/换行，但已经静默这么久，
 * 就把已收到的内容当成一条完整指令执行。
 * 为什么要这样？很多串口助手默认不勾"发送新行"，如果只认回车，
 * 裁判发一条 "111" 我们会一直等回车，表现为"串口没反应"，白白丢分。 */
#define LINE_IDLE_MS	100

static char     s_line[LINE_MAX];
static uint8_t  s_len        = 0;
static uint32_t s_lastRxTick = 0;		/* 最后一个字节的到达时刻 */

static char ToUpper(char c)
{
	return (c >= 'a' && c <= 'z') ? (char)(c - 32) : c;
}

/* 跳过行首的空格 / 制表符 */
static const char * SkipSpace(const char *s)
{
	while (*s == ' ' || *s == '\t')
	{
		s++;
	}
	return s;
}

/* 判断是不是以 GET 开头的查询指令（不区分大小写，允许 "GET A" / "GETA" / "get a"） */
static uint8_t IsGetCommand(const char *s)
{
	s = SkipSpace(s);
	return (ToUpper(s[0]) == 'G' && ToUpper(s[1]) == 'E' && ToUpper(s[2]) == 'T') ? 1 : 0;
}

/* 解析十进制整数：成功返回 1 并把结果写进 *out；失败返回 0。
 * 要求整行都是数字才认（可以带正负号），这样 "GET A" 这类指令会自然被判为失败。 */
static uint8_t ParseInt(const char *s, int32_t *out)
{
	int32_t v      = 0;
	uint8_t neg    = 0;
	uint8_t digits = 0;

	s = SkipSpace(s);

	if (*s == '+' || *s == '-')
	{
		neg = (*s == '-') ? 1 : 0;
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
		return 0;					/* 没有数字，或者后面还有多余字符 */
	}

	*out = neg ? -v : v;
	return 1;
}

/* 执行一条完整指令 */
static void Serial_ExecLine(char *line)
{
	int32_t v;

	if (IsGetCommand(line))
	{
		/* 第 3 项 c：上位机读变量 —— 回传当前值 + 回车换行 */
		Serial_SendInt(Menu_GetA());
		Serial_SendString("\r\n");
		return;
	}

	if (ParseInt(line, &v))
	{
		/* 第 3 项 b：上位机修改变量 —— 0、正数、负数都能正确解析 */
		Menu_SetA(v);

		/* 如果想在串口助手里看到"已收到"，把下面两行的注释去掉即可回显。
		 * 注意：回显会让串口窗口里混入额外数据，做 3c 测试（GET A）时建议关掉。 */
		/* Serial_SendInt(v);      */
		/* Serial_SendString("\r\n"); */
	}
	/* 其他内容一律忽略，不回复，避免污染 GET A 的返回值 */
}

void Serial_Task(void)
{
	while (s_rxTail != s_rxHead)
	{
		uint8_t ch = s_rxBuf[s_rxTail];
		s_rxTail = (uint16_t)((s_rxTail + 1) % RX_BUF_SIZE);

		s_lastRxTick = Tick_Get();		/* 记录"刚才还有数据在来" */

		if (ch == '\r' || ch == '\n')
		{
			/* 收到行尾：如果这一行有内容，就当成一条指令执行 */
			if (s_len > 0)
			{
				s_line[s_len] = '\0';
				Serial_ExecLine(s_line);
				s_len = 0;
			}
			/* 连续的 \r\n 只会触发一次执行，空行自动忽略 */
		}
		else if (s_len < LINE_MAX - 1)
		{
			s_line[s_len] = (char)ch;
			s_len++;
		}
		else
		{
			s_len = 0;				/* 太长，说明不是合法指令，整行丢掉 */
		}
	}

	/* 兜底：一直没有回车，但数据已经静默 LINE_IDLE_MS，就当作一条完整指令 */
	if (s_len > 0 && (uint32_t)(Tick_Get() - s_lastRxTick) >= LINE_IDLE_MS)
	{
		s_line[s_len] = '\0';
		Serial_ExecLine(s_line);
		s_len = 0;
	}
}
