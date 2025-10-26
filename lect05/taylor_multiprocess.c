#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <string.h>
#include <math.h>

#define N 8          // 전체 데이터 개수
#define TERMS 5      // 테일러 급수 항 개수
#define NUM_CHILD 2  // 자식 프로세스 개수
#define MAXLINE 256

// sin(x) 테일러 급수 계산 함수
void sinx_taylor(int num_elements, int terms, double* x, double* result) {
    for (int i = 0; i < num_elements; i++) {
        double value = x[i];
        double numer = x[i] * x[i] * x[i];
        double denom = 6.0; // 3!
        int sign = -1;

        for (int j = 1; j <= terms; j++) {
            value += (double)sign * numer / denom;
            numer *= x[i] * x[i];
            denom *= (2.0 * (double)j + 2.0) * (2.0 * (double)j + 3.0);
            sign *= -1;
        }
        result[i] = value;
    }
}

int main() {
    int fd[2];
    pid_t pid;
    double x[N] = {0, M_PI/8, M_PI/6, M_PI/4, M_PI/3, M_PI/2, M_PI*2/3, M_PI};
    double result[N];
    int range = N / NUM_CHILD; // 각 자식이 맡을 구간 크기

    if (pipe(fd) == -1) {
        perror("pipe");
        exit(1);
    }

    for (int i = 0; i < NUM_CHILD; i++) {
        pid = fork();

        if (pid < 0) {
            perror("fork");
            exit(1);
        }

        // 자식 프로세스
        if (pid == 0) {
            close(fd[0]); // 읽기 닫기
            int start = i * range;
            int end = (i == NUM_CHILD - 1) ? N : start + range;

            double partial_result[range];
            sinx_taylor(end - start, TERMS, &x[start], partial_result);

            // 부모에게 결과 전달
            write(fd[1], partial_result, sizeof(double) * (end - start));

            close(fd[1]);
            exit(0);
        }
    }

    // 부모 프로세스
    close(fd[1]); // 쓰기 닫기
    int status;

    // 자식들의 결과를 순서대로 수신
    int received = 0;
    while (received < N) {
        int n = read(fd[0], &result[received], sizeof(double) * range);
        if (n > 0) {
            received += n / sizeof(double);
        } else break;
    }

    close(fd[0]);

    // 모든 자식 종료 대기
    for (int i = 0; i < NUM_CHILD; i++) {
        wait(&status);
    }

    // 결과 출력
    printf("=== Taylor Series sin(x) 결과 ===\n");
    for (int i = 0; i < N; i++) {
        printf("x=%.3f → sin(x)≈%f | 실제 sin(x)=%f\n", x[i], result[i], sin(x[i]));
    }

    return 0;
}

