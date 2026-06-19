#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <time.h>
#include <math.h>
#include "../common/include/network.h"
#include "../common/include/solver.h"
#include "../common/include/setpoint.h"

typedef struct {
    double x[1];
    double u;
    double setpoint;
    pthread_mutex_t mutex;
} shared_data;

shared_data shared;

void tank_dynamics(double t, double *x, double u, double *dxdt) {
    double A = 1.0;      // Tank cross-sectional area (m2)
    double a = 0.01;     // Outlet tube cross-sectional area (m2)
    double qm = 0.1;     // Maximum pump flow rate (m3/s)
    double g = 9.8;     // Acceleration due to gravity (m/s2)
    double Vo = 0.5;     // Outlet valve opening (0.0 to 1.0)
    double H_max = 30.0; // Maximum water level (m)

    // x[0] represents the water level n(t)
    // Ensure level is not negative due to numerical errors in sqrt
    double level = (x[0] > 0) ? x[0] : 0;

    if (level >= H_max && (qm * u) > (Vo * a * sqrt(2 * g * level))) {
        dxdt[0] = 0.0;
        return;
    }

    // dxdt[0] -> Derivative of the level (dn/dt)
    dxdt[0] = (qm * u - Vo * a * sqrt(2 * g * level)) / A;
}

void* simulation_thread(void*arg) {
    double dt = 0.01; // 10 ms
    double t = 0.0;
    double local_u = 0.0;
    double local_x[1] = {0.0};
    struct timespec next_cycle;

    clock_gettime(CLOCK_MONOTONIC, &next_cycle);

    while (1) {
        pthread_mutex_lock(&shared.mutex);
        local_u = shared.u;
        pthread_mutex_unlock(&shared.mutex);

        rk4(tank_dynamics, t, local_x, local_u, dt, 1);
        t += dt;

        pthread_mutex_lock(&shared.mutex);
        shared.x[0] = local_x[0];
        pthread_mutex_unlock(&shared.mutex);

        next_cycle.tv_nsec += (long)(dt * 1000000000.0);
        if (next_cycle.tv_nsec >= 1000000000L) {
            next_cycle.tv_sec += 1;
            next_cycle.tv_nsec -= 1000000000L;
        }
        clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &next_cycle, NULL);
    }
    return NULL;
}

void* network_thread(void* arg) {
    double current_state = 0.0;
    double error = 0.0;
    double prev_error = 0.0;
    double v_n = 0.0;
    double s_n = 0.0;

    double ki = (1.0 / 30.0);
    double kp = 100.0;

    struct timespec req_start;
    struct timespec req_end;
    struct timespec program_start;
    double rtt_seconds;
    double timestamp = 0.0;

    FILE *csv_file = fopen("tank_network_log.csv", "w");
    if (csv_file == NULL) {
        fprintf(stderr, "Error opening CSV file for writing.\n");
        return NULL;
    }

    fprintf(csv_file, "Timestamp(s),RTT(ms),Level(m),ControlInput(0-1)\n");

    clock_gettime(CLOCK_MONOTONIC, &program_start);

    while (1) {
        pthread_mutex_lock(&shared.mutex);
        current_state = shared.x[0];
        pthread_mutex_unlock(&shared.mutex);

        error = shared.setpoint - current_state;

        clock_gettime(CLOCK_MONOTONIC, &req_start);

        s_n = call_fuzzy_controller(error * ki, (error - prev_error) * kp);

        clock_gettime(CLOCK_MONOTONIC, &req_end);

        rtt_seconds = (req_end.tv_sec - req_start.tv_sec) + 
                      (req_end.tv_nsec - req_start.tv_nsec) / 1000000000.0;
        double rtt_ms = rtt_seconds * 1000.0;

        timestamp = (req_start.tv_sec - program_start.tv_sec) + 
                    (req_start.tv_nsec - program_start.tv_nsec) / 1000000000.0;

        v_n = v_n + s_n;
        if (v_n > 1.0) v_n = 1.0; 
        if (v_n < 0.0) v_n = 0.0;
        prev_error = error;

        pthread_mutex_lock(&shared.mutex);
        shared.u = v_n;
        pthread_mutex_unlock(&shared.mutex);

        fprintf(csv_file, "%.3f,%.4f,%.4f,%.4f\n", timestamp, rtt_ms, current_state, v_n);
        fflush(csv_file);
    }

    fclose(csv_file);
    return NULL;
}

int main(int argc, char *argv[]) {
    shared.x[0] = 0.0;
    shared.u = 0.0;
    shared.setpoint = 20.0;
    
    if (argc > 1) {
        shared.setpoint = atof(argv[1]);
    }

    if (pthread_mutex_init(&shared.mutex, NULL) != 0) {
        fprintf(stderr, "Mutex init failed\n");
        return 1;
    }

    pthread_t thread_sim, thread_net;

    pthread_create(&thread_sim, NULL, simulation_thread, NULL);
    pthread_create(&thread_net, NULL, network_thread, NULL);

    pthread_join(thread_sim, NULL);
    pthread_join(thread_net, NULL);

    pthread_mutex_destroy(&shared.mutex);
    return 0;
}