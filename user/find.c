#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fs.h"

char path[512];
int first;

char*
fmtname()
{
    char *p;

  // Find first character after last slash.
    for(p=path+strlen(path); p >= path && *p != '/'; p--);
    p++;

  // Return blank-padded name.
    return p;
}

void find(char *filename){
    int fd;
    struct dirent de;
    struct stat st;
    int i;
    if((fd = open(path, 0)) < 0){
        fprintf(2, "find: cannot open %s\n", path);
        return;
    }
    if(fstat(fd, &st) < 0){
        fprintf(2, "find: cannot stat %s\n", path);
        close(fd);
        return;
    }

    switch(st.type){
    case T_FILE:
        if(first){
            fprintf(2,"find: %s is not a directory.\n");
            break;
        }
        if(!strcmp(filename,fmtname())){
            printf("%s\n",path);
        }
        break;

    case T_DIR:
        first = 0;
        i = strlen(path);
        if(i + 1 + DIRSIZ + 1 > sizeof path){
            printf("find: path too long\n");
            break;
        }
        path[i]='/';
        i++;
        while(read(fd, &de, sizeof(de)) == sizeof(de)){
            if(de.inum == 0)
                continue;
            if(de.name[0] == '.'){
                if(de.name[1] == 0)
                    continue;
                else if(de.name[1] == '.' && de.name[2] == 0)
                    continue;
            }
            memmove(path+i, de.name, DIRSIZ);
            path[i+DIRSIZ] = 0;
            find(filename);
        }
        path[i] = 0;
        break;
    }
    close(fd);
}

int
main(int argc, char **argv)
{
    if(argc != 3){
        fprintf(2, "usage: find <directory to search from> <file to search>\n");
        exit();
    }
    strcpy(path,argv[1]);
    first = strlen(path);
    if(path[first-1]=='/')
        path[first-1] = 0;
    find(argv[2]);
    exit();
}
