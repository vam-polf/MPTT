/* ═══════════════════════════════════════════════════════════════════════════════
 * MPTT 咔嗒声测试固件 v2 — 中文逐行注释版
 *
 * 功能：只用 I2C（完全不涉及 I2S），通过切换 WM8960 扬声器音量的"模拟静音"
 *       和"最大增益"来产生咔嗒声，验证 DAC→混音器→Class D→扬声器这条模拟
 *       通路是否正常。
 *
 * 原理（Datasheet p43/p79）：
 *   WM8960 的设计者承认：在信号非零时切换音量会产生"audible click"。
 *   他们专门设计了过零检测（Zero Cross）来防止这个咔嗒声。
 *   我们反其道而行——关掉过零检测（ZC=0），让切换立即生效 → 咔嗒声必出。
 *
 *   R40(0x28) SPKLVOL[6:0]:
 *     1111111(127) = +6dB 最大音量
 *     0110000(48)  = -73dB
 *     0101111(47)~0000000(0) = Analogue MUTE（模拟静音——PGA 输出物理断开）
 *
 *   切换 0(模拟静音) ↔ 127(+6dB)：
 *     静音 → PGA 断开 → 功放输入无偏置 → 输出跳变
 *     最大 → PGA 全开  → 功放输入有偏置 → 输出跳变
 *     两次跳变 = 两次"嗒" = 一个完整周期
 *
 * 硬件：STM32WLE5CBU6 (E77-400MBL) + WM8960CGEFL/RV
 * VU Latch 协议：先写 L(VU=0)装入暂存器，再写 R(VU=1)触发两声道同时更新
 * ═══════════════════════════════════════════════════════════════════════════════ */

#include <stdint.h>  /* uint8_t, uint16_t, uint32_t */


/* ─────────────────────────────────────────────────────────────────────────────
 * 第 0 部分：外设寄存器地址
 *
 * STM32 的所有外设都映射到内存地址空间，读写这些地址 = 控制硬件。
 * volatile 告诉编译器"每次都要真的读/写内存，别缓存"。
 * ───────────────────────────────────────────────────────────────────────────── */

/* RCC: 复位与时钟控制，基址 0x58000000 */
#define RCC_BASE      0x58000000
#define RCC_CR        (*(volatile uint32_t *)(RCC_BASE + 0x00))  /* +0x00: 时钟控制——HSI16开关 */
#define RCC_AHB2ENR   (*(volatile uint32_t *)(RCC_BASE + 0x4C))  /* +0x4C: AHB2外设时钟——GPIOA/B */
#define RCC_APB1ENR1  (*(volatile uint32_t *)(RCC_BASE + 0x58))  /* +0x58: APB1外设时钟——TIM2/I2C1 */

/* GPIO: 引脚控制，每端口 16 个引脚，基址间隔 0x400 */
#define GPIOA_BASE    0x48000000               /* PA0~PA15: PA3=MCLK */
#define GPIOB_BASE    0x48000400               /* PB0~PB15: PB6=SCL, PB7=SDA */
#define GPIO_MODER(b)  (*(volatile uint32_t *)((b) + 0x00))   /* 引脚模式: 00=入 01=出 10=AF 11=模拟 */
#define GPIO_OTYPER(b) (*(volatile uint32_t *)((b) + 0x04))   /* 输出类型: 0=推挽 1=开漏(I2C必须!) */
#define GPIO_PUPDR(b)  (*(volatile uint32_t *)((b) + 0x0C))   /* 上下拉: 00=无 01=上拉 10=下拉 */
#define GPIO_AFRL(b)   (*(volatile uint32_t *)((b) + 0x20))   /* AF选低8脚: PA0~PA7各4bit */

/* I2C1: I2C 总线控制器，基址 0x40005400。WM8960 从机地址 = 0x1A(7位) */
#define I2C1_BASE     0x40005400
#define I2C1_CR1      (*(volatile uint32_t *)(I2C1_BASE + 0x00))  /* 控制1: bit0=PE 外设使能 */
#define I2C1_CR2      (*(volatile uint32_t *)(I2C1_BASE + 0x04))  /* 控制2: 地址+字节数+START */
#define I2C1_TIMINGR  (*(volatile uint32_t *)(I2C1_BASE + 0x10))  /* 时序: 决定SCL频率 */
#define I2C1_ISR      (*(volatile uint32_t *)(I2C1_BASE + 0x18))  /* 状态: TXIS/TC/NACKF */
#define I2C1_ICR      (*(volatile uint32_t *)(I2C1_BASE + 0x1C))  /* 清除: 写1清标志 */
#define I2C1_TXDR     (*(volatile uint32_t *)(I2C1_BASE + 0x28))  /* 发送数据: 写字节到这里 */

/* SRAM 检查点: 在 0x20000100 放 8 字节，pyocd 远程读取即可知运行状态 */
#define RESULT ((volatile uint8_t *)0x20000100)


/* ─────────────────────────────────────────────────────────────────────────────
 * delay() — 忙等待延时
 * volatile 防编译器把空循环优化掉（-Os 下没 volatile 循环直接消失）
 * ───────────────────────────────────────────────────────────────────────────── */
static void delay(volatile uint32_t n) { while(n--); }


/* ─────────────────────────────────────────────────────────────────────────────
 * wm_write() — 通过 I2C 向 WM8960 写 9 位寄存器
 *
 * 参数: reg=7位寄存器地址  val=9位数据
 * 返回: 0=成功  1/2=发字节超时  3=等完成超时  4=从机NACK
 *
 * WM8960 I2C 协议 (Datasheet p58):
 *   第1字节 = (reg<<1) | (val>>8 & 1)  → 7bit地址 + 1bit数据高位
 *   第2字节 = val & 0xFF               → 8bit数据低位
 * ───────────────────────────────────────────────────────────────────────────── */
static uint8_t wm_write(uint8_t reg, uint16_t val) {
    uint8_t b1 = (reg << 1) | ((val >> 8) & 1);  /* 第1字节: 地址(高7bit)+数据bit8(低1bit) */
    uint8_t b2 = val & 0xFF;                       /* 第2字节: 数据低8位 */

    I2C1_ICR = 0x3F38;                 /* 清除所有I2C状态标志(写1清零)，白纸状态开始 */
    I2C1_CR2 = (0x1A << 1)            /* 从机地址=WM8960(0x1A左移1位→0x34) */
             | (2 << 16)               /* NBYTES=2: 发2字节 */
             | (1 << 25)               /* AUTOEND=1: 发完自动STOP */
             | (1 << 13);              /* START=1: 生成START条件，开始传输 */

    uint32_t t = 100000;               /* 超时计数器≈几毫秒 */
    while (!(I2C1_ISR & (1<<1)) && --t); /* 等 TXIS=1(发送寄存器空) */
    if(!t) return 1;                    /* 超时→返回1 */
    I2C1_TXDR = b1;                    /* 发第1字节 */

    t = 100000;
    while (!(I2C1_ISR & (1<<1)) && --t); /* 等TXIS再变1 */
    if(!t) return 2;                    /* 超时→返回2 */
    I2C1_TXDR = b2;                    /* 发第2字节 */

    t = 100000;
    while (!(I2C1_ISR & (1<<5)) && --t); /* 等 TC=1(传输完成) */
    if(!t) return 3;                    /* 超时→从机没响应→返回3 */
    I2C1_ICR = (1<<5);                 /* 清TC标志 */

    if (I2C1_ISR & (1<<4)) return 4;   /* NACKF=1→从机拒绝→返回4 */
    return 0;                           /* 成功 */
}


/* ─────────────────────────────────────────────────────────────────────────────
 * main() — 五步初始化 + 咔嗒声循环
 * ───────────────────────────────────────────────────────────────────────────── */
int main(void) {

    /* ====== 步骤1: 系统时钟切到 HSI16 = 16MHz ====== */
    RCC_CR |= (1 << 8);                                    /* HSION=1: 开HSI16振荡器 */
    while (!(RCC_CR & (1 << 10)));                         /* 等HSIRDY=1(振荡稳定) */
    (*(volatile uint32_t *)(RCC_BASE + 0x08)) = 0x00000001; /* RCC_CFGR SW=01: 选HSI16作SYSCLK */
    while (((*(volatile uint32_t *)(RCC_BASE + 0x08)) & 0x0C) != 0x04); /* 等SWS=01(切换完成) */
    RCC_AHB2ENR |= (1<<0)|(1<<1);                          /* 开GPIOA+GPIOB时钟 */
    RCC_APB1ENR1 |= (1 << 0);                              /* 开TIM2时钟(用来出MCLK) */
    RCC_APB1ENR1 |= (1 << 21);                             /* 开I2C1时钟(用来配WM8960) */
    delay(1000);                                           /* 等外设时钟稳定 */
    RESULT[0] = 0xAA;                                      /* ✅ [0]=AA: 启动完成 */


    /* ====== 步骤2: TIM2 CH4 PWM → PA3 → ~2.286MHz 方波(MCLK) ======
     * TIM2 时钟=HSI16=16MHz, ARR=6→周期=7tick→16M/7≈2.286MHz
     * CCR4=3→占空比 3/7≈43% (p13允许40-60%)
     * DCLKDIV=÷3→762kHz ∈ [700,800] ✅
     * 用 AF1(TIM2_CH4) 而非 AF5(I2S2_MCK) 因为后者有疑似硅片bug不出波形 */
    GPIO_MODER(GPIOA_BASE) = (GPIO_MODER(GPIOA_BASE) & ~(3<<6)) | (2<<6); /* PA3=AF(10) */
    GPIO_AFRL(GPIOA_BASE)  = (GPIO_AFRL(GPIOA_BASE)  & ~(0xF<<12)) | (1<<12); /* PA3→AF1=TIM2_CH4 */
    (*(volatile uint32_t *)(0x40000000 + 0x2C)) = 6;     /* ARR=6: PWM周期=7tick→2.286MHz */
    (*(volatile uint32_t *)(0x40000000 + 0x40)) = 3;     /* CCR4=3: 占空比 3/7≈43% */
    (*(volatile uint32_t *)(0x40000000 + 0x1C)) = (6<<12)|(1<<11); /* CCMR2: OC4M=110(PWM1)+OC4PE=1 */
    (*(volatile uint32_t *)(0x40000000 + 0x20)) = (1<<12);         /* CCER: CC4E=1使能CH4输出 */
    (*(volatile uint32_t *)(0x40000000 + 0x00)) = 1;              /* CR1: CEN=1启动定时器 */
    RESULT[1] = 0x01;                                      /* ✅ [1]=01: MCLK已输出 */


    /* ====== 步骤3: I2C1初始化，准备与WM8960通信 ======
     * PB6=SCL, PB7=SDA, 都设AF4(I2C1), 开漏+上拉(I2C协议要求), 100kHz */
    GPIO_MODER(GPIOB_BASE) &= ~((3<<12)|(3<<14));          /* 清除PB6/PB7模式位 */
    GPIO_MODER(GPIOB_BASE) |=  ((2<<12)|(2<<14));          /* PB6/PB7=10=AF模式 */
    GPIO_OTYPER(GPIOB_BASE) |= (1<<6)|(1<<7);              /* 开漏输出(I2C必须!) */
    GPIO_PUPDR(GPIOB_BASE)  |= ((1<<12)|(1<<14));          /* 内部上拉≈40kΩ */
    GPIO_AFRL(GPIOB_BASE)   |= ((4<<24)|(4<<28));          /* AF4=I2C1 */
    I2C1_TIMINGR = 0x00503D5B;                             /* 100kHz标准模式时序 */
    I2C1_CR1 = 1;                                          /* PE=1: 使能I2C1 */
    RESULT[2] = 0x1A;                                      /* ✅ [2]=1A: I2C就绪 */


    /* ====== 步骤4: 配置WM8960 ======
     * 建立完整模拟通路: DAC→输出混音器→扬声器PGA→Class D功放
     * 特别注意: 新增 DACMU=0(解除DAC软静音)，保证DAC输出VMID偏置进入PGA */
    uint8_t e = 0;                                         /* e=累积错误码, 0=全部成功 */

    e |= wm_write(0x0F, 0x000); delay(50000);              /* R15 Reset: 复位芯片到默认 */
    e |= wm_write(0x19, 0x080); delay(800000);             /* R25 POWER1: VMIDSEL=01(2×50k快充) 等~50ms */
    e |= wm_write(0x19, 0x140);                            /* R25 POWER1: VMIDSEL=10(250k省电)+VREF=1+DIGENB=0(使能!) */
    e |= wm_write(0x1A, 0x198);                            /* R26 POWER2: DACL+DACR+SPKL+SPKR */
    e |= wm_write(0x2F, 0x00C);                            /* R47 POWER3: LOMIX+ROMIX */
    delay(3200000);                                        /* 等~200ms模拟电路稳定 */
    e |= wm_write(0x04, 0x000);                            /* R4  CLOCK1: SYSCLK=MCLK直通 */
    e |= wm_write(0x08, 0x044);                            /* R8  CLOCK2: DCLKDIV=010(÷3→667kHz) BCLKDIV=4 */
    e |= wm_write(0x07, 0x002);                            /* R7  IFACE1: I²S格式/16-bit/Slave(虽然不用I2S, 配了无害) */
    e |= wm_write(0x09, 0x040);                            /* R9  IFACE2: ALRCGPIO=1(ADCLRC释放为GPIO) */
    e |= wm_write(0x0A, 0x0FF);                            /* R10 LDAC: VU=0, VOL=0xFF(0dB) → 装入Latch */
    e |= wm_write(0x0B, 0x1FF);                            /* R11 RDAC: VU=1, VOL=0xFF(0dB) → 触发两声道 */
    e |= wm_write(0x22, 0x100);                            /* R34 LOUTMIX: LD2LO=1 → DAC→输出混音器 */
    e |= wm_write(0x05, 0x000);                            /* R5  DACCTL1: DACMU=0 解DAC软静音!
                                                             * ★ v2新增: 默认DACMU=1(静音), DAC输出被强制归零
                                                             *   必须清零, 否则扬声器PGA收到的是0而非VMID偏置 */
    e |= wm_write(0x28, 0x07F);                            /* R40 SPK_L: VU=0, ZC=0, VOL=127(+6dB) → 装入Latch */
    e |= wm_write(0x29, 0x1FF);                            /* R41 SPK_R: VU=1, ZC=0, VOL=127(+6dB) → 触发两声道 */
    e |= wm_write(0x31, 0x0F7);                            /* R49 CLASSD: SPK_OP_EN=11 + Class D参数=0x37 */
    delay(3200000);                                        /* 等~200ms功放稳定 */
    RESULT[3] = e ? 0xEE : 0x00;                           /* ✅ [3]=00(全成功) 或 EE(有错误) */


    /* ====== 步骤5: 咔嗒声循环 ======
     *
     * ★ v2核心改动: 从切 DACMU(数字软静音, 无效) 改为切 SPKLVOL(模拟静音)
     *
     * Datasheet p43原文:
     *   "If the volume is adjusted while the signal is a non-zero value,
     *    an audible click can occur."
     *   "In order to prevent this click noise, a zero cross function is provided."
     *
     * 我们: ZC=0(关过零检测) → 切换立即生效 → 咔嗒声必定产生
     *
     * VU Latch协议(Datasheet p43 Fig16):
     *   1. 写 L: VU=0 → 新音量装入Latch(暂不生效)
     *   2. 写 R: VU=1 → 装入R Latch + 同时触发L和R更新
     *
     * 一个完整周期(约2秒):
     *   ┌─ MUTE ──────────────────────┬─ UNMUTE ────────────────────┐
     *   │ R40 VU=0 VOL=0  装入L latch │ R40 VU=0 VOL=127 装入L latch│
     *   │ R41 VU=1 VOL=0  触发→静音   │ R41 VU=1 VOL=127 触发→最大  │
     *   │ delay ~1秒 (扬声器安静)     │ delay ~1秒 (扬声器"嗒!")    │
     *   └─────────────────────────────┴─────────────────────────────┘
     *
     * 解码各次 wm_write 的参数:
     *   R40(0x28)=0x000: bit8 VU=0, bit7 ZC=0, bits[6:0] VOL=0(analog mute)
     *   R41(0x29)=0x100: bit8 VU=1, bit7 ZC=0, bits[6:0] VOL=0(analog mute)
     *   R40(0x28)=0x07F: bit8 VU=0, bit7 ZC=0, bits[6:0] VOL=127(+6dB max)
     *   R41(0x29)=0x1FF: bit8 VU=1, bit7 ZC=0, bits[6:0] VOL=127(+6dB max)
     *
     * 如果听到嗒嗒声 → 模拟通路(DAC→混音器→PGA→Class D→扬声器)正常 ✅
     *   之前I2S测试没声音 = I2S数字端的问题
     * 如果完全无声 → 模拟通路断开(电池/SPKVDD/Class D/扬声器) ❌ */

    RESULT[4] = 0x55;                                      /* ✅ [4]=55: 进入click循环 */

    while (1) {                                            /* 死循环，直到断电或复位 */
        /* ── MUTE 阶段 ── */
        e |= wm_write(0x28, 0x000);                        /* R40: VU=0, ZC=0, VOL=0(模拟静音) → 装入Latch */
        e |= wm_write(0x29, 0x100);                        /* R41: VU=1, ZC=0, VOL=0(模拟静音) → 触发! 两声道静音 */
        delay(16000000);                                   /* 等~1秒(这段时间扬声器应该安静) */

        /* ── UNMUTE 阶段 ── 从模拟静音→+6dB最大音量，必有电压跳变! */
        e |= wm_write(0x28, 0x07F);                        /* R40: VU=0, ZC=0, VOL=127(+6dB) → 装入Latch */
        e |= wm_write(0x29, 0x1FF);                        /* R41: VU=1, ZC=0, VOL=127(+6dB) → 触发! → "嗒!" */
        delay(16000000);                                   /* 等~1秒(仔细听这一瞬间) */

        RESULT[5]++;                                       /* 咔嗒计数+1, pyocd读此值可知程序在跑 */
    }
}

/* ═══════════════════════════════════════════════════════════════════════════════
 * 附: SRAM检查点速查卡
 *
 * pyocd read:  pyocd cmd -t stm32wle5cbux -c "halt" -c "read8 0x20000100 8"
 * 期望输出:    20000100: AA 01 1A 00 55 XX XX XX
 *                [0]AA=启动 [1]01=MCLK [2]1A=I2C [3]00=WM8960OK [4]55=循环 [5]=咔嗒计数
 *
 * 烧录:
 *   pyocd erase -t stm32wle5cbux -f 1000000 --chip
 *   pyocd flash -t stm32wle5cbux -f 1000000 click_test.bin --base-address 0x08000000
 *
 * v1→v2改动:
 *   +R5 DACMU=0(解DAC静音, 否则PGA收不到VMID偏置)
 *   循环: DACMU(R5) → SPKLVOL(R40/R41) 模拟静音↔最大音量
 *   原理: 数字静音无效 → 改用datasheet p43背书的"切音量出click"
 * ═══════════════════════════════════════════════════════════════════════════════ */
