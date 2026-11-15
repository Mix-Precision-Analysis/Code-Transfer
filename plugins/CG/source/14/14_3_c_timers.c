#include "wtime.h"
#include <stdlib.h>

/*  Prototype  */
void wtime( double * );


/*****************************************************************/
/******         E  L  A  P  S  E  D  _  T  I  M  E          ******/
/*****************************************************************/
static void wtime__abi_1(float * p0)
{
    double p0_tmp = (double)(*p0);
    wtime_(&p0_tmp);
    *p0 = (float)p0_tmp;
}


float elapsed_time_1();


double elapsed_time( void )
{
    double t;

    wtime( &t );
    return( t );
}

float elapsed_time_1()
{
    float t;

    wtime__abi_1( &t );
    return( t );
}




float start[64], elapsed[64];

/*****************************************************************/
/******            T  I  M  E  R  _  C  L  E  A  R          ******/
/*****************************************************************/
void timer_clear( int n )
{
    elapsed[n] = 0.0;
}


/*****************************************************************/
/******            T  I  M  E  R  _  S  T  A  R  T          ******/
/*****************************************************************/
void timer_start( int n )
{
    start[n] = elapsed_time_1();
}


/*****************************************************************/
/******            T  I  M  E  R  _  S  T  O  P             ******/
/*****************************************************************/
void timer_stop( int n )
{
    float t; float now;

    now = elapsed_time_1();
    t = now - start[n];
    elapsed[n] += t;

}


/*****************************************************************/
/******            T  I  M  E  R  _  R  E  A  D             ******/
/*****************************************************************/
double timer_read( int n )
{
    return( elapsed[n] );
}

