double randlc_1(float * p0, float p1);


double randlc(double *x, float a)
{
    *x = *x + a;
    return *x;
}

double randlc_1(float * x, float a)
{
    *x = *x + a;
    return *x;
}



int main(void)
{
    float dum[3] = {1.0, 2.0, 3.0};
    dum[0] = randlc_1(&(dum[1]), dum[2]);
    return 0;
}
