/*###########################################################
 # Copyright (c) 2023. BNU-HKBU UIC RoboMaster              #
 #                                                          #
 # This program is free software: you can redistribute it   #
 # and/or modify it under the terms of the GNU General      #
 # Public License as published by the Free Software         #
 # Foundation, either version 3 of the License, or (at      #
 # your option) any later version.                          #
 #                                                          #
 # This program is distributed in the hope that it will be  #
 # useful, but WITHOUT ANY WARRANTY; without even           #
 # the implied warranty of MERCHANTABILITY or FITNESS       #
 # FOR A PARTICULAR PURPOSE.  See the GNU General           #
 # Public License for more details.                         #
 #                                                          #
 # You should have received a copy of the GNU General       #
 # Public License along with this program.  If not, see     #
 # <https://www.gnu.org/licenses/>.                         #
 ###########################################################*/

#include "main.h"

#include "bsp_print.h"
#include "bsp_uart.h"
#include "cmsis_os.h"
#include "protocol.h"

#define RX_SIGNAL (1 << 0)



communication::Referee* referee = nullptr;
bsp::UART* referee_uart = nullptr;

void refereeTask(void* arg) {
    UNUSED(arg);
    uint32_t length;
    uint8_t* data;

    while (true) {
        /* wait until rx data is available */
        uint32_t flags = osThreadFlagsWait(RX_SIGNAL, osFlagsWaitAll, osWaitForever);
        if (flags & RX_SIGNAL) {  // unnecessary check
            /* time the non-blocking rx / tx calls (should be <= 1 osTick) */
            length = referee_uart->Read(&data);
            referee->Receive(communication::package_t{data, (int)length});
        }
    }
}

void RM_RTOS_Init(void) {
    print_use_uart(&huart8);


    referee_uart = new bsp::UART(&huart3);
    referee_uart->SetupRx(300);
    referee_uart->SetupTx(300);

    referee = new communication::Referee(referee_uart);
}

void RM_RTOS_Threads_Init(void) {
}

void RM_RTOS_Default_Task(const void* argument) {
    UNUSED(argument);

    while (true) {
        set_cursor(0, 0);
        clear_screen();
        print("Chassis Volt: %.3f\r\n", referee->power_heat_data.chassis_volt / 1000.0);
        print("Chassis Curr: %.3f\r\n", referee->power_heat_data.chassis_current / 1000.0);
        print("Chassis Power: %.3f\r\n", referee->power_heat_data.chassis_power);
        print("Chassis Power Limit: %d\r\n", referee->game_robot_status.chassis_power_limit);
        print("\r\n");
        print("Shooter Cooling Heat: %hu\r\n",
              referee->power_heat_data.shooter_id1_17mm_cooling_heat);
        print("Bullet Frequency: %hhu\r\n", referee->shoot_data.bullet_freq);
        print("Bullet Speed: %.3f\r\n", referee->shoot_data.bullet_speed);
        print("\r\n");
        print("Current HP %d/%d\n", referee->game_robot_status.remain_HP,
              referee->game_robot_status.max_HP);
        print("Remain bullet %d\n", referee->bullet_remaining.bullet_remaining_num_17mm);
        osDelay(100);
    }
}