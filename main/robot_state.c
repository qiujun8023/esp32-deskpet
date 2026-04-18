#include "robot_state.h"

#include "display/robot_eyes.h"

atomic_int robot_state_auto_mode = EYE_MODE_NORMAL;

void robot_state_set_auto_mode(int mode) {
    atomic_store(&robot_state_auto_mode, mode);
    robot_eyes_set_mode((eye_mode_t)mode);
}
