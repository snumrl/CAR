#ifndef __DEEP_PHYSICS_CHARACTER_CONFIGURATIONS_H__
#define __DEEP_PHYSICS_CHARACTER_CONFIGURATIONS_H__

#define NEW_JOINTS
#define FOOT_OFFSET (0.0)
#define ROOT_HEIGHT_OFFSET (0.00)
#define TERMINAL_ROOT_DIFF_THRESHOLD (1.0)
#define TERMINAL_ROOT_DIFF_ANGLE_THRESHOLD (0.5*M_PI)
#define TERMINAL_ROOT_HEIGHT_LOWER_LIMIT (0.0)
#define TERMINAL_ROOT_HEIGHT_UPPER_LIMIT (2.0)

#ifdef CMU_JOINTS
#define INPUT_MOTION_SIZE 78
#define FOOT_CONTACT_OFFSET 70
#endif

#ifdef NEW_JOINTS
#define INPUT_MOTION_SIZE 54
#define FOOT_CONTACT_OFFSET 52
#endif

#define KV_RATIO (0.1)
#define JOINT_DAMPING (0.05)
#define FOOT_CONTACT_THERESHOLD (0.3)

#define CHARACTER_TYPE "humanoid"
#endif
