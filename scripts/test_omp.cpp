#include <iostream>
#include <omp.h>
int main() {
    int nthreads = 0;
    #pragma omp parallel
    {
        #pragma omp single
        nthreads = omp_get_num_threads();
    }
    std::cout << "OpenMP is active with " << nthreads << " threads\n";
    return 0;
}
