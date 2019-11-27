#include "kernel/types.h"
#include "user/user.h"
#include "kernel/fcntl.h"

#define EXEC  1
#define REDIR 2
#define PIPE  3

#define MAXARGS 10

struct cmd {
  int type;
};

struct execcmd {
  int type;
  char *argv[MAXARGS];
  char *eargv[MAXARGS];
};

struct redircmd {
  int type;
  struct cmd *cmd;
  char *file;
  char *efile;
  int mode;
  int fd;
};

struct pipecmd {
  int type;
  struct cmd *left;
  struct cmd *right;
};

struct pipecmd pcmd;
struct redircmd rcmd[2];
struct execcmd ecmd[2];

void
panic(char *s)
{
  fprintf(2, "%s\n", s);
  exit(-1);
}

int
fork1(void)
{
  int pid;

  pid = fork();
  if(pid < 0)
    panic("fork");
  return pid;
}

void
runcmd(struct cmd *cmd)
{
  int p[2];
  struct execcmd *ecmd;
  struct pipecmd *pcmd;
  struct redircmd *rcmd;

  if(cmd == 0)
    exit(-1);

  switch(cmd->type){
  default:
    panic("runcmd");

  case EXEC:
    ecmd = (struct execcmd*)cmd;
    if(ecmd->argv[0] == 0)
      exit(-1);
    exec(ecmd->argv[0], ecmd->argv);
    fprintf(2, "exec %s failed\n", ecmd->argv[0]);
    break;

  case REDIR:
    rcmd = (struct redircmd*)cmd;
    close(rcmd->fd);
    if(open(rcmd->file, rcmd->mode) < 0){
      fprintf(2, "open %s failed\n", rcmd->file);
      exit(-1);
    }
    runcmd(rcmd->cmd);
    break;

  case PIPE:
    pcmd = (struct pipecmd*)cmd;
    if(pipe(p) < 0)
      panic("pipe");
    if(fork1() == 0){
      close(1);
      dup(p[1]);
      close(p[0]);
      close(p[1]);
      runcmd(pcmd->left);
    }
    if(fork1() == 0){
      close(0);
      dup(p[0]);
      close(p[0]);
      close(p[1]);
      runcmd(pcmd->right);
    }
    close(p[0]);
    close(p[1]);
    wait(0);
    wait(0);
    break;
  }
  exit(0);
}


int
getcmd(char *buf, int nbuf)
{
  fprintf(2, "@ ");
  memset(buf, 0, nbuf);
  gets(buf, nbuf);
//   printf(buf);
  if(buf[0] == 0) // EOF
    return -1;
  return 0;
}

struct cmd *parsecmd(char*);

int
main(void)
{
  static char buf[100];
  int fd;

  // Ensure that three file descriptors are open.
  while((fd = open("console", O_RDWR)) >= 0){
    if(fd >= 3){
      close(fd);
      break;
    }
  }

  // Read and run input commands.
  while(getcmd(buf, sizeof(buf)) >= 0){
    buf[strlen(buf)-1] = 0;  // chop \n
    if(buf[0] == 'c' && buf[1] == 'd' && buf[2] == ' '){
      // Chdir must be called by the parent, not the child.
      if(chdir(buf+3) < 0)
        fprintf(2, "cannot cd %s\n", buf+3);
      continue;
    }
    if(fork1() == 0)
      runcmd(parsecmd(buf));
    wait(0);
  }
  exit(0);
}

//==========Copy and paste is over=====================

struct cmd *parsecmd(char* cmdstr){
    char *p;
    char *q;
    int i;
    struct redircmd *rp;
    struct execcmd *ep;
    // printf("input:%s\n",cmdstr);
    if((p = strchr(cmdstr,'|'))){
        *p = 0;
        for(q = p-1;*q == ' '; q--){
            *q = 0;
        }
        for(q = p + 1; *q == ' '; q++){
            *q = 0;
        }
        // printf("pcmd.left = %s\n",cmdstr);
        pcmd.left = parsecmd(cmdstr);
        // printf("pcmd.right = %s\n",q);
        pcmd.right = parsecmd(q);
        pcmd.type = PIPE;
        return (struct cmd *) &pcmd;
    }else if((p = strchr(cmdstr,'>'))){
        rp = rcmd;
        *p = 0;
        for(q = p-1;*q == ' '; q--){
            *q = 0;
        }
        for(q = p + 1; *q == ' '; q++){
            *q = 0;
        }
        if(rp->type)
            rp++;
        rp->type = REDIR;
        rp->fd = 1;
        rp->mode = O_WRONLY|O_CREATE;
        rp->file = q;
        // printf("file:%s\n",q);
        // printf("cmdstr:%s\n",cmdstr);
        rp->cmd = parsecmd(cmdstr);
        return (struct cmd *) rp;
    }else if((p = strchr(cmdstr,'<'))){
        rp = rcmd;
        *p = 0;
        for(q = p-1;*q == ' '; q--){
            *q = 0;
        }
        for(q = p + 1; *q == ' '; q++){
            *q = 0;
        }
        if(rp->type)
            rp++;
        rp->type = REDIR;
        rp->fd = 0;
        rp->mode = O_RDONLY;
        rp->file = q;
        rp->cmd = parsecmd(cmdstr);
        return (struct cmd *) rp;
    }else{
        if(cmdstr[0] == 0)
            return 0;
        ep = ecmd;
        i = 1;
        if(ep->type)
            ep++;
        ep->type = EXEC;
        ep->argv[0] = cmdstr;
        p = cmdstr;
        while((p = strchr(p,' '))){
            for(;*p == ' ';p++)
                *p = 0;
            ep->argv[i] = p;
            i++;
        }
        // for(i = 0; ep->argv[i]; i++){
        //     printf(ep->argv[i]);
        //     printf("\n");
        // }
        return (struct cmd *) ep;
    }
}