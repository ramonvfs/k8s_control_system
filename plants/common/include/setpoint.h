#ifndef SETPOINTS_H
#define SETPOINTS_H

double get_car_setpoint(int exp_type, double t) {
    if (exp_type == 1) {
        return 20.0; // ms (72 km/h)
    } else {
        if (t <= 50.0) {
            return 20.0; // ms (72 km/h)
        } else if (t <= 100.0) {
            return 41.67; // ms (150 km/h)
        } else if (t <= 150.0) {
            return 13.89; // ms (50 km/h)
        } else {
            return 50.0; // ms (180 km/h)
        }
    }
}

double get_dc_motor_setpoint(int exp_type, double t) {
    if (exp_type == 1) {
        return 52.36; // rad/s (500 RPM)
    } else {
        if (t <= 10.0) {
            return 52.36; // rad/s (500 RPM)
        } else if (t <= 20.0) {
            return 31.42; // rad/s (300 RPM)
        } else if (t <= 30.0) {
            return 62.83; // rad/s (600 RPM)
        } else {
            return 41.89; // rad/s (400 RPM)
        }
    }   
}

double get_tank_setpoint(int exp_type, double t) {
    if (exp_type == 1) {
        return 20.0; // m
    } else {
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
}

#endif // SETPOINTS_H