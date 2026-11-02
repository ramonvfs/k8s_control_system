/*
 * Armature-controlled DC motor with constant load torque.
 *
 *   J dw/dt = K i - b w - tc
 *   L di/dt = V - K w - R i
 *
 * States x[0] = w (rad/s), x[1] = i (A); input V = armature voltage (V).
 */
#include "plant.h"

#define MOTOR_R  0.2    /* Armature resistance (Ohm) */
#define MOTOR_L  0.1    /* Armature inductance (H) */
#define MOTOR_J  0.05   /* Rotor moment of inertia (kg.m^2) */
#define MOTOR_B  0.001  /* Viscous friction coefficient (N.m.s) */
#define MOTOR_K  0.5    /* Torque / back-EMF constant (N.m/A = V.s/rad) */
#define MOTOR_TC 0.2    /* Load torque (N.m) */

static void motor_dynamics(double t, double *x, double u, double *dxdt) {
    (void)t;
    /* dxdt[0] -> angular acceleration */
    dxdt[0] = (MOTOR_K * x[1] - MOTOR_B * x[0] - MOTOR_TC) / MOTOR_J;
    /* dxdt[1] -> derivative of the armature current */
    dxdt[1] = (u - MOTOR_K * x[0] - MOTOR_R * x[1]) / MOTOR_L;
}

static double motor_setpoint(int exp_type, double t) {
    if (exp_type == 1) return 52.36;  /* 500 RPM */
    if (t <= 10.0)     return 52.36;  /* 500 RPM */
    if (t <= 20.0)     return 31.42;  /* 300 RPM */
    if (t <= 30.0)     return 62.83;  /* 600 RPM */
    return 41.89;                     /* 400 RPM */
}

static const plant_model_t dc_motor = {
    .name = "dc_motor",
    .state_dim = 2,
    .output_index = 0,
    .dt = 0.01,
    .dynamics = motor_dynamics,
    .setpoint = motor_setpoint,
    .ki = 1.0 / 2000.0,
    .kp = 0.5,
    .u_min = -36.0,
    .u_max = 36.0,
    .output_desc = "Angular velocity (rad/s)",
    .input_desc = "Armature voltage (V)",
};

int main(int argc, char *argv[]) {
    return plant_run(&dc_motor, argc, argv);
}
