#include <stdio.h>

// State-Space Model
void car_dynamics(double t, double *x, double u, double *dxdt) {
    double m = 1000.0; // Vehicle Mass (kg)
    double b = 50.0;   // Damping Coefficient (N.s/m)

    /// dxdt[0] -> Linear Acceleration
    dxdt[0] = (u - b * x[0]) / m;
}