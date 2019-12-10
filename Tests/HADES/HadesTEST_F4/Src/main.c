#include "main.h"

#include <string.h>

#include "MPRLS.h"
#include "IIS2MDC.h"
#include "BMI088.h"
#include "TMP100.h"

#include "KalmanRollPitch.h"

I2C_HandleTypeDef hi2c1;
I2C_HandleTypeDef hi2c2;
I2C_HandleTypeDef hi2c3;

UART_HandleTypeDef huart1;
UART_HandleTypeDef huart2;
UART_HandleTypeDef huart3;


void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_USB_OTG_FS_USB_Init(void);
static void MX_USART1_UART_Init(void);
static void MX_USART3_UART_Init(void);
static void MX_USART2_UART_Init(void);
static void MX_I2C1_Init(void);
static void MX_I2C2_Init(void);
static void MX_I2C3_Init(void);

/* Sensors */
MPRLSBarometer bar;
IISMagnetometer mag;
BMI088IMU imu;
TMP100 tmp;

/* State estimators */
KalmanRollPitch kalrp;

void printDebug(char *buf) {
	HAL_UART_Transmit(&huart3, (uint8_t *) buf, strlen(buf), HAL_MAX_DELAY);
}

int main(void)
{

  HAL_Init();

  SystemClock_Config();

  MX_GPIO_Init();
  MX_USB_OTG_FS_USB_Init();
  MX_USART1_UART_Init();
  MX_USART3_UART_Init();
  MX_USART2_UART_Init();
  MX_I2C1_Init();
  MX_I2C2_Init();
  MX_I2C3_Init();

  HAL_Delay(100);

  printDebug("NAVC started.\r\n");
  HAL_Delay(100);
  printDebug("Initialising sensors...\r\n");
  HAL_Delay(100);

  /* Initialise pressure sensor */
  uint8_t status = MPRLSBarometer_Init(&bar, &hi2c1, BARNRST_GPIO_Port, BARNRST_Pin, GPIOA, INTBAR_Pin);
  if (status == MPRLS_STATUS_POWERED) {
	  printDebug("Barometer initialised.\r\n");
  }

  /* Initialise magnetometer */
  status = IISMagnetometer_Init(&mag, &hi2c1, GPIOA, INTMAG_Pin);
  if (status == 1) {
	  printDebug("Magnetometer initialised.\r\n");
  }

  /* Initialise IMU */
  status = BMI088_Init(&imu, &hi2c1, GPIOA, INTACC_Pin, GPIOA, INTGYR_Pin);
  if (status == 1) {
	  printDebug("IMU initialised.\r\n");
  }

  /* Initialise temperature sensor */
  TMP100_Init(&tmp, &hi2c1);
  printDebug("Temperature sensor initialised.\r\n");


  /* Initialise Kalman filter (roll and pitch) */
  float KALRP_INIT_P = 10.0f;
  float KALRP_Q[] = {0.002077734f, 0.001385156f};//{0.00002077734f, 0.00001385156f};
  float KALRP_R[] = {0.04927288f, 0.04927288f, 0.06948246f};//{0.00004927288f, 0.00004927288f, 0.00006948246f};
  KalmanRollPitch_Init(&kalrp, KALRP_INIT_P, KALRP_Q, KALRP_R);

  uint32_t timerBar = 0;
  uint32_t timerMag = 0;
  uint32_t timerAcc = 0;
  uint32_t timerGyr = 0;
  uint32_t timerTmp = 0;
  uint32_t timerDbg = 0;
  uint32_t timerLED = 0;

  const uint32_t SAMPLE_TIME_BAR_MS = 10;
  const uint32_t SAMPLE_TIME_MAG_MS = 10;
  const uint32_t SAMPLE_TIME_ACC_MS = 5;
  const uint32_t SAMPLE_TIME_GYR_MS = 1;
  const uint32_t SAMPLE_TIME_TMP_MS = 320;
  const uint32_t SAMPLE_TIME_DBG_MS = 250;
  const uint32_t SAMPLE_TIME_LED_MS = 1000;

  printDebug("Starting main loop...\r\n");
  while (1)
  {
	  /* Gyroscope and Kalman filter prediction */
	  if (HAL_GetTick() - timerGyr >= SAMPLE_TIME_GYR_MS) {
		  float T = (HAL_GetTick() - timerGyr) * 0.001f;

		  BMI088_ReadGyr(&imu);

		  KalmanRollPitch_Update(&kalrp, imu.gyr, T);

		  timerGyr += SAMPLE_TIME_GYR_MS;
	  }

	  /* Accelerometer and Kalman filter update */
	  if (HAL_GetTick() - timerAcc >= SAMPLE_TIME_ACC_MS) {
		  BMI088_ReadAcc(&imu);

		  /* Note: Airspeed measurement not implemented yet! */
		  KalmanRollPitch_UpdateMeasurement(&kalrp, imu.gyr, imu.acc, 0.0f);

		  timerAcc += SAMPLE_TIME_ACC_MS;
	  }

	  /* Barometer */
	  if (HAL_GetTick() - timerBar >= SAMPLE_TIME_BAR_MS) {
		  /* Read pressure */
		  MPRLSBarometer_ReadPressure(&bar);

		  timerBar += SAMPLE_TIME_BAR_MS;
	  }

	  /* Magnetometer */
	  if (HAL_GetTick() - timerMag >= SAMPLE_TIME_MAG_MS) {
		  IISMagnetomer_Read(&mag);

		  timerMag += SAMPLE_TIME_MAG_MS;
	  }

	  /* Temperature sensor */
	  if (HAL_GetTick() - timerTmp >= SAMPLE_TIME_TMP_MS) {
		  TMP100_Read(&tmp);

		  timerTmp += SAMPLE_TIME_TMP_MS;
	  }

	  /* Debug USB output */
	  if (HAL_GetTick() - timerDbg >= SAMPLE_TIME_DBG_MS) {
		  char buf[256];
		  sprintf(buf, "[%ld] Bar: %f | Mag: %f %f %f | Acc: %f %f %f | Gyr: %f %f %f | Tmp: %f | Roll: %f | Pitch: %f\r\n",
				  HAL_GetTick(),
				  bar.pressurePa,
				  mag.x, mag.y, mag.z,
				  imu.acc[0], imu.acc[1], imu.acc[2],
				  imu.gyr[0], imu.gyr[1], imu.gyr[2],
				  tmp.temp_C,
				  kalrp.phi * 57.2957795131f,
				  kalrp.theta * 57.2957795131f);

		  printDebug(buf);

		  timerDbg += SAMPLE_TIME_DBG_MS;
	  }

	  /* Heartbeat LED */
	  if (HAL_GetTick() - timerLED >= SAMPLE_TIME_LED_MS) {
		  HAL_GPIO_TogglePin(GPIOB, LEDA_Pin);

		  timerLED += SAMPLE_TIME_LED_MS;
	  }

  }

}


void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};


  __HAL_RCC_PWR_CLK_ENABLE();
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLM = 25;
  RCC_OscInitStruct.PLL.PLLN = 336;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV2;
  RCC_OscInitStruct.PLL.PLLQ = 7;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }


  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV4;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV2;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_5) != HAL_OK)
  {
    Error_Handler();
  }
}


static void MX_I2C1_Init(void)
{

  hi2c1.Instance = I2C1;
  hi2c1.Init.ClockSpeed = 100000;
  hi2c1.Init.DutyCycle = I2C_DUTYCYCLE_2;
  hi2c1.Init.OwnAddress1 = 0;
  hi2c1.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
  hi2c1.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
  hi2c1.Init.OwnAddress2 = 0;
  hi2c1.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
  hi2c1.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;
  if (HAL_I2C_Init(&hi2c1) != HAL_OK)
  {
    Error_Handler();
  }

}

static void MX_I2C2_Init(void)
{

  hi2c2.Instance = I2C2;
  hi2c2.Init.ClockSpeed = 100000;
  hi2c2.Init.DutyCycle = I2C_DUTYCYCLE_2;
  hi2c2.Init.OwnAddress1 = 0;
  hi2c2.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
  hi2c2.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
  hi2c2.Init.OwnAddress2 = 0;
  hi2c2.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
  hi2c2.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;
  if (HAL_I2C_Init(&hi2c2) != HAL_OK)
  {
    Error_Handler();
  }


}


static void MX_I2C3_Init(void)
{

  hi2c3.Instance = I2C3;
  hi2c3.Init.ClockSpeed = 100000;
  hi2c3.Init.DutyCycle = I2C_DUTYCYCLE_2;
  hi2c3.Init.OwnAddress1 = 0;
  hi2c3.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
  hi2c3.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
  hi2c3.Init.OwnAddress2 = 0;
  hi2c3.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
  hi2c3.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;
  if (HAL_I2C_Init(&hi2c3) != HAL_OK)
  {
    Error_Handler();
  }


}

/* GPS UART */
static void MX_USART1_UART_Init(void)
{

  huart1.Instance = USART1;
  huart1.Init.BaudRate = 9600;
  huart1.Init.WordLength = UART_WORDLENGTH_8B;
  huart1.Init.StopBits = UART_STOPBITS_1;
  huart1.Init.Parity = UART_PARITY_NONE;
  huart1.Init.Mode = UART_MODE_TX_RX;
  huart1.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart1.Init.OverSampling = UART_OVERSAMPLING_16;
  if (HAL_UART_Init(&huart1) != HAL_OK)
  {
    Error_Handler();
  }

}

/* FCC <> NAVC UART */
static void MX_USART2_UART_Init(void)
{

  huart2.Instance = USART2;
  huart2.Init.BaudRate = 115200;
  huart2.Init.WordLength = UART_WORDLENGTH_8B;
  huart2.Init.StopBits = UART_STOPBITS_1;
  huart2.Init.Parity = UART_PARITY_NONE;
  huart2.Init.Mode = UART_MODE_TX_RX;
  huart2.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart2.Init.OverSampling = UART_OVERSAMPLING_16;
  if (HAL_UART_Init(&huart2) != HAL_OK)
  {
    Error_Handler();
  }

}

/* DEBUG UART */
static void MX_USART3_UART_Init(void)
{

  huart3.Instance = USART3;
  huart3.Init.BaudRate = 115200;
  huart3.Init.WordLength = UART_WORDLENGTH_8B;
  huart3.Init.StopBits = UART_STOPBITS_1;
  huart3.Init.Parity = UART_PARITY_NONE;
  huart3.Init.Mode = UART_MODE_TX_RX;
  huart3.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart3.Init.OverSampling = UART_OVERSAMPLING_16;
  if (HAL_UART_Init(&huart3) != HAL_OK)
  {
    Error_Handler();
  }

}

static void MX_USB_OTG_FS_USB_Init(void)
{

}

static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};

  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOH_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOC, GPSLNAEN_Pin|GPSNRST_Pin|FCCTXA_Pin|FCCTXB_Pin 
                          |FCCTXC_Pin|FCCTXD_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOB, BARNRST_Pin|LEDA_Pin|LEDB_Pin|LEDC_Pin 
                          |LEDD_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin : GPSLNAEN_Pin */
  GPIO_InitStruct.Pin = GPSLNAEN_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_PULLDOWN;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPSLNAEN_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pin : GPSNRST_Pin */
  GPIO_InitStruct.Pin = GPSNRST_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_PULLUP;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPSNRST_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pins : FCCRXA_Pin FCCRXB_Pin FCCRXC_Pin FCCRXD_Pin 
                           GPSPPS_Pin */
  GPIO_InitStruct.Pin = FCCRXA_Pin|FCCRXB_Pin|FCCRXC_Pin|FCCRXD_Pin 
                          |GPSPPS_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

  /*Configure GPIO pins : INTACC_Pin INTGYR_Pin INTMAG_Pin INTBAR_Pin */
  GPIO_InitStruct.Pin = INTACC_Pin|INTGYR_Pin|INTMAG_Pin|INTBAR_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /*Configure GPIO pins : FCCTXA_Pin FCCTXB_Pin FCCTXC_Pin FCCTXD_Pin */
  GPIO_InitStruct.Pin = FCCTXA_Pin|FCCTXB_Pin|FCCTXC_Pin|FCCTXD_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

  /*Configure GPIO pin : BARNRST_Pin */
  GPIO_InitStruct.Pin = BARNRST_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_PULLUP;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(BARNRST_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pins : LEDA_Pin LEDB_Pin LEDC_Pin LEDD_Pin */
  GPIO_InitStruct.Pin = LEDA_Pin|LEDB_Pin|LEDC_Pin|LEDD_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  /*Configure GPIO pin : PA9 */
  GPIO_InitStruct.Pin = GPIO_PIN_9;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /*Configure GPIO pins : PA10 PA11 PA12 */
  GPIO_InitStruct.Pin = GPIO_PIN_10|GPIO_PIN_11|GPIO_PIN_12;
  GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
  GPIO_InitStruct.Alternate = GPIO_AF10_OTG_FS;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

}


void Error_Handler(void)
{

}

#ifdef  USE_FULL_ASSERT
void assert_failed(uint8_t *file, uint32_t line)
{ 

}
#endif
