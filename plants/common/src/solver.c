#include "./solver.h"

void rk4(void (*f)(double, double*, double, double*), double t, double *x, double u, double dt, int dim) {
    double k1[dim], k2[dim], k3[dim], k4[dim], xtemp[dim];

    // Step 1
    f(t, x, u, k1);

    // Step 2
    for (int i = 0; i < dim; i++) {
        xtemp[i] = x[i] + k1[i] * (dt / 2.0);
    }
    f(t + dt / 2.0, xtemp, u, k2);

    // Step 3
    for (int i = 0; i < dim; i++) {
        xtemp[i] = x[i] + k2[i] * (dt / 2.0);
    }
    f(t + dt / 2.0, xtemp, u, k3);

    // Step 4
    for (int i = 0; i < dim; i++) {
        xtemp[i] = x[i] + k3[i] * dt;
    }
    f(t + dt, xtemp, u, k4);

    // Final Step
    for (int i = 0; i < dim; i++) {
        x[i] += (dt / 6.0) * (k1[i] + 2.0 * k2[i] + 2.0 * k3[i] + k4[i]);
    }
}