#include <unistd.h>
#include <signal.h>
#include <stdio.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <stdlib.h>
#include <string.h>
#include <semaphore.h>
#include <sys/mman.h>
#include <getopt.h>
#include <time.h>

typedef struct shared_memory_t{
int buffer1;
int buffer2;
int nur_done;
int vac_done;
int all_vac_done;
int priority_queue_size;
sem_t sem_buffer;
sem_t sem_citizen;
sem_t sem_vac;
sem_t sem_buffer_s;
sem_t sem_buffer_e1;
sem_t sem_buffer_e2;
}shared_memory_t;

void SIGUSR1_handler(int signa);
void SIGINT_handler(int signa);
int is_queue_empty(int * queue, int size);
int find_min_element_queue(int * queue,int size);
int queue_used(int * queue,int size);
volatile sig_atomic_t cit_wake = 1;

int main(int argc, char ** argv){

    int i=0;
    int n,v,c,b,t;
    int parser = 0;
    int parser_number=0;
    int fd1=0;
    int inputcheck[6]={0,0,0,0,0,0};
    char path[1024];
    char printformat[10000];
    int process;
    sigset_t cur_mask,old_mask;
    struct sigaction usr1act={.sa_handler=SIGUSR1_handler};
    struct sigaction sigintact;
    struct sigaction old_usr1act;

    srand(time(NULL));

    while((parser = getopt(argc, argv, "n:v:c:b:t:i:")) != -1)   
    {
        switch(parser)  
        {  
            case 'n':
            {
                if(inputcheck[0]>=1){
                    fprintf(stderr,"Same input twice.\n");
                    return -1;
                }
                sscanf(optarg,"%d",&n);
                inputcheck[0]++;
                break;
            }
            case 'v':
            {
                if(inputcheck[1]>=1){
                    fprintf(stderr,"Same input twice.\n");
                    return -1;
                }

                sscanf(optarg,"%d",&v);
                inputcheck[1]++;
                break;
            }
            case 'c':
            {
                if(inputcheck[2]>=1){
                    fprintf(stderr,"Same input twice.\n");
                    return -1;
                }
                sscanf(optarg,"%d",&c);
                inputcheck[2]++;
                break;
            }  
            case 'b':
            {
                if(inputcheck[3]>=1){
                    fprintf(stderr,"Same input twice.\n");
                    return -1;
                }
                sscanf(optarg,"%d",&b);
                inputcheck[3]++;
                break;
            }
            case 't':
            {
                if(inputcheck[4]>=1){
                    fprintf(stderr,"Same input twice.\n");
                    return -1;
                }
                sscanf(optarg,"%d",&t);
                inputcheck[4]++;
                break;
            }
            case 'i':
            {
                if(inputcheck[5]>=1){
                    fprintf(stderr,"Same input twice.\n");
                    return -1;
                }
                sprintf(path,"%s",optarg);
                inputcheck[5]++;
                break;
            }
            default:
            {
                fprintf(stderr,"The command line arguments are missing/invalid.\n");
                return -1;
            }
            break;
        }
        parser_number++;  
    }
    if(parser_number!=6){
        fprintf(stderr,"Too many/less arguments.\n");
        return -1;
    }
    if(n<2){
        fprintf(stderr,"Nurse does not meet requirements.(N must be greater than or equal 2)\n");
        return -1;
    }
    if(v<2){
        fprintf(stderr,"Vaccinator does not meet requirements.(V must be greater than or equal 2)\n");
        return -1;
    }
    if(c<3){
        fprintf(stderr,"Citizen does not meet requirements.(C must be greater than or equal 3)\n");
        return -1;
    }
    if(t<1){
        fprintf(stderr,"T value does not meet requirements.(T must be greater than or equal 1)\n");
        return -1;
    }
    if(b<t*c+1){
        fprintf(stderr,"Buffer does not meet requirements.(B must be greater than or equal t*c+1)\n");
        return -1;
    }

    sprintf(printformat,"Welcome to the GTU344 clinic. Number of citizens to vaccinate c=%d with t=%d doses.\n",c,t);
    write(1,printformat,strlen(printformat));

    usr1act.sa_handler=SIGUSR1_handler;
    usr1act.sa_flags=SA_NODEFER;
    sigaction(SIGUSR1,&usr1act,&old_usr1act);

    sigintact.sa_handler=SIGINT_handler;
    sigintact.sa_flags=SA_NODEFER;
    sigaction(SIGINT,&sigintact,NULL);

    sigemptyset(&cur_mask);
    sigaddset(&cur_mask,SIGUSR1);
    sigaddset(&cur_mask,SIGINT);

    fd1=open(path,O_RDONLY);
    if(fd1<0){
        perror("Open error");
        exit(EXIT_FAILURE);
    }

    shared_memory_t * shm = mmap(NULL,sizeof(shared_memory_t),PROT_READ | PROT_WRITE,MAP_SHARED | MAP_ANONYMOUS,-1,0);
    int * nurse_pid = mmap(NULL,sizeof(int)*n,PROT_READ | PROT_WRITE,MAP_SHARED | MAP_ANONYMOUS,-1,0);
    int * vaccinator_pid = mmap(NULL,sizeof(int)*v,PROT_READ | PROT_WRITE,MAP_SHARED | MAP_ANONYMOUS,-1,0);
    int * citizen_pid = mmap(NULL,sizeof(int)*c,PROT_READ | PROT_WRITE,MAP_SHARED | MAP_ANONYMOUS,-1,0);
    int * priority_queue = mmap(NULL,sizeof(int)*c,PROT_READ | PROT_WRITE,MAP_SHARED | MAP_ANONYMOUS,-1,0);
    int * vac_return = mmap(NULL,sizeof(int)*v,PROT_READ | PROT_WRITE,MAP_SHARED | MAP_ANONYMOUS,-1,0);
    sem_init(&shm->sem_buffer,1,1);
    sem_init(&shm->sem_citizen,1,0);
    sem_init(&shm->sem_vac,1,1);
    sem_init(&shm->sem_buffer_s,1,b);
    sem_init(&shm->sem_buffer_e1,1,0);
    sem_init(&shm->sem_buffer_e2,1,0);
    shm->priority_queue_size=c;

    for(i=0;i<n;i++){
        process=fork();
        if(process==0){
            nurse_pid[i]=getpid();
            sigprocmask(SIG_BLOCK,&cur_mask,&old_mask);
            while(cit_wake>=1){
                sigsuspend(&old_mask);
            }
            sigprocmask(SIG_UNBLOCK,&cur_mask,NULL);
            char c;
            int bytes;
            do{
                bytes=read(fd1,&c,1);
                if(bytes<0){
                    perror("Read error");
                    exit(EXIT_FAILURE);
                }
                else if(bytes>0){
                    
                    if(c=='1'){
                        sem_wait(&shm->sem_buffer_s);
                        sem_wait(&shm->sem_buffer);
                        shm->buffer1++;
                        sprintf(printformat,"Nurse %d (pid=%d) has brought vaccine %c: the clinic has %d vaccine1 and %d vaccine2.\n",i+1,getpid(),c,shm->buffer1,shm->buffer2);
                        write(1,printformat,strlen(printformat));
                        sem_post(&shm->sem_buffer);
                        sem_post(&shm->sem_buffer_e1);
                    }
                    else if(c=='2'){
                        sem_wait(&shm->sem_buffer_s);
                        sem_wait(&shm->sem_buffer);
                        shm->buffer2++;
                        sprintf(printformat,"Nurse %d (pid=%d) has brought vaccine %c: the clinic has %d vaccine1 and %d vaccine2.\n",i+1,getpid(),c,shm->buffer1,shm->buffer2);
                        write(1,printformat,strlen(printformat));
                        sem_post(&shm->sem_buffer);
                        sem_post(&shm->sem_buffer_e2);
                    }
                    
                }     
            }
            while(bytes!=0);
            /*int c1=0;
            for(c1=0;c1<n;c1++){
                sprintf(printformat,"NURSE PID:%d\n",nurse_pid[c1]);
                write(1,printformat,strlen(printformat));
                if(nurse_pid[c1]!=getpid()){
                    kill(nurse_pid[c1],SIGKILL);
                }
            }*/
            sem_wait(&shm->sem_buffer);
            shm->nur_done++;
            if(shm->nur_done==n){
                write(1,"Nurses have carried all vaccines to the buffer, terminating.\n",strlen("Nurses have carried all vaccines to the buffer, terminating.\n"));
            }
            sem_post(&shm->sem_buffer); 
            i=n+1;
            close(fd1);
            return 0;
        }
        else{
            nurse_pid[i]=process;
        }                           
    }
    for(i=0;i<v;i++){
        process=fork();
        int vac_citizen=0;
        if(process==0){
            vaccinator_pid[i]=getpid();
            sigprocmask(SIG_BLOCK,&cur_mask,&old_mask);
            while(cit_wake>=1){
                sigsuspend(&old_mask);
            }
            sigprocmask(SIG_UNBLOCK,&cur_mask,NULL);
            do{ 
                sem_wait(&shm->sem_vac);
                if(shm->all_vac_done == 1){
                    break;
                }
                sem_wait(&shm->sem_buffer_e1);
                if(shm->all_vac_done == 1){
                    break;
                }
                sem_wait(&shm->sem_buffer_e2);
                sem_wait(&shm->sem_buffer);

                int priority_citizen;
                if(shm->priority_queue_size==0){
                    if(is_queue_empty(citizen_pid,c)==0){
                        break;
                    }
                    do{
                        priority_citizen=rand()%c;
                    }
                    while(citizen_pid[priority_citizen]==0);
                    priority_citizen=citizen_pid[priority_citizen];
                }
                if(is_queue_empty(priority_queue,c)!=0 && shm->priority_queue_size>0){
                    priority_citizen=find_min_element_queue(priority_queue,shm->priority_queue_size);
                    shm->priority_queue_size--;
                }

                sprintf(printformat,"Vaccinator %d (pid=%d) calls the citizen %d in the clinic.\n",i+1,getpid(),priority_citizen);
                write(1,printformat,strlen(printformat));
                shm->vac_done++;
                vac_citizen++;
                shm->buffer1--;
                shm->buffer2--;
                kill(priority_citizen,SIGUSR1);
                sem_post(&shm->sem_buffer_s);
                sem_post(&shm->sem_buffer_s);
                sem_post(&shm->sem_buffer);
                sem_wait(&shm->sem_citizen);
                //sem_post(&shm->sem_citizen);
                //sem_wait(&shm->sem_citizen2);                   
            }
            while(shm->vac_done<t*c);

            /*int c2=0;
            for(c2=0;c2<v;c2++){
                sprintf(printformat,"VAC PID:%d\n",vaccinator_pid[c2]);
                write(1,printformat,strlen(printformat));
                if(vaccinator_pid[c2]!=getpid()){
                    kill(vaccinator_pid[c2],SIGKILL);
                }
            }*/
            shm->all_vac_done=1;
            sem_post(&shm->sem_vac);
            sem_post(&shm->sem_buffer_e1);
            sem_wait(&shm->sem_buffer);
            vac_return[i]=vac_citizen;
            //sprintf(printformat,"Vaccinator %d (pid=%d) vaccinated %d doses.\n",i,getpid(),vac_citizen);
            //write(1,printformat,strlen(printformat));
            sem_post(&shm->sem_buffer);
            i=v+1;
            close(fd1);
            return 0;
        }
        else{
            vaccinator_pid[i]=process;
        }                             
    }
    for(i=0;i<c;i++){
        process=fork();
        if(process==0){
            citizen_pid[i]=getpid();
            int j;
            for(j=0;j<t;j++){
                sigprocmask(SIG_BLOCK,&cur_mask,&old_mask);
                while(cit_wake>=1){
                    sigsuspend(&old_mask);
                }
                cit_wake=1;
                sigprocmask(SIG_UNBLOCK,&cur_mask,NULL);
                /*int priority_citizen;
                char c_id[20];
                do{
                    sprintf(printformat,"Priority id:%d, curr id:%d\n",priority_citizen,getpid());
                    write(1,printformat,strlen(printformat));
                }
                while(priority_citizen != getpid());*/

                //write(1,"CITIZEN LOOP ENDED\n",strlen("CITIZEN LOOP ENDED\n")); 
                
                //sem_wait(&shm->sem_citizen);
                //sem_wait(&shm->sem_buffer);
                sem_wait(&shm->sem_buffer);
                sprintf(printformat,"Citizen %d (pid=%d) is vaccinated for the %d time: the clinic has %d vaccine1 and %d vaccine2.\n",i+1,getpid(),j+1,shm->buffer1,shm->buffer2);
                write(1,printformat,strlen(printformat));
                sem_post(&shm->sem_buffer);
                sem_post(&shm->sem_citizen);
                if(j==t-1){
                    citizen_pid[i]=0;
                    sprintf(printformat,"The citizen %d (pid=%d) is leaving. Remaining citizens to vaccinates: %d\n",i+1,getpid(),queue_used(citizen_pid,c));
                    write(1,printformat,strlen(printformat));
                }
                sem_post(&shm->sem_vac);
                //sem_post(&shm->sem_buffer);
                //sem_post(&shm->sem_citizen2);
            }
            
            i=c+1;
            close(fd1);
            return 0;
        }
        else{
            citizen_pid[i]=process;
            priority_queue[i]=process;
        }                            
    }
    if(process!=0){
        //sprintf(printformat,"MAIN PROCESS CALL pid:%d n:%d v:%d c:%d\n",getpid(),n,v,c);
        //write(1,printformat,strlen(printformat));
        for(i=0;i<n;i++){
            kill(nurse_pid[i],SIGUSR1);            
        }
        for(i=0;i<v;i++){
            kill(vaccinator_pid[i],SIGUSR1);            
        }
        /*for(i=0;i<c;i++){
            sprintf(printformat,"MPC Citizen pid:%d\n",citizen_pid[i]);
            write(1,printformat,strlen(printformat));
            kill(citizen_pid[i],SIGUSR1);            
        }*/
        int status;
        for(i=0;i<n+v+c;i++){
            if(i<n){
                waitpid(nurse_pid[i],&status,0);
            }
            else if(i<n+v){
                waitpid(vaccinator_pid[i-n],&status,0);
            }
            else{
                waitpid(citizen_pid[i-n-v],&status,0);
            }
        }
        for(i=0;i<v;i++){
            sprintf(printformat,"Vaccinator %d (pid=%d) vaccinated %d doses. ",i,vaccinator_pid[i],vac_return[i]);
            write(1,printformat,strlen(printformat));

        }
        write(1,"\n",strlen("\n"));

        write(1,"The clinic is now closed. Stay healthy!\n",strlen("The clinic is now closed. Stay healthy!\n"));
        close(fd1);
    }
    
    sem_destroy(&shm->sem_buffer);
    sem_destroy(&shm->sem_citizen);
    sem_destroy(&shm->sem_vac);
    sem_destroy(&shm->sem_buffer_s);
    sem_destroy(&shm->sem_buffer_e1);
    sem_destroy(&shm->sem_buffer_e2);
    munmap(shm,sizeof(shared_memory_t));
    munmap(nurse_pid,sizeof(int)*n);
    munmap(vaccinator_pid,sizeof(int)*v);
    munmap(citizen_pid,sizeof(int)*c);
    munmap(priority_queue,sizeof(int)*c);
    return 0;
}

void SIGUSR1_handler(int signa){
    //char printfor[1000];
    //sprintf(printfor,"pid:%d SIGUSR1 recieved.\n",getpid());
    //write(1,printfor,strlen(printfor));
    cit_wake=0;
    signal(signa,SIGUSR1_handler);
}

void SIGINT_handler(int signa){

    char printfor[1000];
    sprintf(printfor,"pid:%d SIGUSR1 recieved. Exiting.\n",getpid());
    write(1,printfor,strlen(printfor));
    exit(EXIT_FAILURE);
}

int is_queue_empty(int * queue,int size){
    int i;
    if(queue == NULL){
        return 0;
    }
    else{
        for(i=0;i<size;i++){
            if(queue[i]!=0){
                return 1;
            }
        }
    }
    return 0;
}

int find_min_element_queue(int * queue,int size){
    int i;
    int min=queue[0];
    int min_index=0;
    for(i=0;i<size;i++){
        if(queue[i]<min){
            min=queue[i];
            min_index=i;
        }
    }
    for(i=min_index;i<size-1;i++){
        queue[i]=queue[i+1];
    }
    queue[size-1]=0;
    return min;
}

int queue_used(int * queue,int size){
    int i,used=0;
    for(i=0;i<size;i++){
        if(queue[i]>0){
            used++;
        }
    }
    return used;
}