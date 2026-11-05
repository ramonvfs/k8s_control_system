#ifndef PLANT_H
#define PLANT_H

/*
 * Plant description consumed by the shared runtime (plant_runtime.c).
 *
 * A plant binary only defines its physics (state-space model), its setpoint
 * profile and the gains of the incremental fuzzy-PI loop; the runtime
 * provides the real-time simulation thread, the network/control thread,
 * configuration, signal handling and CSV logging.
 *
 * Control law executed every iteration k of the control thread:
 *
 *   error_k = setpoint_k - y_k
 *   e_k     = ki * error_k                      (normalized error)
 *   ed_k    = kp * (error_k - error_{k-1})      (normalized error variation)
 *   s_k     = FIS(e_k, ed_k)                    (remote fuzzy controller, in [-1, 1])
 *   v_k     = sat(v_{k-1} + s_k, u_min, u_max)  (control input applied to the plant)
 *
 * The names ki/kp follow the incremental (velocity-form) PI analogy:
 * the FIS output is an increment, so the term proportional to e acts as the
 * integral action and the term proportional to the variation of e acts as
 * the proportional action.
 */

#define PLANT_MAX_STATES 4

typedef struct {
    const char *name;            /* binary/plant identifier, e.g. "dc_motor" */
    int state_dim;               /* number of states (<= PLANT_MAX_STATES) */
    int output_index;            /* index of the controlled state y in x[] */
    double dt;                   /* integration step and simulation period (s) */

    /* dx/dt = f(t, x, u) */
    void (*dynamics)(double t, double *x, double u, double *dxdt);
    /* reference profile r(t) for a given experiment type */
    double (*setpoint)(int exp_type, double t);

    double ki;                   /* gain applied to the error */
    double kp;                   /* gain applied to the error variation */
    double u_min, u_max;         /* saturation of the control input v */

    /* Units, used only for documentation in the startup banner. */
    const char *output_desc;     /* e.g. "Velocity (rad/s)" */
    const char *input_desc;      /* e.g. "Armature voltage (V)" */
} plant_model_t;

/*
 * Runs the plant until DURATION_S elapses or SIGINT/SIGTERM is received.
 *
 * Command line (kept compatible with the original binaries):
 *   <binary> [exp_type] [csv_file]
 *
 * Environment variables: see docs/plants.md or the banner printed at startup.
 */
int plant_run(const plant_model_t *model, int argc, char *argv[]);

#endif
