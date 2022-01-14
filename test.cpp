#include <iostream>
#include "easyantileak.hpp"

int main()
{
    setUpTrace(&std::cout);
    int *p1 = new int;

    int *p2 = new int[10];

    int *p3 = ::operator new(int);

    delete[] p2;
    delete p1;
}
