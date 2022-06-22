#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <unistd.h>
#include <sys/types.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

#include "extra_lists.h"
#include "bloom_travel.h"
#include "get_string.h"

int compare( const void* a, const void* b ){
  return strcmp( *(const char**)a, *(const char**)b );
}

int main(int argc, char *argv[]){
    int numMonitors = atoi(argv[2]);
    int bufferSize = atoi(argv[4]);
    char* sizeOfBloom = argv[8];
    char* input_dir = (char*)malloc(sizeof(char*) * 60);
    strcpy(input_dir, "");
    strcat(input_dir, "./");
    strcat(input_dir, argv[10]);                                 //takes arguments from command line

    //////////////////////////////  taking the subdirectories names  ////////////////////////////////////////

    struct dirent *de; 
    DIR *dr = opendir(input_dir);                       //opens directory 

    if(dr == NULL){                                        //returns NULL if couldn't open directory
        printf("Couldn't open current directory!");
        free(input_dir);                 
        closedir(dr);                                  //close directory
        return 0;
    }

    char** filename = (char**)malloc(sizeof(char*) * 1000);

    country* country_head = (country*)malloc(sizeof(country));                  //creates country head
    country_head->country = (char*)malloc(sizeof(char) * 60);
    country_head->next = NULL;
    
    int files = 0;
    while((de = readdir(dr)) != NULL){                          //reads directory given

        if(strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0)
	        continue;

        filename[files] = (char*)malloc(strlen(de->d_name) + 1);

        strcpy(filename[files], de->d_name);                                //array with subdirectories with names of the countries

        if(files == 0)
            strcpy(country_head->country, "-1");                               //sets name of country in head  as empty
        
        country_push(country_head, filename[files]);                        //pushes new country into countries list

        files++;
    }

    qsort(filename, files, sizeof(char*), compare);      //sorting the array

    /////////////////////////////// in case of more monitors than countries /////////////////////////////////////
    
    if(files < numMonitors){
        int counter = numMonitors - files;

        numMonitors = numMonitors - counter;
    }

    ///////////////////////////////////  passing the paths to each monitor with round robin  /////////////////////////////////////////
    
    char* dirname = (char*)malloc(sizeof(char) * 120);
    int i = 0;
    char* path[files];
    int counter[numMonitors];

    for(int i = 0; i < numMonitors; i++)
        counter[i] = 0;
    
    for(int k = 0; k < files; k++){
        
        strcpy(dirname, "");
        strcpy(dirname, input_dir);
        strcat(dirname, "/");
        strcat(dirname, filename[k]);            //create subdirectory path names
        
        path[k] = (char*)malloc(strlen(dirname) + 1);
        strcpy(path[k], dirname);      
        
        counter[i]++;

        country* temp = country_head;
        while(temp != NULL){
            if(strcmp(temp->country, filename[k]) == 0)
                temp->monitor = i;
            temp = temp->next;
        }
        
        i++;
        if(i == numMonitors)                            //round robin, selection of monitor
            i = 0;
    }                

    /////////////////////////////// sockets and ports /////////////////////////////////////////////////////

    char* port = (char*)malloc(sizeof(char) * 100);
    strcpy(port, "8000");

    int sock[numMonitors];
    struct sockaddr_in mon_server;
    struct sockaddr *mon_serverptr = (struct sockaddr*) &mon_server;
    struct hostent *myip;
    
    //////////////////////////////// creation of monitors /////////////////////////////////////////////////

    int pid[numMonitors];                                           //table with monitors

    int j, c, num, p = 0;
    for(int i = 0; i < numMonitors; i++){

        char* pathPerMonitor[counter[i]];
        j = i;
        c = 0;
        num = counter[i];
        while(num != 0){
            pathPerMonitor[c] = (char*)malloc(strlen(path[j]) + 1);
            strcpy(pathPerMonitor[c], path[j]);
            j += numMonitors;
            c++;
            num--;
        }   //creates table with paths for each monitor to be used as argument for execvp

        int space = counter[i] + 12;

        char** args = (char**)malloc(sizeof(char*) * space);
            
        args[0] = "./monitorServer"; args[2] = port; args[4] = argv[12]; args[6] = argv[4]; args[8] = argv[6]; args[10] = argv[8];
        args[1] = "-p"; args[3] = "-t"; args[5] = "-b"; args[7] = "-c"; args[9] = "-s";
        
        int k = 0;
        for(int j = 11; j < space - 1; j++){
            args[j] = pathPerMonitor[k];
            k++;
        }

        args[space - 1] = NULL;

        pid[i] = fork();                                            //creates child-monitor
        if(pid[i] < 0){
            perror("Can't fork! Error occured!");
            exit(1);
        }

        if(pid[i] == 0){
            pid[i] = getpid();                                  //gives monitors unique ids
            
            execvp("./monitorServer", args);         //calls child with its pipe names as arguments
            perror("execvp");

        }

        //parent here
        for(int j = 0; j < counter[i]; j++)
            free(pathPerMonitor[j]);
           
        free(args);

        if((sock[i] = socket(AF_INET, SOCK_STREAM, 0)) < 0){       //creation of socket
            perror("socket"); exit(1);
        }

        if((myip = gethostbyname("localhost")) == NULL){
            herror("gethostbyname"); exit(1);
        }

        mon_server.sin_family = AF_INET;                    //internet address family
        memcpy(&mon_server.sin_addr, myip->h_addr, myip->h_length) ;
        mon_server.sin_port = htons(atoi(port));                  //port to bind socket

        int status;
        do{
            status = connect(sock[i], mon_serverptr, sizeof(mon_server));
        }while(status < 0);
        printf("Connecting to port %d\n", atoi(port));        
        
        p++;
        strcpy(port, "");
        sprintf(port, "800%d", p);                //new port for each monitor, serial way
    }  

    /////////////////////////////////////////// receiving bloomfilters for every virus //////////////////////
    
    bloomfilter* b_head = (bloomfilter*)malloc(sizeof(bloomfilter));
    b_head->virus = (char*)malloc(sizeof(char) * 60);
    strcpy(b_head->virus, "");
    b_head->filter = (char*)malloc(sizeof(char) * atoi(sizeOfBloom) + 1);
    strcpy(b_head->filter, "");
    b_head->monitor = -1;
    b_head->next = NULL;
    
    int r; char* bloom_virus; char* bloomFilter;
    char content[bufferSize]; strcpy(content, "");
    char* text = (char*)malloc(sizeof(char) * ( atoi(sizeOfBloom) + 61 ) + 1);
    strcpy(text, ""); 

    for(int i = 0; i < numMonitors; i++){
        
        while(1){
            
            if(content[0] != '!'){
                if((r = read(sock[i], content, bufferSize)) < 0){              //reads content
                    perror("TravelMonitor: Error in reading!"); exit(1);
                }
                
                content[r] = '\0';
                strcat(text, content);

                if(content[0] == '@'){
                    bloom_virus = strtok(text, "/");
                    bloomFilter = strtok(NULL, "/");                            //take virus and filter with strtok
                    
                    bloomfilter_push(b_head, bloom_virus, bloomFilter, i, atoi(sizeOfBloom));      //create new bloom node

                    strcpy(text, "");
                    strcpy(content, "");
                }
            }

            else{
                strcpy(text, "");
                strcpy(content, "");
                break;
            }

        }

        printf("TravelMonitor was informed that Monitor %d is ready for quests!\n", i);
    }

    /////////////////////////////////////////// time for quests /////////////////////////////////////////////
    
    stats* stats_head = (stats*)malloc(sizeof(stats));
    stats_head->countryTo = (char*)malloc(sizeof(char) * 60); strcpy(stats_head->countryTo, "-1");
    stats_head->date = (char*)malloc(sizeof(char) * 60); strcpy(stats_head->date, "-1");
    stats_head->virus = (char*)malloc(sizeof(char) * 60); strcpy(stats_head->virus, "-1");
    stats_head->accept = 0; stats_head->reject = 0;
    stats_head->next = NULL;

    int total_requests = 0;
    int total_accepted = 0;
    int total_rejected = 0;

    char buffer[BUFSIZ];

    char msg[bufferSize];

    while(1){    

        sleep(1);       
        
        strcpy(buffer, "");
        printf("Enter command: ");
        fgets(buffer, BUFSIZ, stdin);               //reads command

        char** Command;

        int num, counter = 0, i = 0;

        /* travelRequest */
        if(strstr(buffer, "travelRequest") != 0){
            int flag = 0;
            
            while(buffer[i] != '\n'){
                if(buffer[i] == ' ')
                    counter++;
                i++;
            }

            if(counter == 5)
                num = 6;   
            else
                num = 0;                                       //used to check many cases of command
            
            if(num != 0)
                Command = get_command(buffer, num);

            if(num == 6){
                total_requests++;

                int check, flag_b = 0;

                country* temp = country_head;
                while(temp != NULL){
                    if(strcmp(temp->country, Command[3]) == 0){
                        bloomfilter* temp_b = b_head;
                        while(temp_b != NULL){
                            if(temp_b->monitor == temp->monitor && strcmp(temp_b->virus, Command[5]) == 0){
                                int ch = bloom_check(temp_b, sizeOfBloom, Command[1]);
                                if (ch == 1){
                                    total_rejected++;
                                    check = 0;
                                }
                                                        
                                else {
                                    int s = 0;
                                    char* com = strtok(buffer,"\n");

                                    strcpy(msg, "");

                                    while(strlen(com) > s){
                                        strncpy(msg, com+s, bufferSize);
                                            
                                        if(write(sock[temp->monitor], msg, bufferSize) < 0){                     //passes msg to monitors piece by piece according to its size
                                            perror("TravelMonitor: Error in writing!"); exit(1);
                                        }    
                                        
                                        s+=bufferSize;           
                                    }

                                    if(write(sock[temp->monitor], "!", bufferSize) < 0){                     //sends ! to monitors, for them to know when to stop receiving content
                                        perror("TravelMonitor: Error in writing!"); exit(1);
                                    }

                                    strcpy(text, ""); strcpy(content, "");
                                    char* ans; char* c;
                                    while(1){
            
                                        if(content[0] != '!'){
                                            if((r = read(sock[temp->monitor], content, bufferSize)) < 0){              //reads content
                                                perror("TravelMonitor: Error in reading!"); exit(1);
                                            }
                                            content[r] = '\0';
                                            strcat(text, content);
                                        }

                                        else{
                                            ans = strtok(text, "/");
                                            c = strtok(NULL, "/");                          //separates info from monitor
                                            
                                            break;
                                        }

                                    }

                                    printf("Answer from Monitor: %s ", ans);

                                    if(strcmp(ans, "YES") == 0){
                                        total_accepted++;
                                        check = 1;                                                              
                                        printf(": Vaccinated on %s : REQUEST ACCEPTED – HAPPY TRAVELS", c);
                                    }

                                    else{
                                        total_rejected++;
                                        check = 0;                                       

                                        if(strcmp(c, "case1") == 0)
                                            printf(": REQUEST REJECTED – YOU WILL NEED ANOTHER VACCINATION BEFORE TRAVEL DATE\n");
                                        else
                                            printf(": REQUEST REJECTED – YOU ARE NOT VACCINATED\n");
                                    }

                                    printf("\n");
                                }                           //prints the messages requested

                                flag_b = 1;
                                break;
                            }
                            
                            temp_b = temp_b->next;
                        }

                        if(flag_b == 0){                                                            //in any other case
                            printf("NO : REQUEST REJECTED – YOU ARE NOT VACCINATED\n");             //citizenID surely not vaccinated
                            total_rejected++;
                            check = 0;
                        }
                    }
                    temp = temp->next;
                }

                stats_push(stats_head, Command[4], Command[2], Command[5], check);      //creates new statistics node using requests counters

                for(int i = 0; i < num; i++)
                    free(Command[i]);
                free(Command);
            }
            else
                printf("ERROR!\n");
        }
    
        /* travelStats */
        if(strstr(buffer, "travelStats") != 0){

            int total = 0, accept = 0, reject = 0;
            
            while(buffer[i] != '\n'){
                if(buffer[i] == ' ')
                    counter++;
                i++;
            }

            if(counter == 4)
                num = 5;   
            else if(counter == 3)
                num = 4;                                       
            else
                num = 0;                            //used to check many cases of command
            
            if(num != 0)
                Command = get_command(buffer, num);

            if(num == 5){                       //virusName date1 date2 country
                stats* temp = stats_head;
                while(temp != NULL){
                    if(strcmp(temp->countryTo, Command[4]) == 0){
                        if(strcmp(temp->virus, Command[1]) == 0){
                            char** date1 = get_date(temp->date);
                            char** date2 = get_date(Command[2]);
                            char** date3 = get_date(Command[3]);                //get the date limits from the command and the current date we are about to examine

                            int flag1 = 0, flag2 = 0;

                            if(strcmp(temp->date, Command[2]) == 0 || strcmp(temp->date, Command[3]) == 0){
                                total++;
                                if(temp->accept == 1)
                                    accept++;
                                else if(temp->reject == 1) 
                                    reject++;
                            }
                                
                            else{
                                if(strcmp(date2[2], date1[2]) < 0)
                                    flag1 = 1;
                                else if(strcmp(date2[2], date1[2]) == 0 && strcmp(date2[1], date1[1]) < 0)
                                    flag1 = 1;
                                else if(strcmp(date2[2], date1[2]) == 0 && strcmp(date2[1], date1[1]) == 0 && strcmp(date2[0], date1[0]) < 0)
                                    flag1 = 1;
                                
                                if(strcmp(date1[2], date3[2]) < 0)
                                    flag2 = 1;
                                else if(strcmp(date1[2], date3[2]) == 0 && strcmp(date1[1], date3[1]) < 0)
                                    flag2 = 1;
                                else if(strcmp(date1[2], date3[2]) == 0 && strcmp(date1[1], date3[1]) == 0 && strcmp(date1[0], date3[0]) < 0)
                                    flag2 = 1;

                                if(flag1 == 1 && flag2 == 1){
                                    total++;
                                    if(temp->accept == 1)
                                        accept++;
                                    else if(temp->reject == 1) 
                                        reject++;
                                }
                            
                            }                                                   //checking if the current date is inbetween the dates given by user and increasing counter if positive

                            for(int i = 0; i < 3; i++){
                                free(date1[i]); free(date2[i]); free(date3[i]);
                            }
                            free(date1); free(date2); free(date3);
                            
                        }
                    }
                    temp = temp->next;
                }

                printf("%s:\nTOTAL REQUESTS: %d\nACCEPTED: %d\nREJECTED: %d\n", Command[4], total, accept, reject);
                
                for(int i = 0; i < num; i++)
                    free(Command[i]);
                free(Command);
            }
            
            else if(num == 4){                  //virusName date1 date2
                
                int flag = 0;
                
                stats* temp = stats_head;
                while(temp != NULL){
                    if(strcmp(temp->virus, Command[1]) == 0){
                        flag = 1;
                        break;
                    }
                    temp = temp->next;
                }

                if(flag == 1){
                    temp = stats_head;
                    while(temp != NULL){
                        if(strcmp(temp->virus, Command[1]) == 0){
                            char** date1 = get_date(temp->date);
                            char** date2 = get_date(Command[2]);
                            char** date3 = get_date(Command[3]);                //get the date limits from the command and the current date we are about to examine

                            int flag1 = 0, flag2 = 0;

                            if(strcmp(temp->date, Command[2]) == 0 || strcmp(temp->date, Command[3]) == 0){
                                total++;
                                if(temp->accept == 1)
                                    accept++;
                                else if(temp->reject == 1) 
                                    reject++;
                            }
                                
                            else{
                                if(strcmp(date2[2], date1[2]) < 0)
                                    flag1 = 1;
                                else if(strcmp(date2[2], date1[2]) == 0 && strcmp(date2[1], date1[1]) < 0)
                                    flag1 = 1;
                                else if(strcmp(date2[2], date1[2]) == 0 && strcmp(date2[1], date1[1]) == 0 && strcmp(date2[0], date1[0]) < 0)
                                    flag1 = 1;
                                
                                if(strcmp(date1[2], date3[2]) < 0)
                                    flag2 = 1;
                                else if(strcmp(date1[2], date3[2]) == 0 && strcmp(date1[1], date3[1]) < 0)
                                    flag2 = 1;
                                else if(strcmp(date1[2], date3[2]) == 0 && strcmp(date1[1], date3[1]) == 0 && strcmp(date1[0], date3[0]) < 0)
                                    flag2 = 1;

                                if(flag1 == 1 && flag2 == 1){
                                    total++;
                                    if(temp->accept == 1)
                                        accept++;
                                    else if(temp->reject == 1) 
                                        reject++;
                                }
                            
                            }                                                   //checking if the current date is inbetween the dates given by user and increasing counter if positive

                            for(int i = 0; i < 3; i++){
                                free(date1[i]); free(date2[i]); free(date3[i]);
                            }
                            free(date1); free(date2); free(date3);
                            
                        }
                        temp = temp->next;
                    }

                    printf("TOTAL REQUESTS: %d\nACCEPTED: %d\nREJECTED: %d\n", total, accept, reject);
                }

                else if(flag == 0)
                    printf("ERROR!\n");

                for(int i = 0; i < num; i++)
                    free(Command[i]);
                free(Command);
            }
            
            else
                printf("ERROR!\n");

        }
        
        /* addVaccinationRecords */
        if(strstr(buffer, "addVaccinationRecords") != 0){

            while(buffer[i] != '\n'){
                if(buffer[i] == ' ')
                    counter++;
                i++;
            }

            if(counter == 1)
                num = 2;                                        
            else
                num = 0;                            //used to check many cases of command
            
            if(num != 0)
                Command = get_command(buffer, num);
            
            if(num == 2){
                
                int monitor_num;
                country* temp = country_head;
                while(temp != NULL){
                    if(strcmp(temp->country, Command[1]) == 0){
                        monitor_num = temp->monitor;

                        int s = 0;
                        char* com = strtok(buffer,"\n");

                        strcpy(msg, "");

                        while(strlen(com) > s){
                            strncpy(msg, com+s, bufferSize);
                                
                            if(write(sock[temp->monitor], msg, bufferSize) < 0){                     //passes msg to monitors piece by piece according to its size
                                perror("TravelMonitor: Error in writing!"); exit(1);
                            }    
                            
                            s+=bufferSize;           
                        }

                        if(write(sock[temp->monitor], "!", bufferSize) < 0){                     //sends ! to monitors, for them to know when to stop receiving content
                            perror("TravelMonitor: Error in writing!"); exit(1);
                        }

                        break;
                    }
                    temp = temp->next;
                }
                
                strcpy(content, ""); strcpy(text, ""); 
                    
                while(1){
                    
                    if(content[0] != '!'){
                        if((r = read(sock[monitor_num], content, bufferSize)) < 0){              //reads content
                            perror("TravelMonitor: Error in reading!"); exit(1);
                        }
                        
                        content[r] = '\0';
                        strcat(text, content);

                        if(content[0] == '@'){
                            bloom_virus = strtok(text, "/");
                            bloomFilter = strtok(NULL, "/");
                            
                            int c = delete_bloom_node(b_head, bloom_virus, monitor_num);           //delete old bloom nodes for monitor needed
                            if(c != 1)
                                bloomfilter_push(b_head, bloom_virus, bloomFilter, monitor_num, atoi(sizeOfBloom));        //put new bloom node sent by updated monitor

                            strcpy(text, "");
                            strcpy(content, "");
                        }
                    }

                    else{
                        strcpy(text, "");
                        strcpy(content, "");
                        break;
                    }

                }
                                                
                for(int i = 0; i < num; i++)
                    free(Command[i]);
                free(Command);

            }
            else
                printf("ERROR!\n");
            
        }

        /* searchVaccinationStatus */
        if(strstr(buffer, "searchVaccinationStatus") != 0){
            
            while(buffer[i] != '\n'){
                if(buffer[i] == ' ')
                    counter++;
                i++;
            }
            
            if(counter == 1)
                num = 2;   
            else
                num = 0;                                       //used to check many cases of command
            
            if(num != 0)
                Command = get_command(buffer, num);

            if(num == 2){

                for(int i = 0; i < numMonitors; i++){

                    int s = 0;
                    char* com = strtok(buffer,"\n");

                    strcpy(msg, "");

                    while(strlen(com) > s){
                        strncpy(msg, com+s, bufferSize);
                            
                        if(write(sock[i], msg, bufferSize) < 0){                     //passes msg to monitors piece by piece according to its size
                            perror("TravelMonitor: Error in writing!"); exit(1);
                        }    
                        
                        s+=bufferSize;           
                    }

                    if(write(sock[i], "!", bufferSize) < 0){                     //sends ! to monitors, for them to know when to stop receiving content
                        perror("TravelMonitor: Error in writing!"); exit(1);
                    }
                    
                    char* status = (char*)malloc(sizeof(char) * 600);
                    strcpy(status, ""); strcpy(content, "");

                    while(1){
                        
                        if(content[0] != '!'){
                            if((r = read(sock[i], content, bufferSize)) < 0){              //reads content
                                perror("TravelMonitor: Error in reading!"); exit(1);
                            }
                            
                            content[r] = '\0';
                            strcat(status, content);

                            if(content[0] == '\n'){
                                printf("%s", status);               //prints status of citizenID sent by monitor

                                strcpy(status, "");
                                strcpy(content, "");
                            }
                        }

                        else{
                            strcpy(status, "");
                            strcpy(content, "");
                            break;
                        }

                    }
                    free(status);
                }
                for(int i = 0; i < num; i++)
                    free(Command[i]);
                free(Command);
                
            }
            else
                printf("ERROR!\n");
            
        }

        /* exit */
        if (strstr(buffer, "exit") != 0){    

            char* com = strtok(buffer,"\n");

            for(int i = 0; i < numMonitors; i++){
                
                int s = 0;
                while(strlen(com) > s){
                    strncpy(msg, com+s, bufferSize);
                        
                    if(write(sock[i], msg, bufferSize) < 0){                     //passes path to monitors piece by piece according to its size
                        perror("TravelMonitor: Error in writing!"); exit(1);
                    }    
                    
                    s+=bufferSize;           
                }

                if(write(sock[i], "!", bufferSize) < 0){                     //sends ! to monitors, for them to know when to stop receiving content
                    perror("TravelMonitor: Error in writing!"); exit(1);
                }

            }

            FILE *fp;
            char filename[20];
            sprintf(filename, "./log_file.%ld", (long)getpid());                //creates logfile
            fp = fopen(filename, "w");

            if(fp == NULL){
                printf("Unable to create file.\n");
                exit(EXIT_FAILURE);
            }

            country* temp = country_head->next;
            while(temp != NULL){
                fputs(temp->country, fp);
                fputs("\n", fp);
                temp = temp->next;
            }                                                               //puts travelmonitor's countries in logfile

            fputs("TOTAL TRAVEL REQUESTS:", fp);
            fprintf(fp,"%d",total_requests);
            fputs("\n",fp);

            fputs("ACCEPTED:", fp);
            fprintf(fp,"%d", total_accepted);
            fputs("\n",fp);

            fputs("REJECTED:", fp);
            fprintf(fp,"%d", total_rejected);                           //puts travelmonitor's statistics in logfile

            fclose(fp);

            break;
        }
    }


    /////////////////////////////////////// free memory ///////////////////////////////////////////////////////
    
    for(int i = 0; i < files; i++)
        free(path[i]);

    free(port);
    
    for(int i = 0; i < files; i++)
        free(filename[i]);
    free(filename);
    free(dirname); free(input_dir);  
    
    closedir(dr);

    free_country(country_head);
    free_bloom(b_head);
    free(text);
    
    free_stats(stats_head);

    for(int i = 0; i < numMonitors; i++)
        waitpid(pid[i], NULL, 0);

    for(int i = 0; i < numMonitors; i++)
        close(sock[i]);

}