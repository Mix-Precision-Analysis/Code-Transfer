float randlc(float * x, float a)
{
    double t1, t2, t3, t4, a1, a2, x1, x2, z;
    double r23 = 1.0;
    double t23 = 2.0;
    double r46 = 1.0;
    double t46 = 4.0;

    t1 = r23 * a;
    a1 = (int)t1;
    a2 = a - t23 * a1;

    t1 = r23 * (*x);
    x1 = (int)t1;
    x2 = (*x) - t23 * x1;
    t1 = a1 * x2 + a2 * x1;
    t2 = (int)(r23 * t1);
    z = t1 - t23 * t2;
    t3 = t23 * z + a2 * x2;
    t4 = (int)(r46 * t3);
    (*x) = t3 - t46 * t4;

    return (r46 * (*x));
}

int main(void)
{
    float dum[3] = { 1.0, 1.0, 1.0 };
    dum[0] = randlc(&(dum[1]), dum[2]);
    return 0;
}
