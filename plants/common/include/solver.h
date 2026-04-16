#ifndef SOLVER_H
#define SOLVER_H

void rk4(void (*f)(double, double*, double, double*), double t, double *x, double u, double dt, int dim);

#endif
