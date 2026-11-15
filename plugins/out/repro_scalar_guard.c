double randlc(double *x, double a)
{
    *x = *x + a;
    return *x;
}

int main(void)
{
    double dum[3] = {1.0, 2.0, 3.0};
    dum[0] = randlc(&(dum[1]), dum[2]);
    return 0;
}
