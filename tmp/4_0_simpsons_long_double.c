#include <time.h>
#include <stdarg.h>
#include <inttypes.h>
#include <math.h>
#include <stdio.h>

long double pi;
long double fun(long double xarg)
{
  long double result;
  result = sin(pi * xarg);
  return result;
}

long double fun_1(double xarg)
{
  long double result;
  result = sin(pi * xarg);
  return result;
}



int main(int argc, char **argv)
{

  const int n = 1000000;
  double a;
  double b;
  double h;
  double s1;
  double x;

  a = 0.0;
  b = 1.0;
  pi = acos(-1.0);
  h = (b - a) / (2.0 * n);
  s1 = 0.0;
  x = a;
  s1 = (fun_1(a) + fun_1(b));

  for (int l = 0; l < n; l++)
  {
    x += h;
    s1 = s1 + 4.0 * fun_1(x);
    x = x + h;
    s1 = s1 + 2.0 * fun_1(x);
  }
  s1 = s1 * h * pi / 3.0;

  printf("%.16Le\n", (long double)s1);

  return 0;
}
