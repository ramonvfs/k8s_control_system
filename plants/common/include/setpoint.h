#ifndef SETPOINTS_H
#define SETPOINTS_H

double get_car_setpoint(int exp_type, double t) {
    if (exp_type == 1) {
        return 20.0; // ms (72 km/h)
    } else {
        if (t <= 200.0) {
            return 20.0; // ms (72 km/h)
        } else if (t <= 400.0) {
            return 41.67; // ms (150 km/h)
        } else if (t <= 600.0) {
            return 13.89; // ms (50 km/h)
        } else {
            return 50.0; // ms (180 km/h)
        }
    }
}

double get_dc_motor_setpoint(int exp_type, double t) {
    if (exp_type == 1) {
        return 104.72; // rad/s (1000 RPM)
    } else {
        if (t <= 0.05) {
            return 104.72; // rad/s (1000 RPM)
        } else if (t <= 0.1) {
            return 52.36; // rad/s (500 RPM)
        } else if (t <= 0.15) {
            return 157.08; // rad/s (1500 RPM)
        } else {
            return 83.78; // rad/s (800 RPM)
        }
    }   
}

double get_tank_setpoint(int exp_type, double t) {
    if (t <= 50.0) { 
        return 20.0; // m
    } else if (t <= 100.0) {
        return 10.0; // m
    } else if (t <= 150.0) {
        return 25.0; // m
    } else {
        return 15.0; // m
    }
}

#endif // SETPOINTS_H