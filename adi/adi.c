/**
 * This version is stamped on May 10, 2016
 *
 * Contact:
 *   Louis-Noel Pouchet <pouchet.ohio-state.edu>
 *   Tomofumi Yuki <tomofumi.yuki.fr>
 *
 * Web address: http://polybench.sourceforge.net
 */
/* adi.c: this file is part of PolyBench/C */

#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <math.h>
#include <stdlib.h>

/* Include polybench common header. */
/* Include benchmark-specific header. */
/* Fixed dataset sizes (LARGE_DATASET). */
static const int TSTEPS = 500;
static const int N = 1000;
/* Begin inlined benchmark parameters */
/* End inlined benchmark parameters */

/* Array initialization. */
static
void init_array (int n,
		 double *u)
{
  int i, j;

  for (i = 0; i < n; i++)
    for (j = 0; j < n; j++)
      {
	u[(((size_t)(i)) * (n) + (j))]=  (double)(i + n-j) / n;
      }
}

/* DCE code. Must scan the entire live-out data.
   Can be used also to check the correctness of the output. */
static
void print_array(int n,
		 double *u)

{
  printf("%.17g\n", (double)u[0]);
}

/* Main computational kernel. The whole function will be timed,
   including the call and return. */
/* Based on a Fortran code fragment from Figure 5 of
 * "Automatic Data and Computation Decomposition on Distributed Memory Parallel Computers"
 * by Peizong Lee and Zvi Meir Kedem, TOPLAS, 2002
 */
static
void kernel_adi(int tsteps,
		 int n,
		 double *u,
		 double *v,
		 double *p,
		 double *q)
{
  int t, i, j;
  double DX, DY, DT;
  double B1, B2;
  double mul1, mul2;
  double a, b, c, d, e, f;
  DX = 1.0/(double)n;
  DY = 1.0/(double)n;
  DT = 1.0/(double)tsteps;
  B1 = 2.0;
  B2 = 1.0;
  mul1 = B1 * DT / (DX * DX);
  mul2 = B2 * DT / (DY * DY);

  a = -mul1 /  2.0;
  b = 1.0+mul1;
  c = a;
  d = -mul2 / 2.0;
  e = 1.0+mul2;
  f = d;

 for (t=1; t<=tsteps; t++) {
    //Column Sweep
    for (i=1; i<n-1; i++) {
      v[(((size_t)(0)) * (n) + (i))]= 1.0;
      p[(((size_t)(i)) * (n) + (0))]= 0.0;
      q[(((size_t)(i)) * (n) + (0))]= v[(((size_t)(0)) * (n) + (i))];
      for (j=1; j<n-1; j++) {
        p[(((size_t)(i)) * (n) + (j))]= -c / (a*p[(((size_t)(i)) * (n) + (j-1))]+b);
        q[(((size_t)(i)) * (n) + (j))]= (-d*u[(((size_t)(j)) * (n) + (i-1))]+(1.0+2.0*d)*u[(((size_t)(j)) * (n) + (i))]- f*u[(((size_t)(j)) * (n) + (i+1))]-a*q[(((size_t)(i)) * (n) + (j-1))])/(a*p[(((size_t)(i)) * (n) + (j-1))]+b);
      }

      v[(((size_t)(n-1)) * (n) + (i))]= 1.0;
      for (j=n-2; j>=1; j--) {
        v[(((size_t)(j)) * (n) + (i))]= p[(((size_t)(i)) * (n) + (j))]* v[(((size_t)(j+1)) * (n) + (i))]+ q[(((size_t)(i)) * (n) + (j))];
      }
    }
    //Row Sweep
    for (i=1; i<n-1; i++) {
      u[(((size_t)(i)) * (n) + (0))]= 1.0;
      p[(((size_t)(i)) * (n) + (0))]= 0.0;
      q[(((size_t)(i)) * (n) + (0))]= u[(((size_t)(i)) * (n) + (0))];
      for (j=1; j<n-1; j++) {
        p[(((size_t)(i)) * (n) + (j))]= -f / (d*p[(((size_t)(i)) * (n) + (j-1))]+e);
        q[(((size_t)(i)) * (n) + (j))]= (-a*v[(((size_t)(i-1)) * (n) + (j))]+(1.0+2.0*a)*v[(((size_t)(i)) * (n) + (j))]- c*v[(((size_t)(i+1)) * (n) + (j))]-d*q[(((size_t)(i)) * (n) + (j-1))])/(d*p[(((size_t)(i)) * (n) + (j-1))]+e);
      }
      u[(((size_t)(i)) * (n) + (n-1))]= 1.0;
      for (j=n-2; j>=1; j--) {
        u[(((size_t)(i)) * (n) + (j))]= p[(((size_t)(i)) * (n) + (j))]* u[(((size_t)(i)) * (n) + (j+1))]+ q[(((size_t)(i)) * (n) + (j))];
      }
    }
  }
}

int main(void)
{
  /* Retrieve problem size. */
  int n = N;
  int tsteps = TSTEPS;

  /* Variable declaration/allocation. */
  double *u = (double*)calloc((size_t)(n) * (n), sizeof(double));
  double *v = (double*)calloc((size_t)(n) * (n), sizeof(double));
  double *p = (double*)calloc((size_t)(n) * (n), sizeof(double));
  double *q = (double*)calloc((size_t)(n) * (n), sizeof(double));

  /* Initialize array(s). */
  init_array (n, u);

  /* Run kernel. */
  kernel_adi (tsteps, n, u, v, p, q);

  /* Prevent dead-code elimination. All live-out data must be printed
     by the function call in argument. */
  print_array(n, u);

  /* Be clean. */
  free((void*)u);
  free((void*)v);
  free((void*)p);
  free((void*)q);

  return 0;
}
