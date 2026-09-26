#ifndef FW2EMU_HARDWARE_VREG_H
#define FW2EMU_HARDWARE_VREG_H
#include "pico.h"
enum vreg_voltage {
    VREG_VOLTAGE_0_55 = 0, VREG_VOLTAGE_0_60, VREG_VOLTAGE_0_65, VREG_VOLTAGE_0_70,
    VREG_VOLTAGE_0_75, VREG_VOLTAGE_0_80, VREG_VOLTAGE_0_85, VREG_VOLTAGE_0_90,
    VREG_VOLTAGE_0_95, VREG_VOLTAGE_1_00, VREG_VOLTAGE_1_05, VREG_VOLTAGE_1_10,
    VREG_VOLTAGE_1_15, VREG_VOLTAGE_1_20, VREG_VOLTAGE_1_25, VREG_VOLTAGE_1_30,
    VREG_VOLTAGE_DEFAULT = VREG_VOLTAGE_1_10,
};
static inline void vreg_set_voltage(enum vreg_voltage v) { (void)v; }
static inline void vreg_disable_voltage_limit(void) {}
#endif
