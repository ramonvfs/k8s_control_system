#include <stdio.h>

// State-Space Model
void motor_dynamics(double *x, double u, double *dxdt) {
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
