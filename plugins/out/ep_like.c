float randlc_1(float * p0, float p1);

void vranlc_1(int p0, float * p1, float p2, float * p3);


float randlc(double *x, float a)
{
    *x = *x + a;
    return *x;
}

void vranlc(int n, double *x_seed, float a, double y[])
{
    if (n == 0)
        return;
    y[0] = randlc(x_seed, a);
}

void vranlc_1(int n, float * x_seed, float a, float * y)
{
    if (n == 0)
        return;
    y[0] = randlc_1(x_seed, a);
}



float randlc_1(float * x, float a)
{
    *x = *x + a;
    return *x;
}



int main(void)
{
    float dum[3] = {1.0, 1.0, 1.0};
    vranlc_1(0, &(dum[0]), dum[1], &(dum[2]));
    dum[0] = randlc_1(&(dum[1]), dum[2]);
    return 0;
}
