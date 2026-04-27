#include <stdio.h>
#include <unistd.h>
#include "../common/include/network.h"
#include "../common/include/solver.h"

// State-Space Model
void motor_dynamics(double t, double *x, double u, double *dxdt) {
    double R = 0.371;    // Eletric Resistance (Ohms)
    double L = 0.000161;    // Eletric Inductance (H)
    double J = 0.0001460;   // Moment of Inertia of The Rotor (kg.m^2)
    double b = 0.0000214;    // Motor Viscous Friction Constant (N.m.s)
    double K = 0.116;   // Motor Torque Constant (N.m/Amp)
    double tc = 1.0;    // Torque required to move the load (N.m)

    // dxdt[0] -> Angular Acceleration
    dxdt[0] = (K * x[1] - b * x[0] - tc) / J;

    // dxdt[1] -> Derivative of Current
    dxdt[1] = (u - K * x[0] - R * x[1]) / L;
}

int main() {
    double x[2] = {0.0, 0.0}; // Initial State
    double setpoint = 104.72;  // rad/s (1000 RPM))
    double t = 0.0;
    double dt = 0.00001;

    double v_n = 0.0;           // Control Signal
    double error = 0;
    double prev_error = 0;
    double s_n = 0.0;

    double ki = (1.0 / 2000.0);
    double kp = 0.5;

    while(1) {
        error = setpoint - x[0];

        s_n = call_fuzzy_controller(error * ki, (error - prev_error) * kp);

        v_n = v_n + s_n;

        // Saturation (Max Motor Voltage)
        if (v_n > 36.0) v_n = 36.0;
        if (v_n < -36.0) v_n = -36.0;

        prev_error = error;

        rk4(motor_dynamics, t, x, v_n, dt, 2);
        t += dt;

        printf("T: %6.2f | Vel: %6.2f rad/s | Er: %6.2f | U: %6.2f\n", t, x[0], error, v_n);
        
        usleep(10000);
    }

    return 0;
}
