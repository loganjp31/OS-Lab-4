/*
 * Lab 4 - Scheduler
 *
 * IMPORTANT:
 * - Do NOT change print_log() format (autograder / TA diff expects exact output).
 * - Do NOT change the order of operations in the main tick loop.
 * - You may change internal implementations of the TODO functions freely,
 *   as long as behavior matches the lab requirements.
 * - compile: $make
 *   run testcase: $./groupX_scheduler < test_input.txt
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "queue.h"

/*
 * Assumptions / Lab rules:
 * - user priority values are 0,1,2,3 (0 = Real-Time, 1..3 = user queues)
 * - all processes have mem_req > 0
 * - RT process memory is ALWAYS 64MB (reserved); user processes share 960MB
 * - user memory allocation range is [64, 1023], integer MB, contiguous blocks
 * - continuous allocation policy: First Fit (per modified handout)
 * - Processes are sorted by arrival_time in the test files
 */

/* ----------------------------
 * Global “hardware resources”
 * ---------------------------- */
int printers = 2;
int scanners = 1;
int modems = 1;
int cd_drives = 2;

/* Total user-available memory (excluding RT reserved region) */
int memory = 960;
int memory_real_time = 64;

/* ----------------------------
 * Ready queues (provided by queue.h / queue.c)
 * ---------------------------- */
queue_t rt_queue;
queue_t sub_queue;
queue_t user_queue[3];

/* ---------------------------- */
typedef struct free_block free_block_t;
free_block_t *freelist;

#define MAX_PROCESSES 128

typedef enum {
    NEW,
    SUBMITTED,
    READY,
    RUNNING,
    TERMINATED
} proc_state_t;

typedef struct process {
    int pid;

    int arrival_time;
    int init_prio;
    int cpu_total;
    int mem_req;
    int printers;
    int scanners;
    int modems;
    int cds;

    int cpu_remain;
    int current_prio;
    proc_state_t state;

    int mem_start;
} process_t;

typedef struct free_block {
    int start;
    int size;
    struct free_block *next;
} free_block_t;

/* =========================================================
 * MEMORY
 * ========================================================= */

void memory_init(void) {
    freelist = (free_block_t*)malloc(sizeof(free_block_t));
    freelist->start = 64;
    freelist->size = 960;
    freelist->next = NULL;
}

int memory_can_allocate(int req_size){
    free_block_t *curr = freelist;
    while (curr != NULL) {
        if (curr->size >= req_size) return 1;
        curr = curr->next;
    }
    return 0;
}

int memory_allocate(process_t *p) {
    free_block_t *curr = freelist;
    free_block_t *prev = NULL;

    while (curr != NULL) {
        if (curr->size >= p->mem_req) {
            p->mem_start = curr->start;

            curr->start += p->mem_req;
            curr->size -= p->mem_req;

            if (curr->size == 0) {
                if (prev == NULL) freelist = curr->next;
                else prev->next = curr->next;
                free(curr);
            }
            return 0;
        }
        prev = curr;
        curr = curr->next;
    }
    return -1;
}

/* ✅ FIXED: Fully coalescing memory blocks */
int memory_free(process_t *p)
{
    free_block_t *new_block = (free_block_t *)malloc(sizeof(free_block_t));
    new_block->start = p->mem_start;
    new_block->size = p->mem_req;
    new_block->next = NULL;

    free_block_t *prev = NULL;
    free_block_t *curr = freelist;

    /* Insert in sorted order */
    while (curr != NULL && curr->start < new_block->start)
    {
        prev = curr;
        curr = curr->next;
    }

    new_block->next = curr;

    if (prev == NULL)
        freelist = new_block;
    else
        prev->next = new_block;

    /* ✅ Iteratively merge with adjacent blocks */
    int merged;

    do
    {
        merged = 0;

        free_block_t *cur = freelist;

        while (cur != NULL && cur->next != NULL)
        {
            if (cur->start + cur->size == cur->next->start)
            {
                free_block_t *next = cur->next;
                cur->size += next->size;
                cur->next = next->next;
                free(next);
                merged = 1;
            }
            else
            {
                cur = cur->next;
            }
        }

    } while (merged);

    return 0;
}

/* ---- Resource helpers ---- */
int resource_available(process_t *p) {
    return (p->printers <= printers &&
            p->scanners <= scanners &&
            p->modems   <= modems &&
            p->cds      <= cd_drives);
}

void resource_occupy(process_t *p) {
    printers  -= p->printers;
    scanners  -= p->scanners;
    modems    -= p->modems;
    cd_drives -= p->cds;
}

void resource_free(process_t *p) {
    printers  += p->printers;
    scanners  += p->scanners;
    modems    += p->modems;
    cd_drives += p->cds;
}

/* ---- Arrival ---- */
void arrival(process_t *p) {
    if (p->init_prio == 0) {
        p->state = READY;
        p->mem_start = 0;
        queue_push(&rt_queue, p);
    } else {
        p->state = SUBMITTED;
        queue_push(&sub_queue, p);
    }
}

/* ---- Admit ---- */
void admit_process(void) {
    while (!queue_empty(&sub_queue)) {
        process_t *p = queue_peek(&sub_queue);

        if (memory_can_allocate(p->mem_req) && resource_available(p)) {
            queue_pop(&sub_queue);

            memory_allocate(p);
            resource_occupy(p);

            p->state = READY;
            queue_push(&user_queue[p->init_prio - 1], p);
        } else {
            break;
        }
    }
}

/* ---- Dispatch ---- */
process_t *dispatch(process_t **cur_running_rt) {

    if (*cur_running_rt != NULL) {
        if ((*cur_running_rt)->cpu_remain > 0) {
            return *cur_running_rt;
        } else {
            *cur_running_rt = NULL;
        }
    }

    while (!queue_empty(&rt_queue)) {
        process_t *p = queue_pop(&rt_queue);
        if (p->cpu_remain > 0) {
            *cur_running_rt = p;
            return p;
        }
    }

    for (int i = 0; i < 3; i++) {
        while (!queue_empty(&user_queue[i])) {
            process_t *p = queue_pop(&user_queue[i]);
            if (p->cpu_remain > 0) {
                return p;
            }
        }
    }

    return NULL;
}

void run_process(process_t *p) {
    if (!p) return;
    p->state = RUNNING;
    p->cpu_remain--;
}

void post_run(process_t *p, process_t **cur_running_rt) {
    if (p == NULL) return;

    if (p->cpu_remain <= 0) {
        p->state = TERMINATED;

        if (p->init_prio != 0) {
            memory_free(p);
            resource_free(p);
        }

        if (p->init_prio == 0) {
            *cur_running_rt = NULL;
        }

        return;
    }

    if (p->init_prio == 0) {
        *cur_running_rt = p;
        return;
    }

    if (p->current_prio < 3) {
        p->current_prio++;
    }

    p->state = READY;
    queue_push(&user_queue[p->current_prio - 1], p);
}

int termination_check(int processNo, int process_count, process_t *cur_running_rt) {
    return  processNo == process_count  &&
            cur_running_rt == NULL      &&
            queue_empty(&rt_queue)      &&
            queue_empty(&sub_queue)     &&
            queue_empty(&user_queue[0]) &&
            queue_empty(&user_queue[1]) &&
            queue_empty(&user_queue[2]);
}

void print_log(process_t *p, int time) {
    if (!p) {
        printf("[t=%d] IDLE\n", time);
    } else {
        printf("[t=%d] RUN PID=%d PR=%d CPU=%d MEM_ST=%d MEM=%d P=%d S=%d M=%d C=%d\n",
            time,
            p->pid,
            p->current_prio,
            p->cpu_remain,
            p->mem_start,
            p->mem_req,
            p->printers,
            p->scanners,
            p->modems,
            p->cds);
    }
}

int main(void) {
    queue_init(&rt_queue);
    queue_init(&sub_queue);
    for (int i = 0; i < 3; i++) queue_init(&user_queue[i]);

    memory_init();

    process_t processes[MAX_PROCESSES];
    int process_count = 0;

    while (process_count < MAX_PROCESSES) {
        int a,p,cpu,mem,pr,sc,mo,cd;
        if (scanf("%d %d %d %d %d %d %d %d",
                  &a,&p,&cpu,&mem,&pr,&sc,&mo,&cd) != 8) break;

        processes[process_count] = (process_t){
            .pid = process_count,
            .arrival_time = a,
            .init_prio = p,
            .cpu_total = cpu,
            .mem_req = mem,
            .printers = pr,
            .scanners = sc,
            .modems = mo,
            .cds = cd,
            .cpu_remain = cpu,
            .current_prio = p,
            .state = NEW,
            .mem_start = 0
        };

        process_count++;
    }

    int processNo = 0;
    process_t *cur_running_rt = NULL;

    for (int time = 0;; time++) {
        for (; processNo < process_count; processNo++) {
            if (processes[processNo].arrival_time == time)
                arrival(&processes[processNo]);
            else break;
        }

        admit_process();

        process_t *p = dispatch(&cur_running_rt);

        run_process(p);
        print_log(p, time);
        post_run(p, &cur_running_rt);

        if (termination_check(processNo, process_count, cur_running_rt))
            break;
    }

    return 0;
}