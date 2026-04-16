from flask import Flask, request, jsonify
import numpy as np
import skfuzzy as fuzzy
from skfuzzy import control as ctrl

app = Flask(__name__)

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

# --- FLASK ROUTES ---

@app.route('/control', methods=['POST'])
def control():
    data = request.json
    e = data.get('error', 0)
    de = data.get('delta_error', 0)
    
    try:
        e_val = max(min(float(e), 1.0), -1.0)
        de_val = max(min(float(de), 1.0), -1.0)

        fuzzy_sim.input['error'] = e_val
        fuzzy_sim.input['delta'] = de_val
        
        fuzzy_sim.compute()
        
        u_val = fuzzy_sim.output.get('output', 0.0)
        
    except Exception as ex:
        print(f"Error Fuzzy: {ex}")
        u_val = 0.0
        
    return jsonify({"u": float(u_val)})

if __name__ == '__main__':
    app.run(host='0.0.0.0', port=5000)
