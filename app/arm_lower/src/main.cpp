#include "mbed.h"
#include "rover_config.h"
#include "CANMsg.h"
#include "PwmIn.h"
#include "PID.h"
#include "Motor.h"
#include "ArmJointController.h"

const ArmJointController::t_jointConfig turnTableConfig = {
        .motor = {
                .pwmPin = MOTOR1,
                .dirPin = MOTOR1_DIR,
                .inverted = true,
                .freqInHz = MOTOR_DEFAULT_FREQUENCY_HZ,
                .limit = 1.0
        },

        .encoder = {
                .pwmPin = ENC_A1,
                .zeroAngleDutyCycle = 0.502f,
                .minAngleDegrees = -100.0f,
                .maxAngleDegrees = 100.0f,
                .inverted = true
        },

        .limSwitchMinPin = LIM_1A,
        .limSwitchMaxPin = LIM_1B,

        .velocityPID = {
                .P    = 1.0f,
                .I    = 0.0f,
                .D    = 0.0f,
                .bias = 0.0f,
                .interval = 0.05f
        },

        .positionPID = {
                .P    = 4.50f,
                .I    = 0.98f,
                .D    = 0.0f,
                .bias = 0.0f,
                .interval = 0.05f
        },

        .minInputVelocityDegPerSec = -20.0f,
        .maxInputVelocityDegPerSec = 20.0f,
        .minOutputMotorDutyCycle = -1.0f,
        .maxOutputMotorDutyCycle = 1.0f
};

const ArmJointController::t_jointConfig shoulderConfig = {
        .motor = {
                .pwmPin = MOTOR2,
                .dirPin = MOTOR2_DIR,
                .inverted = false,
                .freqInHz = MOTOR_DEFAULT_FREQUENCY_HZ,
                .limit = 1.0
        },

        .encoder = {
                .pwmPin = ENC_A2,
                .zeroAngleDutyCycle = 0.752f,
                .minAngleDegrees = -1.0f,
                .maxAngleDegrees = 140.0f,
                .inverted = true
        },

        .limSwitchMinPin = LIM_2B,
        .limSwitchMaxPin = LIM_2A,

        .velocityPID = {
                .P    = 0.65f,
                .I    = 0.20f,
                .D    = 0.0f,
                .bias = 0.0f,
                .interval = 0.05f
        },

        .positionPID = {
                .P    = 6.1f,
                .I    = 0.0f,
                .D    = 0.0f,
                .bias = 0.0f,
                .interval = 0.05f
        },

        .minInputVelocityDegPerSec = -20.0f,
        .maxInputVelocityDegPerSec = 20.0f,
        .minOutputMotorDutyCycle = -1.0f,
        .maxOutputMotorDutyCycle = 1.0f
};

const ArmJointController::t_jointConfig elbowConfig = {
        .motor = {
                .pwmPin = MOTOR3,
                .dirPin = MOTOR3_DIR,
                .inverted = true,
                .freqInHz = MOTOR_DEFAULT_FREQUENCY_HZ,
                .limit = 1.0
        },

        .encoder = {
                .pwmPin = ENC_A3,
                .zeroAngleDutyCycle = 0.755f,
                .minAngleDegrees = -160.0f,
                .maxAngleDegrees = 1.0f,
                .inverted = false
        },

        .limSwitchMinPin = LIM_3B,
        .limSwitchMaxPin = LIM_3A,

        .velocityPID = {
                .P    = 0.7f,
                .I    = 0.05f,
                .D    = 0.0f,
                .bias = 0.0f,
                .interval = 0.05f
        },

        .positionPID = {
                .P    = 16.0f,
                .I    = 0.97f,
                .D    = 0.0f,
                .bias = 0.0f,
                .interval = 0.05f
        },

        .minInputVelocityDegPerSec = -20.0f,
        .maxInputVelocityDegPerSec = 20.0f,
        .minOutputMotorDutyCycle = -1.0f,
        .maxOutputMotorDutyCycle = 1.0f
};

Serial             pc(SERIAL_TX, SERIAL_RX, ROVER_DEFAULT_BAUD_RATE);
CAN                can(CAN_RX, CAN_TX, ROVER_CANBUS_FREQUENCY);
CANMsg             rxMsg;

DigitalOut         ledErr(LED1);
DigitalOut         ledCAN(LED4);

ArmJointController turnTableController(turnTableConfig, ArmJointController::velocityPID);
ArmJointController shoulderController(shoulderConfig, ArmJointController::velocityPID);
ArmJointController elbowController(elbowConfig, ArmJointController::velocityPID);

ArmJointController* p_armJointControllers[3];

Timer              canSendTimer;
Timer              canWatchDog;

enum t_joint {
    turnTable,
    shoulder,
    elbow
};

void printCANMsg(CANMessage& msg) {
    pc.printf("  ID      = 0x%.3x\r\n", msg.id);
    pc.printf("  Type    = %d\r\n", msg.type);
    pc.printf("  Format  = %d\r\n", msg.format);
    pc.printf("  Length  = %d\r\n", msg.len);
    pc.printf("  Data    =");
    for(int i = 0; i < msg.len; i++)
        pc.printf(" 0x%.2X", msg.data[i]);
    pc.printf("\r\n");
}

enum armCommand {
 
    setTurnTableControlMode = ROVER_ARM_LOWER_CANID,
    setTurnTableMotion,
    setShoulderControlMode,
    setShoulderMotion,
    setElbowControlMode,
    setElbowMotion,

    firstCommand = setTurnTableControlMode,
    lastCommand  = setElbowMotion

};

void initCAN() {
    can.filter(ROVER_ARM_LOWER_CANID, ROVER_CANID_FILTER_MASK, CANStandard);

    // for (int canHandle = firstCommand; canHandle <= lastCommand; canHandle++) {
    //     can.filter(RX_ID + canHandle, 0xFFF, CANStandard, canHandle);
    // }
}

ArmJointController::t_jointControlMode handleSetControlMode(t_joint joint, CANMsg *p_newMsg) {
    ArmJointController::t_jointControlMode controlMode;
    *p_newMsg >> controlMode;

    MBED_WARN_ON_ERROR(p_armJointControllers[joint]->setControlMode(controlMode));

    PRINT_INFO("Set joint %d control mode to %d\r\n", joint, controlMode);

    return controlMode;
}

float handleSetMotion(t_joint joint, CANMsg *p_newMsg) {
    float motionData = 0;
    *p_newMsg >> motionData;

    ArmJointController::t_jointControlMode controlMode = p_armJointControllers[joint]->getControlMode();

    switch (controlMode) {
        case ArmJointController::motorDutyCycle:
            p_armJointControllers[joint]->setMotorDutyCycle(motionData);
            break;
        case ArmJointController::velocityPID:
            p_armJointControllers[joint]->setVelocityDegreesPerSec(motionData);
            break;
        case ArmJointController::positionPID:
            p_armJointControllers[joint]->setAngleDegrees(motionData);
            break;
    }

    PRINT_INFO("Set joint %d motion data to %f with control mode %d\r\n", joint, motionData, controlMode);

    return motionData;
}

void processCANMsg(CANMsg *p_newMsg) {

//    PRINT_INFO("Recieved CAN message with ID %X\r\n", p_newMsg->id);

    switch (p_newMsg->id) {
        case setTurnTableControlMode:
            handleSetControlMode(turnTable, p_newMsg);
            break;

        case setTurnTableMotion:
            handleSetMotion(turnTable, p_newMsg);
            break;

        case setShoulderControlMode:
            handleSetControlMode(shoulder, p_newMsg);
            break;

        case setShoulderMotion:
            handleSetMotion(shoulder, p_newMsg);
            break;

        case setElbowControlMode:
            handleSetControlMode(elbow, p_newMsg);
            break;

        case setElbowMotion:
            handleSetMotion(elbow, p_newMsg);
            break;

        default:
            pc.printf("Recieved unimplemented command\r\n");
            break;
    }
}

void sendJointAngles() {

    CANMsg txMsg(0);
    float angle = 0;

    for (unsigned int i = 0; i < 3; i++) {

        angle = p_armJointControllers[i]->getAngleDegrees();

//        char arr[sizeof(angle)];
//        memcpy(arr, &angle, sizeof(angle));

        txMsg.clear();
        txMsg.id = ROVER_JETSON_START_CANID_MSG_ARM_LOWER + i;
        txMsg << angle;

        MBED_ASSERT_WARN(can.write(txMsg));

    }
}
 
int main(void)
{
    p_armJointControllers[turnTable] = &turnTableController;
    p_armJointControllers[shoulder]  = &shoulderController;
    p_armJointControllers[elbow]     = &elbowController;

    PRINT_INFO("Lower arm program Started\r\n\r\n");

    initCAN();

    turnTableController.setControlMode(ArmJointController::motorDutyCycle);
    shoulderController.setControlMode(ArmJointController::motorDutyCycle);
    elbowController.setControlMode(ArmJointController::motorDutyCycle);

    canSendTimer.start();
    canWatchDog.start();

    while (1) {

        if (can.read(rxMsg)) {
            canWatchDog.reset();
            processCANMsg(&rxMsg);
            rxMsg.clear();
            ledCAN = !ledCAN;
        }

        if (canSendTimer.read() > 0.1) {
            sendJointAngles();
            canSendTimer.reset();
        }

        turnTableController.update();
        shoulderController.update();
        elbowController.update();

    }
}
 
