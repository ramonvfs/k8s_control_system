#ifndef SOLVER_H
#define SOLVER_H

/*
 * Classical fixed-step 4th-order Runge-Kutta integrator.
 *
 * Advances x (dimension dim) in place from t to t + dt for the system
 * dx/dt = f(t, x, u), holding the input u constant over the step
 * (zero-order hold).
 */
void rk4(void (*f)(double, double*, double, double*), double t, double *x, double u, double dt, int dim);

#endif
