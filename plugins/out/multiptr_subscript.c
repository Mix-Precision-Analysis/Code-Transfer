void zero3(double*** z, int n1, int n2, int n3) {
    z[0][0][0] = 0.0;
}

int main(void) {
    int lt = 1;
    double ****u = 0;
    zero3(u[lt], 1, 1, 1);
    return 0;
}
