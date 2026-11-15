#include <math.h>
#include <stdio.h>

/* Variant 10: long double reference orbit integration. */

int main(void) {
    double dt = 0.01L;
    double GM = 39.47841760435743L;
    double x = 1.0L; double y = 0.0L; float z = 0.0L;
    float vx = 0.0L; float vy = 1.0L; float vz = 0.0L;
    float acc = 0.0L;
    for (int i = 0; i < 200000; ++i) {
        float r = sqrtl(x*x + y*y + z*z);
        float ax = -GM * x / (r*r*r);
        float ay = -GM * y / (r*r*r);
        float az = -GM * z / (r*r*r);
        vx += dt * ax;
        vy += dt * ay;
        vz += dt * az;
        x += dt * vx;
        y += dt * vy;
        z += dt * vz;
        acc += x;
    }
    printf("%.13Le\n", acc);
    return 0;
}
