#include <time.h>
#include <stdarg.h>
#include <inttypes.h>
#include <math.h>
#include <stdio.h>

double pi;
long double fun_1(double p0);

long double fun_2(long double p0);


long double fun(long double xarg)
{
  long double result;
  result = sinl(pi * xarg);
  return result;
}

long double fun_1(double xarg)
{
  double result;
  result = sinl(pi * xarg);
  return result;
}



long double fun_2(long double xarg)
{
  double result;
  result = sinl(pi * xarg);
  return result;
}



int main(int argc, char **argv)
{

  const int n = 1000000;
  double a;
  double b;
  long double h;
  long double s1;
  long double x;

  a = 0.0;
  b = 1.0;
  pi = acosl(-1.0);
  h = (b - a) / (2.0 * n);
  s1 = 0.0;
  x = a;
  s1 = (fun_1(a) + fun_1(b));

  for (int l = 0; l < n; l++)
  {
    x += h;
    s1 = s1 + 4.0 * fun_2(x);
    x = x + h;
    s1 = s1 + 2.0 * fun_2(x);
  }
  s1 = s1 * h * pi / 3.0;

  printf("%.16Le\n", (long double)s1);

  return 0;
}
