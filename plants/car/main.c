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

    double v_n = 0.0;           // Control Signal
    double error = 0;
    double prev_error = 0;
    double s_n = 0.0;

    double ki = 0.033;
    double kp = 50;

    while(1) {
        error = setpoint - x[0];

        s_n = call_fuzzy_controller(error * ki, (error - prev_error) * kp);

        v_n = v_n + s_n;

         // Saturation
        if (v_n > 8000.0) v_n = 8000.0; 
        if (v_n < -4000.0) v_n = -4000.0;

        prev_error = error;

        rk4(car_dynamics, t, x, v_n, dt, 1);
        t += dt;

        printf("T: %6.2f | Vel: %6.2f km/h | Er: %6.2f | U: %7.2f N\n", t, x[0] * 3.6, error, v_n);

        usleep(10000);
    }

    return 0;
}