/*
 * Vehicle longitudinal speed (first-order model).
 *
 *   m dv/dt = F - b v
 *
 * State x[0] = v (m/s); input F = traction/braking force (N).
 */
#include "plant.h"

#define CAR_MASS    450.0  /* Vehicle mass (kg) */
#define CAR_DAMPING 20.0   /* Damping coefficient (N.s/m) */

static void car_dynamics(double t, double *x, double u, double *dxdt) {
    (void)t;
    /* dxdt[0] -> linear acceleration */
    dxdt[0] = (u - CAR_DAMPING * x[0]) / CAR_MASS;
}

static double car_setpoint(int exp_type, double t) {
    if (exp_type == 1) return 20.0;   /* 72 km/h */
    if (t <= 50.0)     return 20.0;   /* 72 km/h */
    if (t <= 100.0)    return 41.67;  /* 150 km/h */
    if (t <= 150.0)    return 13.89;  /* 50 km/h */
    return 50.0;                      /* 180 km/h */
}

static const plant_model_t car = {
    .name = "car",
    .state_dim = 1,
    .output_index = 0,
    .dt = 0.01,
    .dynamics = car_dynamics,
    .setpoint = car_setpoint,
    .ki = 1.0 / 180.0,
    .kp = 5.0,
    .u_min = -4000.0,
    .u_max = 8000.0,
    .output_desc = "Velocity (m/s)",
    .input_desc = "Traction force (N)",
};

int main(int argc, char *argv[]) {
    return plant_run(&car, argc, argv);
}
