#include <stdio.h>
#include <unistd.h>
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
    double setpoint = 100.0;
    double t = 0.0;
    double dt = 0.01;

    double u = 0.0;           // Control Signal

    while(1) {
        double erro = setpoint - x[0];

        u = erro * 1.2; 

        rk4(motor_dynamics, t, x, u, dt, 2);
        t += dt;

        printf("T: %6.2f | Vel: %6.2f rad/s | Cor: %6.2f A | Erro: %6.2f\n", t, x[0], x[1], erro);
        
        usleep(10000);
    }

    return 0;
}
