#include <stdlib.h>

int main(void)
{
    double* qv_cpu;
    qv_cpu = (double*)malloc(16 * sizeof(double));
    free(qv_cpu);
    return 0;
}
