#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <errno.h>
#include <sys/types.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <pthread.h>
#include <ctype.h>

#include "get_string.h"
#include "bloom.h"
#include "cyclic_buff.h"                //struct of cyclic buffer and function declaration included

pthread_cond_t empty;
pthread_cond_t full;
pthread_mutex_t mutex;                  //mutex for cond_var
pthread_mutex_t mtx;                    //general

int flag_exit = 0;
int sig_cnt = 0;                        //counter of signals received for empty_cond_var
int sizeOfBloom;
record* head;

struct args{
    cycBuff* cycbuff;
    record* recs;
    txt* txts;
    bloom* blms;
    country* cntrs;
    virus* vrs;
};

int main(int argc, char *argv[]){
    
    int port = atoi(argv[2]); 
    int numThreads = atoi(argv[4]);
    int socketBufferSize = atoi(argv[6]);
    int cyclicBufferSize = atoi(argv[8]);
    sizeOfBloom = atoi(argv[10]);

    char** paths = (char**)malloc(sizeof(char*) * 1000); 

    int i = 11, j = 0;
    int num_of_paths = 0;                   //counter of paths

    while(argv[i] != NULL){
        paths[j] = argv[i];
        num_of_paths++;                     //counts number of countries in monitor
        i++; j++;
    }

    //////////////////////////////////////////  creates paths for regular files and creates record list  ///////////////////////////////////

    char** filename = (char**)malloc(sizeof(char*) * 1000);
    
    int txt_cnt = 0;
    
    for(int j = 0; j < num_of_paths; j++){
        
        struct dirent *de1;                     //pointer for subdirectory entry
        DIR *dr1 = opendir(paths[j]);            //opens subdirectory

        if(dr1 == NULL){                                       //returns NULL if couldn't open subdirectory
            printf("Couldn't open current subdirectory!");
            return 0;
        }
        
        while((de1 = readdir(dr1)) != NULL){
            
            if(de1->d_type == DT_REG){               //if it's a regular file
                filename[txt_cnt] = (char*)malloc(sizeof(char) * 190);
                strcpy(filename[txt_cnt], "");
                strcpy(filename[txt_cnt], paths[j]);
                strcat(filename[txt_cnt], "/");
                strcat(filename[txt_cnt], de1->d_name);      //create filename
                txt_cnt++;
            }

        }
        closedir(dr1);                         //close subdirectory

    }    

    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

    head = (record*)malloc(sizeof(record));
    create_head(head);                                                 //creates head of record list
    
    txt* t_head = (txt*)malloc(sizeof(txt));                            //creates head of txt used for logfiles
    t_head->filename = (char*)malloc(sizeof(char) * 60);
    strcpy(t_head->filename, "");
    t_head->next = NULL;

    bloom* bloom_head = (bloom*)malloc(sizeof(bloom));              //creates head of bloomfilters
    bloom_head->virus = (char*)malloc(sizeof(char) * 60);
    strcpy(bloom_head->virus, "-1");							
    bloom_head->bit_array = (char*)malloc(sizeof(char) * sizeOfBloom);
    strcpy(bloom_head->bit_array, "-1");
    bloom_head->next = NULL;

    country* c_head = (country*)malloc(sizeof(country));                  //creates country head
    c_head->country = (char*)malloc(sizeof(char) * 60);
    strcpy(c_head->country, "-1");
    c_head->next = NULL;

    virus* virus_head = (virus*)malloc(sizeof(virus));    
    record* temp = head;
    virus_push(virus_head, temp);                                  //creates the head of the virus list that includes the skiplists for each virus
    virus_head->next = NULL;

    cycBuff* cycbuff = (cycBuff*)malloc(sizeof(cycBuff));

    init_buff(cycbuff, cyclicBufferSize);             //initializing the cyclic buffer

    args* argss = (args*)malloc(sizeof(args));
    
    argss->cycbuff = cycbuff;
    argss->recs = head;                               
    argss->txts = t_head;
    argss->blms = bloom_head;  
    argss->cntrs = c_head; 
    argss->vrs = virus_head;                        //sets the arguments passed in obtain for the creation of threads
    
    ////////////////////////////////////////// socket and port /////////////////////////////////////////////////////////////////////////////

    int sock, newsock;
    struct sockaddr_in mon_server, mon_client;
    struct sockaddr *mon_serverptr = (struct sockaddr*) &mon_server;
    struct sockaddr *mon_clientptr = (struct sockaddr*) &mon_client;
    socklen_t mon_clientlen = sizeof(mon_client);

    if((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0){       //creation of socket
        perror("socket"); exit(1);
    }

    u_int reuse = 1;
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));          //for reusing ports in multiple sockets

    mon_server.sin_family = AF_INET;                    //internet address family
    mon_server.sin_addr.s_addr = htonl(INADDR_ANY);
    mon_server.sin_port = htons(port);                  //port to bind socket

    if(pthread_mutex_init(&mutex, NULL) != 0){                      //initializing the mutex used for cyclic buffer
        printf("Error in initializing mutex!\n"); exit(1);
    }

    if(pthread_mutex_init(&mtx, NULL) != 0){                      //initializing the mutex used for obtaining paths from buffer
        printf("Error in initializing mutex!\n"); exit(1);
    }

    if(pthread_cond_init(&empty, 0) != 0){                      //initializing the empty condition variable
        printf("Error in initializing cond_empty!\n");
    }

	if(pthread_cond_init(&full, 0) != 0){                       //initializing the full condition variable
        printf("Error in initializing cond_full!\n");
    }

    if(bind(sock, mon_serverptr, sizeof(mon_server)) < 0){
        perror("bind"); exit(1);
    }

    if(listen(sock, 5) < 0){
        perror("listen"); exit(1);
    }

    if((newsock = accept(sock, mon_clientptr, &mon_clientlen)) < 0){
        perror("accept"); exit(1);
    }

    int p = 0, buff_cnt = 0;                                //counter of how many paths were added
    while(p < txt_cnt && argss->cycbuff->counter < cyclicBufferSize){
        place_path(argss, filename[p]);                                     //place path in buffer
        p++; 
        buff_cnt++;
    } 
     
    pthread_t *thr_ids;
    if((thr_ids = (pthread_t*)malloc(sizeof(pthread_t) * numThreads)) == NULL){
        perror("thread malloc"); exit(1) ;
    }

    for(int i = 0; i < numThreads; i++){                                                        //create threads
        if((pthread_create(&(thr_ids[i]), NULL, obtain_path, (void*) argss)) != 0){
            perror("creating threads"); exit(1);
        }
    }

    int num_of_txts = txt_cnt;
   
    while(buff_cnt <= txt_cnt){
         
        while(argss->cycbuff->counter > 0 || sig_cnt < numThreads)                                      
            pthread_cond_wait(&empty, &mutex);
        

        if(buff_cnt == txt_cnt)                                     //if all paths are in buffer break loop 
            break;
        
        pthread_mutex_unlock(&mutex);
        pthread_mutex_lock(&mutex);

        for(int i = 0; i < cyclicBufferSize; i++)                   //clearing the buffer 
            free(argss->cycbuff->data[i]);
        free(argss->cycbuff->data);

        init_buff(argss->cycbuff, cyclicBufferSize);                      //clearing counters and setting a clear array buffer

        int j = num_of_txts - buff_cnt;

        int p = num_of_txts - j;                                         //finding the path to place in buffer
        
        while(p < txt_cnt && argss->cycbuff->counter < cyclicBufferSize){
            place_path(argss, filename[p]);                                 //putting path in buffer
            p++; 
            buff_cnt++;
        }
        
        pthread_cond_broadcast(&full);
        
        pthread_mutex_unlock(&mutex);
          
    }

    /////////////////////////////////////////// passing bloom filters to parent ///////////////////////////////////////////////
    
    bloom* tempb = argss->blms;
    char k[socketBufferSize]; 

    while(tempb != NULL){
        strcpy(k, "");
        char* word = (char*)malloc(strlen(tempb->virus) + strlen(tempb->bit_array) + 6);
        strcpy(word, "");
        strcpy(word, tempb->virus);
        strcat(word, "/");
        strcat(word, tempb->bit_array);
        strcat(word, "/");
        
        int s = 0; 
                
        while(strlen(word) > s){
            strncpy(k, word+s, socketBufferSize);
            
            if(write(newsock, k, socketBufferSize) < 0){                     //passes path to monitors piece by piece according to its size
                perror("Monitor: Error in writing!"); exit(1);
            }     
            
            s+=socketBufferSize;           
        }
        
        if(write(newsock, "@", socketBufferSize) < 0){                     //sends ! to travelmonitor, to know that monitor's ready for quests
            perror("Monitor: Error in writing!"); exit(1);
        }

        free(word);

        tempb = tempb->next;
    }

    if(write(newsock, "!", socketBufferSize) < 0){                     //sends ! to travelmonitor, to know that monitor's ready for quests
        perror("Monitor: Error in writing!"); exit(1);
    }

    /////////////////////////////////////////// receiving quests //////////////////////////////////////////////////////////////
    
    int r;
    char q[50];
    char* quest = (char*)malloc(sizeof(char) * socketBufferSize * 50);
    strcpy(quest, ""); strcpy(q, "");

    int total_requests = 0;
    int total_accepted = 0;
    int total_rejected = 0;

    char msg[socketBufferSize];

    while(1){

        if((r = read(newsock, q, socketBufferSize)) < 0){              //reads content
            if(errno == EINTR)
                continue;
            perror("Monitor: Error in reading!"); exit(1);
        }      
    
        q[r] = '\0';

        if(q[0] != '!')                                            //until the ! is sent
            strcat(quest, q);
        
        else{
            
            char** Command;

            int num, counter = 0, i = 0;

            /* exit */
            if(strstr(quest, "exit") != 0){

                FILE *fp1;
                char filename[20];
                sprintf(filename, "./log_file.%ld", (long)getpid());
                fp1 = fopen(filename, "w");                             //creates logfile

                if(fp1 == NULL){
                    printf("Unable to create file.\n");
                    exit(EXIT_FAILURE);
                }

                country* temp = c_head->next;
                while(temp != NULL){
                    fputs(temp->country, fp1);
                    fputs("\n", fp1);
                    temp = temp->next;
                }                                                   //puts monitor's countries in logfile

                fputs("TOTAL TRAVEL REQUESTS:", fp1);
                fprintf(fp1,"%d",total_requests);
                fputs("\n", fp1);

                fputs("ACCEPTED:", fp1);
                fprintf(fp1,"%d", total_accepted);
                fputs("\n", fp1);

                fputs("REJECTED:", fp1);
                fprintf(fp1,"%d", total_rejected);              //puts statistics in logfile

                fclose(fp1);

                break;

            }

            /* travelRequest */
            if(strstr(quest, "travelRequest") != 0){
                
                while(quest[i] != '\0'){
                    if(quest[i] == ' ')
                        counter++;
                    i++;
                }

                if(counter == 5)
                    num = 6;   
                else
                    num = 0;                                       //used to check many cases of command
                
                if(num != 0){
                    Command = get_command_monitor(quest, num);
                    strcpy(quest, "");

                    total_requests++;
                
                    virus* temp = virus_head;
                    while(temp != NULL){
                        if(strcmp(temp->virus_name, Command[5]) == 0){
                            if(skiplist_search(temp->vaccinated_persons, Command[1]) == 1){
                                record* t = head;
                                while(t != NULL){
                                    if(strcmp(t->id, Command[1]) == 0 && strcmp(t->virus, Command[5]) == 0){
                                        char** date1 = get_date(t->date);
                                        char** date2 = get_date(Command[2]);
                                        char** date3 = (char**)malloc(sizeof(char*) * 60);          //accepted limit date for traveling

                                        for(int i = 0; i < 3; i++)
                                            date3[i] = (char*)malloc(sizeof(char) * 60);

                                        strcpy(date3[0], date1[0]);
                                        if(strcmp(date1[1], "06") > 0){
                                            strcpy(date3[1], "");
                                            sprintf(date3[1], "0%d", atoi(date1[1]) - 6);

                                            strcpy(date3[2], "");
                                            sprintf(date3[2], "%d", atoi(date1[2]) + 1);
                                        }
                                        else{
                                            strcpy(date3[1], "");
                                            sprintf(date3[1], "%d", atoi(date1[1]) + 6);

                                            strcpy(date3[2], "");
                                            sprintf(date3[2], "%d", atoi(date1[2]));
                                        }                                                       //creates date3 that is used for 6 months period checking
                                        
                                        //checks if date is suitable for traveling or not and sends the correct case in travel monitor
                                        if(atoi(date2[2]) > atoi(date3[2]) || atoi(date2[2]) < atoi(date1[2])){
                                            total_rejected++;  
                                                                   
                                            strcpy(msg, "");
                                            char* word = (char*)malloc(strlen("NO") + strlen("case1") + 6);
                                            strcpy(word, "");
                                            strcpy(word, "NO");
                                            strcat(word, "/");
                                            strcat(word, "case1");
                                            strcat(word, "/");
                                            
                                            int s = 0; 
                                                    
                                            while(strlen(word) > s){
                                                strncpy(msg, word+s, socketBufferSize);
                                                
                                                if(write(newsock, msg, socketBufferSize) < 0){                     //passes case to monitors piece by piece according to its size
                                                    perror("Monitor: Error in writing!"); exit(1);
                                                }     
                                                
                                                s+=socketBufferSize;           
                                            }                                                                                 
                                                                                   
                      
                                            if(write(newsock, "!", socketBufferSize) < 0){                            
                                                perror("Monitor: Error in writing!"); exit(1);
                                            }

                                            free(word);
                                        }

                                        else{
                                            if(atoi(date2[1]) == atoi(date3[1])){
                                                if(atoi(date2[0]) <= atoi(date3[0])){
                                                    total_accepted++;  
                                                    
                                                    strcpy(msg, "");
                                                    char* word = (char*)malloc(strlen("YES") + strlen(t->date) + 6);
                                                    strcpy(word, "");
                                                    strcpy(word, "YES");
                                                    strcat(word, "/");
                                                    strcat(word, t->date);
                                                    strcat(word, "/");
                                                    printf("%s\n", word);
                                                    
                                                    int s = 0; 
                                                            
                                                    while(strlen(word) > s){
                                                        strncpy(msg, word+s, socketBufferSize);
                                                        
                                                        if(write(newsock, msg, socketBufferSize) < 0){                     //passes case to monitors piece by piece according to its size
                                                            perror("Monitor: Error in writing!"); exit(1);
                                                        }     
                                                        
                                                        s+=socketBufferSize;           
                                                    }    
                                                    
                                                    if(write(newsock, "!", socketBufferSize) < 0){                            
                                                        perror("Monitor: Error in writing!"); exit(1);
                                                    }

                                                    free(word);                                                                             
                                                                                        
                                                }
                                                else{
                                                    total_rejected++;                                                    
                                                    
                                                    strcpy(msg, "");
                                                    char* word = (char*)malloc(strlen("NO") + strlen("case1") + 6);
                                                    strcpy(word, "");
                                                    strcpy(word, "NO");
                                                    strcat(word, "/");
                                                    strcat(word, "case1");
                                                    strcat(word, "/");
                                                    
                                                    int s = 0; 
                                                            
                                                    while(strlen(word) > s){
                                                        strncpy(msg, word+s, socketBufferSize);
                                                        
                                                        if(write(newsock, msg, socketBufferSize) < 0){                     //passes case to monitors piece by piece according to its size
                                                            perror("Monitor: Error in writing!"); exit(1);
                                                        }     
                                                        
                                                        s+=socketBufferSize;           
                                                    }                                                                                 
                                                                                        
                            
                                                    if(write(newsock, "!", socketBufferSize) < 0){                            
                                                        perror("Monitor: Error in writing!"); exit(1);
                                                    }

                                                    free(word);
                                                }
                                            }
                                            else if(atoi(date2[1]) < atoi(date3[1])){
                                                total_accepted++;                                                
                                                
                                                strcpy(msg, "");
                                                char* word = (char*)malloc(strlen("YES") + strlen(t->date) + 6);
                                                strcpy(word, "");
                                                strcpy(word, "YES");
                                                strcat(word, "/");
                                                strcat(word, t->date);
                                                strcat(word, "/");
                                                
                                                int s = 0; 
                                                        
                                                while(strlen(word) > s){
                                                    strncpy(msg, word+s, socketBufferSize);
                                                    
                                                    if(write(newsock, msg, socketBufferSize) < 0){                     //passes case to monitors piece by piece according to its size
                                                        perror("Monitor: Error in writing!"); exit(1);
                                                    }     
                                                    
                                                    s+=socketBufferSize;           
                                                }    
                                                
                                                if(write(newsock, "!", socketBufferSize) < 0){                            
                                                    perror("Monitor: Error in writing!"); exit(1);
                                                }

                                                free(word);  
                                            }
                                            else{
                                                total_rejected++;                                            
                                                
                                                strcpy(msg, "");
                                                char* word = (char*)malloc(strlen("NO") + strlen("case1") + 6);
                                                strcpy(word, "");
                                                strcpy(word, "NO");
                                                strcat(word, "/");
                                                strcat(word, "case1");
                                                strcat(word, "/");
                                                
                                                int s = 0; 
                                                        
                                                while(strlen(word) > s){
                                                    strncpy(msg, word+s, socketBufferSize);
                                                    
                                                    if(write(newsock, msg, socketBufferSize) < 0){                     //passes case to monitors piece by piece according to its size
                                                        perror("Monitor: Error in writing!"); exit(1);
                                                    }     
                                                    
                                                    s+=socketBufferSize;           
                                                }                                                                                 
                                                                                    
                        
                                                if(write(newsock, "!", socketBufferSize) < 0){                            
                                                    perror("Monitor: Error in writing!"); exit(1);
                                                }

                                                free(word);
                                            }
                                        }
                                        for(int i = 0; i < 3; i++){
                                            free(date1[i]); free(date2[i]); free(date3[i]); 
                                        }                                       
                                        free(date1); free(date2); free(date3);
                                    }
                                    t = t->next;  
                                }
                            }
                            else{
                                total_rejected++;
                                strcpy(msg, "");
                                char* word = (char*)malloc(strlen("NO") + strlen("case2") + 6);
                                strcpy(word, "");
                                strcpy(word, "NO");
                                strcat(word, "/");
                                strcat(word, "case2");
                                strcat(word, "/");
                                
                                int s = 0; 
                                        
                                while(strlen(word) > s){
                                    strncpy(msg, word+s, socketBufferSize);
                                    
                                    if(write(newsock, msg, socketBufferSize) < 0){                     //passes case to monitors piece by piece according to its size
                                        perror("Monitor: Error in writing!"); exit(1);
                                    }     
                                    
                                    s+=socketBufferSize;           
                                }                                                                                 
                                                                        
            
                                if(write(newsock, "!", socketBufferSize) < 0){                            
                                    perror("Monitor: Error in writing!"); exit(1);
                                }

                                free(word);
                            } 
                        }
                        temp = temp->next;
                    }
                    for(int i = 0; i < num; i++)
                        free(Command[i]);
                    free(Command);
                }

            }

            /* addVaccinationRecords */
            if(strstr(quest, "addVaccinationRecords") != 0){
                
                while(quest[i] != '\0'){
                    if(quest[i] == ' ')
                        counter++;
                    i++;
                }

                if(counter == 1)
                    num = 2;   
                else
                    num = 0;                                       //used to check many cases of command
                
                if(num != 0){
                    Command = get_command_monitor(quest, num);
                    strcpy(quest, "");

                    char* file = (char*)malloc(sizeof(char) * 190);
                    char** file_new = (char**)malloc(sizeof(char*) * 1000);

                    int tc = 0;
                    for(int j = 0; j < num_of_paths; j++){
        
                        struct dirent *de1;                     //pointer for subdirectory entry
                        DIR *dr1 = opendir(paths[j]);            //opens subdirectory

                        if(dr1 == NULL){                                       //returns NULL if couldn't open subdirectory
                            printf("Couldn't open current subdirectory!");
                            return 0;
                        }
                        
                        while((de1 = readdir(dr1)) != NULL){

                            int flag = 0;
                            if(de1->d_type == DT_REG){               //if it's a regular file
                                strcpy(file, "");
                                strcpy(file, paths[j]);
                                strcat(file, "/");
                                strcat(file, de1->d_name);      //create filename
                                            
                                txt* t = t_head->next;
                                while(t != NULL){
                                    if(strcmp(t->filename, file) == 0){
                                        flag = 1;
                                        break;
                                    }
                                    t = t->next;
                                }

                                
                                if(flag == 0){
                                    printf("New records were found!\n");

                                    file_new[tc] = (char*)malloc(sizeof(char*) * 200);
                                    strcpy(file_new[tc], file);
                                    
                                    tc++;
                                        
                                }
                            }

                        }
                        closedir(dr1);                         //close subdirectory

                    }    

                    int k = 0, cnt = argss->cycbuff->counter;
                    while(cnt > 0){                            //clearing the buffer 
                        free(argss->cycbuff->data[k]);
                        k++;
                        cnt--;
                    }
                    free(argss->cycbuff->data);

                    init_buff(argss->cycbuff, cyclicBufferSize);

                    int p = 0, buff_cnt = 0;                                //counter pou metraei posa mpikan sinolika
                    while(p < tc && argss->cycbuff->counter < cyclicBufferSize){
                        place_path(argss, file_new[p]);  
                        p++; 
                        buff_cnt++;
                    } 

                    num_of_txts = tc;
   
                    while(buff_cnt <= tc){
                        
                        while(argss->cycbuff->counter > 0 || sig_cnt < numThreads){                                            
                            pthread_cond_wait(&empty, &mutex);
                        }

                        if(buff_cnt == tc){
                            break;
                        }

                        pthread_mutex_unlock(&mutex);
                        pthread_mutex_lock(&mutex);

                        for(int i = 0; i < cyclicBufferSize; i++)                   //clearing the buffer 
                            free(argss->cycbuff->data[i]);
                        free(argss->cycbuff->data);

                        init_buff(argss->cycbuff, cyclicBufferSize);                      //clearing counters and setting a clear array buffer

                        int j = num_of_txts - buff_cnt;

                        int p = num_of_txts - j;                                         //finding the path to place in buffer
                        
                        while(p < tc && argss->cycbuff->counter < cyclicBufferSize){
                            place_path(argss, file_new[p]);  
                            p++; 
                            buff_cnt++;
                        }
                        
                        pthread_cond_broadcast(&full);
                        
                        pthread_mutex_unlock(&mutex);
                        
                    }

                    free(file);                                //finds new records and puts them in array

                    for(int i = 0; i < tc; i++)
                        free(file_new[i]);
                    free(file_new);
                                                            
                }

                tempb = argss->blms;
                
                while(tempb != NULL){
                    strcpy(k, "");
                    char* word = (char*)malloc(strlen(tempb->virus) + strlen(tempb->bit_array) + 6);
                    strcpy(word, "");
                    strcpy(word, tempb->virus);
                    strcat(word, "/");
                    strcat(word, tempb->bit_array);
                    strcat(word, "/");
                    
                    int s = 0; 
                            
                    while(strlen(word) > s){
                        strncpy(k, word+s, socketBufferSize);
                        
                        if(write(newsock, k, socketBufferSize) < 0){                     //passes path to monitors piece by piece according to its size
                            perror("Monitor: Error in writing!"); exit(1);
                        }     
                        
                        s+=socketBufferSize;           
                    }
                    
                    if(write(newsock, "@", socketBufferSize) < 0){                     //sends ! to travelmonitor, to know that monitor's ready for quests
                        perror("Monitor: Error in writing!"); exit(1);
                    }

                    free(word);

                    tempb = tempb->next;
                }

                if(write(newsock, "!", socketBufferSize) < 0){                     //sends ! to travelmonitor, to know that monitor's ready for quests
                    perror("Monitor: Error in writing!"); exit(1);
                }

                for(int i = 0; i < num; i++)
                    free(Command[i]);
                free(Command);

            }

            /* searchVaccinationStatus */
            if(strstr(quest, "searchVaccinationStatus") != 0){
            
                while(quest[i] != '\0'){
                    if(quest[i] == ' ')
                        counter++;
                    i++;
                }

                if(counter == 1)
                    num = 2;   
                else
                    num = 0;                                       //used to check many cases of command
                
                if(num != 0){
                    Command = get_command_monitor(quest, num);
                    strcpy(quest, "");

                    int flag = 0;
                    record* temp = head->next;
                    while(temp != NULL){
                        if(strcmp(temp->id, Command[1]) == 0){
                            //sends info of citizenID in travelmonitor
                            flag = 1;
                            strcpy(k, "");
                            char* word = (char*)malloc(strlen(temp->id) + strlen(temp->firstName) + strlen(temp->lastName) +
                            strlen(temp->country) + strlen(temp->age) + 20);
                            strcpy(word, "");
                            strcpy(word, temp->id); strcat(word, " "); strcat(word, temp->firstName); strcat(word, " ");
                            strcat(word, temp->lastName); strcat(word, " "); strcat(word, temp->country); strcat(word, "\n");
                            strcat(word, "AGE ");strcat(word, temp->age);
                                                                                   
                            int s = 0; 
                                    
                            while(strlen(word) > s){
                                strncpy(k, word+s, socketBufferSize);
                                
                                if(write(newsock, k, socketBufferSize) < 0){                     //passes info to monitors piece by piece according to its size
                                    perror("Monitor: Error in writing!"); exit(1);
                                }     
                                
                                s+=socketBufferSize;           
                            }
                            
                            free(word);

                            if(write(newsock, "\n", socketBufferSize) < 0){                     //sends ! to travelmonitor, to know that monitor's ready for quests
                                perror("Monitor: Error in writing!"); exit(1);
                            }                            

                            record* t = head->next;
                            while(t != NULL){
                                if(strcmp(temp->id, t->id) == 0){
                                    //sends info for every virus a citizenID was vaccinated for or not in travelmonitor
                                    if(strcmp(t->yesNo, "YES") == 0){
                                        strcpy(k, "");
                                        char* word = (char*)malloc(strlen(t->virus) + strlen(t->date) + 25);
                                        strcpy(word, "");
                                        strcpy(word, t->virus); strcat(word, " VACCINATED ON "); strcat(word, t->date);
                                                                                  
                                        int s = 0; 
                                                
                                        while(strlen(word) > s){
                                            strncpy(k, word+s, socketBufferSize);
                                            
                                            if(write(newsock, k, socketBufferSize) < 0){                     //passes info to monitors piece by piece according to its size
                                                perror("Monitor: Error in writing!"); exit(1);
                                            }     
                                            
                                            s+=socketBufferSize;           
                                        }
                                        
                                        free(word);

                                        if(write(newsock, "\n", socketBufferSize) < 0){                     //sends ! to travelmonitor, to know that monitor's ready for quests
                                            perror("Monitor: Error in writing!"); exit(1);
                                        }
                                        
                                    }
                                    else{    
                                        strcpy(k, "");
                                        char* word = (char*)malloc(strlen(t->virus) + 25);
                                        strcpy(word, "");
                                        strcpy(word, t->virus); strcat(word, " NOT YET VACCINATED");
                                                                                                                           
                                        int s = 0; 
                                                
                                        while(strlen(word) > s){
                                            strncpy(k, word+s, socketBufferSize);
                                            
                                            if(write(newsock, k, socketBufferSize) < 0){                     //passes info to monitors piece by piece according to its size
                                                perror("Monitor: Error in writing!"); exit(1);
                                            }     
                                            
                                            s+=socketBufferSize;           
                                        }
                                        
                                        free(word);

                                        if(write(newsock, "\n", socketBufferSize) < 0){                     //sends ! to travelmonitor, to know that monitor's ready for quests
                                            perror("Monitor: Error in writing!"); exit(1);
                                        }
                                        
                                    }
                                }
                                t = t->next;
                            }

                            if(write(newsock, "!", socketBufferSize) < 0){                     //sends ! to travelmonitor, to know that monitor's ready for quests
                                perror("Monitor: Error in writing!"); exit(1);
                            }

                            break;
                        }
                        temp = temp->next;
                    }

                    if(flag == 0){
                        if(write(newsock, "!", socketBufferSize) < 0){                     //sends ! to travelmonitor, to know that monitor's ready for quests
                            perror("Monitor: Error in writing!"); exit(1);
                        }
                    }

                    for(int i = 0; i < num; i++)
                        free(Command[i]);
                    free(Command);
                }
            }
        }

    }

    ///////////////////////////////////////////////////// free destroy close ///////////////////////////////////////////////////// 

    flag_exit = 1;                                      //gives the "signal" to obtain that threads are ready to exit
    pthread_mutex_unlock(&mutex);
    pthread_mutex_lock(&mutex);
    
    argss->cycbuff->counter = 1;
    pthread_cond_broadcast(&full);
        
    pthread_mutex_unlock(&mutex);

    for(int i = 0; i < numThreads; i++)
        pthread_join(thr_ids[i], NULL); 
        
    if(pthread_cond_destroy(&empty) != 0){
        printf("Error in destroying cond_empty!\n"); exit(1);
    }

	if(pthread_cond_destroy(&full) != 0){
        printf("Error in destroying cond_full!\n"); exit(1);
    }    

    if(pthread_mutex_destroy(&mtx) != 0){
        printf("Error in destroying mtx!\n"); exit(1);
    }                                                                               //destroying mutexes and cond vars

    free(thr_ids);  

    delete_cycbuff(argss->cycbuff);

    for(int i = 0; i < txt_cnt; i++)
        free(filename[i]);
    free(filename);

    free(paths);

    free_record(head);
    free_virus(virus_head);
    free_txt(t_head);
    free_country(c_head);
    bloom_free(bloom_head);

    free(argss);
    free(quest);                                                                //frees everything

    printf("Closing connection.\n");
    close(newsock);	                                                           //closes sockets

    exit(0);                                                                    //exits

} //end of monitorServer

////////////////////////////////////// cyclic buffer //////////////////////////////////////////////////////////////////////////

//functions used for cyclic buffer, the struct and the declaration of them are included by cyclic_buff.h

void init_buff(cycBuff* cyc_buff, int cyclicBufferSize){
    
    cyc_buff->start = 0;
    cyc_buff->end = -1;
    cyc_buff->counter = 0;
    cyc_buff->capacity = cyclicBufferSize;
    cyc_buff->data = (char**)malloc(sizeof(char*) * cyclicBufferSize);        

}

void place_path(args* argss, char* paths){
    
    argss->cycbuff->end = (argss->cycbuff->end + 1) % argss->cycbuff->capacity;
    argss->cycbuff->data[argss->cycbuff->end] = (char*)malloc(sizeof(char) * 190);
    strcpy(argss->cycbuff->data[argss->cycbuff->end], "");
    strcpy(argss->cycbuff->data[argss->cycbuff->end], paths);
    argss->cycbuff->counter++;                                  
   
}   //every time a new path comes in, allocate space for it and increase the counter of the buffer

void* obtain_path(void* argss){
    
    args* argus = (args*) argss;
    
    while(1){

        pthread_mutex_lock(&mutex);

        while(argus->cycbuff->counter <= 0){                        //if buffer empty
            sig_cnt++;
            pthread_cond_signal(&empty);                            //signal empty cond var and put on wait the full one
            pthread_cond_wait(&full, &mutex);
        }

        pthread_mutex_unlock(&mutex);

        if(flag_exit == 1){                                         //when the flag is 1 it means the threads are done with their job
            pthread_exit(0);
        }

        char* path = (char*)malloc(sizeof(char) * 190);
        strcpy(path, "");

        pthread_mutex_lock(&mtx);
        
        if(argus->cycbuff->counter > 0){
            strcpy(path, argus->cycbuff->data[argus->cycbuff->start]);
            argus->cycbuff->start = (argus->cycbuff->start + 1) % argus->cycbuff->capacity;         //change the data in cycbuff
            
            record* node = argus->recs;

            txt_push(argus->txts, path);
            node = get_record(node, path);  //built list with records   

            argus->recs = node;

            /////////////////////////////////////////////////// creating bloom filters //////////////////////////////////////////////
            
            int flg = 0;
            int bloomSize = sizeOfBloom;
            
            bloom* bloom_head = argus->blms;
            record* temp = head;                                 //initializes with head->next because the head is empty
            
            if(strcpy(bloom_head->virus, "-1") == 0){
                while(temp != NULL){
                    if(strcmp(temp->yesNo, "YES") == 0){
                        bloom_push(bloom_head, temp, bloomSize);
                        bloom_head->next = NULL;
                        flg = 1;
                        break;
                    }
                    temp = temp->next;
                }                                                           //creates the head of bloom filter

                if(flg == 0){
                    bloom_head->virus = (char*)malloc(sizeof(char) * 60);
                    strcpy(bloom_head->virus, "-1");							
                
                    bloom_head->bit_array = (char*)malloc(sizeof(char) * bloomSize);
                    strcpy(bloom_head->bit_array, "-1");

                    bloom_head->next = NULL;

                    //in this case no people in this monitor are vaccinated
                }

                else{
                    temp = head;
                    bloom* temp_b;

                    int flag;

                    while(temp != NULL){
                        if(strcmp(temp->yesNo, "YES") == 0){
                            
                            flag = 0;
                            temp_b = bloom_head;    

                            while(temp_b != NULL){
                                if(strcmp(temp_b->virus, temp->virus) == 0){        //if the virus exists in bloom filter
                                    bloom_insert(temp_b, temp->id, bloomSize);      //just insert the id in the correct bloom filter
                                    flag = 1;
                                    break; 
                                }
                                else
                                    temp_b = temp_b->next;
                                
                            }                                               

                            if(flag == 0){                                          //if virus not existant in bloom filter yet
                                bloom* node_b = (bloom*)malloc(sizeof(bloom));
                                bloom_push(node_b, temp, bloomSize);                //create new bloom node with the virus
                                temp_b = bloom_head;
                                while(temp_b->next != NULL)
                                    temp_b = temp_b->next;
                                
                                temp_b->next = node_b;
                                node_b->next = NULL;                                //place new bloom node in the correct position
                                
                            }
                        }
                        temp = temp->next;
                    }
                    
                }

            }

            else{
                temp = head;
                bloom* temp_b;

                int flag;

                while(temp != NULL){
                    if(strcmp(temp->yesNo, "YES") == 0){
                        
                        flag = 0;
                        temp_b = bloom_head;    

                        while(temp_b != NULL){
                            if(strcmp(temp_b->virus, temp->virus) == 0){        //if the virus exists in bloom filter
                                bloom_insert(temp_b, temp->id, bloomSize);      //just insert the id in the correct bloom filter
                                flag = 1;
                                break; 
                            }
                            else
                                temp_b = temp_b->next;
                            
                        }                                               

                        if(flag == 0){                                          //if virus not existant in bloom filter yet
                            bloom* node_b = (bloom*)malloc(sizeof(bloom));
                            bloom_push(node_b, temp, bloomSize);                //create new bloom node with the virus
                            temp_b = bloom_head;
                            while(temp_b->next != NULL)
                                temp_b = temp_b->next;
                            
                            temp_b->next = node_b;
                            node_b->next = NULL;                                //place new bloom node in the correct position
                            
                        }
                    }
                    temp = temp->next;
                }
                
            }

            ////////////////////////////////////// list of countries in monitor /////////////////////////////////////////////
            
            record* temp_r = head->next;
            country* temp_c;
            
            while(temp_r != NULL){
                int flag = 0;
                temp_c = argus->cntrs;    

                while(temp_c != NULL){                                                  
                    if(strcmp(temp_c->country, temp_r->country) == 0){                    //if country exists in country list
                        flag = 1;
                        break;
                    }
                    else
                        temp_c = temp_c->next;
                }

                if(flag == 0)                                                        //creates new country node      
                    country_push(argus->cntrs, temp_r->country);                                  
            
                temp_r = temp_r->next;
            } 

            //////////////////////////////////////////////// creates viruses list  ////////////////////////////////////////////////
            
            temp = head;
            virus* temp_v;
            
            while(temp != NULL){
                    
                int flag = 0;
                temp_v = argus->vrs;    

                while(temp_v != NULL){
                    if(strcmp(temp_v->virus_name, temp->virus) == 0){                   //if virus exists in virus list 
                        if(strcmp(temp->yesNo, "YES") == 0)                             //check if the id is vaccinated
                            skiplist_insert(temp_v->vaccinated_persons, temp->id);      //if yes insert it in the vaccinated skiplist of the virus
                        else
                            skiplist_insert(temp_v->not_vaccinated_persons, temp->id);  //if not insert it in the not vaccinated skiplist of the virus
                        
                        flag = 1;
                        break;
                    }
                    else
                        temp_v = temp_v->next;
                }

                if(flag == 0){                                                          //if virus non existant in virus list yet       
                    virus* node_v = (virus*)malloc(sizeof(virus));
                    virus_push(node_v, temp);                                           //create new virus node for the virus
                    temp_v = argus->vrs;
                    while(temp_v->next != NULL)
                        temp_v = temp_v->next;
                    
                    temp_v->next = node_v;
                    node_v->next = NULL;                                                //place the virus node on the correct position

                    if(strcmp(temp->yesNo, "YES") == 0)                                 //check if the id is vaccinated
                        skiplist_insert(node_v->vaccinated_persons, temp->id);          //if yes insert it in the vaccinated skiplist of the virus
                    else
                        skiplist_insert(node_v->not_vaccinated_persons, temp->id);      //if not insert it in the not vaccinated skiplist of the virus
                }
            
                temp = temp->next;
            }
            
            argus->cycbuff->counter--;                                          //decrease counter in cycbuff for checking of capacity
        }
        
        pthread_mutex_unlock(&mtx);
        
        free(path);

        usleep(500000);
    
    }

}

void delete_cycbuff(cycBuff* cyc_buff){
    
    int k = 0, cnt = cyc_buff->counter;
    while(cnt > 0){                            //clearing the buffer 
        free(cyc_buff->data[k]);
        k++;
        cnt--;
    }
    free(cyc_buff->data);

    free(cyc_buff);

}   //frees cyclic's buffer memory