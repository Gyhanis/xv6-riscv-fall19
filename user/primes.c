#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int
main(int argc, char **argv)
{
    int i;
    int j,pid;
    int p[2];
    int c[2];
    pipe(p);
    for(i = 2; i <= 35; i++){
        write(p[1],&i,sizeof(int));
    }
    close(p[1]);
    pid = 0;
start:
    read(p[0],&i,sizeof(int));
    printf("prime %d\n",i);
    while(read(p[0],&j,sizeof(int))){
        if(j%i){
            if(pid == 0){
                pipe(c);
                pid = fork();
                if(pid < 0){
                    fprintf(2,"Error on forking.\n");
                    close(p[0]);
                    close(c[1]);
                    close(c[0]);
                    exit();
                }else if(pid == 0){
                    close(p[0]);
                    p[0] = c[0];
                    close(c[1]);
                    goto start;
                }else{
                    close(c[0]);
                }
            }
            write(c[1],&j,sizeof(int));
        }
    }
    close(c[1]);
    if(pid)
        wait();
    exit();
}
