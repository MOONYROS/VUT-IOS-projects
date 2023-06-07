/**
 * @file proj2.c
 *
 * @author  Lukasek Ondrej <xlukas15@stud.fit.vutbr.cz>
 * 
 * IOS - projekt 2 (synchronizace)
 * Tvorba molekul vody z front procesu kysliku a vodiku.
 * Projekt vyuziva procesy, sdilenou pamet a semafory pro synchornizaci.
 * 
 */

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <semaphore.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <sys/mman.h>
#include <ctype.h>

#define ERRMSG_NO_INVALID   "The first paramter (NO) is invalid! Only numerical digits are allowed.\n"
#define ERRMSG_NH_INVALID   "The second paramter (NH) is invalid! Only numerical digits are allowed.\n"
#define ERRMSG_TI_INVALID   "The tird paramter (TI) is invalid! Only numerical digits are allowed.\n"
#define ERRMSG_TI_OORANGE   "The third paramter (TI) is invalid! Range 0<=TI<=1000 is required.\n"
#define ERRMSG_TB_INVALID   "The fourth paramter (TB) is invalid! Only numerical digits are allowed.\n"
#define ERRMSG_TB_OORANGE   "The fourth paramter (TB) is invalid! Range 0<=TB<=1000 is required.\n"
#define ERRMSG_WRONG_NRPAR  "Wrong number of input parameters!\n"
#define MSG_USAGE           "usage: ./proj2 NO NH TI TB"
#define ERRMSG_CANNOT_WRITE "Cannot open output file proj2.out for write.\n"

#define SEM_MODE        0644
#define H_ATOMS_IN_H2O  2
#define O_ATOMS_IN_H2O  1
#define ATOMS_IN_H2O    3
#define DEFAULT_NO      3
#define DEFAULT_NH      5
#define DEFAULT_TI      100
#define DEFAULT_TB      100
#define INPUT_ARGUMENTS 4
#define NO_ARG_POS      1
#define NH_ARG_POS      2
#define TI_ARG_POS      3
#define TB_ARG_POS      4
#define MAX_WAIT        1000
#define MIROS_IN_MS     1000

typedef struct {
    sem_t *mutex;
    int action;
    int atomu;
    int hotovo;
} Shared;

typedef struct {
    sem_t *mutex;
    int oxygen;
    int hydrogen;
} ACount;

typedef struct {
    sem_t *mutex;
    int count;
} BCount;

void unlink_all_semaphores() 
{
    sem_unlink("/sem_turnstile2");
    sem_unlink("/sem_turnstile");
    sem_unlink("/sem_hydroqueue");
    sem_unlink("/sem_oxyqueue");
    sem_unlink("/sem_barrier");
    sem_unlink("/sem_atom");
    sem_unlink("/sem_shmem");
}

// navrat z chyby s vypsanim hlasky pres perror() s vyuzitim errno
void perror_exit (char *s)
{
    unlink_all_semaphores();
    perror(s);
    exit(1);
}

// navrat z chyby s vypsanim hlasky pres
void error_exit (char *s)
{
    unlink_all_semaphores();
    fprintf(stderr, "%s\n", s);
    exit(1);
}

// alokace sdilene pameti pro procesy
void* create_shared_memory(size_t size)
{
    int protection = PROT_READ | PROT_WRITE;
    int visibility = MAP_SHARED | MAP_ANONYMOUS;
    void *mem = mmap(NULL, size, protection, visibility, -1, 0);
    if(mem == MAP_FAILED)
        perror_exit ("mmap failed");
    return mem;
}

typedef sem_t Semaphore;

// vytvoreni semaforu s osetrenim chyby, chyba semaforu je kriticka chyba a ukonci proces
Semaphore *make_semaphore (char *name, int n)
{
    //sem_unlink(name); // jen pro jistotu, kdyby se nam to nezavrelo po predchozim padu programu ;-)
    Semaphore *sem = sem_open(name, O_CREAT | O_EXCL, SEM_MODE, n); 
    if (sem == SEM_FAILED)
        perror_exit ("sem_open failed");
    return sem;
}

// uvolneni semaforu s osetrenim chyby, chyba semaforu je kriticka chyba a ukonci proces
int sem_signal(Semaphore *sem)
{
    int ret = sem_post(sem);
    if (ret == -1)
        perror_exit ("sem_post failed");
    return ret;
}

// uzamceni semaforu s osetrenim chyby, chyba semaforu je kriticka chyba a ukonci proces
int sem_cwait(Semaphore *sem)
{
    int ret = sem_wait(sem);
    if (ret == -1)
        perror_exit ("sem_wait failed");
    return ret;
}

// test retezce ne vyskyt jen cisel '0' az '9'
int digits_only(const char *s)
{
    while (*s) {
        if (isdigit(*s++) == 0)
            return 0;
    }
    return 1;
}

// makro prvni casti bariery, zprehlednuje kod a pouziva se u H i O procesu
#define BARRIER_PART1 \
{ \
    sem_cwait(barrier->mutex); \
        barrier->count += 1; \
        if( barrier->count == ATOMS_IN_H2O ) { \
            sem_cwait(turnstile2); \
            sem_signal(turnstile); \
        } \
    sem_signal(barrier->mutex); \
    sem_cwait(turnstile); \
    sem_signal(turnstile); \
}

// makro druhe casti bariery, zprehlednuje kod a pouziva se u H i O procesu
#define BARRIER_PART2 \
{ \
    sem_cwait(barrier->mutex); \
        barrier->count -= 1; \
        if( barrier->count == 0 ) { \
            sem_cwait(turnstile); \
            sem_signal(turnstile2); \
        } \
    sem_signal(barrier->mutex); \
    sem_cwait(turnstile2); \
    sem_signal(turnstile2); \
}

// makro na uzavreni vsech semaforu a odmapovani sdilene pameti pred ukoncenim procesu (zprehlednuje kod)
#define CLEANUP \
{ \
    sem_close(turnstile2); \
    sem_close(turnstile); \
    sem_close(hydroQueue); \
    sem_close(oxyQueue); \
    sem_close(barrier->mutex); \
    munmap(barrier, sizeof(BCount)); \
    sem_close(atmcnt->mutex); \
    munmap(atmcnt, sizeof(ACount)); \
    sem_close(shmem->mutex); \
    munmap(shmem, sizeof(Shared)); \
    unlink_all_semaphores(); \
}

int main(int argc, char* argv[]) {

    int NO = DEFAULT_NO; // pocet kysliku
    int NH = DEFAULT_NH; // pocet vodiku
    int TI = DEFAULT_TI; // max. cas v ms ktery atom ceka po vytovreni nez se zaradi do fronty
    int TB = DEFAULT_TB; // max. cas v ms nutny pro vytvoreni molekuly

    if(argc==INPUT_ARGUMENTS+1) {
        int err=0;
        if(digits_only(argv[NO_ARG_POS])) {
            NO=atoi(argv[NO_ARG_POS]);
        } else {
            fprintf(stderr, ERRMSG_NO_INVALID);
            err=1;
        }
        if(digits_only(argv[NH_ARG_POS])) {
            NH=atoi(argv[NH_ARG_POS]);
        } else {
            fprintf(stderr, ERRMSG_NH_INVALID);
            err=1;
        }
        if(digits_only(argv[TI_ARG_POS])) {
            TI=atoi(argv[TI_ARG_POS]);
            if(TI<0 || TI>MAX_WAIT) {
                fprintf(stderr, ERRMSG_TI_OORANGE);
                err=1;
            }
        } else {
            fprintf(stderr, ERRMSG_TI_INVALID);
            err=1;
        }
        if(digits_only(argv[TB_ARG_POS])) {
            TB=atoi(argv[TB_ARG_POS]);
            if(TB<0 || TB>MAX_WAIT) {
                fprintf(stderr, ERRMSG_TB_OORANGE);
                err=1;
            }
        } else {
            fprintf(stderr, ERRMSG_TB_INVALID);
            err=1;
        }
        if(err)
            error_exit(MSG_USAGE);
    } else {
        fprintf(stderr, ERRMSG_WRONG_NRPAR);
        error_exit(MSG_USAGE);
    }

    FILE * fp;
    fp = fopen("proj2.out", "w"); // new file or rewrite existing and write only
    // fp = stdout;
    if(fp == NULL) {
        fprintf(stderr, ERRMSG_CANNOT_WRITE);
        perror_exit(MSG_USAGE);
    }

    int status = 0; // info pro odliseni child a parent procesu a nasledne status pri cekani na ukonceni procesu
    int molekul;    // maximalni pocet molekul, ktery lze vytvorit
    if(NO > NH/H_ATOMS_IN_H2O)
        molekul = NH/H_ATOMS_IN_H2O;
    else
        molekul = NO;

    Shared *shmem = create_shared_memory(sizeof(Shared));   // sdilena pamet pro promenne
    shmem->mutex = make_semaphore("/sem_shmem", 1);
    shmem->action = 0;
    shmem->atomu = 0;

    ACount *atmcnt = create_shared_memory(sizeof(ACount));  // sdilena pamet pro pocitadla procesu vodiku a kysliku
    atmcnt->mutex = make_semaphore("/sem_atom", 1);
    atmcnt->oxygen = 0;
    atmcnt->hydrogen = 0;

    BCount *barrier = create_shared_memory(sizeof(BCount)); // sdilena pamet pro pocitadlo bariery
    barrier->mutex = make_semaphore("/sem_barrier", 1);
    barrier->count = 0;

    sem_t *oxyQueue = make_semaphore("/sem_oxyqueue", 0);     // semafor fronty kysliku
    sem_t *hydroQueue = make_semaphore("/sem_hydroqueue", 0); // semafor fronty vodiku
    sem_t *turnstile = make_semaphore("/sem_turnstile", 0);   // semafor prvniho turniketu
    sem_t *turnstile2 = make_semaphore("/sem_turnstile2", 1); // semafor druheho turniketu

    // pred spustenim vsech procesu zkontrolujeme, jestli nemame jen nevyuzitelene atomy,
    // ktere by se jinak zasekly ve fronte a kdyztak je dopredu uvolnime
    if( molekul==0 ) {
        for(int i=0; i<NO; i++)
            sem_signal(oxyQueue);
        for(int i=0; i<NH; i++)
            sem_signal(hydroQueue);
    }

    for(int i=1; i<=NO+NH; i++){    // pro vsechny atomy (i<=NO je kyslik, i>NO je vodik)
        if(fork() == 0){            // fork() spusti novy process
            status=1;

            srand(getpid());    // kazdy proces musi mit jiny seed, aby dostaval ruzna "nahodna" cisla

            int wtime;
            //wtime = rand() % 100;
            //usleep(wtime*MIROS_IN_MS); // drobne cekani pred startem, aby se rozhodily i starty procesu

            sem_cwait(shmem->mutex);
                fprintf(fp, "%d: %c %d: started\n", ++(shmem->action), i<=NO?'O':'H', i<=NO?i:i-NO);
                fflush(fp);
            sem_signal(shmem->mutex);

            wtime = rand() % (TI+1);
            usleep(wtime*MIROS_IN_MS); // cekani na vlozeni do fronty
            
            if(i<=NO) {
                // **** OXYGEN PROCESS BEGIN *****

                sem_cwait(atmcnt->mutex);
                    //printf("Oxygen %d za atmcnt-mutex\n", i);
                    atmcnt->oxygen += 1;
                    if( atmcnt->hydrogen >= H_ATOMS_IN_H2O ) {
                        sem_signal(hydroQueue);
                        sem_signal(hydroQueue);
                        atmcnt->hydrogen -= H_ATOMS_IN_H2O;
                        sem_signal(oxyQueue);
                        atmcnt->oxygen -= O_ATOMS_IN_H2O;
                        wtime = rand() % (TB+1);
                        usleep(wtime*MIROS_IN_MS); // mame vse pohromade, tak cekame na vytvoreni molekuly
                    } else {
                        sem_signal(atmcnt->mutex);
                    }
                    sem_cwait(shmem->mutex);
                        fprintf(fp, "%d: O %d: going to queue\n", ++(shmem->action), i);
                        fflush(fp);
                    sem_signal(shmem->mutex);
                    
                    sem_cwait(oxyQueue);  // vlozeni kysliku do fronty

                    if(shmem->atomu==molekul*ATOMS_IN_H2O) {   // mame plonkovni atom kysliku
                        sem_cwait(shmem->mutex);
                            fprintf(fp, "%d: O %d: not enough H\n", ++(shmem->action), i);
                            fflush(fp);
                        sem_signal(shmem->mutex);

                        fclose(fp);
                        CLEANUP
                        exit(0);
                    }

                    // bond
                    sem_cwait(shmem->mutex);
                        fprintf(fp, "%d: O %d: creating molecule %d\n", ++(shmem->action), i, shmem->atomu/3+1);
                        fflush(fp);
                    sem_signal(shmem->mutex);

                    // rendezvous
                    BARRIER_PART1

                    // critical point
                    sem_cwait(shmem->mutex);
                        fprintf(fp, "%d: O %d: molecule %d created\n", ++(shmem->action), i, shmem->atomu/3+1);
                        fflush(fp);
                        shmem->atomu++;
                    sem_signal(shmem->mutex);

                    BARRIER_PART2

                sem_signal(atmcnt->mutex);
                // **** OXYGEN PROCESS END *****

            } else {

                // **** HYDROGEN PROCESS BEGIN *****

                sem_cwait(atmcnt->mutex);
                atmcnt->hydrogen += 1;
                if( (atmcnt->hydrogen >= H_ATOMS_IN_H2O) && (atmcnt->oxygen >= O_ATOMS_IN_H2O) ) {
                    sem_signal(hydroQueue);
                    sem_signal(hydroQueue);
                    atmcnt->hydrogen -= H_ATOMS_IN_H2O;
                    sem_signal(oxyQueue);
                    atmcnt->oxygen -= O_ATOMS_IN_H2O;
                    wtime = rand() % (TB+1);
                    usleep(wtime*MIROS_IN_MS); // mame vse pohromade, tak cekame na vytvoreni molekuly
                } else {
                    sem_signal(atmcnt->mutex);
                }
                sem_cwait(shmem->mutex);
                    fprintf(fp, "%d: H %d: going to queue\n", ++(shmem->action), i-NO);
                    fflush(fp);
                sem_signal(shmem->mutex);

                sem_cwait(hydroQueue);  // vlozeni vodiku do fronty

                if(shmem->atomu==molekul*3) { // mame plonkovni atom vodiku
                    sem_cwait(shmem->mutex);
                        fprintf(fp, "%d: H %d: not enough O or H\n", ++(shmem->action), i-NO);
                        fflush(fp);
                    sem_signal(shmem->mutex);

                    fclose(fp);
                    CLEANUP
                    exit(0);
                }

                // bond
                sem_cwait(shmem->mutex);
                    fprintf(fp, "%d: H %d: creating molecule %d\n", ++(shmem->action), i-NO, shmem->atomu/3+1);
                    fflush(fp);
                sem_signal(shmem->mutex);

                // rendezvous
                BARRIER_PART1

                // critical point
                sem_cwait(shmem->mutex);
                    fprintf(fp, "%d: H %d: molecule %d created\n", ++(shmem->action), i-NO, shmem->atomu/3+1);
                    fflush(fp);
                    shmem->atomu++;
                sem_signal(shmem->mutex);

                BARRIER_PART2
                
                // **** HYDROGEN PROCESS END *****
            }

            // pred ukoncenim procesu posledni molekuly zkontrolujeme, jestli nevysi ve fronte nevyuzitelne atomy
            // a kdyztak je uvolnime
            sem_cwait(shmem->mutex);
            if( shmem->atomu==molekul*ATOMS_IN_H2O ) {
                for(int i=molekul; i<NO; i++)
                    sem_signal(oxyQueue);
                for(int i=molekul*H_ATOMS_IN_H2O; i<NH; i++)
                    sem_signal(hydroQueue);
            }
            sem_signal(shmem->mutex);

            fclose(fp);
            CLEANUP
            exit(0);
        }
    }

    if(status==0) {     // parent process
        // cekani v parentu nez skonci vsechny childy
        while (wait(&status) > 0) {
        }
    }
    
    fclose(fp);
    CLEANUP
    return 0;
}
