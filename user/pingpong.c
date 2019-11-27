#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int
main(int argc, char **argv)
{
    int p[2];
    int c[2];
    int r;
    if(pipe(p)){
        fprintf(2,"Error on creating pipe.\n");
        exit();
    }
    if(pipe(c)){
        fprintf(2,"Error on creating pipe.\n");
        close(p[0]);
        close(p[1]);
    }
    r = fork();
    if(r < 0){
        fprintf(2,"Error on forking\n");
        close(p[0]);
        close(p[1]);
        close(c[0]);
        close(c[1]);
    }else if(r == 0){
        close(p[1]);
        close(c[0]);
        read(p[0],&r,sizeof(int));
        if(r == 1){
            printf("%d: received ping\n",getpid());
            write(c[1],&r,sizeof(int));
        }else{
            fprintf(2,"%d: error on receiving ping.\n",getpid());
        }
        close(p[0]);
        close(c[1]);
        exit();
    }else{
        close(p[0]);
        close(c[1]);
        r = 1;
        write(p[1],&r,sizeof(int));
        read(c[0],&r,sizeof(int));
        if(r == 1){
            printf("%d: received pong\n",getpid());
        }else{
            fprintf(2,"%d: error on receiving pong.\n",getpid());
        }
        wait();
        close(p[1]);
        close(c[0]);
        exit();
    }
    exit();
}
