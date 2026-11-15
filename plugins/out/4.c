#include <time.h>
#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
/* Auto-generated variant from Mix-Funarc_ld: accum_bias_down */

#define INPUTS 10

long double fun( long double x )
{
  int k, n = 5;
  long double t1;
  long double d1 = 1.0L;

  t1 = x;

  for( k = 1; k <= n; k++ )
  {
    d1 = 1.96 * d1;
    t1 = t1 + sin (d1 * x) / d1;
  }

  return t1;
}

int main()
{
  int l;
  long int diff = 0;

  int i, j, k; 
  int arr[] = {100,
              200,
              1000,
              2000,
              10000,
              20000,
              100000,
              200000,
              1000000,
              2000000
              };
  double h; double t1; double t2; double dppi;
  // , ans = 5.795776322412856L;
  double s1; 

  // variables for logging/checking
  double result;

  for (l = 0; l < INPUTS; l++)
  {
    int n = arr[l];
    t1 = -1.0;
    dppi = acosl(t1);
    s1 = 0.0;
    t1 = 0.0;
    h = dppi / n;

    for( i = 1; i <= n; i++ )
    {
      t2 = fun (i * h);
      s1 = s1 + sqrtl (h*h + (t2 - t1)*(t2 - t1));
      t1 = t2;
    }

    result += s1;
  }
  
  printf("%lf", (long double)result);
  return 0;
}
