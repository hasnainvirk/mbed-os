/* mbed Microcontroller Library
 * Copyright (c) 2006-2013 ARM Limited
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#include "ublox_low_level_api.h"

#include <stdbool.h>
#include "gpio_api.h"

static gpio_t mdmEn, mdmLvlOe, mdmILvlOe, mdmUsbDet;
static gpio_t gpsEn,mdmPwrOn;

static bool modemOn;
static bool gpsOn;

void ublox_mdm_init(void) {
    gpio_t gpio;
    // start with modem disabled 
    gpio_init_out_ex(&gpio, MDMEN,     0);
    gpio_init_out_ex(&gpio, MDMRST,    1);
    gpio_init_out_ex(&gpio, MDMPWRON,  1);
  
  //  gpio_init_out_ex(&mdmLvlOe,  MDMLVLOE,  1); // LVLEN:  1=disabled
  //  gpio_init_out_ex(&mdmILvlOe, MDMILVLOE, 0); // ILVLEN: 0=disabled
  //  gpio_init_out_ex(&mdmUsbDet, MDMUSBDET, 0);
    gpio_init_out_ex(&gpio, MDMRTS,    0);
    // start with gps disabled 
    gpio_init_out_ex(&gpio, GPSEN,     0);
    gpio_init_out_ex(&gpio, GPSRST,    1);
    // led should be off
    gpio_init_out_ex(&gpio, LED,       0);
    
   // wait_ms(50); // when USB cable is inserted the interface chip issues
	 HAL_Delay(50); // replacing wait_ms() with equivalent hal function
}

void ublox_mdm_powerOn(int usb) {
	gpio_t mdmPwrOn, mdmRst;
    // turn on the mode by enabling power with power on pin low and correct USB detect level
   // gpio_write(&mdmUsbDet, usb ? 1 : 0);  // USBDET: 0=disabled, 1=enabled
     // enable modem
	
	
	//gpio_init_out_ex(&mdmPwrOn,  MDMPWRON,  0);
      //  wait_ms(1);                   // wait until supply switched off
	  //HAL_Delay(0.05);
	//gpio_init_out_ex(&mdmPwrOn,  MDMPWRON,  1);
	
	//// Using Rising Edge of RESET_N pin
     gpio_init_out_ex(&mdmRst,  MDMRST,  0);     
	HAL_Delay(50);
	 gpio_init_out_ex(&mdmRst,  MDMRST,  1);
	HAL_Delay(100);
	
	
	
       /* now we can safely enable the level shifters
        gpio_write(&mdmLvlOe, 0);      // LVLEN:  0=enabled (uart/gpio)
        if (gpio_read(&gpsEn)) 
			gpio_write(&mdmILvlOe, 1); // ILVLEN: 1=enabled (i2c)
		*/
}

void ublox_mdm_powerOff(void) {
    if (gpio_read(&mdmPwrOn)) {
        /* diable all level shifters
        gpio_write(&mdmILvlOe, 0);  // ILVLEN: 0=disabled (i2c)
        gpio_write(&mdmLvlOe, 1);   // LVLEN:  1=disabled (uart/gpio)
        gpio_write(&mdmUsbDet, 0);  // USBDET: 0=disabled
        */ 
		//now we can savely switch off the ldo
        gpio_write(&mdmPwrOn, 0);
		HAL_Delay(1000);							
		gpio_write(&mdmPwrOn, 1); 
	}
}        

void ublox_gps_powerOn(void) {
    if (!gpio_read(&gpsEn)) {
        // switch on power supply
        gpio_write(&gpsEn, 1);          // LDOEN: 1=on
       // wait_ms(1);                     // wait until supply switched off
        HAL_Delay(1);
	/*	if (gpio_read(&mdmEn)) 
            gpio_write(&mdmILvlOe, 1);  // ILVLEN: 1=enabled (i2c)
		*/
    }
}

void ublox_gps_powerOff(void) {
    if (gpio_read(&gpsEn)) {
        gpio_write(&mdmILvlOe, 0);  // ILVLEN: 0=disabled (i2c)
        gpio_write(&gpsEn, 0);      // LDOEN: 0=off
    }
}
