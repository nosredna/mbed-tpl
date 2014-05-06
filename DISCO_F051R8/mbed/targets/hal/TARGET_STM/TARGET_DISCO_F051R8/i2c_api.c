/* mbed Microcontroller Library
 *******************************************************************************
 * Copyright (c) 2014, STMicroelectronics
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 * 3. Neither the name of STMicroelectronics nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *******************************************************************************
 */
#include "i2c_api.h"

#if DEVICE_I2C

#include "cmsis.h"
#include "pinmap.h"
#include "error.h"

/* Timeout values for flags and events waiting loops. These timeouts are
   not based on accurate values, they just guarantee that the application will 
   not remain stuck if the I2C communication is corrupted. */   
#define FLAG_TIMEOUT ((int)0x1000)
#define LONG_TIMEOUT ((int)0x8000)

static const PinMap PinMap_I2C_SDA[] = {
    {PB_9,  I2C_1, STM_PIN_DATA(GPIO_Mode_AF, GPIO_OType_OD, GPIO_PuPd_UP, GPIO_AF_1)},
    {NC,    NC,    0}
};

static const PinMap PinMap_I2C_SCL[] = {
    {PB_8,  I2C_1, STM_PIN_DATA(GPIO_Mode_AF, GPIO_OType_OD, GPIO_PuPd_UP, GPIO_AF_1)},
    {NC,    NC,    0}
};

void i2c_init(i2c_t *obj, PinName sda, PinName scl) {  
    // Determine the I2C to use
    I2CName i2c_sda = (I2CName)pinmap_peripheral(sda, PinMap_I2C_SDA);
    I2CName i2c_scl = (I2CName)pinmap_peripheral(scl, PinMap_I2C_SCL);

    obj->i2c = (I2CName)pinmap_merge(i2c_sda, i2c_scl);
    
    if (obj->i2c == (I2CName)NC) {
        error("I2C pin mapping failed");
    }

    // Enable I2C clock
    if (obj->i2c == I2C_1) {    
        RCC_APB1PeriphClockCmd(RCC_APB1Periph_I2C1, ENABLE);
    }
    //if (obj->i2c == I2C_2) {
    //    RCC_APB1PeriphClockCmd(RCC_APB1Periph_I2C2, ENABLE);
    //}

    // Configure I2C pins
    pinmap_pinout(scl, PinMap_I2C_SCL);
    pin_mode(scl, OpenDrain);
    pinmap_pinout(sda, PinMap_I2C_SDA);
    pin_mode(sda, OpenDrain);
    
    // Reset to clear pending flags if any
    i2c_reset(obj);
    
    // I2C configuration
    i2c_frequency(obj, 100000); // 100 kHz per default    
}

void i2c_frequency(i2c_t *obj, int hz) {
    I2C_TypeDef *i2c = (I2C_TypeDef *)(obj->i2c);
    I2C_InitTypeDef I2C_InitStructure;
    uint32_t tim = 0;
  
    // Values calculated with I2C_Timing_Configuration_V1.0.1.xls file (see AN4235)
    // with Rise time = 100ns and Fall time = 10ns
    switch (hz) {
      case 100000:
          tim = 0x00201D2B; // Standard mode
          break;
      case 200000:
          tim = 0x0010021E; // Fast mode
          break;
      case 400000:
          tim = 0x0010020A; // Fast mode
          break;
      default:
          error("Only 100kHz, 200kHz and 400kHz I2C frequencies are supported.");
          break;
    }
    
    // I2C configuration
    I2C_InitStructure.I2C_Mode                = I2C_Mode_I2C;
    I2C_InitStructure.I2C_AnalogFilter        = I2C_AnalogFilter_Enable;
    I2C_InitStructure.I2C_DigitalFilter       = 0x00;
    I2C_InitStructure.I2C_OwnAddress1         = 0x00;
    I2C_InitStructure.I2C_Ack                 = I2C_Ack_Enable;
    I2C_InitStructure.I2C_AcknowledgedAddress = I2C_AcknowledgedAddress_7bit;
    I2C_InitStructure.I2C_Timing              = tim;
    I2C_Init(i2c, &I2C_InitStructure);
    
    I2C_Cmd(i2c, ENABLE);
}

inline int i2c_start(i2c_t *obj) {
    I2C_TypeDef *i2c = (I2C_TypeDef *)(obj->i2c);
    int timeout;

    // Test BUSY Flag
    timeout = LONG_TIMEOUT;
    while (I2C_GetFlagStatus(i2c, I2C_ISR_BUSY) != RESET) {
        timeout--;
        if (timeout == 0) {
            return 0;
        }
    }

    I2C_GenerateSTART(i2c, ENABLE);

    return 0;
}

inline int i2c_stop(i2c_t *obj) {
    I2C_TypeDef *i2c = (I2C_TypeDef *)(obj->i2c);
  
    I2C_GenerateSTOP(i2c, ENABLE);
  
    return 0;
}

int i2c_read(i2c_t *obj, int address, char *data, int length, int stop) {
    I2C_TypeDef *i2c = (I2C_TypeDef *)(obj->i2c);
    int count;
    int value;
  
    if (length == 0) return 0;

    // Configure slave address, nbytes, reload, end mode and start or stop generation
    I2C_TransferHandling(i2c, address, length, I2C_AutoEnd_Mode, I2C_Generate_Start_Read);
    
    // Read all bytes
    for (count = 0; count < length; count++) {
        value = i2c_byte_read(obj, 0);
        data[count] = (char)value;
    }
    
    return length;
}

int i2c_write(i2c_t *obj, int address, const char *data, int length, int stop) {
    I2C_TypeDef *i2c = (I2C_TypeDef *)(obj->i2c);
    //int timeout;
    int count;
    
    if (length == 0) return 0;

    // TODO: the stop is always sent even with I2C_SoftEnd_Mode. To be corrected.

    // Configure slave address, nbytes, reload, end mode and start or stop generation
    //if (stop) {
        I2C_TransferHandling(i2c, address, length, I2C_AutoEnd_Mode, I2C_Generate_Start_Write);
    //}
    //else {
    //    I2C_TransferHandling(i2c, address, length, I2C_SoftEnd_Mode, I2C_Generate_Start_Write);
    //}
    
    // Write all bytes
    for (count = 0; count < length; count++) {
        if (i2c_byte_write(obj, data[count]) != 1) {
            i2c_stop(obj);
            return 0;
        }
    }

    /*
    if (stop) {
        // Wait until STOPF flag is set
        timeout = LONG_TIMEOUT;
        while (I2C_GetFlagStatus(i2c, I2C_ISR_STOPF) == RESET) {
            timeout--;
            if (timeout == 0) {
                return 0;
            }
        }
        // Clear STOPF flag
        I2C_ClearFlag(i2c, I2C_ICR_STOPCF);
    }
    */
    
    return count;
}

int i2c_byte_read(i2c_t *obj, int last) {
    I2C_TypeDef *i2c = (I2C_TypeDef *)(obj->i2c);
    uint8_t data;
    int timeout;
  
    // Wait until the byte is received
    timeout = FLAG_TIMEOUT;  
    while (I2C_GetFlagStatus(i2c, I2C_ISR_RXNE) == RESET) {
        timeout--;
        if (timeout == 0) {
            return 0;
        }
    }

    data = I2C_ReceiveData(i2c);
    
    return (int)data;
}

int i2c_byte_write(i2c_t *obj, int data) {
    I2C_TypeDef *i2c = (I2C_TypeDef *)(obj->i2c);
    int timeout;

    // Wait until the previous byte is transmitted
    timeout = FLAG_TIMEOUT;
    while (I2C_GetFlagStatus(i2c, I2C_ISR_TXIS) == RESET) {
        timeout--;
        if (timeout == 0) {
            return 0;
        }
    }
    
    I2C_SendData(i2c, (uint8_t)data);
    
    return 1;
}

void i2c_reset(i2c_t *obj) {
    if (obj->i2c == I2C_1) {    
        RCC_APB1PeriphResetCmd(RCC_APB1Periph_I2C1, ENABLE);
        RCC_APB1PeriphResetCmd(RCC_APB1Periph_I2C1, DISABLE);
    }
    //if (obj->i2c == I2C_2) {
    //    RCC_APB1PeriphResetCmd(RCC_APB1Periph_I2C2, ENABLE);
    //    RCC_APB1PeriphResetCmd(RCC_APB1Periph_I2C2, DISABLE);      
    //}
}

#if DEVICE_I2CSLAVE

void i2c_slave_address(i2c_t *obj, int idx, uint32_t address, uint32_t mask) {
    I2C_TypeDef *i2c = (I2C_TypeDef *)(obj->i2c);
    uint16_t tmpreg;
  
    // Get the old register value
    tmpreg = i2c->OAR1;
    // Reset address bits
    tmpreg &= 0xFC00;
    // Set new address
    tmpreg |= (uint16_t)((uint16_t)address & (uint16_t)0x00FE); // 7-bits
    // Store the new register value
    i2c->OAR1 = tmpreg;
}

void i2c_slave_mode(i2c_t *obj, int enable_slave) {
    // Nothing to do
}

// See I2CSlave.h
#define NoData         0 // the slave has not been addressed
#define ReadAddressed  1 // the master has requested a read from this slave (slave = transmitter)
#define WriteGeneral   2 // the master is writing to all slave
#define WriteAddressed 3 // the master is writing to this slave (slave = receiver)

int i2c_slave_receive(i2c_t *obj) {
    // TO BE DONE
    return(0);
}

int i2c_slave_read(i2c_t *obj, char *data, int length) {
    int count = 0;
 
    // Read all bytes
    for (count = 0; count < length; count++) {
        data[count] = i2c_byte_read(obj, 0);
    }
    
    return count;
}

int i2c_slave_write(i2c_t *obj, const char *data, int length) {
    int count = 0;
 
    // Write all bytes
    for (count = 0; count < length; count++) {
        i2c_byte_write(obj, data[count]);
    }
    
    return count;
}


#endif // DEVICE_I2CSLAVE

#endif // DEVICE_I2C
