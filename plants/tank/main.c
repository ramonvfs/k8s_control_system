#include <stdio.h>
#include <math.h>
#include <unistd.h>
#include "../common/include/network.h"
#include "../common/include/solver.h"

void tank_dynamics(double t, double *x, double u, double *dxdt) {
    double A = 2.0;      // Tank cross-sectional area (m2)
    double a = 0.05;     // Outlet tube cross-sectional area (m2)
    double qm = 0.1;     // Maximum pump flow rate (m3/s)
    double g = 9.81;     // Acceleration due to gravity (m/s2)
    double Vo = 0.5;     // Outlet valve opening (0.0 to 1.0)

    // x[0] represents the water level n(t)
    // Ensure level is not negative due to numerical errors in sqrt
    double level = (x[0] > 0) ? x[0] : 0;

    // dxdt[0] -> Derivative of the level (dn/dt)
    dxdt[0] = (qm * u - Vo * a * sqrt(2 * g * level)) / A;
}

int main() {
    double x[1] = {2.5};
    double setpoint = 2.0;
    double t = 0.0;
    double dt = 0.01;

    double v_n = 0.0;           // Control Signal
    double error = 0;
    double prev_error = 0;
    double s_n = 0.0;

    double ki = 0.5;
    double kp = 10;

    while(1) {
        error = setpoint - x[0];

        s_n = call_fuzzy_controller(error * ki, (error - prev_error) * kp);

        v_n = v_n + s_n;

        // Valve Saturation
        if (v_n > 1.0) v_n = 1.0; 
        if (v_n < 0.0) v_n = 0.0;

        prev_error = error;

        rk4(tank_dynamics, t, x, v_n, dt, 1);

        if (x[0] < 0) x[0] = 0;

        t += dt;

        printf("T: %6.2f | N: %6.2f m | Er: %6.2f | Valv: %6.2f%%\n", t, x[0], error, v_n * 100.0);

        usleep(10000);
    }

    return 0;
}