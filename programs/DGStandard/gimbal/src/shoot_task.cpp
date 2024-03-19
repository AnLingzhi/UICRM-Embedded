/*###########################################################
 # Copyright (c) 2023-2024. BNU-HKBU UIC RoboMaster         #
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

#include "shoot_task.h"

static driver::MotorPWMBase* flywheel_left = nullptr;
static driver::MotorPWMBase* flywheel_right = nullptr;

driver::MotorCANBase* steering_motor = nullptr;

bsp::GPIO* shoot_key = nullptr;

// 堵转回调函数
void jam_callback(driver::ServoMotor* servo, const driver::servo_jam_t data) {
    UNUSED(data);
    // 反向旋转
    float servo_target = servo->GetTarget();
    if (servo_target > servo->GetTheta()) {
        float prev_target = servo->GetTarget() - 2 * PI / 8;
        servo->SetTarget(prev_target, true);
    }
}

osThreadId_t shootTaskHandle;

void shootTask(void* arg) {
    UNUSED(arg);
    // 启动等待
    osDelay(1000);
    while (remote_mode == REMOTE_MODE_KILL) {
        osDelay(SHOOT_OS_DELAY);
    }
    // 等待IMU初始化
    while (ahrs->IsCailbrated()) {
        osDelay(SHOOT_OS_DELAY);
    }

    steering_motor->SetTarget(steering_motor->GetOutputShaftTheta());

    Ease* flywheel_speed_ease = new Ease(0, 0.3);

    while (true) {
        if (remote_mode == REMOTE_MODE_KILL) {
            // 死了
            kill_shoot();
            osDelay(SHOOT_OS_DELAY);
            continue;
        }

        if (shoot_flywheel_mode == SHOOT_FRIC_MODE_PREPARING ||
            shoot_flywheel_mode == SHOOT_FRIC_MODE_PREPARED) {
            flywheel_speed_ease->SetTarget(400);
        } else {
            flywheel_speed_ease->SetTarget(0);
        }
        flywheel_speed_ease->Calc();
        flywheel_left->SetOutput(flywheel_speed_ease->GetOutput());
        flywheel_right->SetOutput(flywheel_speed_ease->GetOutput());

        // 检测摩擦轮是否就绪
        // 由shoot_task切换到SHOOT_FRIC_MODE_PREPARED
        if (shoot_flywheel_mode == SHOOT_FRIC_MODE_PREPARING && flywheel_speed_ease->IsAtTarget()) {
            shoot_flywheel_mode = SHOOT_FRIC_MODE_PREPARED;
        }

        if (shoot_flywheel_mode != SHOOT_FRIC_MODE_PREPARED) {
            steering_motor->SetTarget(steering_motor->GetOutputShaftTheta());
            osDelay(SHOOT_OS_DELAY);
            continue;
        }
        /* 摩擦轮就绪后执行以下部分 */

        if (shoot_load_mode == SHOOT_MODE_STOP) {
            steering_motor->SetTarget(steering_motor->GetOutputShaftTheta());
            // todo: 这里为什么要是true才能停下？target_angel为什么会超过范围？
        }
        if (shoot_load_mode == SHOOT_MODE_IDLE) {
            uint8_t loaded = shoot_key->Read();
            if (loaded) {
                steering_motor->SetTarget(steering_motor->GetOutputShaftTheta());
            } else {
                // 没有准备就绪，则旋转拔弹电机
                steering_motor->SetTarget(steering_motor->GetTarget() + 2 * PI / 8, false);
            }
        }
        if (shoot_load_mode == SHOOT_MODE_SINGLE) {
            steering_motor->SetTarget(steering_motor->GetOutputShaftTheta() + 2 * PI / 8, true);
            shoot_load_mode = SHOOT_MODE_IDLE;
        }
        if (shoot_load_mode == SHOOT_MODE_BURST) {
            steering_motor->SetTarget(steering_motor->GetTarget() + 2 * PI / 8, true);
        }

        osDelay(SHOOT_OS_DELAY);
    }
}

void init_shoot() {
    flywheel_left = new driver::MotorPWMBase(&htim1, 1, 1000000, 500, 1000);
    flywheel_right = new driver::MotorPWMBase(&htim1, 4, 1000000, 500, 1000);
    flywheel_left->SetOutput(0);
    flywheel_right->SetOutput(0);

    steering_motor = new driver::Motor2006(can1, 0x207);

    steering_motor->SetTransmissionRatio(36);
    control::ConstrainedPID::PID_Init_t steering_motor_theta_pid_init = {
        .kp = 20,
        .ki = 0,
        .kd = 0,
        .max_out = 2.5 * PI,
        .max_iout = 0,
        .deadband = 0,                                 // 死区
        .A = 0,                                        // 变速积分所能达到的最大值为A+B
        .B = 0,                                        // 启动变速积分的死区
        .output_filtering_coefficient = 0.1,           // 输出滤波系数
        .derivative_filtering_coefficient = 0,         // 微分滤波系数
        .mode = control::ConstrainedPID::OutputFilter  // 输出滤波
    };
    steering_motor->ReInitPID(steering_motor_theta_pid_init, driver::MotorCANBase::THETA);
    control::ConstrainedPID::PID_Init_t steering_motor_omega_pid_init = {
        .kp = 1000,
        .ki = 1,
        .kd = 0,
        .max_out = 10000,
        .max_iout = 4000,
        .deadband = 0,                          // 死区
        .A = 3 * PI,                            // 变速积分所能达到的最大值为A+B
        .B = 2 * PI,                            // 启动变速积分的死区
        .output_filtering_coefficient = 0.1,    // 输出滤波系数
        .derivative_filtering_coefficient = 0,  // 微分滤波系数
        .mode = control::ConstrainedPID::Integral_Limit |       // 积分限幅
                control::ConstrainedPID::OutputFilter |         // 输出滤波
                control::ConstrainedPID::Trapezoid_Intergral |  // 梯形积分
                control::ConstrainedPID::ChangingIntegralRate,  // 变速积分
    };
    steering_motor->ReInitPID(steering_motor_omega_pid_init, driver::MotorCANBase::OMEGA);
    steering_motor->SetMode(driver::MotorCANBase::THETA | driver::MotorCANBase::OMEGA);

    shoot_key = new bsp::GPIO(TRIG_KEY_GPIO_Port, TRIG_KEY_Pin);
    // laser = new bsp::Laser(&htim3, 3, 1000000);
}
void kill_shoot() {
    steering_motor->SetOutput(0);
    flywheel_left->SetOutput(0);
    flywheel_right->SetOutput(0);
}