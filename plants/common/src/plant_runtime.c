#include "plant.h"
#include "network.h"
#include "solver.h"

#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define NSEC_PER_SEC 1000000000L
#define CSV_FLUSH_PERIOD_S 1.0

typedef struct {
    int exp_type;
    char csv_path[512];
    char client_id[CLIENT_ID_MAX];
    const char *controller_url;
    double request_period_ms;   /* 0 = free-running (next request right after the reply) */
    double duration_s;          /* 0 = run until SIGINT/SIGTERM */
    int keepalive;
    long timeout_ms;
    int log_every;
} plant_config_t;

/* State exchanged between the simulation and the control threads. */
typedef struct {
    pthread_mutex_t mutex;
    double x[PLANT_MAX_STATES];
    double t;                   /* simulation time (s) */
    double u;                   /* control input currently applied */
    double setpoint;
} shared_state_t;

static const plant_model_t *g_model;
static plant_config_t g_cfg;
static shared_state_t g_shared;
static volatile sig_atomic_t g_running = 1;

/* ------------------------------------------------------------------------ */
/* Helpers                                                                  */
/* ------------------------------------------------------------------------ */

static void on_signal(int sig) {
    (void)sig;
    g_running = 0;
}

static double env_double(const char *name, double def) {
    const char *v = getenv(name);
    return (v && *v) ? atof(v) : def;
}

static double ts_diff_ms(const struct timespec *end, const struct timespec *start) {
    return (end->tv_sec - start->tv_sec) * 1000.0 +
           (end->tv_nsec - start->tv_nsec) / 1000000.0;
}

static void ts_add_ns(struct timespec *ts, long long ns) {
    ns += ts->tv_nsec;
    ts->tv_sec += ns / NSEC_PER_SEC;
    ts->tv_nsec = ns % NSEC_PER_SEC;
}

static int ts_before(const struct timespec *a, const struct timespec *b) {
    return a->tv_sec < b->tv_sec || (a->tv_sec == b->tv_sec && a->tv_nsec < b->tv_nsec);
}

/* "YYYY-MM-DD HH:MM:SS.uuuuuu" (UTC, same time base as the controller log) */
static void format_wall_time(const struct timespec *ts, char *buf, size_t len) {
    struct tm tm;
    gmtime_r(&ts->tv_sec, &tm);
    size_t n = strftime(buf, len, "%Y-%m-%d %H:%M:%S", &tm);
    snprintf(buf + n, len - n, ".%06ld", ts->tv_nsec / 1000);
}

/* Default client id: CSV file name without directory and ".csv" suffix,
 * e.g. results/car/M4_K2/res_car_M4_K2_EXP2_client3.csv -> res_car_M4_K2_EXP2_client3 */
static void default_client_id(const char *csv_path, char *out, size_t len) {
    const char *base = strrchr(csv_path, '/');
    base = base ? base + 1 : csv_path;
    size_t n = strlen(base);
    if (n >= len) n = len - 1;
    memcpy(out, base, n);
    out[n] = '\0';
    char *dot = strrchr(out, '.');
    if (dot && strcmp(dot, ".csv") == 0) *dot = '\0';
}

static void load_config(int argc, char *argv[]) {
    g_cfg.exp_type = argc > 1 ? atoi(argv[1]) : 1;

    if (argc > 2) {
        snprintf(g_cfg.csv_path, sizeof(g_cfg.csv_path), "%s", argv[2]);
    } else {
        snprintf(g_cfg.csv_path, sizeof(g_cfg.csv_path), "%s_network_log.csv", g_model->name);
    }

    const char *id = getenv("CLIENT_ID");
    if (id && *id) {
        snprintf(g_cfg.client_id, sizeof(g_cfg.client_id), "%s", id);
    } else {
        default_client_id(g_cfg.csv_path, g_cfg.client_id, sizeof(g_cfg.client_id));
    }

    g_cfg.controller_url = getenv("CONTROLLER_URL");
    if (!g_cfg.controller_url || !*g_cfg.controller_url) {
        g_cfg.controller_url = "http://localhost:5000/control";
    }

    g_cfg.request_period_ms = env_double("REQUEST_PERIOD_MS", 0.0);
    g_cfg.duration_s = env_double("DURATION_S", 0.0);
    g_cfg.keepalive = (int)env_double("HTTP_KEEPALIVE", 0);
    g_cfg.timeout_ms = (long)env_double("HTTP_TIMEOUT_MS", 0);
    g_cfg.log_every = (int)env_double("LOG_EVERY", 1);
    if (g_cfg.log_every < 1) g_cfg.log_every = 1;
}

static void print_banner(void) {
    const plant_model_t *m = g_model;
    printf("---------------------------------------------------\n");
    printf("Plant:               %s\n", m->name);
    printf("Output y:            %s\n", m->output_desc);
    printf("Control input v:     %s, saturation [%g, %g]\n", m->input_desc, m->u_min, m->u_max);
    printf("Gains:               ki = %g (error), kp = %g (error variation)\n", m->ki, m->kp);
    printf("Integration:         RK4, dt = %g s (real time)\n", m->dt);
    printf("Experiment type:     %d (%s setpoint)\n", g_cfg.exp_type,
           g_cfg.exp_type == 1 ? "constant" : "piecewise");
    printf("---------------------------------------------------\n");
    printf("Controller URL:      %s\n", g_cfg.controller_url);
    printf("Client id:           %s\n", g_cfg.client_id);
    if (g_cfg.request_period_ms > 0) {
        printf("Request period:      %g ms\n", g_cfg.request_period_ms);
    } else {
        printf("Request period:      free-running (back-to-back requests)\n");
    }
    printf("HTTP keep-alive:     %s\n", g_cfg.keepalive ? "on" : "off (new connection per request)");
    printf("HTTP timeout:        %ld ms%s\n", g_cfg.timeout_ms, g_cfg.timeout_ms ? "" : " (none)");
    printf("Duration:            %g s%s\n", g_cfg.duration_s, g_cfg.duration_s > 0 ? "" : " (until signal)");
    printf("CSV file:            %s (every %d sample(s))\n", g_cfg.csv_path, g_cfg.log_every);
    printf("---------------------------------------------------\n");
    fflush(stdout);
}

/* ------------------------------------------------------------------------ */
/* Simulation thread: integrates the plant in real time with a fixed step.  */
/* ------------------------------------------------------------------------ */

static void *simulation_thread(void *arg) {
    (void)arg;
    const plant_model_t *m = g_model;
    double x[PLANT_MAX_STATES] = {0};
    double t = 0.0;

    struct timespec next_cycle;
    clock_gettime(CLOCK_MONOTONIC, &next_cycle);

    while (g_running) {
        pthread_mutex_lock(&g_shared.mutex);
        double u = g_shared.u;
        pthread_mutex_unlock(&g_shared.mutex);

        rk4(m->dynamics, t, x, u, m->dt, m->state_dim);
        t += m->dt;

        double sp = m->setpoint(g_cfg.exp_type, t);

        pthread_mutex_lock(&g_shared.mutex);
        memcpy(g_shared.x, x, sizeof(double) * m->state_dim);
        g_shared.t = t;
        g_shared.setpoint = sp;
        pthread_mutex_unlock(&g_shared.mutex);

        ts_add_ns(&next_cycle, (long long)(m->dt * NSEC_PER_SEC));
        clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &next_cycle, NULL);
    }
    return NULL;
}

/* ------------------------------------------------------------------------ */
/* Control thread: samples the plant, queries the controller, applies v.    */
/* ------------------------------------------------------------------------ */

static void *control_thread(void *arg) {
    (void)arg;
    const plant_model_t *m = g_model;

    controller_client_t client;
    if (controller_client_init(&client, g_cfg.controller_url, g_cfg.client_id,
                               g_cfg.keepalive, g_cfg.timeout_ms) != 0) {
        fprintf(stderr, "Failed to initialize HTTP client.\n");
        g_running = 0;
        return NULL;
    }

    FILE *csv = fopen(g_cfg.csv_path, "w");
    if (!csv) {
        fprintf(stderr, "Error opening CSV file '%s': %s\n", g_cfg.csv_path, strerror(errno));
        controller_client_cleanup(&client);
        g_running = 0;
        return NULL;
    }
    setvbuf(csv, NULL, _IOFBF, 1 << 16);
    fprintf(csv,
            "TimestampFormat_us,Timestamp_us,t,seq,setpoint,y,error,e,ed,s,v,"
            "requestDelay,connectTime,firstByteTime,measuredLoopTime,requestTime,"
            "httpStatus,curlCode,ok,pod\n");

    double prev_error = 0.0;
    double v = 0.0;
    unsigned long seq = 0;
    long long period_ns = (long long)(g_cfg.request_period_ms * 1000000.0);

    struct timespec start, loop_start, prev_loop_start, req_start, req_end, last_flush, deadline;
    clock_gettime(CLOCK_MONOTONIC, &start);
    last_flush = deadline = prev_loop_start = start;

    while (g_running) {
        clock_gettime(CLOCK_MONOTONIC, &loop_start);
        double loop_ms = seq ? ts_diff_ms(&loop_start, &prev_loop_start) : 0.0;
        prev_loop_start = loop_start;

        pthread_mutex_lock(&g_shared.mutex);
        double y = g_shared.x[m->output_index];
        double sp = g_shared.setpoint;
        double t = g_shared.t;
        pthread_mutex_unlock(&g_shared.mutex);

        double error = sp - y;
        double e = error * m->ki;
        double ed = (error - prev_error) * m->kp;

        struct timespec wall;
        clock_gettime(CLOCK_REALTIME, &wall);

        controller_reply_t reply;
        clock_gettime(CLOCK_MONOTONIC, &req_start);
        controller_client_call(&client, e, ed, seq, &reply);
        clock_gettime(CLOCK_MONOTONIC, &req_end);
        double rtt_ms = ts_diff_ms(&req_end, &req_start);

        /* On failure the increment is 0: the last control input is held. */
        double s = reply.ok ? reply.u : 0.0;
        v += s;
        if (v > m->u_max) v = m->u_max;
        if (v < m->u_min) v = m->u_min;
        prev_error = error;

        pthread_mutex_lock(&g_shared.mutex);
        g_shared.u = v;
        pthread_mutex_unlock(&g_shared.mutex);

        if (seq % g_cfg.log_every == 0) {
            char wall_str[64];
            format_wall_time(&wall, wall_str, sizeof(wall_str));
            long long wall_us = (long long)wall.tv_sec * 1000000LL + wall.tv_nsec / 1000;
            fprintf(csv, "%s,%lld,%.6f,%lu,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,"
                         "%.6f,%.6f,%.6f,%.6f,%.6f,%ld,%d,%d,%s\n",
                    wall_str, wall_us, t, seq, sp, y, error, e, ed, s, v,
                    rtt_ms, reply.connect_ms, reply.first_byte_ms, loop_ms,
                    g_cfg.request_period_ms, reply.http_status, reply.curl_code,
                    reply.ok, reply.pod);
        }
        seq++;

        if (ts_diff_ms(&req_end, &last_flush) >= CSV_FLUSH_PERIOD_S * 1000.0) {
            fflush(csv);
            last_flush = req_end;
        }

        if (g_cfg.duration_s > 0 && ts_diff_ms(&req_end, &start) >= g_cfg.duration_s * 1000.0) {
            g_running = 0;
            break;
        }

        if (period_ns > 0) {
            /* Fixed-rate schedule. On overrun (RTT > period) the next request
             * is issued immediately and the schedule is re-anchored, so late
             * requests are never sent in bursts to catch up. */
            ts_add_ns(&deadline, period_ns);
            struct timespec now;
            clock_gettime(CLOCK_MONOTONIC, &now);
            if (ts_before(&deadline, &now)) {
                deadline = now;
            } else {
                clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &deadline, NULL);
            }
        }
    }

    fclose(csv);
    controller_client_cleanup(&client);
    printf("Control loop finished after %lu requests.\n", seq);
    return NULL;
}

/* ------------------------------------------------------------------------ */

int plant_run(const plant_model_t *model, int argc, char *argv[]) {
    if (model->state_dim < 1 || model->state_dim > PLANT_MAX_STATES) {
        fprintf(stderr, "Invalid state dimension %d\n", model->state_dim);
        return 1;
    }
    g_model = model;
    load_config(argc, argv);
    print_banner();

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = on_signal;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    if (network_global_init() != 0) {
        fprintf(stderr, "libcurl initialization failed\n");
        return 1;
    }
    if (pthread_mutex_init(&g_shared.mutex, NULL) != 0) {
        fprintf(stderr, "Mutex init failed\n");
        return 1;
    }

    pthread_t thread_sim, thread_ctrl;
    pthread_create(&thread_sim, NULL, simulation_thread, NULL);
    pthread_create(&thread_ctrl, NULL, control_thread, NULL);

    pthread_join(thread_ctrl, NULL);
    g_running = 0;
    pthread_join(thread_sim, NULL);

    pthread_mutex_destroy(&g_shared.mutex);
    network_global_cleanup();
    return 0;
}
