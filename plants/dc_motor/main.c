#include <stdio.h>
#include <unistd.h>
#include "../common/network.h"
#include "../common/solver.h"

// State-Space Model
void motor_dynamics(double t, double *x, double u, double *dxdt) {
    double R = 1.0;    // Eletric Resistance (Ohms)
    double L = 0.5;    // Eletric Inductance (H)
    double J = 0.01;   // Moment of Inertia of The Rotor (kg.m^2)
    double b = 0.2;    // Motor Viscous Friction Constant (N.m.s)
    double K = 0.01;   // Motor Torque Constant (N.m/Amp)

    // dxdt[0] -> Angular Acceleration
    dxdt[0] = (K * x[1] - b * x[0]) / J;

    // dxdt[1] -> Derivative of Current
    dxdt[1] = (u - K * x[0] - R * x[1]) / L;
}

int main() {
    double x[2] = {0.0, 0.0}; // Initial State
    double setpoint = 10.0;
    double t = 0.0;
    double dt = 0.01;

    double v_n = 0.0;           // Control Signal
    double error = 0;
    double prev_error = 0;
    double s_n = 0.0;

    double ki = 0.1;
    double kp = 0.05;

    while(1) {
        error = setpoint - x[0];

        s_n = call_fuzzy_controller(error * ki, (error - prev_error) * kp);

        v_n = v_n + s_n;

        // Saturation (Max Motor Voltage)
        if (v_n > 24.0) v_n = 24.0;
        if (v_n < -24.0) v_n = -24.0;

        prev_error = error;

        rk4(motor_dynamics, t, x, v_n, dt, 2);
        t += dt;

        printf("T: %6.2f | Vel: %6.2f rad/s | Er: %6.2f | U: %6.2f\n", t, x[0], error, v_n);
        
        usleep(10000);
    }

    return 0;
}
