#include "persistent_storage.h"
#include <string.h> 
#include "buzzer.h"

/**
  * @brief  将PID参数写入Flash
  * @param  params: 指向要写入的PID参数结构体的指针
  * @retval HAL_StatusTypeDef: HAL状态 (HAL_OK, HAL_ERROR, HAL_BUSY, HAL_TIMEOUT)
  */
HAL_StatusTypeDef Write_PID_To_Flash(PID_Params *params)
{
    FLASH_EraseInitTypeDef EraseInitStruct;
    uint32_t SectorError = 0;
    HAL_StatusTypeDef status = HAL_ERROR;
// --- 进入临界区，关闭中断 ---
    __disable_irq();
	
    // 1. 解锁Flash
    HAL_FLASH_Unlock();

    // 2. 擦除扇区
    // 必须先擦除才能写入。Flash写入只能将1变为0，不能将0变为1。
    EraseInitStruct.TypeErase     = FLASH_TYPEERASE_SECTORS;    // 扇区擦除
    EraseInitStruct.VoltageRange  = FLASH_VOLTAGE_RANGE_3;    // 电压范围，对于F4系列通常是3
    EraseInitStruct.Sector        = FLASH_PID_STORE_SECTOR;   // 选择要擦除的扇区
    EraseInitStruct.NbSectors     = 1;                        // 只擦除一个扇区
//		__HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_PGPERR | FLASH_FLAG_PGSERR);
    if (HAL_FLASHEx_Erase(&EraseInitStruct, &SectorError) != HAL_OK)
    {
			if (HAL_FLASHEx_Erase(&EraseInitStruct, &SectorError) != HAL_OK){
        // 擦除失败，锁定Flash并返回错误
				HAL_FLASH_Lock();
			  __enable_irq(); // --- 退出临界区，恢复中断 ---
				buzzers[0].beep(&buzzers[0], 1000, 50, 50, 2);		
        return HAL_ERROR;
				}
    }

    // 3. 写入数据
    // STM32F4可以按字节、半字、字、双字写入。我们按字(32位)写入，正好对应一个float。
		// address 必须是 uint32_t，因为地址是32位的
    uint32_t address = FLASH_PID_STORE_ADDRESS;
    
    // 将结构体指针转换为 uint32_t 指针，因为我们要按32位字来读取数据
    uint32_t *data_ptr = (uint32_t*)params; 
    
    // 计算总共需要写入多少个32位的字
   
    uint16_t words_to_write = sizeof(PID_Params) / sizeof(uint32_t);

    for (int i = 0; i < words_to_write; i++)
    {
        // 写入一个32位的字 (WORD)
        status = HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, address, data_ptr[i]);
        
        if (status == HAL_OK)
        {
            // 地址每次增加4个字节（一个字的大小）
            address += 4; 
        }
        else
        {
            // 如果写入失败，立即跳出循环
            break;
        }
    }

    // 4. 锁定Flash
    HAL_FLASH_Lock();
    // --- 退出临界区，恢复中断 ---
    __enable_irq();
    return status;
}

/**
  * @brief  从Flash中读取PID参数
  * @param  params: 指向用于存放读取数据的PID参数结构体的指针
  * @retval None
  */
void Read_PID_From_Flash(PID_Params *params)
{
    // 使用memcpy可以方便地将Flash中的连续数据块复制到我们的结构体中
    memcpy(params, (void*)FLASH_PID_STORE_ADDRESS, sizeof(PID_Params));


}