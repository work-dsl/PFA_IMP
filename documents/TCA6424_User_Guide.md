# TCA6424 驱动使用说明

## 1. 概述

TCA6424是德州仪器（TI）生产的24位I²C I/O扩展器，通过I²C总线提供24个可配置的GPIO引脚。本驱动库提供了完整的TCA6424控制接口，支持单引脚操作和批量操作。

### 1.1 主要特性

- **24个I/O引脚**：分为3个8位端口（Port0、Port1、Port2）
- **I²C接口**：支持标准模式（100kHz）和快速模式（400kHz）
- **双向电压电平转换**：VCCI和VCCP可独立配置（1.65V-5.5V）
- **中断输出**：支持输入状态变化中断
- **上电默认状态**：所有引脚默认为输入模式，输出寄存器为0xFF（高电平）

### 1.2 引脚映射

TCA6424有24个I/O引脚，编号为P00-P27，在驱动中映射为0-23：

| 引脚编号 | 驱动编号 | 端口 | 位位置 | 说明 |
|---------|---------|------|--------|------|
| P00-P07 | 0-7     | Port0 | bit0-7  | 第一个8位端口 |
| P10-P17 | 8-15    | Port1 | bit8-15 | 第二个8位端口 |
| P20-P27 | 16-23   | Port2 | bit16-23| 第三个8位端口 |

**24位值映射关系：**
- bit0-7   → Port0 (P00-P07)
- bit8-15  → Port1 (P10-P17)
- bit16-23 → Port2 (P20-P27)

### 1.3 I²C地址配置

TCA6424的I²C地址由ADDR引脚决定：

| ADDR引脚状态 | I²C地址（7位） | 宏定义 |
|------------|--------------|--------|
| 接地（GND） | 0x22         | `TCA6424_I2C_ADDR_L` |
| 接VCCP      | 0x23         | `TCA6424_I2C_ADDR_H` |

## 2. 硬件连接

### 2.1 基本连接

```
MCU                    TCA6424
----                   -------
SCL  <---------------> SCL (Pin 29)
SDA  <---------------> SDA (Pin 30)
GND  <---------------> GND (Pin 25)
3.3V <---------------> VCCI (Pin 31)  [I²C总线电源]
3.3V <---------------> VCCP (Pin 27)  [端口电源]
                      
ADDR <---------------> ADDR (Pin 26)  [地址选择：GND或VCCP]
RESET<---------------> RESET (Pin 28) [复位：可选，需上拉]
INT  <---------------> INT  (Pin 32)  [中断输出：可选，需上拉]
```

### 2.2 上拉电阻

- **SCL/SDA**：需要上拉电阻（典型值4.7kΩ）连接到VCCI
- **RESET**：如果使用，需要上拉电阻连接到VCCP
- **INT**：如果使用中断，需要上拉电阻连接到VCCI或VCCP

### 2.3 电压电平转换

TCA6424支持双向电压电平转换：
- **VCCI**：I²C总线侧电源，连接到MCU的I²C电源
- **VCCP**：端口侧电源，可独立配置（1.65V-5.5V）

**示例配置：**
- MCU使用3.3V，端口需要5V：VCCI=3.3V，VCCP=5V
- MCU使用1.8V，端口使用3.3V：VCCI=1.8V，VCCP=3.3V

## 3. 软件初始化

### 3.1 包含头文件

```c
#include "tca6424.h"
```

### 3.2 设备结构体声明

```c
tca6424_t tca6424_dev;
```

### 3.3 初始化设备

```c
int ret;

/* 初始化TCA6424设备
 * addr7: I²C地址，使用TCA6424_I2C_ADDR_L或TCA6424_I2C_ADDR_H
 * adapter_name: I²C适配器名称，如"i2c1"
 */
ret = tca6424_init(&tca6424_dev, TCA6424_I2C_ADDR_L, "i2c1");
if (ret != 0) {
    /* 初始化失败处理 */
    printf("TCA6424 init failed: %d\n", ret);
    return ret;
}
```

**初始化后的默认状态：**
- 所有引脚配置为**输入模式**（配置寄存器=0xFF）
- 输出寄存器为**0xFF**（高电平）
- 极性反转寄存器为**0x00**（正常极性）
- 缓存状态为**无效**（需要调用`tca6424_refresh_cache`刷新）

## 4. 基本操作

### 4.1 单引脚操作

#### 4.1.1 配置引脚为输出

```c
/* 方法1：先设置输出值，再切换为输出（推荐，避免毛刺） */
ret = tca6424_configure_output_pin(&tca6424_dev, 0, true);  /* P00设为输出，初始高电平 */
if (ret != 0) {
    /* 错误处理 */
}

/* 方法2：直接切换为输出 */
ret = tca6424_pin_mode(&tca6424_dev, 0, false);  /* P00设为输出 */
```

#### 4.1.2 配置引脚为输入

```c
ret = tca6424_pin_mode(&tca6424_dev, 0, true);  /* P00设为输入 */
```

#### 4.1.3 写入引脚电平

```c
/* 设置P00为高电平 */
ret = tca6424_write_pin(&tca6424_dev, 0, true);

/* 设置P00为低电平 */
ret = tca6424_write_pin(&tca6424_dev, 0, false);
```

#### 4.1.4 读取引脚电平

```c
bool level;
ret = tca6424_read_pin(&tca6424_dev, 0, &level);
if (ret == 0) {
    if (level) {
        printf("P00 is HIGH\n");
    } else {
        printf("P00 is LOW\n");
    }
}
```

#### 4.1.5 翻转引脚电平

```c
ret = tca6424_toggle_pin(&tca6424_dev, 0);  /* 翻转P00电平 */
```

### 4.2 批量操作（24位端口）

#### 4.2.1 读取所有输入状态

```c
uint32_t inputs;
ret = tca6424_read_inputs24(&tca6424_dev, &inputs);
if (ret == 0) {
    /* inputs的bit0对应P00，bit23对应P27 */
    if (inputs & (1UL << 0)) {
        printf("P00 is HIGH\n");
    }
    if (inputs & (1UL << 23)) {
        printf("P27 is HIGH\n");
    }
}
```

#### 4.2.2 写入所有输出状态

```c
uint32_t outputs = 0;

/* 设置P00、P10、P20为高电平 */
outputs |= (1UL << 0);   /* P00 */
outputs |= (1UL << 8);   /* P10 */
outputs |= (1UL << 16);  /* P20 */

ret = tca6424_write_outputs24(&tca6424_dev, outputs);
```

#### 4.2.3 读取输出锁存器

```c
uint32_t out_latch;
ret = tca6424_read_outputs24(&tca6424_dev, &out_latch);
/* 注意：读取的是输出锁存器的值，不是引脚的实际电平 */
```

#### 4.2.4 配置所有引脚方向

```c
uint32_t config = 0;

/* 设置P00-P07为输出，P10-P27为输入 */
config = 0x00FF00FFUL;  /* bit0-7=0(输出), bit8-15=1(输入), bit16-23=1(输入) */

ret = tca6424_write_config24(&tca6424_dev, config);
```

### 4.3 掩码更新操作（推荐用于部分位更新）

掩码更新操作使用缓存机制，可以减少I²C读取次数，提高效率。

#### 4.3.1 更新输出端口（部分位）

```c
/* 设置P00为高，P01为低，其他位不变 */
ret = tca6424_update_outputs24(&tca6424_dev, 
                                (1UL << 0),    /* set_mask: 置1的位 */
                                (1UL << 1));   /* clr_mask: 清0的位（优先级高于set_mask） */
```

#### 4.3.2 更新配置寄存器（部分位）

```c
/* 将P00设为输出，P01设为输入，其他位不变 */
ret = tca6424_update_config24(&tca6424_dev,
                                (1UL << 1),    /* set_mask: 设为输入 */
                                (1UL << 0));   /* clr_mask: 设为输出 */
```

#### 4.3.3 更新极性反转寄存器

```c
/* 对P00启用极性反转，P01禁用极性反转 */
ret = tca6424_update_polarity24(&tca6424_dev,
                                 (1UL << 0),   /* set_mask: 启用反转 */
                                 (1UL << 1));  /* clr_mask: 禁用反转 */
```

## 5. 高级功能

### 5.1 极性反转

极性反转功能只对**配置为输入的引脚**有效。启用后，输入电平会被反转。

```c
/* 配置P00为输入 */
tca6424_pin_mode(&tca6424_dev, 0, true);

/* 启用P00的极性反转 */
uint32_t pol = (1UL << 0);
tca6424_write_polarity24(&tca6424_dev, pol);

/* 现在读取P00时，实际低电平会返回高电平，高电平会返回低电平 */
```

### 5.2 缓存管理

驱动内部维护了寄存器缓存，减少I²C访问。如果外部修改了寄存器（如通过硬件复位），需要刷新缓存：

```c
/* 刷新所有寄存器缓存 */
ret = tca6424_refresh_cache(&tca6424_dev);
```

**缓存机制说明：**
- `update_*`函数会自动使用缓存，如果缓存无效会先读取
- `write_*`函数会更新缓存
- `read_*`函数直接从硬件读取，不更新缓存
- 如果怀疑缓存不一致，调用`tca6424_refresh_cache`刷新

### 5.3 原始寄存器访问

如果需要直接访问寄存器，可以使用原始访问函数：

```c
uint8_t data[3];

/* 读取输出寄存器（Port0, Port1, Port2） */
ret = tca6424_read_reg(&tca6424_dev, 
                       TCA6424_REG_OUTPUT_PORT0, 
                       true,    /* 启用自动递增 */
                       data,    /* 接收缓冲区 */
                       3);      /* 读取3个字节 */

/* 写入配置寄存器 */
uint8_t cfg_data[3] = {0xFF, 0x00, 0xFF};  /* Port0全输入，Port1全输出，Port2全输入 */
ret = tca6424_write_reg(&tca6424_dev,
                        TCA6424_REG_CFG_PORT0,
                        true,      /* 启用自动递增 */
                        cfg_data,  /* 发送数据 */
                        3);        /* 写入3个字节 */
```

## 6. 完整示例

### 6.1 LED控制示例

```c
#include "tca6424.h"

tca6424_t tca6424_dev;

void led_example(void)
{
    int ret;
    
    /* 初始化设备 */
    ret = tca6424_init(&tca6424_dev, TCA6424_I2C_ADDR_L, "i2c1");
    if (ret != 0) {
        return;
    }
    
    /* 配置P00-P07为输出，初始为低电平（LED熄灭） */
    for (int i = 0; i < 8; i++) {
        tca6424_configure_output_pin(&tca6424_dev, i, false);
    }
    
    /* LED流水灯效果 */
    while (1) {
        for (int i = 0; i < 8; i++) {
            tca6424_write_pin(&tca6424_dev, i, true);   /* LED亮 */
            delay_ms(100);
            tca6424_write_pin(&tca6424_dev, i, false);  /* LED灭 */
        }
    }
}
```

### 6.2 按键扫描示例

```c
void key_scan_example(void)
{
    int ret;
    bool key_state;
    
    /* 初始化设备 */
    ret = tca6424_init(&tca6424_dev, TCA6424_I2C_ADDR_L, "i2c1");
    if (ret != 0) {
        return;
    }
    
    /* 配置P10-P17为输入（按键输入） */
    for (int i = 8; i < 16; i++) {
        tca6424_pin_mode(&tca6424_dev, i, true);
    }
    
    /* 按键扫描 */
    while (1) {
        for (int i = 8; i < 16; i++) {
            ret = tca6424_read_pin(&tca6424_dev, i, &key_state);
            if (ret == 0 && key_state == false) {  /* 假设按键按下为低电平 */
                printf("Key %d pressed\n", i - 8);
            }
        }
        delay_ms(10);
    }
}
```

### 6.3 批量操作示例

```c
void batch_operation_example(void)
{
    int ret;
    uint32_t outputs;
    
    /* 初始化设备 */
    ret = tca6424_init(&tca6424_dev, TCA6424_I2C_ADDR_L, "i2c1");
    if (ret != 0) {
        return;
    }
    
    /* 配置Port0和Port1为输出，Port2为输入 */
    uint32_t config = 0xFFFF00UL;  /* bit0-15=0(输出), bit16-23=1(输入) */
    tca6424_write_config24(&tca6424_dev, config);
    
    /* 设置所有输出引脚为高电平 */
    outputs = 0xFFFFUL;  /* bit0-15全为1 */
    tca6424_write_outputs24(&tca6424_dev, outputs);
    
    /* 使用掩码更新：只改变P00和P01 */
    tca6424_update_outputs24(&tca6424_dev, 
                             0x00UL,        /* 不置1 */
                             (1UL << 0) | (1UL << 1));  /* P00和P01清0 */
    
    /* 读取所有输入状态 */
    uint32_t inputs;
    tca6424_read_inputs24(&tca6424_dev, &inputs);
    printf("Inputs: 0x%06X\n", inputs);
}
```

## 7. 注意事项

### 7.1 上电默认状态

根据数据手册，TCA6424上电后：
- **配置寄存器**：0xFF（所有引脚为输入）
- **输出寄存器**：0xFF（高电平）
- **极性反转寄存器**：0x00（正常极性）

**建议：** 初始化后根据应用需求重新配置引脚方向。

### 7.2 输出切换毛刺

当引脚从输入切换到输出时，如果输出锁存器值未设置，可能出现毛刺。

**解决方法：** 使用`tca6424_configure_output_pin`函数，它会先设置输出值，再切换为输出模式。

```c
/* 推荐方式：避免毛刺 */
tca6424_configure_output_pin(&tca6424_dev, pin, initial_level);

/* 不推荐：可能产生毛刺 */
tca6424_pin_mode(&tca6424_dev, pin, false);
tca6424_write_pin(&tca6424_dev, pin, level);
```

### 7.3 输入端口寄存器特性

根据数据手册，**输入端口寄存器反映引脚的实际电平**，无论引脚配置为输入还是输出。这意味着：
- 如果引脚配置为输出，读取输入端口寄存器也能读到输出电平
- 如果引脚配置为输入，读取输入端口寄存器读到的是外部输入电平

### 7.4 输出寄存器特性

**输出寄存器**存储的是输出锁存器的值，不是引脚的实际电平：
- 读取输出寄存器得到的是写入的值
- 如果引脚配置为输入，写入输出寄存器不会影响引脚电平
- 如果引脚配置为输出，写入输出寄存器会改变引脚电平

### 7.5 极性反转限制

**极性反转只对配置为输入的引脚有效**：
- 如果引脚配置为输出，设置极性反转无效
- 极性反转影响输入端口寄存器的读取值

### 7.6 I²C总线速度

TCA6424支持：
- **标准模式**：最高100kHz
- **快速模式**：最高400kHz

确保I²C适配器配置的速度不超过400kHz。

### 7.7 中断功能

本驱动库**未实现中断功能**。如需使用中断：
- INT引脚需要上拉电阻
- 需要外部GPIO中断处理
- 中断触发后，读取输入端口寄存器即可清除中断

**中断触发条件：** 任何输入引脚状态变化（从输入端口寄存器上次读取后）

## 8. 寄存器说明

### 8.1 寄存器映射表

| 寄存器地址 | 寄存器名称 | 读写 | 说明 | 默认值 |
|-----------|-----------|------|------|--------|
| 0x00      | Input Port 0 | 只读 | 输入端口0（P00-P07） | 外部电平 |
| 0x01      | Input Port 1 | 只读 | 输入端口1（P10-P17） | 外部电平 |
| 0x02      | Input Port 2 | 只读 | 输入端口2（P20-P27） | 外部电平 |
| 0x04      | Output Port 0 | 读写 | 输出端口0（P00-P07） | 0xFF |
| 0x05      | Output Port 1 | 读写 | 输出端口1（P10-P17） | 0xFF |
| 0x06      | Output Port 2 | 读写 | 输出端口2（P20-P27） | 0xFF |
| 0x08      | Polarity Inversion 0 | 读写 | 极性反转0（P00-P07） | 0x00 |
| 0x09      | Polarity Inversion 1 | 读写 | 极性反转1（P10-P17） | 0x00 |
| 0x0A      | Polarity Inversion 2 | 读写 | 极性反转2（P20-P27） | 0x00 |
| 0x0C      | Configuration 0 | 读写 | 配置寄存器0（P00-P07） | 0xFF |
| 0x0D      | Configuration 1 | 读写 | 配置寄存器1（P10-P17） | 0xFF |
| 0x0E      | Configuration 2 | 读写 | 配置寄存器2（P20-P27） | 0xFF |

**注意：** 寄存器0x03、0x07、0x0B、0x0F为保留寄存器，不应访问。

### 8.2 配置寄存器位定义

- **1** = 输入模式（高阻态）
- **0** = 输出模式

### 8.3 输出寄存器位定义

- **1** = 输出高电平
- **0** = 输出低电平

**注意：** 只对配置为输出的引脚有效。

### 8.4 极性反转寄存器位定义

- **1** = 启用极性反转（只对输入引脚有效）
- **0** = 正常极性

## 9. 错误处理

所有函数返回0表示成功，负数表示错误。常见错误码：

| 错误码 | 宏定义 | 说明 |
|-------|--------|------|
| -1     | -EINVAL | 参数无效（NULL指针、超出范围等） |
| -19    | -ENODEV | 设备未找到（I²C通信失败） |

**错误处理示例：**

```c
int ret = tca6424_init(&tca6424_dev, TCA6424_I2C_ADDR_L, "i2c1");
if (ret != 0) {
    switch (ret) {
        case -EINVAL:
            printf("Invalid parameter\n");
            break;
        case -ENODEV:
            printf("Device not found\n");
            break;
        default:
            printf("Unknown error: %d\n", ret);
            break;
    }
    return ret;
}
```

## 10. 性能优化建议

### 10.1 使用掩码更新

对于部分位的更新，使用`update_*`函数比先读后写更高效：

```c
/* 高效方式：使用缓存，减少I²C读取 */
tca6424_update_outputs24(&tca6424_dev, set_mask, clr_mask);

/* 低效方式：需要两次I²C操作 */
uint32_t outputs;
tca6424_read_outputs24(&tca6424_dev, &outputs);
outputs |= set_mask;
outputs &= ~clr_mask;
tca6424_write_outputs24(&tca6424_dev, outputs);
```

### 10.2 批量操作

对于多个引脚的配置，使用24位批量操作比单引脚操作更高效：

```c
/* 高效方式：一次I²C操作配置所有引脚 */
uint32_t config = 0x00FF00UL;
tca6424_write_config24(&tca6424_dev, config);

/* 低效方式：多次I²C操作 */
for (int i = 0; i < 8; i++) {
    tca6424_pin_mode(&tca6424_dev, i, false);  /* 8次I²C操作 */
}
```

### 10.3 缓存刷新时机

只在必要时刷新缓存：
- 系统复位后
- 怀疑缓存不一致时
- 外部修改了寄存器后

## 11. 常见问题

### Q1: 为什么写入输出寄存器后引脚电平没有变化？

**A:** 检查引脚是否配置为输出模式。如果配置为输入，写入输出寄存器不会影响引脚电平。

```c
/* 确保引脚配置为输出 */
tca6424_pin_mode(&tca6424_dev, pin, false);
tca6424_write_pin(&tca6424_dev, pin, level);
```

### Q2: 如何同时控制多个引脚？

**A:** 使用24位批量操作或掩码更新：

```c
/* 方式1：使用掩码更新 */
tca6424_update_outputs24(&tca6424_dev, 
                          (1UL << 0) | (1UL << 1),  /* P00和P01置1 */
                          (1UL << 2));              /* P02清0 */

/* 方式2：使用24位批量操作 */
uint32_t outputs = (1UL << 0) | (1UL << 1);
tca6424_write_outputs24(&tca6424_dev, outputs);
```

### Q3: 读取输入时为什么得到错误的值？

**A:** 检查以下几点：
1. 引脚是否配置为输入
2. 是否启用了极性反转
3. 外部电路是否正确连接

### Q4: 如何实现LED控制（共阳极）？

**A:** 对于共阳极LED，低电平点亮：

```c
/* 配置为输出，初始高电平（LED熄灭） */
tca6424_configure_output_pin(&tca6424_dev, pin, true);

/* 点亮LED（输出低电平） */
tca6424_write_pin(&tca6424_dev, pin, false);

/* 熄灭LED（输出高电平） */
tca6424_write_pin(&tca6424_dev, pin, true);
```

### Q5: 如何检测按键（上拉输入）？

**A:** 按键按下为低电平：

```c
/* 配置为输入（内部上拉，如果支持） */
tca6424_pin_mode(&tca6424_dev, pin, true);

/* 读取按键状态 */
bool level;
tca6424_read_pin(&tca6424_dev, pin, &level);
if (!level) {
    /* 按键按下 */
}
```

## 12. API参考

### 12.1 初始化函数

```c
int tca6424_init(tca6424_t *dev, uint8_t addr7, const char *adapter_name);
int tca6424_refresh_cache(tca6424_t *dev);
```

### 12.2 原始寄存器访问

```c
int tca6424_read_reg(tca6424_t *dev, uint8_t reg, bool auto_inc, uint8_t *rx_buf, uint8_t len);
int tca6424_write_reg(tca6424_t *dev, uint8_t reg, bool auto_inc, uint8_t *tx_buf, uint8_t len);
```

### 12.3 24位端口访问

```c
int tca6424_read_inputs24(tca6424_t *dev, uint32_t *in_bits);
int tca6424_read_outputs24(tca6424_t *dev, uint32_t *out_latch_bits);
int tca6424_write_outputs24(tca6424_t *dev, uint32_t out_latch_bits);
int tca6424_read_polarity24(tca6424_t *dev, uint32_t *pol_bits);
int tca6424_write_polarity24(tca6424_t *dev, uint32_t pol_bits);
int tca6424_read_config24(tca6424_t *dev, uint32_t *cfg_bits);
int tca6424_write_config24(tca6424_t *dev, uint32_t cfg_bits);
```

### 12.4 掩码更新操作

```c
int tca6424_update_outputs24(tca6424_t *dev, uint32_t set_mask, uint32_t clr_mask);
int tca6424_update_config24(tca6424_t *dev, uint32_t set_mask, uint32_t clr_mask);
int tca6424_update_polarity24(tca6424_t *dev, uint32_t set_mask, uint32_t clr_mask);
```

### 12.5 单引脚操作

```c
int tca6424_pin_mode(tca6424_t *dev, uint8_t pin, bool input);
int tca6424_read_pin(tca6424_t *dev, uint8_t pin, bool *level);
int tca6424_write_pin(tca6424_t *dev, uint8_t pin, bool level);
int tca6424_toggle_pin(tca6424_t *dev, uint8_t pin);
int tca6424_configure_output_pin(tca6424_t *dev, uint8_t pin, bool initial_level);
```

详细函数说明请参考源代码中的Doxygen注释。

## 13. 版本历史

- **V1.0** (2025-01-XX)
  - 初始版本
  - 实现基本I/O操作
  - 支持24位批量操作
  - 支持单引脚操作
  - 实现寄存器缓存机制