#ifndef CYCLIC_BUFFER_H
#define CYCLIC_BUFFER_H

typedef struct cycBuff cycBuff;
typedef struct args args;

struct cycBuff{
    int start;              //start of buff
    int end;                //end of buff
    int capacity;           //capacity of items in buff
    int counter;            //counter of items in buff
    char** data;            //paths in buff
};

void* mon_childserver(int newsock);

void init_buff(cycBuff* cyc_buff, int cyclicBufferSize);

void place_path(args* argss, char* paths);

void* obtain_path(void* argss);

void* consumer(void* ptr);

void delete_cycbuff(cycBuff* cyc_buff);

#endif