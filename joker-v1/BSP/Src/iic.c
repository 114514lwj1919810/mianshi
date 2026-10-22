/* AUTO-FILE-DOC
 * 文件职责：I2C 总线驱动。
 * 阅读方法：函数注释列出直接调用者、参数/指针方向和关键位运算；文件级变量的完整引用位置见工程根目录《代码阅读说明.md》。
 * 范围声明：本注释描述当前源码，不替代原理图、电气极性和硬件实测。
 */
#include "iic.h"

#include "./SYSTEM/delay/delay.h"

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
static I2C_HandleTypeDef s_iic1;

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
static uint8_t s_iic1_inited = 0U;


/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
#define IIC_BUS_RECOVER_PULSE_COUNT     9U
#define IIC_BUS_RECOVER_DELAY_US        5U

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
/**
 * @brief I2C 总线驱动：执行内部处理（对应符号 Iic_RecoverBusGpio）。
 * @调用关系 直接调用者：iic.c:Iic_Init。
 * @局部状态 主要临时变量：gpio、pulse_index；均只在本次调用中有效。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static void Iic_RecoverBusGpio(void)
{
    GPIO_InitTypeDef gpio = {0};
    uint8_t pulse_index;

    /* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
    HAL_GPIO_WritePin(GPIOB, IIC1_SCL_PIN | IIC1_SDA_PIN, GPIO_PIN_SET);

    /* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
    gpio.Pin = IIC1_SCL_PIN | IIC1_SDA_PIN;
    gpio.Mode = GPIO_MODE_OUTPUT_OD;
    gpio.Pull = GPIO_PULLUP;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOB, &gpio);

    /* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
    HAL_GPIO_WritePin(GPIOB, IIC1_SCL_PIN | IIC1_SDA_PIN, GPIO_PIN_SET);
    delay_us(IIC_BUS_RECOVER_DELAY_US);

    /* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
    for (pulse_index = 0U; pulse_index < IIC_BUS_RECOVER_PULSE_COUNT; pulse_index++)
    {
        HAL_GPIO_WritePin(GPIOB, IIC1_SCL_PIN, GPIO_PIN_RESET);
        delay_us(IIC_BUS_RECOVER_DELAY_US);
        HAL_GPIO_WritePin(GPIOB, IIC1_SCL_PIN, GPIO_PIN_SET);
        delay_us(IIC_BUS_RECOVER_DELAY_US);
    }

    /* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
    HAL_GPIO_WritePin(GPIOB, IIC1_SDA_PIN, GPIO_PIN_RESET);
    delay_us(IIC_BUS_RECOVER_DELAY_US);
    HAL_GPIO_WritePin(GPIOB, IIC1_SCL_PIN, GPIO_PIN_SET);
    delay_us(IIC_BUS_RECOVER_DELAY_US);
    HAL_GPIO_WritePin(GPIOB, IIC1_SDA_PIN, GPIO_PIN_SET);
    delay_us(IIC_BUS_RECOVER_DELAY_US);
}

/**
 * @brief I2C 总线驱动：初始化（对应符号 Iic_Init）。
 * @调用关系 直接调用者：app_main.c:App_Init。
 * @局部状态 主要临时变量：gpio、status；均只在本次调用中有效。
 * @return 返回值含义由声明类型和调用处分支共同约定；0/1 通常表示失败/成功或否/是。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
uint8_t Iic_Init(void)
{
    GPIO_InitTypeDef gpio = {0};
    HAL_StatusTypeDef status;

    /* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
    s_iic1_inited = 0U;

    /* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
    __HAL_RCC_GPIOB_CLK_ENABLE();

    /* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
    Iic_RecoverBusGpio();

    /* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
    gpio.Pin = IIC1_SCL_PIN | IIC1_SDA_PIN;
    gpio.Mode = GPIO_MODE_AF_OD;
    gpio.Pull = GPIO_PULLUP;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    gpio.Alternate = IIC1_GPIO_AF;
    HAL_GPIO_Init(GPIOB, &gpio);

    /* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
    __HAL_RCC_I2C1_CLK_ENABLE();
    __HAL_RCC_I2C1_FORCE_RESET();
    __HAL_RCC_I2C1_RELEASE_RESET();

    /* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
    s_iic1.Instance = IIC1_PERIPH;
    s_iic1.Init.Timing = IIC1_TIMING_100KHZ;
    s_iic1.Init.OwnAddress1 = 0U;
    s_iic1.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
    s_iic1.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
    s_iic1.Init.OwnAddress2 = 0U;
    s_iic1.Init.OwnAddress2Masks = I2C_OA2_NOMASK;
    s_iic1.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
    s_iic1.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;

    status = HAL_I2C_Init(&s_iic1);
    if (status != HAL_OK)
    {
        s_iic1_inited = 0U;
        return 0U;
    }

    /* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
    status = HAL_I2CEx_ConfigAnalogFilter(&s_iic1, I2C_ANALOGFILTER_ENABLE);
    if (status != HAL_OK)
    {
        s_iic1_inited = 0U;
        return 0U;
    }

    status = HAL_I2CEx_ConfigDigitalFilter(&s_iic1, 0U);
    if (status != HAL_OK)
    {
        s_iic1_inited = 0U;
        return 0U;
    }

    s_iic1_inited = 1U;
    return 1U;
}

/**
 * @brief I2C 总线驱动：初始化（对应符号 Iic_IsInited）。
 * @调用关系 直接调用者：iic.c:Iic_Test1。
 * @return 返回值含义由声明类型和调用处分支共同约定；0/1 通常表示失败/成功或否/是。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
uint8_t Iic_IsInited(void)
{
    return s_iic1_inited;
}

/**
 * @brief I2C 总线驱动：读取（对应符号 Iic_IsDeviceReady）。
 * @调用关系 直接调用者：eeprom.c:Eeprom_Check、eeprom.c:Eeprom_Write、iic.c:Iic_Test2、iic.c:Iic_Test3、pcf8563.c:Pcf8563_Check。
 * @param dev_addr_hal 按值传入；函数内部修改不会回写调用方变量。
 * @param trials 按值传入；函数内部修改不会回写调用方变量。
 * @param timeout_ms 按值传入；函数内部修改不会回写调用方变量。
 * @return 返回值含义由声明类型和调用处分支共同约定；0/1 通常表示失败/成功或否/是。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
uint8_t Iic_IsDeviceReady(uint16_t dev_addr_hal, uint32_t trials, uint32_t timeout_ms)
{
    if (s_iic1_inited == 0U)
    {
        return 0U;
    }

    if (HAL_I2C_IsDeviceReady(&s_iic1, dev_addr_hal, trials, timeout_ms) == HAL_OK)
    {
        return 1U;
    }

    return 0U;
}

/**
 * @brief I2C 总线驱动：写入（对应符号 Iic_MasterWrite）。
 * @调用关系 直接调用者：未发现普通 C 直接调用；可能由启动文件、中断向量、回调表或外部模块入口调用。
 * @param dev_addr_hal 按值传入；函数内部修改不会回写调用方变量。
 * @param data 只读输入指针；调用方必须保证地址有效且容量满足接口约定。
 * @param len 按值传入；函数内部修改不会回写调用方变量。
 * @param timeout_ms 按值传入；函数内部修改不会回写调用方变量。
 * @return 返回值含义由声明类型和调用处分支共同约定；0/1 通常表示失败/成功或否/是。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
uint8_t Iic_MasterWrite(uint16_t dev_addr_hal,
                        const uint8_t *data,
                        uint16_t len,
                        uint32_t timeout_ms)
{
    if (s_iic1_inited == 0U)
    {
        return 0U;
    }

    if ((data == 0) && (len != 0U))
    {
        return 0U;
    }

    if (len == 0U)
    {
        return 1U;
    }

    if (HAL_I2C_Master_Transmit(&s_iic1,
                                dev_addr_hal,
                                (uint8_t *)data,
                                len,
                                timeout_ms) == HAL_OK)
    {
        return 1U;
    }

    return 0U;
}

/**
 * @brief I2C 总线驱动：读取（对应符号 Iic_MasterRead）。
 * @调用关系 直接调用者：未发现普通 C 直接调用；可能由启动文件、中断向量、回调表或外部模块入口调用。
 * @param dev_addr_hal 按值传入；函数内部修改不会回写调用方变量。
 * @param data 输入/输出指针（函数可能通过它回写结果）；调用方必须保证地址有效且容量满足接口约定。
 * @param len 按值传入；函数内部修改不会回写调用方变量。
 * @param timeout_ms 按值传入；函数内部修改不会回写调用方变量。
 * @return 返回值含义由声明类型和调用处分支共同约定；0/1 通常表示失败/成功或否/是。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
uint8_t Iic_MasterRead(uint16_t dev_addr_hal,
                       uint8_t *data,
                       uint16_t len,
                       uint32_t timeout_ms)
{
    if (s_iic1_inited == 0U)
    {
        return 0U;
    }

    if ((data == 0) && (len != 0U))
    {
        return 0U;
    }

    if (len == 0U)
    {
        return 1U;
    }

    if (HAL_I2C_Master_Receive(&s_iic1,
                               dev_addr_hal,
                               data,
                               len,
                               timeout_ms) == HAL_OK)
    {
        return 1U;
    }

    return 0U;
}

/**
 * @brief I2C 总线驱动：写入（对应符号 Iic_MemWrite）。
 * @调用关系 直接调用者：eeprom.c:Eeprom_Write、pcf8563.c:Pcf8563_WriteReg、pcf8563.c:Pcf8563_Init、pcf8563.c:Pcf8563_SetTime。
 * @param dev_addr_hal 按值传入；函数内部修改不会回写调用方变量。
 * @param mem_addr 按值传入；函数内部修改不会回写调用方变量。
 * @param mem_addr_size 按值传入；函数内部修改不会回写调用方变量。
 * @param data 只读输入指针；调用方必须保证地址有效且容量满足接口约定。
 * @param len 按值传入；函数内部修改不会回写调用方变量。
 * @param timeout_ms 按值传入；函数内部修改不会回写调用方变量。
 * @return 返回值含义由声明类型和调用处分支共同约定；0/1 通常表示失败/成功或否/是。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
uint8_t Iic_MemWrite(uint16_t dev_addr_hal,
                     uint16_t mem_addr,
                     uint16_t mem_addr_size,
                     const uint8_t *data,
                     uint16_t len,
                     uint32_t timeout_ms)
{
    if (s_iic1_inited == 0U)
    {
        return 0U;
    }

    if ((data == 0) && (len != 0U))
    {
        return 0U;
    }

    if (len == 0U)
    {
        return 1U;
    }

    if (HAL_I2C_Mem_Write(&s_iic1,
                          dev_addr_hal,
                          mem_addr,
                          mem_addr_size,
                          (uint8_t *)data,
                          len,
                          timeout_ms) == HAL_OK)
    {
        return 1U;
    }

    return 0U;
}

/**
 * @brief I2C 总线驱动：读取（对应符号 Iic_MemRead）。
 * @调用关系 直接调用者：eeprom.c:Eeprom_Read、pcf8563.c:Pcf8563_ClearVoltageLowFlag、pcf8563.c:Pcf8563_Init、pcf8563.c:Pcf8563_ReadTime。
 * @param dev_addr_hal 按值传入；函数内部修改不会回写调用方变量。
 * @param mem_addr 按值传入；函数内部修改不会回写调用方变量。
 * @param mem_addr_size 按值传入；函数内部修改不会回写调用方变量。
 * @param data 输入/输出指针（函数可能通过它回写结果）；调用方必须保证地址有效且容量满足接口约定。
 * @param len 按值传入；函数内部修改不会回写调用方变量。
 * @param timeout_ms 按值传入；函数内部修改不会回写调用方变量。
 * @return 返回值含义由声明类型和调用处分支共同约定；0/1 通常表示失败/成功或否/是。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
uint8_t Iic_MemRead(uint16_t dev_addr_hal,
                    uint16_t mem_addr,
                    uint16_t mem_addr_size,
                    uint8_t *data,
                    uint16_t len,
                    uint32_t timeout_ms)
{
    if (s_iic1_inited == 0U)
    {
        return 0U;
    }

    if ((data == 0) && (len != 0U))
    {
        return 0U;
    }

    if (len == 0U)
    {
        return 1U;
    }

    if (HAL_I2C_Mem_Read(&s_iic1,
                         dev_addr_hal,
                         mem_addr,
                         mem_addr_size,
                         data,
                         len,
                         timeout_ms) == HAL_OK)
    {
        return 1U;
    }

    return 0U;
}

/**
 * @brief I2C 总线驱动：执行内部处理（对应符号 Iic_Test1）。
 * @调用关系 直接调用者：未发现普通 C 直接调用；可能由启动文件、中断向量、回调表或外部模块入口调用。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
void Iic_Test1(void)
{
    /* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
    (void)Iic_IsInited();
}

/**
 * @brief I2C 总线驱动：执行内部处理（对应符号 Iic_Test2）。
 * @调用关系 直接调用者：未发现普通 C 直接调用；可能由启动文件、中断向量、回调表或外部模块入口调用。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
void Iic_Test2(void)
{
    /* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
    (void)Iic_IsDeviceReady(0xA0U, 1U, IIC_DEFAULT_TIMEOUT_MS);
}

/**
 * @brief I2C 总线驱动：执行内部处理（对应符号 Iic_Test3）。
 * @调用关系 直接调用者：未发现普通 C 直接调用；可能由启动文件、中断向量、回调表或外部模块入口调用。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
void Iic_Test3(void)
{
    /* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
    (void)Iic_IsDeviceReady(0xA2U, 1U, IIC_DEFAULT_TIMEOUT_MS);
}
