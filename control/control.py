import numpy as np
import skfuzzy as fuzz
from skfuzzy import control as ctrl
from flask import Flask, request, jsonify

app = Flask(__name__)

error = ctrl.Antecedent(np.arange(-150, 151, 1), 'error')
delta_error = ctrl.Antecedent(np.arange(-150, 151, 1), 'delta_error')
output = ctrl.Consequent(np.arange(-12, 13, 1), 'output')

error['negative'] = fuzz.trimf(error.universe, [-150, -150, 0])
error['zero']     = fuzz.trimf(error.universe, [-50, 0, 50])
error['positive'] = fuzz.trimf(error.universe, [0, 150, 150])

delta_error['negative'] = fuzz.trimf(delta_error.universe, [-150, -150, 0])
delta_error['zero']     = fuzz.trimf(delta_error.universe, [-50, 0, 50])
delta_error['positive'] = fuzz.trimf(delta_error.universe, [0, 150, 150])

output['low']    = fuzz.trimf(output.universe, [-12, -12, 0])
output['medium'] = fuzz.trimf(output.universe, [-6, 0, 6])
output['high']   = fuzz.trimf(output.universe, [0, 12, 12])

rules = [
    ctrl.Rule(error['positive'] & delta_error['positive'], output['high']),
    ctrl.Rule(error['positive'] & delta_error['zero'],     output['high']),
    ctrl.Rule(error['positive'] & delta_error['negative'], output['medium']),
    
    ctrl.Rule(error['zero'] & delta_error['positive'], output['medium']),
    ctrl.Rule(error['zero'] & delta_error['zero'],     output['medium']),
    ctrl.Rule(error['zero'] & delta_error['negative'], output['medium']),
    
    ctrl.Rule(error['negative'] & delta_error['positive'], output['medium']),
    ctrl.Rule(error['negative'] & delta_error['zero'],     output['low']),
    ctrl.Rule(error['negative'] & delta_error['negative'], output['low']),
]

control_sys = ctrl.ControlSystem(rules)
fuzzy_sim = ctrl.ControlSystemSimulation(control_sys)

# --- ENDPOINT ---

@app.route('/control', methods=['POST'])
def control():
    data = request.json
    e = data.get('error', 0)
    de = data.get('delta_error', 0)
    
    try:
        fuzzy_sim.input['error'] = e
        fuzzy_sim.input['delta_error'] = de
        fuzzy_sim.compute()
        
        u = fuzzy_sim.output.get('output', 0.0)
    except Exception as ex:
        print(f"Error Fuzzy: {ex}")
        u = 0.0
        
    return jsonify({"u": u})

if __name__ == '__main__':
    app.run(host='0.0.0.0', port=5000)