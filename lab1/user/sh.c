// Shell.
#include "kernel/types.h"
#include "user/user.h"
#include "kernel/stat.h"
#include "kernel/fcntl.h"
#include "kernel/fs.h"
#include <stddef.h>

// Parsed command representation
#define EXEC  1
#define REDIR 2
#define PIPE  3
#define LIST  4
#define BACK  5

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

struct listcmd {
  int type;
  struct cmd *left;
  struct cmd *right;
};

struct backcmd {
  int type;
  struct cmd *cmd;
};

#define HISTSIZE 21
char history[HISTSIZE][100];
int historySt = 0;
int historyEnd = 0;
int historyPos = 0;

int fork1(void);  // Fork but panics on failure.
void panic(char*);
struct cmd *parsecmd(char*);
void runcmd(struct cmd*) __attribute__((noreturn));

int
isatty (int fd)
{
  struct stat st;
  if (fstat(fd, &st) < 0) {
      printf("Error calling fstat\n");
      exit(1);
  }
  // 1 == CONSOLE
  return st.type == T_DEVICE && st.dev == 1;
}

// Execute cmd.  Never returns.
void
runcmd(struct cmd *cmd)
{
  int p[2];
  struct backcmd *bcmd;
  struct execcmd *ecmd;
  struct listcmd *lcmd;
  struct pipecmd *pcmd;
  struct redircmd *rcmd;

  if(cmd == 0)
    exit(1);

  switch(cmd->type){
  default:
    panic("runcmd");

  case EXEC:
    ecmd = (struct execcmd*)cmd;
    if(ecmd->argv[0] == 0)
      exit(1);
    exec(ecmd->argv[0], ecmd->argv);
    fprintf(2, "exec %s failed\n", ecmd->argv[0]);
    break;

  case REDIR:
    rcmd = (struct redircmd*)cmd;
    close(rcmd->fd);
    if(open(rcmd->file, rcmd->mode) < 0){
      fprintf(2, "open %s failed\n", rcmd->file);
      exit(1);
    }
    runcmd(rcmd->cmd);
    break;

  case LIST:
    lcmd = (struct listcmd*)cmd;
    if(fork1() == 0)
      runcmd(lcmd->left);
    wait(0);
    runcmd(lcmd->right);
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

  case BACK:
    bcmd = (struct backcmd*)cmd;
    if(fork1() == 0)
      runcmd(bcmd->cmd);
    else
      wait(0);
    break;
  }
  exit(0);
}

char* knowncommands[] = {
    "ls","cat","echo","history","cd","wait","sleep","pingpong","primes","find","xargs","uptime",
    "wc","zombie","grind","usertests","stressfs","sh","rm","mkdir","ln","kill","init","grep","forktest"
};

char* common_longest_prefix(const char* a, const char* b) {
    int len = 0;
    while (a[len] && a[len] == b[len]) len++;
    char* prefix = (char*) malloc(strlen(a));
    strcpy(prefix, a);
    prefix[len] = '\0';
    return prefix;
}

int
completecmd(char *buf)
{
  if (!strlen(buf)) return 0;
  int matchcount = 0;
  char *match = NULL;
  char *fileidx = strrchr(buf, ' ');
  // If the buffer contains a space, look for filenames
  if (fileidx) {
    struct dirent dir;
    int fd = open(".", O_RDONLY);
    fileidx += 1;
    if (fd >= 0) {
      while (read(fd, &dir, sizeof(dir)) == sizeof(dir)) {
        if (dir.inum == 0) continue;
        if (!strncmp(dir.name, fileidx, strlen(fileidx))) {
          if (matchcount) {
            if (matchcount == 1) printf("\n%s", match);
            printf("\n%s", dir.name);
            matchcount++;
            char* newmatch = common_longest_prefix(match, dir.name);
            free(match);
            match = newmatch;
          } else {
            char *copy = malloc(strlen(dir.name));
            strcpy(copy, dir.name);
            match = copy;
            matchcount = 1;
          }
        }
      }
      close(fd);
    }
  } 
  else {  // Otherwise, look for command matches
    for (int i = 0; i < sizeof(knowncommands) / sizeof(knowncommands[0]); i++) {
      if (!strncmp(knowncommands[i], buf, strlen(buf))) {
        if (matchcount) {
          if (matchcount == 1) printf("\n%s", match);
          printf("\n%s", knowncommands[i]);
          matchcount++;
          char* newmatch = common_longest_prefix(match, knowncommands[i]);
          free(match);
          match = newmatch;
        } else {
          char *copy = malloc(strlen(knowncommands[i]));
          strcpy(copy, knowncommands[i]);
          match = copy;
          matchcount = 1;
        }
      }
    }
  }
  char* result;
  if (matchcount == 1) {
    // If a single match is found, complete the command
    result = malloc(strlen(fileidx) + strlen(match) + 3);
    strcpy(result, "\t");
    strcpy(result + 1, buf);
    strcpy(result + 1 + strlen(buf), "\t");
    if(fileidx)
      strcpy(result + 2 + strlen(buf), match + strlen(fileidx));
    else
      strcpy(result + 2 + strlen(buf), match + strlen(buf));
  } else if (matchcount > 1) {
    printf("\n$ ");
    int buflen = strlen(buf);
    result = malloc(buflen + strlen(match) + 3);
    strcpy(result, "\t\t");
    strcpy(result + 2, buf);
    strcpy(result + 2 + buflen, match + strlen(fileidx));
  } else {
    result = malloc(strlen(buf) + 2);
    strcpy(result, "\t");
    strcpy(result + 1, buf);
  }
  write(1, result, strlen(result));
  free(result);
  return strlen(buf);
}

void write_shell_stdin(char* buf) {
  char *result = malloc(strlen(buf) + 3);
  strcpy(result, "\t\t");
  strcpy(result + 2, buf);
  write(1, result, strlen(result));
}

int
getcmd(char *buf, int nbuf)
{
  if (isatty(0)) {
    write(2, "$ ", 2);
  }
  memset(buf, 0, nbuf);
  int idx = 0, cc = 0;
  char c;
  for(; idx+1 < nbuf; ) {
    cc = read(0, &c, 1);
    if(cc < 1)
      break;
    if(c == '\t') {  // Tab key
      buf[idx] = 0;
      idx -= completecmd(buf);
    }
    else if(c == '\x1b') {
      char d = 0, e = 0;
      read(0, &d, 1);
      read(0, &e, 1);
      if (d == '[' && e == 'A') {
        if (historyPos > historySt) historyPos--;
        write_shell_stdin(history[historyPos]);
      } else if (d == '[' && e == 'B') {
        if (historyPos < historyEnd) historyPos++;
        write_shell_stdin(history[historyPos]);
      }
    }
    else {
      buf[idx++] = c;
      if (c == '\n' || c == '\r') break;
    }
  }
  buf[idx] = '\0';
  if(buf[0] == 0) // EOF
    return -1;
  return 0;
}

void 
add_history(char *cmd) {
  int size = historyEnd - historySt;
  if (size < 0) size += HISTSIZE;
  strcpy(history[historyEnd], cmd);
  // exchange \n to 0
  history[historyEnd][strlen(cmd) - 1] = 0;
  historyEnd++;
  historyPos = historyEnd;
  if(historyEnd == HISTSIZE) historyEnd = 0;
  if (size == HISTSIZE - 1) {
      historySt++;
      if(historySt == HISTSIZE) historySt = 0;
  }
}

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
    if (strlen(buf) > 1) add_history(buf);
    if(buf[0] == 'c' && buf[1] == 'd' && buf[2] == ' '){
      // Chdir must be called by the parent, not the child.
      buf[strlen(buf)-1] = 0;  // chop \n
      if(chdir(buf+3) < 0)
        fprintf(2, "cannot cd %s\n", buf+3);
      continue;
    }
    else if(!strcmp(buf, "wait\n")){
      // wait must be called by the parent, not the child.
      int status, pid;
      while((pid = wait(&status)) != -1) 
        printf("%d done, status：%d\n", pid, status);
      continue;
    }
    else if(!strcmp(buf, "history\n")){
      for(int i = historySt, j = 1; i != historyEnd; j++)
      {
        printf("%d %s\n", j, history[i]);
        i++;
        if (i == HISTSIZE) i = 0;
      }
      continue;
    }
    struct cmd *curcmd = parsecmd(buf);
    if(fork1() == 0)
      runcmd(curcmd);
    else if (curcmd->type != BACK)
      wait(0);
  }
  // this line to help xarg not timeout after supporting isatty
  write(2, "$ \n", 3);
  exit(0);
}

void
panic(char *s)
{
  fprintf(2, "%s\n", s);
  exit(1);
}

int
fork1(void)
{
  int pid;

  pid = fork();
  if(pid == -1)
    panic("fork");
  return pid;
}

//PAGEBREAK!
// Constructors

struct cmd*
execcmd(void)
{
  struct execcmd *cmd;

  cmd = malloc(sizeof(*cmd));
  memset(cmd, 0, sizeof(*cmd));
  cmd->type = EXEC;
  return (struct cmd*)cmd;
}

struct cmd*
redircmd(struct cmd *subcmd, char *file, char *efile, int mode, int fd)
{
  struct redircmd *cmd;

  cmd = malloc(sizeof(*cmd));
  memset(cmd, 0, sizeof(*cmd));
  cmd->type = REDIR;
  cmd->cmd = subcmd;
  cmd->file = file;
  cmd->efile = efile;
  cmd->mode = mode;
  cmd->fd = fd;
  return (struct cmd*)cmd;
}

struct cmd*
pipecmd(struct cmd *left, struct cmd *right)
{
  struct pipecmd *cmd;

  cmd = malloc(sizeof(*cmd));
  memset(cmd, 0, sizeof(*cmd));
  cmd->type = PIPE;
  cmd->left = left;
  cmd->right = right;
  return (struct cmd*)cmd;
}

struct cmd*
listcmd(struct cmd *left, struct cmd *right)
{
  struct listcmd *cmd;

  cmd = malloc(sizeof(*cmd));
  memset(cmd, 0, sizeof(*cmd));
  cmd->type = LIST;
  cmd->left = left;
  cmd->right = right;
  return (struct cmd*)cmd;
}

struct cmd*
backcmd(struct cmd *subcmd)
{
  struct backcmd *cmd;

  cmd = malloc(sizeof(*cmd));
  memset(cmd, 0, sizeof(*cmd));
  cmd->type = BACK;
  cmd->cmd = subcmd;
  return (struct cmd*)cmd;
}
//PAGEBREAK!
// Parsing

char whitespace[] = " \t\r\n\v";
char symbols[] = "<|>&;()";

int
gettoken(char **ps, char *es, char **q, char **eq)
{
  char *s;
  int ret;

  s = *ps;
  while(s < es && strchr(whitespace, *s))
    s++;
  if(q)
    *q = s;
  ret = *s;
  switch(*s){
  case 0:
    break;
  case '|':
  case '(':
  case ')':
  case ';':
  case '&':
  case '<':
    s++;
    break;
  case '>':
    s++;
    if(*s == '>'){
      ret = '+';
      s++;
    }
    break;
  default:
    ret = 'a';
    while(s < es && !strchr(whitespace, *s) && !strchr(symbols, *s))
      s++;
    break;
  }
  if(eq)
    *eq = s;

  while(s < es && strchr(whitespace, *s))
    s++;
  *ps = s;
  return ret;
}

int
peek(char **ps, char *es, char *toks)
{
  char *s;

  s = *ps;
  while(s < es && strchr(whitespace, *s))
    s++;
  *ps = s;
  return *s && strchr(toks, *s);
}

struct cmd *parseline(char**, char*);
struct cmd *parsepipe(char**, char*);
struct cmd *parseexec(char**, char*);
struct cmd *nulterminate(struct cmd*);

struct cmd*
parsecmd(char *s)
{
  char *es;
  struct cmd *cmd;

  es = s + strlen(s);
  cmd = parseline(&s, es);
  peek(&s, es, "");
  if(s != es){
    fprintf(2, "leftovers: %s\n", s);
    panic("syntax");
  }
  nulterminate(cmd);
  return cmd;
}

struct cmd*
parseline(char **ps, char *es)
{
  struct cmd *cmd;

  cmd = parsepipe(ps, es);
  while(peek(ps, es, "&")){
    gettoken(ps, es, 0, 0);
    cmd = backcmd(cmd);
  }
  if(peek(ps, es, ";")){
    gettoken(ps, es, 0, 0);
    cmd = listcmd(cmd, parseline(ps, es));
  }
  return cmd;
}

struct cmd*
parsepipe(char **ps, char *es)
{
  struct cmd *cmd;

  cmd = parseexec(ps, es);
  if(peek(ps, es, "|")){
    gettoken(ps, es, 0, 0);
    cmd = pipecmd(cmd, parsepipe(ps, es));
  }
  return cmd;
}

struct cmd*
parseredirs(struct cmd *cmd, char **ps, char *es)
{
  int tok;
  char *q, *eq;

  while(peek(ps, es, "<>")){
    tok = gettoken(ps, es, 0, 0);
    if(gettoken(ps, es, &q, &eq) != 'a')
      panic("missing file for redirection");
    switch(tok){
    case '<':
      cmd = redircmd(cmd, q, eq, O_RDONLY, 0);
      break;
    case '>':
      cmd = redircmd(cmd, q, eq, O_WRONLY|O_CREATE|O_TRUNC, 1);
      break;
    case '+':  // >>
      cmd = redircmd(cmd, q, eq, O_WRONLY|O_CREATE, 1);
      break;
    }
  }
  return cmd;
}

struct cmd*
parseblock(char **ps, char *es)
{
  struct cmd *cmd;

  if(!peek(ps, es, "("))
    panic("parseblock");
  gettoken(ps, es, 0, 0);
  cmd = parseline(ps, es);
  if(!peek(ps, es, ")"))
    panic("syntax - missing )");
  gettoken(ps, es, 0, 0);
  cmd = parseredirs(cmd, ps, es);
  return cmd;
}

struct cmd*
parseexec(char **ps, char *es)
{
  char *q, *eq;
  int tok, argc;
  struct execcmd *cmd;
  struct cmd *ret;

  if(peek(ps, es, "("))
    return parseblock(ps, es);

  ret = execcmd();
  cmd = (struct execcmd*)ret;

  argc = 0;
  ret = parseredirs(ret, ps, es);
  while(!peek(ps, es, "|)&;")){
    if((tok=gettoken(ps, es, &q, &eq)) == 0)
      break;
    if(tok != 'a')
      panic("syntax");
    cmd->argv[argc] = q;
    cmd->eargv[argc] = eq;
    argc++;
    if(argc >= MAXARGS)
      panic("too many args");
    ret = parseredirs(ret, ps, es);
  }
  cmd->argv[argc] = 0;
  cmd->eargv[argc] = 0;
  return ret;
}

// NUL-terminate all the counted strings.
struct cmd*
nulterminate(struct cmd *cmd)
{
  int i;
  struct backcmd *bcmd;
  struct execcmd *ecmd;
  struct listcmd *lcmd;
  struct pipecmd *pcmd;
  struct redircmd *rcmd;

  if(cmd == 0)
    return 0;

  switch(cmd->type){
  case EXEC:
    ecmd = (struct execcmd*)cmd;
    for(i=0; ecmd->argv[i]; i++)
      *ecmd->eargv[i] = 0;
    break;

  case REDIR:
    rcmd = (struct redircmd*)cmd;
    nulterminate(rcmd->cmd);
    *rcmd->efile = 0;
    break;

  case PIPE:
    pcmd = (struct pipecmd*)cmd;
    nulterminate(pcmd->left);
    nulterminate(pcmd->right);
    break;

  case LIST:
    lcmd = (struct listcmd*)cmd;
    nulterminate(lcmd->left);
    nulterminate(lcmd->right);
    break;

  case BACK:
    bcmd = (struct backcmd*)cmd;
    nulterminate(bcmd->cmd);
    break;
  }
  return cmd;
}
