#include <stdio.h>
#include <unistd.h>
#include "../common/solver.h"

// State-Space Model
void car_dynamics(double t, double *x, double u, double *dxdt) {
    double m = 1000.0; // Vehicle Mass (kg)
    double b = 50.0;   // Damping Coefficient (N.s/m)

    /// dxdt[0] -> Linear Acceleration
    dxdt[0] = (u - b * x[0]) / m;
}

int main() {
    double x = {0.0};
    double setpoint = 100.0;
    double t = 0.0;
    double dt = 0.01;

    double u = 0.0;           // Control Signal

    while(1) {
        double error = setpoint - x;

        u = error * 3;

        rk4(car_dynamics, t, &x, u, dt, 1);
        t += dt;

        printf("T: %6.2f | Vel: %6.2f m/s | Er: %6.2f\n", t, x, error);

        usleep(10000);
    }

    return 0;
}