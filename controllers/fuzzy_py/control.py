"""Fuzzy control service (Python / scikit-fuzzy implementation).

Functionally equivalent to controllers/fuzzy_cpp: same FIM, same HTTP API
and same per-pod CSV log schema (see docs/controller.md and
docs/data_schema.md). Configuration is read from environment variables.

Endpoints
  POST /control  {"error": e, "delta_error": de, "client_id": "...", "seq": n}
                 -> {"u": s, "pod": "<pod name>"}
  GET  /health   -> {"status": "ok", "pod": "<pod name>"}
"""
import collections
import os
import signal
import socket
import sys
import threading
import time
from datetime import datetime, timezone

import numpy as np
import skfuzzy as fuzzy
from flask import Flask, jsonify, request
from skfuzzy import control as ctrl

# --- CONFIGURATION ---

PORT = int(os.environ.get("PORT", "5000"))
POD_NAME = os.environ.get("POD_NAME") or os.environ.get("HOSTNAME") or socket.gethostname()
NODE_NAME = os.environ.get("NODE_NAME", "-")
LOG_ENABLED = os.environ.get("CONTROLLER_LOG", "1") != "0"
LOG_DIR = os.environ.get("LOG_DIR", ".")
EXP_NAME = os.environ.get("EXP_NAME", "")
RATE_WINDOW_S = int(os.environ.get("RATE_WINDOW_MS", "1000")) / 1000.0 or 1.0

START_WALL = datetime.now(timezone.utc)
START_MONO = time.perf_counter()

# --- FUZZY INFERENCE MACHINE (FIM) ---

# 1. Universe of discourse: Normalized range [-1, 1]
universe = np.linspace(-1, 1, 101)
error = ctrl.Antecedent(universe, 'error')
delta = ctrl.Antecedent(universe, 'delta')
output = ctrl.Consequent(universe, 'output')

# 2. Membership Functions (7 classes: LN, MN, SN, ZZ, SP, MP, LP)
# Trapezoidal at extremes, Triangular in the middle
names = ['LN', 'MN', 'SN', 'ZZ', 'SP', 'MP', 'LP']

for var in [error, delta, output]:
    var['LN'] = fuzzy.trapmf(var.universe, [-1, -1, -0.7, -0.4])
    var['MN'] = fuzzy.trimf(var.universe, [-0.7, -0.4, -0.1])
    var['SN'] = fuzzy.trimf(var.universe, [-0.4, -0.1, 0])
    var['ZZ'] = fuzzy.trimf(var.universe, [-0.1, 0, 0.1])
    var['SP'] = fuzzy.trimf(var.universe, [0, 0.1, 0.4])
    var['MP'] = fuzzy.trimf(var.universe, [0.1, 0.4, 0.7])
    var['LP'] = fuzzy.trapmf(var.universe, [0.4, 0.7, 1, 1])

# 3. Rule Base
rule_matrix = [
    ['LN', 'LN', 'LN', 'LN', 'MN', 'SN', 'ZZ'], # Error LN
    ['LN', 'LN', 'LN', 'MN', 'SN', 'ZZ', 'SP'], # Error MN
    ['LN', 'LN', 'MN', 'SN', 'ZZ', 'SP', 'MP'], # Error SN
    ['LN', 'MN', 'SN', 'ZZ', 'SP', 'MP', 'LP'], # Error ZZ
    ['MN', 'SN', 'ZZ', 'SP', 'MP', 'LP', 'LP'], # Error SP
    ['SN', 'ZZ', 'SP', 'MP', 'LP', 'LP', 'LP'], # Error MP
    ['ZZ', 'SP', 'MP', 'LP', 'LP', 'LP', 'LP']  # Error LP
]

rules = []
for i, e_label in enumerate(names):
    for j, de_label in enumerate(names):
        out_label = rule_matrix[i][j]
        rules.append(ctrl.Rule(error[e_label] & delta[de_label], output[out_label]))

# 4. Engine Creation
fuzzy_ctrl = ctrl.ControlSystem(rules)
fuzzy_sim = ctrl.ControlSystemSimulation(fuzzy_ctrl)

# The simulation object is stateful and not thread-safe; Flask serves
# requests from multiple threads, so inference is serialized.
fim_lock = threading.Lock()
arrivals = collections.deque()  # request times inside the rate window


# --- ASYNCHRONOUS CSV LOGGER ---

class CsvLogger:
    """Buffers lines in memory and writes them from a background thread once
    per flush period, keeping disk I/O off the request path."""

    def __init__(self, path, header, flush_period=1.0):
        self._file = open(path, "w")
        self._file.write(header)
        self._file.flush()
        self._pending = []
        self._lock = threading.Lock()
        self._stop = threading.Event()
        self._flush_period = flush_period
        self._writer = threading.Thread(target=self._run, daemon=True)
        self._writer.start()

    def push(self, line):
        with self._lock:
            self._pending.append(line)

    def _drain(self):
        with self._lock:
            batch, self._pending = self._pending, []
        self._file.writelines(batch)
        self._file.flush()

    def _run(self):
        while not self._stop.wait(self._flush_period):
            self._drain()

    def close(self):
        if self._stop.is_set():
            return
        self._stop.set()
        self._writer.join()
        self._drain()
        self._file.close()


def log_path():
    name = "Result_CSS_" + START_WALL.strftime("%Y-%m-%d_%H-%M-%S")
    if EXP_NAME:
        name += "_Exp_" + EXP_NAME
    return os.path.join(LOG_DIR, f"{name}_pod_{POD_NAME}.csv")


def csv_safe(s):
    s = str(s)
    for c in ',"\r\n':
        s = s.replace(c, '_')
    return s or '-'


logger = None
if LOG_ENABLED:
    logger = CsvLogger(
        log_path(),
        "TimestampFormat_us,Timestamp_us,ElapsedTime_us,Pod,IIoT_name,RemoteAddr,"
        "seq,e,ed,s,Request_rate,LockWait_us,Inference_us,Handler_us\n")

# --- FLASK ROUTES ---

app = Flask(__name__)


@app.route('/control', methods=['POST'])
def control():
    t_entry = time.perf_counter()
    t_wall = datetime.now(timezone.utc)  # UTC, like the C++ controller and the plants

    data = request.get_json(silent=True)
    try:
        e = float(data.get('error', 0))
        de = float(data.get('delta_error', 0))
        seq = int(data.get('seq', -1))
        client = data.get('client_id', '')
    except (AttributeError, TypeError, ValueError):
        return jsonify({"error": "Invalid JSON or Processing Error"}), 400
    t_parsed = time.perf_counter()

    with fim_lock:
        t_locked = time.perf_counter()
        try:
            e_val = max(min(e, 1.0), -1.0)
            de_val = max(min(de, 1.0), -1.0)

            fuzzy_sim.input['error'] = e_val
            fuzzy_sim.input['delta'] = de_val

            fuzzy_sim.compute()

            u_val = fuzzy_sim.output.get('output', 0.0)

        except Exception as ex:
            print(f"Error Fuzzy: {ex}")
            u_val = 0.0
        t_done = time.perf_counter()

        arrivals.append(t_locked)
        while arrivals and t_locked - arrivals[0] > RATE_WINDOW_S:
            arrivals.popleft()
        in_window = len(arrivals)

    if logger:
        us = lambda dt: int(dt * 1e6)
        logger.push(
            f"{t_wall.strftime('%Y-%m-%d %H:%M:%S.%f')},{int(t_wall.timestamp() * 1e6)},"
            f"{us(t_entry - START_MONO)},{POD_NAME},{csv_safe(client)},{request.remote_addr},"
            f"{seq},{e:.8f},{de:.8f},{float(u_val):.8f},"
            f"{in_window / RATE_WINDOW_S:.2f},{us(t_locked - t_parsed)},"
            f"{us(t_done - t_locked)},{us(t_done - t_entry)}\n")

    return jsonify({"u": float(u_val), "pod": POD_NAME})


@app.route('/health', methods=['GET'])
def health():
    return jsonify({"status": "ok", "pod": POD_NAME})


def shutdown(signum, frame):
    print(f"Signal {signum} received, shutting down...", flush=True)
    sys.exit(0)


if __name__ == '__main__':
    app.json.sort_keys = False  # keep "u" first in the reply
    signal.signal(signal.SIGTERM, shutdown)
    signal.signal(signal.SIGINT, shutdown)
    print(f"Fuzzy Inference Machine (FIM) [python] pod={POD_NAME} node={NODE_NAME} "
          f"port={PORT} log={log_path() if logger else 'disabled'}", flush=True)
    try:
        app.run(host='0.0.0.0', port=PORT)
    finally:
        if logger:
            logger.close()
