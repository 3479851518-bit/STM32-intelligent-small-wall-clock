#include "stm32f1xx_hal.h"
#include "oled.h"

#define SCROLL_STEP_MS   35
#define SCROLL_LINE_Y    32

I2C_HandleTypeDef hi2c1;

static const char SCROLL_TEXT[] = "  Hello,STM32!  ";

void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_I2C1_Init(void);

int main(void)
{
    HAL_Init();
    SystemClock_Config();

    MX_GPIO_Init();
    MX_I2C1_Init();


    OLED_Init();


    OLED_ShowString(0, 45, "Hello");
    OLED_ShowString(0, 48, "Count:");
    OLED_ShowNum(56, 48, 0, 5);
    OLED_Refresh();

    const int16_t text_w = (int16_t)(sizeof(SCROLL_TEXT) - 1) * 8;
    int16_t offset = 0;
    int32_t count = 0;
    uint32_t last_scroll = HAL_GetTick();
    uint32_t last_second = last_scroll;
    uint32_t last_led = last_scroll;

    while (1) {
        uint32_t now = HAL_GetTick();


        if (now - last_scroll >= SCROLL_STEP_MS) {
            last_scroll = now;
            OLED_ClearPages(SCROLL_LINE_Y / 8, 2);
            OLED_ShowStringAt((int16_t)(OLED_WIDTH - offset), SCROLL_LINE_Y, SCROLL_TEXT);
            OLED_RefreshPages(SCROLL_LINE_Y / 8, 2);

            offset++;
            if (offset > (int16_t)(OLED_WIDTH + text_w)) {
                offset = 0;
            }
        }

        if (now - last_second >= 1000) {
            last_second = now;
            count++;
            OLED_ClearPages(6, 2);
            OLED_ShowString(0, 48, "Count:");
            OLED_ShowNum(56, 48, count, 5);
            OLED_RefreshPages(6, 2);
        }

        if (now - last_led >= 500) {
            last_led = now;
            HAL_GPIO_TogglePin(GPIOC, GPIO_PIN_13);
        }
    }
}


void SystemClock_Config(void)
{
}


void SysTick_Handler(void)
{
    HAL_IncTick();
}

static void MX_GPIO_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    __HAL_RCC_GPIOC_CLK_ENABLE();

    GPIO_InitStruct.Pin = GPIO_PIN_13;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);
}


static void MX_I2C1_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_I2C1_CLK_ENABLE();

    GPIO_InitStruct.Pin = GPIO_PIN_6 | GPIO_PIN_7;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_OD;
    GPIO_InitStruct.Pull = GPIO_PULLUP;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

    hi2c1.Instance = I2C1;
    hi2c1.Init.ClockSpeed = 400000;
    hi2c1.Init.DutyCycle = I2C_DUTYCYCLE_2;
    hi2c1.Init.OwnAddress1 = 0;
    hi2c1.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
    hi2c1.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
    hi2c1.Init.OwnAddress2 = 0;
    hi2c1.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
    hi2c1.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;
    (void)HAL_I2C_Init(&hi2c1);
}
