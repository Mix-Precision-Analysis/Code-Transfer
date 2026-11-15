#ifndef CLEANUP_UNUSED_STUBS_H
#define CLEANUP_UNUSED_STUBS_H

#include <math.h>
#include <float.h>
#include <stdio.h>

typedef unsigned int gsl_mode_t;

typedef struct gsl_sf_result_struct
{
    double val;
    double err;
} gsl_sf_result;

typedef struct gsl_sf_result_e10_struct
{
    double val;
    double err;
    int e10;
} gsl_sf_result_e10;

typedef struct gsl_sum_levin_u_workspace
{
    double sum_plain;
    size_t terms_used;
    double *dsum;
    double *q_den;
    double *q_num;
    double *dq_den;
    double *dq_num;
} gsl_sum_levin_u_workspace;

#define GSL_SUCCESS 0
#define GSL_FAILURE -1
#define GSL_EDOM 1

#define GSL_PREC_DOUBLE 0
#define GSL_MODE_PREC(mode) (mode)

#define GSL_DBL_EPSILON DBL_EPSILON
#define GSL_MACH_EPS DBL_EPSILON
#define GSL_ROOT3_DBL_EPSILON DBL_EPSILON
#define GSL_ROOT4_DBL_EPSILON (sqrt(sqrt(DBL_EPSILON)))
#define GSL_ROOT5_DBL_EPSILON (pow(DBL_EPSILON, 0.2))
#define GSL_ROOT6_DBL_EPSILON (pow(DBL_EPSILON, 1.0 / 6.0))
#define GSL_DBL_MIN DBL_MIN
#define GSL_DBL_MAX DBL_MAX
#define GSL_NAN NAN
#define GSL_POSINF INFINITY
#define GSL_NEGINF (-INFINITY)
#define GSL_LOG_DBL_MAX (log(DBL_MAX))
#define GSL_LOG_DBL_MIN (log(DBL_MIN))
#define GSL_LOG_DBL_EPSILON (log(DBL_EPSILON))
#define GSL_SQRT_DBL_MAX (sqrt(DBL_MAX))
#define GSL_SQRT_DBL_MIN (sqrt(DBL_MIN))

#define GSL_MAX(a, b) ((a) > (b) ? (a) : (b))
#define GSL_MIN(a, b) ((a) < (b) ? (a) : (b))
#define GSL_MAX_DBL(a, b) ((a) > (b) ? (a) : (b))
#define GSL_SQRT_DBL_EPSILON (sqrt(DBL_EPSILON))

#define GSL_SIGN(x) (((x) > 0) - ((x) < 0))

#define GSL_ERROR(msg, code) ((void)(msg), (code))
#define GSL_ERROR_VAL(msg, code, val) ((void)(msg), (void)(val), (code))
#define GSL_ERROR_SELECT_2(...) (0)
#define GSL_ERROR_SELECT_3(...) (0)
#define GSL_ERROR_SELECT_4(...) (0)

#define GSL_EUNDRFLW 15
#define GSL_EMAXITER 16
#define GSL_EOVRFLW 17
#define GSL_ESANITY 18
#define GSL_IS_ODD(n) (((n) & 1) != 0)

#define GSL_SF_GAMMA_XMAX 171.0

#ifndef M_SQRT3
#define M_SQRT3 1.7320508075688772935
#endif

static inline int gsl_sf_exp_mult_err_e(double lnpre_val, double lnpre_err,
                                        double x, double x_err,
                                        gsl_sf_result *result)
{
    (void)lnpre_val;
    (void)lnpre_err;
    (void)x;
    (void)x_err;
    if (result)
    {
        result->val = 0.0;
        result->err = 0.0;
    }
    return 0;
}

int gsl_sf_expm1_e(double x, gsl_sf_result *result);
int gsl_sf_log_1plusx_e(double x, gsl_sf_result *result);
int gsl_sf_log_1plusx_e_f(float x, gsl_sf_result *result);
#ifndef gsl_sf_log_1plusx_e_dispatch
#define gsl_sf_log_1plusx_e_dispatch(x, result) \
    _Generic((x), float: gsl_sf_log_1plusx_e_f, double: gsl_sf_log_1plusx_e, long double: gsl_sf_log_1plusx_e)(x, result)
#endif
int gsl_sf_gammainv_e(double x, gsl_sf_result *result);
int gsl_sf_lngamma_e(double x, gsl_sf_result *result);
int gsl_sf_lngamma_sgn_e(double x, gsl_sf_result *result, double *sgn);
/* Intentionally omit lnpoch/pochrel prototypes to avoid signature conflicts
 * with mixed-precision definitions in generated sources. */
int gsl_sf_laguerre_2_e(double a, double x, gsl_sf_result *result);

gsl_sum_levin_u_workspace *gsl_sum_levin_u_alloc(const size_t n);
void gsl_sum_levin_u_free(gsl_sum_levin_u_workspace *w);

static int stat_tc;
static int stat_lg2;
static int stat_1;
static int stat_2;
static int stat_e;
static int stat;
static int stat_apn;
static int stat_a;
static int stat_poch;
static double p8;

extern double *y;
extern double *dy;

#define px11 0.0
#define px5 0.0

#endif
