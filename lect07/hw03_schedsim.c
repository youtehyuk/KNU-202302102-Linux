#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/time.h>
#include <time.h>

#define NCHILD 10

typedef enum {
    ST_READY = 0,
    ST_RUNNING,
    ST_SLEEP,
    ST_DONE
} state_t;

typedef struct {
    pid_t pid;
    state_t st;
    int tq_rem;
    int sleep_rem;
    long ready_wait_ticks;
    int in_ready_q;
} PCB;

typedef struct {
    int q[NCHILD * 8];
    int front;
    int rear;
} ReadyQ;

static PCB pcb[NCHILD];
static ReadyQ readyq;
static int TIME_QUANTUM = 3;

static volatile sig_atomic_t tick_flag = 0;
static volatile sig_atomic_t io_req_flag = 0;

static volatile sig_atomic_t child_run_flag = 0;

static void rq_init(ReadyQ *rq) {
    rq->front = 0;
    rq->rear = 0;
}

static int rq_empty(ReadyQ *rq) {
    return rq->front == rq->rear;
}

static void rq_push(ReadyQ *rq, int idx) {
    rq->q[rq->rear] = idx;
    rq->rear = (rq->rear + 1) % (NCHILD * 8);
}

static int rq_pop(ReadyQ *rq) {
    int idx = rq->q[rq->front];
    rq->front = (rq->front + 1) % (NCHILD * 8);
    return idx;
}

static int idx_by_pid(pid_t p) {
    for (int i = 0; i < NCHILD; i++) {
        if (pcb[i].pid == p) return i;
    }
    return -1;
}

static int any_not_done(void) {
    for (int i = 0; i < NCHILD; i++) {
        if (pcb[i].st != ST_DONE) return 1;
    }
    return 0;
}

static int all_tq_zero_for_notdone(void) {
    for (int i = 0; i < NCHILD; i++) {
        if (pcb[i].st != ST_DONE && pcb[i].tq_rem > 0) return 0;
    }
    return 1;
}

static void set_ready(int i) {
    if (pcb[i].st == ST_DONE) return;
    pcb[i].st = ST_READY;
    if (pcb[i].tq_rem > 0 && pcb[i].in_ready_q == 0) {
        rq_push(&readyq, i);
        pcb[i].in_ready_q = 1;
    }
}

static void set_sleep(int i, int io_time) {
    if (pcb[i].st == ST_DONE) return;
    pcb[i].st = ST_SLEEP;
    pcb[i].sleep_rem = io_time;
    pcb[i].in_ready_q = 0;
}

static void on_alarm(int sig) {
    (void)sig;
    tick_flag = 1;
}

static void on_sigchld(int sig) {
    (void)sig;
    while (1) {
        int status;
        pid_t p = waitpid(-1, &status, WNOHANG);
        if (p <= 0) break;
        int idx = idx_by_pid(p);
        if (idx >= 0) {
            pcb[idx].st = ST_DONE;
            pcb[idx].sleep_rem = 0;
            pcb[idx].in_ready_q = 0;
        }
    }
}

static void on_io_request(int sig) {
    (void)sig;
    io_req_flag = 1;
}

static void child_on_run(int sig) {
    (void)sig;
    child_run_flag = 1;
}

static void child_main(void) {
    srand((unsigned int)(getpid() ^ (unsigned int)time(NULL)));
    int cpu_burst = (rand() % 10) + 1;

    signal(SIGUSR1, child_on_run);

    while (1) {
        pause();
        if (!child_run_flag) continue;
        child_run_flag = 0;

        cpu_burst--;

        if (cpu_burst <= 0) {
            int r = rand() % 2;
            if (r == 0) {
                _exit(0);
            } else {
                kill(getppid(), SIGUSR2);
                _exit(0);
            }
        }
    }
}

static void print_tick(long tick, int running) {
    printf("[tick %ld] running=", tick);
    if (running < 0) printf("none ");
    else printf("P%02d ", running);

    printf("| ");
    for (int i = 0; i < NCHILD; i++) {
        char c = '?';
        if (pcb[i].st == ST_READY) c = 'R';
        else if (pcb[i].st == ST_RUNNING) c = 'X';
        else if (pcb[i].st == ST_SLEEP) c = 'S';
        else if (pcb[i].st == ST_DONE) c = 'D';

        if (pcb[i].st == ST_SLEEP) {
            printf("P%02d:%c(tq=%d,io=%d) ", i, c, pcb[i].tq_rem, pcb[i].sleep_rem);
        } else {
            printf("P%02d:%c(tq=%d) ", i, c, pcb[i].tq_rem);
        }
    }
    printf("\n");
    fflush(stdout);
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <TIME_QUANTUM>\n", argv[0]);
        return 1;
    }

    TIME_QUANTUM = atoi(argv[1]);
    if (TIME_QUANTUM <= 0) {
        fprintf(stderr, "TIME_QUANTUM must be positive.\n");
        return 1;
    }

    srand((unsigned int)time(NULL));
    rq_init(&readyq);

    signal(SIGALRM, on_alarm);
    signal(SIGCHLD, on_sigchld);
    signal(SIGUSR2, on_io_request);

    for (int i = 0; i < NCHILD; i++) {
        pid_t pid = fork();
        if (pid < 0) {
            perror("fork");
            return 1;
        }
        if (pid == 0) {
            child_main();
            _exit(0);
        }

        pcb[i].pid = pid;
        pcb[i].st = ST_READY;
        pcb[i].tq_rem = TIME_QUANTUM;
        pcb[i].sleep_rem = 0;
        pcb[i].ready_wait_ticks = 0;
        pcb[i].in_ready_q = 0;

        set_ready(i);
    }

    struct itimerval it;
    it.it_interval.tv_sec = 1;
    it.it_interval.tv_usec = 0;
    it.it_value.tv_sec = 1;
    it.it_value.tv_usec = 0;

    if (setitimer(ITIMER_REAL, &it, NULL) < 0) {
        perror("setitimer");
        return 1;
    }

    long tick = 0;
    int running = -1;

    while (any_not_done()) {
        while (!tick_flag) pause();
        tick_flag = 0;
        tick++;

        if (running >= 0 && pcb[running].st == ST_DONE) {
            running = -1;
        }

        for (int i = 0; i < NCHILD; i++) {
            if (pcb[i].st == ST_READY) pcb[i].ready_wait_ticks++;
        }

        for (int i = 0; i < NCHILD; i++) {
            if (pcb[i].st == ST_SLEEP) {
                pcb[i].sleep_rem--;
                if (pcb[i].sleep_rem <= 0) {
                    pcb[i].sleep_rem = 0;
                    set_ready(i);
                }
            }
        }

        if (io_req_flag) {
            io_req_flag = 0;
            if (running >= 0 && pcb[running].st == ST_RUNNING) {
                int io_time = (rand() % 5) + 1;
                set_sleep(running, io_time);
                running = -1;
            }
        }

        if (running >= 0 && pcb[running].st == ST_RUNNING) {
            pcb[running].tq_rem--;
            if (pcb[running].tq_rem <= 0) {
                pcb[running].tq_rem = 0;
                pcb[running].st = ST_READY;
                set_ready(running);
                running = -1;
            }
        }

        if (all_tq_zero_for_notdone()) {
            for (int i = 0; i < NCHILD; i++) {
                if (pcb[i].st != ST_DONE) {
                    pcb[i].tq_rem = TIME_QUANTUM;
                    if (pcb[i].st == ST_READY) set_ready(i);
                }
            }
        }

        if (running < 0) {
            while (!rq_empty(&readyq)) {
                int cand = rq_pop(&readyq);
                pcb[cand].in_ready_q = 0;

                if (pcb[cand].st == ST_READY && pcb[cand].tq_rem > 0) {
                    running = cand;
                    pcb[running].st = ST_RUNNING;
                    break;
                }
            }
        }

        print_tick(tick, running);

        if (running >= 0 && pcb[running].st == ST_RUNNING) {
            kill(pcb[running].pid, SIGUSR1);
        }
    }

    it.it_interval.tv_sec = 0;
    it.it_interval.tv_usec = 0;
    it.it_value.tv_sec = 0;
    it.it_value.tv_usec = 0;
    setitimer(ITIMER_REAL, &it, NULL);

    long sum = 0;
    for (int i = 0; i < NCHILD; i++) sum += pcb[i].ready_wait_ticks;
    double avg = (double)sum / (double)NCHILD;

    printf("\n=== RESULT ===\n");
    printf("TIME_QUANTUM = %d\n", TIME_QUANTUM);
    printf("Total ticks = %ld\n", tick);
    printf("Average READY wait (ticks) = %.2f\n", avg);

    return 0;
}

