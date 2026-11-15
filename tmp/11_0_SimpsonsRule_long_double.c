#include <stdio.h>
#include <stdlib.h>
#include <math.h>

long double f(long double x) {
    return x * x;
}

long double f_1(double x) {
    return x * x;
}



long double f_2(long double x) {
    return x * x;
}



long double f_3(float x) {
    return x * x;
}



void floatSimpsonsRuleV1F1(long double lrange, long double urange, int n) {

    if (n % 2 != 0) {
        printf("n has to be an even number\n");
        return;
    }
    long double step = (urange - lrange) / (long double)n;
    
    long double sum = f(lrange);
    for (int i = 1; i < n; i = i + 2) {
        // even-th term: 4 * f(x_{i})
        sum += 4 * f(lrange + (long double)i * step);
        // odd-th term: 2 * f(x_{i+1})
        sum += 2 * f(lrange + (long double)(i + 1) * step);
    }
    sum += f(urange);
    sum *= (step / 3.0);
    
    printf("%.16Le\n", (long double)sum);

}

void floatSimpsonsRuleV1F1_1(double lrange, float urange, int n) {

    if (n % 2 != 0) {
        printf("n has to be an even number\n");
        return;
    }
    float step = (urange - lrange) / (long double)n;
    
    long double sum = f_1(lrange);
    for (int i = 1; i < n; i = i + 2) {
        // even-th term: 4 * f(x_{i})
        sum += 4 * f_2(lrange + (long double)i * step);
        // odd-th term: 2 * f(x_{i+1})
        sum += 2 * f_2(lrange + (long double)(i + 1) * step);
    }
    sum += f_3(urange);
    sum *= (step / 3.0);
    
    printf("%.16Le\n", (long double)sum);

}



int main(int argc, char** argv) {

    floatSimpsonsRuleV1F1_1((double)(13223113), (float)(14223113), 20000000);
}
