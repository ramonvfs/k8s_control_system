#include <stdio.h>
#include <unistd.h>
#include "../common/network.h"
#include "../common/solver.h"

// State-Space Model
void car_dynamics(double t, double *x, double u, double *dxdt) {
    double m = 1000.0; // Vehicle Mass (kg)
    double b = 50.0;   // Damping Coefficient (N.s/m)

    /// dxdt[0] -> Linear Acceleration
    dxdt[0] = (u - b * x[0]) / m;
}

int main() {
    double x[1] = {0.0};
    double setpoint = 30.0;
    double t = 0.0;
    double dt = 0.01;

    double u = 0.0;           // Control Signal
    double error = 0;
    double prev_error = 0;

    while(1) {
        error = setpoint - x[0];

        u = call_fuzzy_controller(error, error - prev_error);

        prev_error = error;

        rk4(car_dynamics, t, x, u, dt, 1);
        t += dt;

        printf("T: %6.2f | Vel: %6.2f m/s | Er: %6.2f\n | U: %6.2f\n", t, x, error, u);

        usleep(10000);
    }

    return 0;
}