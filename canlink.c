/* $Header$ */

/*
 * canlink
 *
 * Given a list of files, determine which ones can be linked together
 * with no loss of data.
 *
 * File owner, group, mode, and/or modification time optionally can be
 * required to match.  File contents must match to be considered linkable.
 *
 */


#include <stdio.h>	/* for fprintf, fgets, stderr */
#include <stdlib.h>	/* for malloc, qsort */
#include <unistd.h>	/* for getopt */
#include <string.h>	/* for strlen, strdup, strrchr */
#include <sys/stat.h>	/* for stat */
#include <fcntl.h>	/* for open, O_RDONLY */
#include <values.h>	/* for MAXINT */
#ifdef MD5
#include <global.h>	/* for macros in md5.h */
#include <md5.h>	/* for MD5 hashing routines */
#include <string.h>	/* for memcpy */
#endif


#define FALSE 0
#define TRUE !FALSE
typedef char bool;


struct namelist_struct {
  struct namelist_struct *next;
  char *name;
};

struct stat_struct {
  char *name;
  ushort st_mode;
  uid_t st_uid;
  gid_t st_gid;
  off_t st_size;
  time_t st_mtim;
  char *st_name;
  dev_t st_dev;
  ino_t st_ino;
#ifdef MD5
  unsigned char *digest;
#endif
};


#ifdef __STDC__
void usage(char *progname);
int main(int argc, char **argv);
char *get_filename(int argc, char **argv, int *optindp);
char *basename(char *name);
int mystrcmp(char *a, char *b);
int sortstat(struct stat_struct *a, struct stat_struct *b);
int filecompare(struct stat_struct *stat1, struct stat_struct *stat2);
void nullify(struct stat_struct *statp);
#ifdef MD5
unsigned char *md5_file(char *name);
#endif

#else
void usage();
int main();
char *get_filename();
char *basename();
int mystrcmp();
int sortstat();
int filecompare();
void nullify();
#ifdef MD5
unsigned char *md5_file();
#endif

#endif


bool uflag = FALSE;
bool gflag = FALSE;
bool mflag = FALSE;
bool tflag = FALSE;
bool nflag = FALSE;
bool zflag = TRUE;
bool lflag = TRUE;
bool hflag = TRUE;

bool stdin_files;
bool sortnameq;		/* used by sortstat to ignore names or not */

#define DMASK_INIT	0x01
#define DMASK_STAT	0x02
#define DMASK_COMP	0x04
#define DMASK_MD5	0x08
#define DMASK_TEST	0x10
int  debugbits = 0;
int  debugor = 0x01;


void usage(progname)
char *progname;
{
  fprintf(stderr, "\
\n\
Determine if files can safely be linked.\n\
\n\
usage: %s [-u] [-g] [-m] [-t] [-n] [-z|-Z] [-l|-L] [-h|-H] [files...]\n\
\n\
  -u: files must have the same user ID\n\
  -g: files must have the same group ID\n\
  -m: files must have the same mode\n\
  -t: files must have the same modify time\n\
  -n: files must have the same basename\n\
  -z: omit    zero-length files (default)\n\
  -Z: include zero-length files\n\
  -l: omit    file pairs that are hard links to each other (default)\n\
  -L: include file pairs that are hard links to each other\n\
  -h: do    use MD5 hashing (default)\n\
  -H: don't use MD5 hashing\n\
File names can either be specified on the command line or\n\
as standard input, one file per line.\n\
\n\
$Revision$\n\
", progname);
}


int main(argc, argv)
int argc;
char **argv;
{
  struct namelist_struct *namelist, *namelist_new;
  char *newname;
  int numnames;

  struct stat_struct *stats, *statsp, *statsp2, *statsp3;
  struct stat statbuf;

  /* for getopt */
  int c;
  /*extern char *optarg;*/
  extern int optind;

  while ((c = getopt(argc, argv, "uUgGmMtTnNzZlLhHdD")) != EOF) {
    switch (c) {
    case 'u': uflag = TRUE ; break;
    case 'U': uflag = FALSE; break;
    case 'g': gflag = TRUE ; break;
    case 'G': gflag = FALSE; break;
    case 'm': mflag = TRUE ; break;
    case 'M': mflag = FALSE; break;
    case 't': tflag = TRUE ; break;
    case 'T': tflag = FALSE; break;
    case 'n': nflag = TRUE ; break;
    case 'N': nflag = FALSE; break;
    case 'z': zflag = TRUE ; break;
    case 'Z': zflag = FALSE; break;
    case 'l': lflag = TRUE ; break;
    case 'L': lflag = FALSE; break;
#ifdef MD5
    case 'h': hflag = TRUE ; break;
    case 'H': hflag = FALSE; break;
#else
    case 'h':
    case 'H': fprintf(stderr, "MD5 hashing is not implemented.\n"); exit(1);
#endif
    case 'd': debugbits |= debugor;	/* fall through */
    case 'D': debugor <<= 1; break;
    case '?': usage(argv[0]); exit(1);
    }
  }

  stdin_files = (optind == argc);	/* where do we get filenames from ? */

  if (debugbits) {
#define TF(x)	(x ? "TRUE " : "FALSE")
#define MN(x)	(x ? "must" : "need not")
#define OI(x)	(x ? "omit" : "include")
#define DN(x)	(x ? "" : "not ")
    fprintf(stderr, "flags:\n");
    fprintf(stderr, "  u: %s   (files %s have the same user ID)\n", TF(uflag), MN(uflag));
    fprintf(stderr, "  g: %s   (files %s have the same group ID)\n", TF(gflag), MN(gflag));
    fprintf(stderr, "  m: %s   (files %s have the same mode)\n", TF(mflag), MN(mflag));
    fprintf(stderr, "  t: %s   (files %s have the same modify time)\n", TF(tflag), MN(tflag));
    fprintf(stderr, "  t: %s   (files %s have the same basename)\n", TF(nflag), MN(nflag));
    fprintf(stderr, "  z: %s   (omit zero-length files)\n", TF(zflag), OI(zflag));
    fprintf(stderr, "  l: %s   (omit file pairs that are hard links to each other)\n", TF(lflag), OI(lflag));
    fprintf(stderr, "  h: %s   (do %suse MD5 hashing)\n", TF(hflag), DN(hflag));
    fprintf(stderr, "  DMASK_INIT: %s\n", TF(debugbits & DMASK_INIT));
    fprintf(stderr, "  DMASK_STAT: %s\n", TF(debugbits & DMASK_STAT));
    fprintf(stderr, "  DMASK_COMP: %s\n", TF(debugbits & DMASK_COMP));
    fprintf(stderr, "  DMASK_MD5 : %s\n", TF(debugbits & DMASK_MD5 ));
    fprintf(stderr, "  DMASK_TEST: %s\n", TF(debugbits & DMASK_TEST));
  }
  /* get and count file names */
  namelist = NULL;
  numnames = 0;
  while ((newname=get_filename(argc,argv,&optind)) != NULL) {
    namelist_new = (struct namelist_struct *)malloc(sizeof(struct namelist_struct));
    namelist_new->next = namelist;
    namelist_new->name = newname;
    namelist = namelist_new;
    if (debugbits & DMASK_INIT)
      fprintf(stderr, "initial %d: %s\033K\r", numnames, newname);
    numnames++;
  }
  if (debugbits & DMASK_INIT) fprintf(stderr, "\033K");

  if (numnames == 0) {
    usage(argv[0]);
    exit(1);
  }

  /* copy file names into array */
  stats = (struct stat_struct *)calloc(numnames+1, sizeof(struct stat_struct));
  for (statsp=stats+numnames-1; statsp>=stats; statsp--) {
    statsp->name = namelist->name;
    namelist_new = namelist->next;
    free(namelist);
    namelist = namelist_new;
  }

  /* find status of each file */
  for (statsp=stats; statsp<stats+numnames; statsp++) {
    if (-1 == stat(statsp->name, &statbuf)) {
      fprintf(stderr, "File ignored: cannot stat file \"%s\"\n", statsp->name);
      perror("");
      nullify(statsp);
    } else {
      if (! S_ISREG(statbuf.st_mode)) {
	fprintf(stderr, "File ignored: not a regular file: \"%s\"\n", statsp->name);
	nullify(statsp);
      } else {
	statsp->st_size  =         statbuf.st_size ;
	statsp->st_mode  = mflag ? statbuf.st_mode  : 0;
	statsp->st_uid   = uflag ? statbuf.st_uid   : 0;
	statsp->st_gid   = gflag ? statbuf.st_gid   : 0;
	statsp->st_mtim  = tflag ? statbuf.st_mtime : 0;
	statsp->st_name  = nflag ? basename(statsp->name) : "";
	statsp->st_dev   =         statbuf.st_dev  ;
	statsp->st_ino   =         statbuf.st_ino  ;
#ifdef MD5
	statsp->digest = NULL;
#endif
	if (debugbits & DMASK_STAT)
	  fprintf(stderr, "stat %d/%d: %s\033K\r", statsp-stats, numnames, statsp->name);
      }
    }
  }
  /* insert sentinal */
  nullify(stats+numnames);
  if (debugbits & DMASK_STAT) fprintf(stderr, "\033K");

  /* sort files by size, mode, uid, gid, mtime, name */
  sortnameq = TRUE;
  qsort(stats, numnames+1, sizeof(struct stat_struct), (int (*)())sortstat);

  /* for files with identical attributes, compare contents */
  sortnameq = FALSE;
  statsp = stats;
  if (zflag)
    while (statsp->st_size == 0)
      statsp++;
  for ( ; statsp->name != NULL; ) {
    /* find next one that doesn't match this one */
    statsp2 = statsp+1;
    while (0 == sortstat(statsp, statsp2))
      statsp2++;
    /* compare each one in this group against all others in this group */
    for ( ; statsp<statsp2; statsp++) {
#ifdef MD5
      if (debugbits & DMASK_TEST) fprintf(stderr, "statsp+2 = 0x%x, statsp2 = 0x%x, hflag = %d, statsp->digest = 0x%x\n", statsp+2, statsp2, hflag, statsp->digest);
      if ((statsp+2 < statsp2) && hflag)	/* at least 3 files to compare */
	if (statsp->digest == NULL)
	  statsp->digest = md5_file(statsp->name);
#endif
      for (statsp3=statsp+1; statsp3<statsp2; statsp3++) {
	if (debugbits & DMASK_COMP)
	  fprintf(stderr, "compare %d:%d:%d %s %s\033K\r", statsp-stats, statsp3-stats, statsp2-stats, statsp->name, statsp3->name);
	if (0 == filecompare(statsp, statsp3)) {
	  printf("%s\t%s\n", statsp->name, statsp3->name);
	  break;
	}
      }
#ifdef MD5
      if (NULL != statsp->digest) {
	free(statsp->digest);
	statsp->digest = NULL;
      }
#endif
    }
  }
  if (debugbits & DMASK_MD5 ) fprintf(stderr, "\n\033K\033A");
  if (debugbits & DMASK_COMP) fprintf(stderr, "\033K");
  return 0;
}


char *get_filename(argc, argv, optindp)
int argc, *optindp;
char **argv;
{
  if (stdin_files) {
    /* get file names from standard input */
    char line[1000];
    int len;
    if (fgets(line, sizeof(line), stdin) == NULL)
      return NULL;
    else {
      len = strlen(line);
      if (line[len-1] != '\n') {
	fprintf(stderr, "file name too long:\n%s\n", line);
	exit(2);
      }
      line[len-1] = '\0';
      return strdup(line);
    }
  } else {
    /* get file names from the command line */
    if (*optindp < argc)
      return argv[(*optindp)++];
    else
      return NULL;
  }
}


/* return a pointer to the basename of the given filename */
char *basename(name)
     char *name;
{
  char *retval = strrchr(name, '/');
  if (retval == NULL)
    return name;
  else
    return retval+1;
}


/* compare two strings with strcmp, but take care of NULLS */
int mystrcmp(a,b)
     char *a, *b;
{
  if (a == NULL && b == NULL) return  0;
  if (a == NULL)              return  1;
  if (b == NULL)              return -1;
  return strcmp(a,b);
}


/* sort files by size, mode, uid, gid, mtime, basename, name */
int sortstat(a, b)
struct stat_struct *a, *b;
{
  int cmp;
  if (a->st_size  < b->st_size ) return -1;
  if (a->st_size  > b->st_size ) return  1;
  if (a->st_mode  < b->st_mode ) return -1;
  if (a->st_mode  > b->st_mode ) return  1;
  if (a->st_uid   < b->st_uid  ) return -1;
  if (a->st_uid   > b->st_uid  ) return  1;
  if (a->st_gid   < b->st_gid  ) return -1;
  if (a->st_gid   > b->st_gid  ) return  1;
  if (a->st_mtim  < b->st_mtim ) return -1;
  if (a->st_mtim  > b->st_mtim ) return  1;
  cmp = mystrcmp(a->st_name, b->st_name);
  if (cmp) return cmp;
  if (sortnameq)
    return mystrcmp(a->name, b->name);
  else
    return 0;
}


int filecompare(stat1, stat2)
struct stat_struct *stat1, *stat2;
{
  int fd1, fd2;
  int bytesleft, bytesthis;
  char cmparr1[65536], cmparr2[65536];
  char *pt1, *pt2, *pt1end;

  if (stat1->st_ino == stat2->st_ino && stat1->st_dev == stat2->st_dev) {
    if (lflag) return -1;  /* pretend they don't match so they're not printed */
    else       return  0;  /* yes, they match -- print them */
  }

#ifdef MD5
  if (stat1->digest != NULL && stat2->digest == NULL)
    if (NULL == (stat2->digest = md5_file(stat2->name)))
      return -1;
  if (stat2->digest != NULL && stat1->digest == NULL)
    if (NULL == (stat1->digest = md5_file(stat1->name)))
      return -1;
  if (stat1->digest != NULL) {
    if (0 != memcmp(stat1->digest, stat2->digest, 16))
      return -1;
  }
#endif

  if (-1 == (fd1=open(stat1->name, O_RDONLY))) {
    fprintf(stderr, "cannot open \"%s\"\n", stat1->name);
    perror("open");
    close(fd1);
    return -1;
  }
  if (-1 == (fd2=open(stat2->name, O_RDONLY))) {
    fprintf(stderr, "cannot open \"%s\"\n", stat2->name);
    perror("open");
    close(fd1); close(fd2);
    return -1;
  }

  bytesleft = stat1->st_size;	/* same as stat2->st_size */
  while (bytesleft>0) {
    bytesthis = bytesleft < sizeof(cmparr1) ? bytesleft : sizeof(cmparr1);
    if (bytesthis != read(fd1, cmparr1, bytesthis)) {
      fprintf(stderr, "problems reading \"%s\"\n", stat1->name);
      perror("read");
      close(fd1); close(fd2);
      return -1;
    }
    if (bytesthis != read(fd2, cmparr2, bytesthis)) {
      fprintf(stderr, "problems reading \"%s\"\n", stat2->name);
      perror("read");
      close(fd1); close(fd2);
      return -1;
    }
    pt1end = cmparr1+bytesthis;
    for (pt1=cmparr1, pt2=cmparr2; pt1<pt1end; pt1++, pt2++)
      if (*pt1 != *pt2) {
	close(fd1); close(fd2);
	return -1;	/* mis-compare */
      }
    bytesleft -= bytesthis;
  }
  close(fd1); close(fd2);
  return 0;	/* success */
}


void nullify(statp)
struct stat_struct *statp;
{
  statp->name    = NULL;
  statp->st_size = MAXINT;
}


#ifdef MD5
unsigned char *md5_file(name)
char *name;
{
  MD5_CTX mdContext;
  unsigned char *digest;
  int fd;
  int bytesread;
  char filebuf[65536];

  if (debugbits & DMASK_MD5) fprintf(stderr, "\nMD5: %s\033K\033A\r", name);

  if (-1 == (fd=open(name, O_RDONLY))) {
    fprintf(stderr, "cannot open \"%s\"\n", name);
    perror("open");
    close(fd);
    return NULL;
  }

  MD5Init(&mdContext);
  while (0 < (bytesread = read(fd, filebuf, sizeof(filebuf)))) {
    MD5Update(&mdContext, filebuf, bytesread);
  }
  digest = (unsigned char *)malloc(16);
  MD5Final(digest, &mdContext);
  close(fd);
  return digest;
}
#endif


