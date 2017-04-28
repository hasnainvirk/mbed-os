/* mbed Microcontroller Library
 * Copyright (c) 2017 ARM Limited
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
#include "stdbool.h"
#include "modem_api.h"
#include "gpio_api.h"
#include "platform/mbed_wait_api.h"
#include "PinNames.h"

#ifdef DEVICE_MODEM
static void press_power_button(int time_ms)
{
    gpio_t gpio;

    gpio_init_out_ex(&gpio, MDMPWRON, 1);
    gpio_write(&gpio, 0);
    wait_ms(time_ms);
    gpio_write(&gpio, 1);
}

void modem_init()
{
   //does nothing at the moment, TODO: MultiTech to add hardware initialization stuff if needed
}

void modem_deinit()
{
    //does nothing at the moment, TODO: MultiTech to add hardware de-initialization stuff if needed
}
void modem_power_up()
{
    /* keep the power line low for 150 milisecond */
    press_power_button(300);
    /* give modem a little time to respond */
    wait_ms(100);
}

void modem_power_down()
{
    gpio_t gpio;

    gpio_init_out_ex(&gpio, MDMPWRON, 0);
    wait_ms(400);
}
#endif //DEVICE_MODEM
