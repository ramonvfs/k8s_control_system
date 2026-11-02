/*
 * Single water tank with pump inflow and gravity (Torricelli) outflow.
 *
 *   A dh/dt = qm u - Vo a sqrt(2 g h)
 *
 * State x[0] = h (m); input u in [0, 1] = pump command (inflow = qm u).
 * The level is clamped at H_max: above it, net inflow is discarded.
 */
#include <math.h>

#include "plant.h"

#define TANK_A     1.0   /* Tank cross-sectional area (m^2) */
#define TANK_a     0.1   /* Outlet pipe cross-sectional area (m^2) */
#define TANK_QM    2.0   /* Maximum pump flow rate (m^3/s) */
#define TANK_G     9.8   /* Gravity (m/s^2) */
#define TANK_VO    0.5   /* Outlet valve opening (0.0 to 1.0) */
#define TANK_H_MAX 30.0  /* Maximum water level (m) */

static void tank_dynamics(double t, double *x, double u, double *dxdt) {
    (void)t;
    /* Avoid sqrt of a negative level caused by numerical error */
    double level = (x[0] > 0) ? x[0] : 0;
    double inflow = TANK_QM * u;
    double outflow = TANK_VO * TANK_a * sqrt(2 * TANK_G * level);

    if (level >= TANK_H_MAX && inflow > outflow) {
        dxdt[0] = 0.0;
        return;
    }
    /* dxdt[0] -> derivative of the level */
    dxdt[0] = (inflow - outflow) / TANK_A;
}

static double tank_setpoint(int exp_type, double t) {
    if (exp_type == 1) return 20.0;
    if (t <= 50.0)     return 20.0;
    if (t <= 100.0)    return 10.0;
    if (t <= 150.0)    return 25.0;
    return 15.0;
}

static const plant_model_t tank = {
    .name = "tank",
    .state_dim = 1,
    .output_index = 0,
    .dt = 0.01,
    .dynamics = tank_dynamics,
    .setpoint = tank_setpoint,
    .ki = 1.0 / 30.0,
    .kp = 100.0,
    .u_min = 0.0,
    .u_max = 1.0,
    .output_desc = "Water level (m)",
    .input_desc = "Pump command (0-1)",
};

int main(int argc, char *argv[]) {
    return plant_run(&tank, argc, argv);
}
