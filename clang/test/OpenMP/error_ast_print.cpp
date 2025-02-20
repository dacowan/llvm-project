// RUN: %clang_cc1 -verify -fopenmp -ast-print %s | FileCheck %s
// RUN: %clang_cc1 -fopenmp -x c++ -std=c++11 -emit-pch -o %t %s
// RUN: %clang_cc1 -fopenmp -std=c++11 -include-pch %t -fsyntax-only -verify %s -ast-print | FileCheck %s

// RUN: %clang_cc1 -verify -fopenmp-simd -ast-print %s | FileCheck %s
// RUN: %clang_cc1 -fopenmp-simd -x c++ -std=c++11 -emit-pch -o %t %s
// RUN: %clang_cc1 -fopenmp-simd -std=c++11 -include-pch %t -fsyntax-only -verify %s -ast-print | FileCheck %s
// expected-no-diagnostics

#ifndef HEADER
#define HEADER

void foo() {}
// CHECK: template <typename T, int N> int tmain(T argc, char **argv)
// CHECK: static int a;
// CHECK-NEXT: #pragma omp error at(execution)
// CHECK-NEXT: a = argv[0][0];
// CHECK-NEXT: ++a;
// CHECK-NEXT: #pragma omp error at(execution)
// CHECK-NEXT: {
// CHECK-NEXT: int b = 10;
// CHECK-NEXT: T c = 100;
// CHECK-NEXT: a = b + c;
// CHECK-NEXT: }
// CHECK-NEXT: #pragma omp error at(execution)
// CHECK-NEXT: foo();
// CHECK-NEXT: return N;

template <typename T, int N>
int tmain(T argc, char **argv) {
  T b = argc, c, d, e, f, g;
  static int a;
#pragma omp error at(execution)
  a = argv[0][0];
  ++a;
#pragma omp error at(execution)
  {
    int b = 10;
    T c = 100;
    a = b + c;
  }
#pragma omp  error at(execution)
  foo();
return N;
}

// CHECK: int main(int argc, char **argv)
// CHECK-NEXT: int b = argc, c, d, e, f, g;
// CHECK-NEXT: static int a;
// CHECK-NEXT: #pragma omp error at(execution)
// CHECK-NEXT: a = 2;
// CHECK-NEXT: #pragma omp error at(execution)
// CHECK-NEXT: foo();
int main (int argc, char **argv) {
  int b = argc, c, d, e, f, g;
  static int a;
#pragma omp error at(execution)
   a=2;
#pragma omp error at(execution)
  foo();
}
#endif
