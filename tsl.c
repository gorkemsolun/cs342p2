#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <malloc.h>
#include <assert.h>

/* We want the extra information from these definitions */
#ifndef __USE_GNU
#define __USE_GNU
#endif /* __USE_GNU */

#include <ucontext.h>
#include "tsl.h"

// you will implement your library in this file. 
// you can define your internal structures, macros here. 
// such as: #define ...
// if you wish you can use another header file (not necessary). 
// but you should not change the tsl.h header file. 
// you can also define and use global variables here.
// you can define and use additional functions (as many as you wish) 
// in this file; besides the tsl library functions desribed in the assignment. 
// these additional functions will not be available for applications directly. 

// TCB structure
typedef struct TCB {
    int tid; // thread identifier
    unsigned int state; // thread state
    ucontext_t context; // pointer to context structure

    /*
        typedef struct ucontext_t
        {
        unsigned long int __ctx(uc_flags);
        struct ucontext_t *uc_link;
        stack_t uc_stack;
        mcontext_t uc_mcontext;
        sigset_t uc_sigmask;
        struct _libc_fpstate __fpregs_mem;
        } ucontext_t;
    */

    char* stack; // pointer to stack
} TCB;

// Queue implementation
typedef struct QueueNode {
    TCB* tcb; // pointer to TCB
    struct QueueNode* next; // pointer to next node
} QueueNode;

typedef struct tcbQueue {
    QueueNode* front; // pointer to front of the queue
    QueueNode* rear; // pointer to rear of the queue
} tcbQueue;

tcbQueue* createTCBQueue() {
    tcbQueue* queue = (tcbQueue*)malloc(sizeof(tcbQueue));
    queue->front = NULL;
    queue->rear = NULL;
    return queue;
}

// Create a new ready queue
void enqueue(tcbQueue* queue, TCB* tcb) {
    QueueNode* newNode = (QueueNode*)malloc(sizeof(QueueNode));
    newNode->tcb = tcb;
    newNode->next = NULL;

    if (queue->rear == NULL) {
        queue->front = newNode;
        queue->rear = newNode;
    } else {
        queue->rear->next = newNode;
        queue->rear = newNode;
    }
}

// Dequeue from the queue
TCB* dequeue(tcbQueue* queue) {
    if (queue->front == NULL) {
        return NULL;
    }

    QueueNode* temp = queue->front;
    TCB* tcb = temp->tcb;
    queue->front = queue->front->next;

    if (queue->front == NULL) {
        queue->rear = NULL;
    }

    free(temp);
    return tcb;
}

// Check if the queue is empty
int isReadyQueueEmpty(tcbQueue* queue) {
    return queue->front == NULL;
}

// Queue implementation end

// Define states
#define TSL_RUNNING 1
#define TSL_READY 2

// Global variables

int currentThreadCount = 0; // Current number of threads
tcbQueue* readyQueue; // ready queue
tcbQueue* runningQueue; // running queue
int next_tid = 1; // next thread id
int tsl_init(int salg);
int tsl_create_thread(void (*tsf)(void*), void* targ);
int tsl_yield(int tid);
int tsl_exit();
int tsl_join(int tid);
int tsl_cancel(int tid);
int tsl_gettid();
TCB* removeFromQueue(tcbQueue* queue, int tid);

// This is the main function of the library and library will be initialized here
// This function will be called exatcly once
// salg is the scheduling algorithm to be used
int tsl_init(int salg) {

    readyQueue = createTCBQueue(); //createReadyQueue();
    runningQueue = createTCBQueue(); //createRunningQueue();

    // Add current(main) thread's TCB to running queue
    TCB* mainTCB = (TCB*)malloc(sizeof(TCB));
    mainTCB->tid = next_tid++; // Assuming main thread has tid 1
    mainTCB->state = TSL_RUNNING; // Set state to running
    mainTCB->stack = NULL; // No stack for main thread
    enqueue(runningQueue, mainTCB);

    int success = 1;// TODO: Success should be handled
    if (success) {
        return 0;
    } else {
        return TSL_ERROR;
    }

}

// creates a new thread
// tsf is the function(the thread start function or root function) to be executed by the new thread
// The application will define this thread start function, and its address will be the first argument
// Function can take one argument of type void*
// targ is a pointer to a value or structure that will be passed to the thread start function, if nothing is to be passed, it will be NULL
// The function will return the thread id(tid) of the new thread, unique among all threads in the application
int tsl_create_thread(void (*tsf)(void*), void* targ) {
    if (currentThreadCount >= TSL_MAXTHREADS) {
        return TSL_ERROR; // Maximum number of threads reached
    }

    // Create a new TCB for the new thread
    TCB* newTCB = (TCB*)malloc(sizeof(TCB));
    newTCB->tid = next_tid++;
    newTCB->state = TSL_READY;
    newTCB->stack = malloc(TSL_STACKSIZE); // TODO: this should be checked // Allocate memory for the stack

    enqueue(readyQueue, newTCB); // Add the new thread to the ready queue

    currentThreadCount++; // Increment the number of threads

    // Create a new context for the new thread
    getcontext(&newTCB->context);

    // TODO: this should be checked
    newTCB->context.uc_mcontext.gregs[REG_EIP] = (unsigned int)stub; // Set the instruction pointer to the stub function
    newTCB->context.uc_mcontext.gregs[REG_ESP] = (unsigned int)newTCB->stack; // Set the stack pointer to the top of the stack
    newTCB->stack += TSL_STACKSIZE; // Move the stack pointer to the bottom of the stack

    // TODO: this should be checked // Initialize the stack by putting tsl and targ into the stack
    unsigned int* stack_ptr = (unsigned int*)newTCB->context.uc_mcontext.gregs[REG_ESP];
    *stack_ptr-- = (unsigned int)targ; // Push targ onto the stack
    *stack_ptr-- = (unsigned int)tsf; // Push tsf onto the stack

    int success = 1; // TODO: Success should be handled
    if (success) {
        return newTCB->tid;
    } else {
        return TSL_ERROR;
    }
}


// TODO: STUB should be implemented
// TODO: STUB SHOULD BE PUT IN THE CORRECT PLACE
// stub wrapper function for the thread start function
void stub(void (*tsf) (void*), void* targ) {
    // new thread will start its execution here
    tsf(targ); // then we will call the thread start function

    // tsf will retun to here
    tsl_exit(); // now ask for termination
}

// yields the processor to another thread, a context switch will occur
// tid is the thread id of the thread to which the processor will be yielded
// if tid is TSL_ANY, the processor will be yielded to any thread selected by the scheduling algorithm from the threads that are ready to run
// selected threads context will be loaded and it will start to execute
// if there is no ready thread to run other than calling thread, scheduler will select the calling thread to run next
// save calling threads context then restore it later
// if tid > 0, but no ready thread with the given tid, the function will return TSL_ERROR
// this function will not return until the calling thread is scheduled to run again
int tsl_yield(int tid) {
    int next_tid = 0; // TODO: select the next thread to run


    // TODO: not return until the calling thread is scheduled to run again
    int success = 1;
    if (success) {
        return next_tid;
    } else {
        return TSL_ERROR; // TODO: no ready thread with the given tid
    }
}

// terminates the calling thread
// TCB and stack of the calling thread will be kept until another thread calls tsl_join with the tid of the calling thread
// this function will not return
int tsl_exit() {
    return (0);
}

// waits for the termination of another thread with the given tid, then returns
// before returning, the TCB and stack of the terminated thread will be deallocated
int tsl_join(int tid) {
    int success = 1;
    if (success) {
        return tid;
    } else {
        return TSL_ERROR; // TODO: no thread with the given tid
    }
}

// cancels another thread with the given tid asynchronously, the target thread will be immediately terminated
int tsl_cancel(int tid) {

    TCB* tcb = removeFromQueue(readyQueue, tid); //if does not exist in ready queue, it will return NULL
    if(tcb == NULL){
        tcb = removeFromQueue(runningQueue, tid); 
        if (tcb == NULL) {
            return TSL_ERROR; 
        }
    }
    
    free(tcb->stack);
    free(tcb);

    return TSL_SUCCESS;
}

// helper function to remove a thread from the queue
// finds the thread id of the calling thread and removes it
// returns the TCB pointer of removed thread, returns NULL if tcbQueue is empty

TCB* removeFromQueue(tcbQueue* queue, int tid){
    if(queue == NULL || queue->front == NULL){
        return NULL;
    }

    QueueNode* iterator = queue->front;
    QueueNode* prev = NULL;

    while(iterator != NULL){
        if(iterator->tcb->tid == tid){
            if(prev == NULL){
                queue->front = iterator->next;
            } else {
                prev->next = iterator->next;
            }

            if(iterator->next == NULL){
                queue->rear = prev;
            }

            TCB* tcb = iterator->tcb;
            free(iterator);
            return tcb;
        }

        prev = iterator;
        iterator = iterator->next;
    }

    return NULL;

}

// returns the thread id of the calling thread
// returns 0 if runningQueue is empty
int tsl_gettid() {
    if(runningQueue != NULL && runningQueue->front != NULL) {
        return runningQueue->front->tcb->tid;
    }

    return 0;
}