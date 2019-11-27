#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/param.h"
char args[MAXARG][63];
char *parg[MAXARG];

int
main(int argc, char **argv)
{
    int i,j;
    char c;
    if(argc < 2){
        fprintf(2, "usage: xargs <cmd> [args..]\n");
        exit();
    }
    if(argc > MAXARG+1){
        fprintf(2,"xargs: too many arguments\n");
    }
    for(i = 0; i < argc-1; i++){
        parg[i] = argv[i+1];
    }
    j = 0;
    while(read(0,&c,sizeof(char))){
        if(c == '\n'){
            args[i][j] = 0;
            parg[i] = args[i];
            i++;
            j = 0;
        }else{
            args[i][j] = c;
            j++;
        }
    }
    exec(argv[1],parg);
    exit();
}
