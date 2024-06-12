#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <pthread.h>
#include "options.h"

#define DELAY_SCALE 1000


struct array {
    int size;
    int *arr;
    pthread_mutex_t *arrMutex; //Mutex para cada posicion del array
    pthread_mutex_t iterMutex; //Mutex para controlar las iteraciones totales
    int currentIterations; //Variable que almacena iteraciones totales (contador compartido)
    int operationsA; //Variable que almacena iteraciones de increment
    int operationsB; //Variable que almacena iteraciones de cambios
};

struct args { //Argumentos de cada thread
    int thread_num;
    int delay;
    int iterations;
    struct array *array;
};

struct thread_info {
    pthread_t    id;
    struct args *args;
};

void apply_delay(int delay) {
    for(int i = 0; i < delay * DELAY_SCALE; i++); // waste time
}

void print_array(struct array arr) {
    int total = 0;

    for(int i = 0; i < arr.size; i++) {
        total += arr.arr[i];
        printf("%d ", arr.arr[i]);
    }

    printf("\nTotal: %d\n", total);
    printf("\nTotal de iteraciones: %d, %d increments + %d changes\n", arr.currentIterations,arr.operationsA,arr.operationsB);

}


void *increment(void *ptr){

    struct args *args = ptr;
    int i, pos, val;


    for(i = 0; i < args->iterations; i++) {

        //Si se alcanzan las iteraciones especificadas salimos del bucle
        if (args->array->operationsA == args->iterations) {
            break;
        }

        //Bloqueamos contador y hacemos suma de iteraciones
        pthread_mutex_lock(&args->array->iterMutex);
        args->array->operationsA++;
        args->array->currentIterations = args->array->operationsA+args->array->operationsB;
        pthread_mutex_unlock(&args->array->iterMutex);

        pos = rand() % args->array->size;

        //Bloqueamos la seccion critica
        pthread_mutex_lock(&args->array->arrMutex[pos]);
        printf("Thread %d increasing position %d\n", args->thread_num, pos);

        val = args->array->arr[pos];
        apply_delay(args->delay);

        val ++;
        apply_delay(args->delay);

        args->array->arr[pos] = val;

        pthread_mutex_unlock(&args->array->arrMutex[pos]);
        apply_delay(args->delay);

    }

    return NULL;
}

void *change_values(void *ptr){

    struct args *args = ptr;
    int i, pos, val, pos2, val2;

    for(i = 0; i < args->iterations; i++) {

        //Comprobamos si ya se han hecho las iteraciones indicadas
        if (args->array->operationsB == args->iterations) {
            break;
        }

        pthread_mutex_lock(&args->array->iterMutex);
        args->array->operationsB++;
        args->array->currentIterations = args->array->operationsA+args->array->operationsB;
        pthread_mutex_unlock(&args->array->iterMutex);

        pos = rand() % args->array->size;
        pos2 = rand() % args->array->size;

        if (pos == pos2) { //En caso de ser iguales elegimos otro pos2
            pos2 = (pos2 + 1) % args->array->size;
        }

        /*Utilizamos una reserva ordenada para prevenir deadlocks, por ejemplo
          el thread X bloquea "pos" y llega otro thread Y que bloquea "pos2" cuando X
          intente bloquear "pos2" no podrá, lo mismo sucederá si Y intenta bloquear
          "pos". De esta forma todos los hilos bloquean en el mismo orden evitando
          esta situación. */

        if (pos < pos2) {
            pthread_mutex_lock(&args->array->arrMutex[pos]);
            pthread_mutex_lock(&args->array->arrMutex[pos2]);
        } else {
            pthread_mutex_lock(&args->array->arrMutex[pos2]);
            pthread_mutex_lock(&args->array->arrMutex[pos]);
        }

        printf("Thread %d decreasing pos %d increasing pos %d\n", args->thread_num, pos, pos2);

        val = args->array->arr[pos];
        apply_delay(args->delay);

        val--;
        apply_delay(args->delay);

        args->array->arr[pos] = val;
        apply_delay(args->delay);

        val2 = args->array->arr[pos2];
        apply_delay(args->delay);

        val2++;
        apply_delay(args->delay);
        args->array->arr[pos2] = val2;

        pthread_mutex_unlock(&args->array->arrMutex[pos2]);
        pthread_mutex_unlock(&args->array->arrMutex[pos]);
        apply_delay(args->delay);

    }

    return NULL;
}




struct thread_info *start_threads(struct options opt, struct array *array){

    int i,x;
    struct thread_info *threads;
    int thr = opt.num_threads;

    printf("Creating %d threads for increment and %d threads for change\n", opt.num_threads, opt.num_threads);
    threads = malloc(sizeof(struct thread_info) * opt.num_threads * 2);

    if (threads == NULL) {
        printf("Not enough memory\n");
        exit(1);
    }

    // Creamos los hilos para increment
    for (i = 0; i < opt.num_threads; i++) {

        threads[i].args = malloc(sizeof(struct args));

        threads[i].args -> thread_num = i;
        threads[i].args -> array      = array;
        threads[i].args -> delay      = opt.delay;
        threads[i].args -> iterations = opt.iterations;


        if (0 != pthread_create(&threads[i].id, NULL, increment, threads[i].args)) {
            printf("Could not create thread #%d\n", i);
            exit(1);
        }

    }

    //Creamos los hilos para change
    for (x=0; x<opt.num_threads; x++){
        threads[x+thr].args = malloc(sizeof(struct args));

        threads[x+thr].args -> thread_num = x+thr;
        threads[x+thr].args -> array      = array;
        threads[x+thr].args -> delay      = opt.delay;
        threads[x+thr].args -> iterations = opt.iterations;

        if (0 != pthread_create(&threads[x+thr].id, NULL, change_values, threads[x+thr].args)) {
            printf("Could not create thread #%d\n", x+thr);
            exit(1);
        }

    }

    return threads;
}

void wait(struct options opt, struct array *array, struct thread_info *threads) {
    // Esperamos a que los hilos terminen
    int i;

    for (i = 0; i < opt.num_threads*2; i++) {
        pthread_join(threads[i].id, NULL);
    }

    print_array(*array);

    for (i = 0; i < opt.num_threads * 2; i++) {
        free(threads[i].args);
    }

    free(array->arrMutex);
    free(threads);
    free(array->arr);
}

void init(struct array *array) {

    array->arrMutex = malloc(array->size*sizeof(pthread_mutex_t));  //Reservar espacio de memoria para cada mutex
    pthread_mutex_init(&array->iterMutex, NULL);
    array->currentIterations=0;
    array->operationsA=0;
    array->operationsB=0;

    int i;
    for(i=0; i < array->size; i++){  //Inicializar mutex a NULL
        pthread_mutex_init(&array->arrMutex[i], NULL);
    }

}

int main (int argc, char **argv)
{
    struct options       opt;
    struct array         arr;
    struct thread_info *thrs;


    srand(time(NULL));

    // Default values for the options
    opt.num_threads  = 5;
    opt.size         = 10;
    opt.iterations   = 100;
    opt.delay        = 1000;

    read_options(argc, argv, &opt);

    arr.size = opt.size;
    arr.arr  = malloc(arr.size * sizeof(int));

    memset(arr.arr, 0, arr.size * sizeof(int));

    init(&arr);
    thrs = start_threads(opt, &arr);

    wait(opt, &arr, thrs);

    return 0;
}