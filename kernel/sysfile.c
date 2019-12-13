//
// File-system system calls.
// Mostly argument checking, since we don't trust
// user code, and calls into file.c and fs.c.
//

#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "stat.h"
#include "spinlock.h"
#include "proc.h"
#include "fs.h"
#include "sleeplock.h"
#include "file.h"
#include "fcntl.h"

#include "memlayout.h"

// Fetch the nth word-sized system call argument as a file descriptor
// and return both the descriptor and the corresponding struct file.
static int
argfd(int n, int *pfd, struct file **pf)
{
  int fd;
  struct file *f;

  if(argint(n, &fd) < 0)
    return -1;
  if(fd < 0 || fd >= NOFILE || (f=myproc()->ofile[fd]) == 0)
    return -1;
  if(pfd)
    *pfd = fd;
  if(pf)
    *pf = f;
  return 0;
}

// Allocate a file descriptor for the given file.
// Takes over file reference from caller on success.
static int
fdalloc(struct file *f)
{
  int fd;
  struct proc *p = myproc();

  for(fd = 0; fd < NOFILE; fd++){
    if(p->ofile[fd] == 0){
      p->ofile[fd] = f;
      return fd;
    }
  }
  return -1;
}

uint64
sys_dup(void)
{
  struct file *f;
  int fd;

  if(argfd(0, 0, &f) < 0)
    return -1;
  if((fd=fdalloc(f)) < 0)
    return -1;
  filedup(f);
  return fd;
}

uint64
sys_read(void)
{
  struct file *f;
  int n;
  uint64 p;

  if(argfd(0, 0, &f) < 0 || argint(2, &n) < 0 || argaddr(1, &p) < 0)
    return -1;
  return fileread(f, p, n);
}

uint64
sys_write(void)
{
  struct file *f;
  int n;
  uint64 p;

  if(argfd(0, 0, &f) < 0 || argint(2, &n) < 0 || argaddr(1, &p) < 0)
    return -1;

  return filewrite(f, p, n);
}

uint64
sys_close(void)
{
  int fd;
  struct file *f;

  if(argfd(0, &fd, &f) < 0)
    return -1;
  myproc()->ofile[fd] = 0;
  fileclose(f);
  return 0;
}

uint64
sys_fstat(void)
{
  struct file *f;
  uint64 st; // user pointer to struct stat

  if(argfd(0, 0, &f) < 0 || argaddr(1, &st) < 0)
    return -1;
  return filestat(f, st);
}

// Create the path new as a link to the same inode as old.
uint64
sys_link(void)
{
  char name[DIRSIZ], new[MAXPATH], old[MAXPATH];
  struct inode *dp, *ip;

  if(argstr(0, old, MAXPATH) < 0 || argstr(1, new, MAXPATH) < 0)
    return -1;

  begin_op(ROOTDEV);
  if((ip = namei(old)) == 0){
    end_op(ROOTDEV);
    return -1;
  }

  ilock(ip);
  if(ip->type == T_DIR){
    iunlockput(ip);
    end_op(ROOTDEV);
    return -1;
  }

  ip->nlink++;
  iupdate(ip);
  iunlock(ip);

  if((dp = nameiparent(new, name)) == 0)
    goto bad;
  ilock(dp);
  if(dp->dev != ip->dev || dirlink(dp, name, ip->inum) < 0){
    iunlockput(dp);
    goto bad;
  }
  iunlockput(dp);
  iput(ip);

  end_op(ROOTDEV);

  return 0;

bad:
  ilock(ip);
  ip->nlink--;
  iupdate(ip);
  iunlockput(ip);
  end_op(ROOTDEV);
  return -1;
}

// Is the directory dp empty except for "." and ".." ?
static int
isdirempty(struct inode *dp)
{
  int off;
  struct dirent de;

  for(off=2*sizeof(de); off<dp->size; off+=sizeof(de)){
    if(readi(dp, 0, (uint64)&de, off, sizeof(de)) != sizeof(de))
      panic("isdirempty: readi");
    if(de.inum != 0)
      return 0;
  }
  return 1;
}

uint64
sys_unlink(void)
{
  struct inode *ip, *dp;
  struct dirent de;
  char name[DIRSIZ], path[MAXPATH];
  uint off;

  if(argstr(0, path, MAXPATH) < 0)
    return -1;

  begin_op(ROOTDEV);
  if((dp = nameiparent(path, name)) == 0){
    end_op(ROOTDEV);
    return -1;
  }

  ilock(dp);

  // Cannot unlink "." or "..".
  if(namecmp(name, ".") == 0 || namecmp(name, "..") == 0)
    goto bad;

  if((ip = dirlookup(dp, name, &off)) == 0)
    goto bad;
  ilock(ip);

  if(ip->nlink < 1)
    panic("unlink: nlink < 1");
  if(ip->type == T_DIR && !isdirempty(ip)){
    iunlockput(ip);
    goto bad;
  }

  memset(&de, 0, sizeof(de));
  if(writei(dp, 0, (uint64)&de, off, sizeof(de)) != sizeof(de))
    panic("unlink: writei");
  if(ip->type == T_DIR){
    dp->nlink--;
    iupdate(dp);
  }
  iunlockput(dp);

  ip->nlink--;
  iupdate(ip);
  iunlockput(ip);

  end_op(ROOTDEV);

  return 0;

bad:
  iunlockput(dp);
  end_op(ROOTDEV);
  return -1;
}

static struct inode*
create(char *path, short type, short major, short minor)
{
  struct inode *ip, *dp;
  char name[DIRSIZ];

  if((dp = nameiparent(path, name)) == 0)
    return 0;

  ilock(dp);

  if((ip = dirlookup(dp, name, 0)) != 0){
    iunlockput(dp);
    ilock(ip);
    if(type == T_FILE && (ip->type == T_FILE || ip->type == T_DEVICE))
      return ip;
    iunlockput(ip);
    return 0;
  }

  if((ip = ialloc(dp->dev, type)) == 0)
    panic("create: ialloc");

  ilock(ip);
  ip->major = major;
  ip->minor = minor;
  ip->nlink = 1;
  iupdate(ip);

  if(type == T_DIR){  // Create . and .. entries.
    dp->nlink++;  // for ".."
    iupdate(dp);
    // No ip->nlink++ for ".": avoid cyclic ref count.
    if(dirlink(ip, ".", ip->inum) < 0 || dirlink(ip, "..", dp->inum) < 0)
      panic("create dots");
  }

  if(dirlink(dp, name, ip->inum) < 0)
    panic("create: dirlink");

  iunlockput(dp);

  return ip;
}

uint64
sys_open(void)
{
  char path[MAXPATH];
  int fd, omode;
  struct file *f;
  struct inode *ip;
  int n;

  if((n = argstr(0, path, MAXPATH)) < 0 || argint(1, &omode) < 0)
    return -1;

  begin_op(ROOTDEV);

  if(omode & O_CREATE){
    ip = create(path, T_FILE, 0, 0);
    if(ip == 0){
      end_op(ROOTDEV);
      return -1;
    }
  } else {
    if((ip = namei(path)) == 0){
      end_op(ROOTDEV);
      return -1;
    }
    ilock(ip);
    if(ip->type == T_DIR && omode != O_RDONLY){
      iunlockput(ip);
      end_op(ROOTDEV);
      return -1;
    }
  }

  if(ip->type == T_DEVICE && (ip->major < 0 || ip->major >= NDEV)){
    iunlockput(ip);
    end_op(ROOTDEV);
    return -1;
  }

  if((f = filealloc()) == 0 || (fd = fdalloc(f)) < 0){
    if(f)
      fileclose(f);
    iunlockput(ip);
    end_op(ROOTDEV);
    return -1;
  }

  if(ip->type == T_DEVICE){
    f->type = FD_DEVICE;
    f->major = ip->major;
    f->minor = ip->minor;
  } else {
    f->type = FD_INODE;
  }
  f->ip = ip;
  f->off = 0;
  f->readable = !(omode & O_WRONLY);
  f->writable = (omode & O_WRONLY) || (omode & O_RDWR);

  iunlock(ip);
  end_op(ROOTDEV);

  return fd;
}

uint64
sys_mkdir(void)
{
  char path[MAXPATH];
  struct inode *ip;

  begin_op(ROOTDEV);
  if(argstr(0, path, MAXPATH) < 0 || (ip = create(path, T_DIR, 0, 0)) == 0){
    end_op(ROOTDEV);
    return -1;
  }
  iunlockput(ip);
  end_op(ROOTDEV);
  return 0;
}

uint64
sys_mknod(void)
{
  struct inode *ip;
  char path[MAXPATH];
  int major, minor;

  begin_op(ROOTDEV);
  if((argstr(0, path, MAXPATH)) < 0 ||
     argint(1, &major) < 0 ||
     argint(2, &minor) < 0 ||
     (ip = create(path, T_DEVICE, major, minor)) == 0){
    end_op(ROOTDEV);
    return -1;
  }
  iunlockput(ip);
  end_op(ROOTDEV);
  return 0;
}

uint64
sys_chdir(void)
{
  char path[MAXPATH];
  struct inode *ip;
  struct proc *p = myproc();

  begin_op(ROOTDEV);
  if(argstr(0, path, MAXPATH) < 0 || (ip = namei(path)) == 0){
    end_op(ROOTDEV);
    return -1;
  }
  ilock(ip);
  if(ip->type != T_DIR){
    iunlockput(ip);
    end_op(ROOTDEV);
    return -1;
  }
  iunlock(ip);
  iput(p->cwd);
  end_op(ROOTDEV);
  p->cwd = ip;
  return 0;
}

uint64
sys_exec(void)
{
  char path[MAXPATH], *argv[MAXARG];
  int i;
  uint64 uargv, uarg;

  if(argstr(0, path, MAXPATH) < 0 || argaddr(1, &uargv) < 0){
    return -1;
  }
  memset(argv, 0, sizeof(argv));
  for(i=0;; i++){
    if(i >= NELEM(argv)){
      goto bad;
    }
    if(fetchaddr(uargv+sizeof(uint64)*i, (uint64*)&uarg) < 0){
      goto bad;
    }
    if(uarg == 0){
      argv[i] = 0;
      break;
    }
    argv[i] = kalloc();
    if(argv[i] == 0)
      panic("sys_exec kalloc");
    if(fetchstr(uarg, argv[i], PGSIZE) < 0){
      goto bad;
    }
  }

  int ret = exec(path, argv);

  for(i = 0; i < NELEM(argv) && argv[i] != 0; i++)
    kfree(argv[i]);

  return ret;

 bad:
  for(i = 0; i < NELEM(argv) && argv[i] != 0; i++)
    kfree(argv[i]);
  return -1;
}

uint64
sys_pipe(void)
{
  uint64 fdarray; // user pointer to array of two integers
  struct file *rf, *wf;
  int fd0, fd1;
  struct proc *p = myproc();

  if(argaddr(0, &fdarray) < 0)
    return -1;
  if(pipealloc(&rf, &wf) < 0)
    return -1;
  fd0 = -1;
  if((fd0 = fdalloc(rf)) < 0 || (fd1 = fdalloc(wf)) < 0){
    if(fd0 >= 0)
      p->ofile[fd0] = 0;
    fileclose(rf);
    fileclose(wf);
    return -1;
  }
  if(copyout(p->pagetable, fdarray, (char*)&fd0, sizeof(fd0)) < 0 ||
     copyout(p->pagetable, fdarray+sizeof(fd0), (char *)&fd1, sizeof(fd1)) < 0){
    p->ofile[fd0] = 0;
    p->ofile[fd1] = 0;
    fileclose(rf);
    fileclose(wf);
    return -1;
  }
  return 0;
}

#define i2a(x) (((x)<<PGSHIFT) + PHYSTOP)
#define a2i(x) ((((uint64)x)-PHYSTOP) >> PGSHIFT)
uint64
sys_mmap(void)
{
  uint64 length,len;
  int prot;
  int flags;
  int fd;
  uint64 off;
  uint64 addr;
  struct proc *p;
  struct mmanager *man;
  struct file *f;
  int i;
  int skip;
  if(argaddr(1,&length) < 0 || argint(2,&prot) < 0 || argint(3,&flags) < 0 ||
    argint(4,&fd) < 0 || argaddr(5,&off) < 0){
    printf("sys_mmap:load argument failed\n");
    return -1;
  }
  p = myproc();
  man = &(p->man);
  if( man->full && man->head == man->tail){
    printf("sys_mmap:mmemory is full\n");
    return -1;
  }
  len = length;
  if(length % PGSIZE){
    length >>= PGSHIFT;
    length ++;
  }else
    length >>= PGSHIFT;

  // alen = man->head - man->tail;
  // if(alen <= 0)
  //   alen += 32;


  // if(length > alen){
  //   printf("sys_mmap:mmemory is not enough\n");
  //   return -1;
  // }

  if(man->head < man->tail){
    if((man->len - man->tail) > length){
      skip = 0;
    }else if(man->head > length){
      skip = 1;
    }else{
      printf("sys_mmap:mmemory is not enough\n");
      return -1;
    }
  }else if(man->head == man->tail){
    if(length <= man->len){
      skip = 0;
    }else{
      printf("sys_mmap:mmemory is not enough\n");
      return -1;
    }
  }else{
    if((man->head-man->tail) >= length){
      skip = 0;
    }else{
      printf("sys_mmap:mmemory is not enough\n");
      return -1;
    }
  }

  for(i = 0; i < NOFILE; i++){
    if(man->mfile[i].f == 0)
      break;
  }
  if(i == NOFILE){
    printf("sys_mmap:files being mapped are too many\n");
    return -1;
  }

  f = p->ofile[fd];
  if(f == 0){
    printf("sys_mmap:invalid file descriptor\n");
    return -1;
  }

  if(prot == 0){
    printf("sys_mmap:strange prot\n");
    return -1;
  }

  if(prot & PROT_READ){
    if(f->readable == 0){
      printf("sys_mmap:file not readable\n");
      return -1;
    }
  }

  if(prot & PROT_WRITE && (flags == MAP_SHARED)){
    if(f->writable == 0){
      printf("sys_mmap:file not writable\n");
      return -1;
    }
  }

  if(skip){
    addr = PHYSTOP;
    man->tail = 0;
  }else{
    addr = PHYSTOP + (man->tail << PGSHIFT);
  }
  man->mfile[i].f = f;
  man->mfile[i].prop = prot | flags;
  man->mfile[i].start = (void*)i2a(man->tail);
  man->tail += length;
  man->mfile[i].end = man->mfile[i].start + len;
  man->mfile[i].off = off;
  filedup(f);
  if(man->head == man->tail)
    man->full = 1;
  return addr;
}

int findFileE(void* va,struct mmanager *man,int isEnd){
  int i;
  if(isEnd){
    for(i = 0; i < NOFILE; i++){
      if(man->mfile[i].end == va){
        return i;
      }
    }
  }else{
    for(i = 0; i < NOFILE; i++){
      if(man->mfile[i].start == va){
        return i;
      }
    }
  }
  return -1;
}

// int findFile(void* va,struct mmanager *man){
//   int i;
//   for (i = 0; i < NOFILE; i++){
//     if(va >= man->mfile[i].start && va < man->mfile[i].end){
//       return i;
//     }
//   }
//   return -1;
// }

uint64
sys_munmap(void)
{
  uint64 length;
  uint64 addr,end;
  struct mfile* mf;
  int fd;
  int i;
  struct mmanager *man = &(myproc()->man);
  pagetable_t pagetable = myproc()->pagetable;
  
  if(argaddr(1,&length) < 0 || argaddr(0,&addr) < 0 ){
    printf("sys_munmap:load argument failed\n");
    return -1;
  }

  end = addr + length;
  fd = findFileE((void*)addr,man,0);
  if(fd >= 0){
    mf = &(man->mfile[fd]);
    while(addr < end){
      uint64 p;
      uint64 nextpage = (PGROUNDDOWN(addr) + PGSIZE);
      p = (uint64) mf->end;
      p = (p > nextpage)?nextpage:p;
      p = (p > end)?end:p;
      if(walkaddr(pagetable,addr)){
        if((mf->prop & PROT_WRITE) && !(mf->prop & 1)){
          uint64 temp = mf->f->off;
          mf->f->off = mf->off;
          filewrite(mf->f,addr,(uint64)(p-addr));
          mf->f->off = temp;
        }
        if(p == (uint64)mf->end || p == nextpage){
          uvmunmap(pagetable,PGROUNDDOWN(addr),PGSIZE,1);
          man->full = 0;
        }
      }

      if(p == (uint64)mf->end){
        fileclose(mf->f);
        mf->f = 0;
        if(findFileE((void*)nextpage,man,0)){
          addr = PHYSTOP;
          if(man->tail == (uint64)a2i(nextpage)){
            man->tail = 0;
          }
          man->head = 0;
          return 0;
        }
      }else{
        mf->off += (uint64)p - addr;
        mf->start = (void*)p;
      }
      man->head++;
      addr = (uint64)nextpage;
    }
    return 0;
  }else{
    fd = findFileE((void*)end,man,1);
    if(fd == -1){
      printf("sys_munmap:not edge, can't ummap\n");
      return -1;
    }else{
      mf = &(man->mfile[fd]);
      while(addr < end){
        uint64 p;
        uint64 prevpage = (uint64) (PGROUNDDOWN(end-1));
        p = (uint64) mf->start;
        p = (p > prevpage) ?  p : prevpage;
        p = (p > end) ?       p : end;
        if(walkaddr(pagetable,addr)){
          if((mf->prop & PROT_WRITE) && !(mf->prop & 1)){
            uint64 tmp = mf->f->off;
            mf->f->off = mf->off + (p - (uint64)mf->start);
            filewrite(mf->f, p, end - p);
            mf->f->off = tmp;
          }
          if(p == (uint64)mf->start || p == prevpage){
            uvmunmap(pagetable, prevpage, PGSIZE,1);
            man->full = 0;
          }
        }

        if(p == PHYSTOP){
          uint64 tmp;
          fileclose(mf->f);
          mf->f = 0;
          tmp = PHYSTOP;
          for(i = 0; i < NOFILE; i++){
            tmp = (tmp < (PGROUNDDOWN((uint64)man->mfile[i].end)+PGSIZE))?(PGROUNDDOWN((uint64)man->mfile[i].end)+PGSIZE):tmp;
          }
          man->tail = a2i(tmp);
          man->tail %= 32;
          return 0;
        }else{
          mf->end = (void*)p;
        }
        man->tail--;
        end = prevpage;
      }
      return 0;
    }
  }
}
