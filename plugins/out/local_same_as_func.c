static float power_1(double p0, int p1);


float randlc(float *x, float a) {
    *x = a;
    return *x;
}

static float power(float a, int n) {
    float aj = a;
    float rdummy;
    float power = 1.0f;
    if (n)
        rdummy = randlc(&power, aj);
    return power + rdummy;
}

static float power_1(double a, int n) {
    float aj = a;
    float rdummy;
    float power = 1.0f;
    if (n)
        rdummy = randlc(&power, aj);
    return power + rdummy;
}



int main(void) {
    double a = 2.0;
    return (int)power_1(a, 1);
}
