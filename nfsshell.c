/*
 * Copyright, 1991, 1992, by Leendert van Doorn (leendert@cs.vu.nl)
 *
 * This material is copyrighted by Leendert van Doorn, 1991, 1992. The usual
 * standard disclaimer applies, especially the fact that the author nor the
 * Vrije Universiteit, Amsterdam are liable for any damages caused by direct or
 * indirect use of the information or functionality provided by this program.
 */

/*
 * nfs - A shell that provides access to NFS file systems
 */
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <signal.h>
#include <setjmp.h>
#include <netdb.h>
#include <errno.h>
#include <rpc/rpc.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/vnode.h>
#include <sys/vfs.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/time.h>
#include <ufs/inode.h>
#include "mount.h"
#include "nfs_prot.h"

/*
 * Fundamental constants
 */
#define	NARGVEC		100	/* maximum number of arguments */

/*
 * List of command identifiers
 */
#define	CMD_UNKNOWN	0	/* unknown command */
#define	CMD_HOST	1	/* host <host> */
#define	CMD_UID		2	/* uid [<uid>] */
#define	CMD_GID		3	/* gid [<gid>] */
#define	CMD_CD		4	/* cd [<path>] */
#define	CMD_LCD		5	/* lcd [<path>] */
#define	CMD_CAT		6	/* cat <filespec> */
#define	CMD_LS		7	/* ls [-l] <filespec> */
#define	CMD_GET		8	/* get <filespec> */
#define	CMD_DF		9	/* df */
#define	CMD_MOUNT	10	/* mount [-u] [-p] [<host>:]<path> */
#define	CMD_UMOUNT	11	/* umount */
#define	CMD_UMOUNTALL	12	/* umountall */
#define	CMD_EXPORT	13	/* export */
#define	CMD_DUMP	14	/* dump */
#define	CMD_STATUS	15	/* status */
#define	CMD_HELP	16	/* help */
#define	CMD_QUIT	17	/* quit */
#define	CMD_RM		18	/* rm <file> */
#define	CMD_LN		19	/* ln <file1> <file2> */
#define	CMD_MV		20	/* mv <file1> <file2> */
#define	CMD_MKDIR	21	/* mkdir <dir> */
#define	CMD_RMDIR	22	/* rmdir <dir> */
#define	CMD_CHMOD	23	/* chmod <mode> <file> */
#define	CMD_CHOWN	24	/* chown <uid>[.<gid>] <file> */
#define	CMD_PUT		25	/* put <local-file> [<remote-file>] */
#define CMD_HANDLE	26	/* handle [<file-handle>] */
#define CMD_MKNOD	27	/* mknod <file> <major> <minor> */
#define CMD_MROOT	28	/* mroot */

/*
 * Key word table
 */
struct keyword {
    char *kw_command;
    int kw_value;
    char *kw_help;
} keyword[] = {
    "host",	CMD_HOST,	"<host> - set remote host name",
    "uid",	CMD_UID,	"[<uid>] - set remote user id",
    "gid",	CMD_GID,	"[<gid>] - set remote group id",
    "cd",	CMD_CD,		"[<path>] - change remote working directory",
    "lcd",	CMD_LCD,	"[<path>] - change local working directory",
    "cat",	CMD_CAT,	"<filespec> - display remote file",
    "ls",	CMD_LS,		"[-l] <filespec> - list remote directory",
    "get",	CMD_GET,	"<filespec> - get remote files",
    "df",	CMD_DF,		"- file system information",
    "rm",	CMD_RM,		"<file> - delete remote file",
    "ln",	CMD_LN,		"<file1> <file2> - link file",
    "mv",	CMD_MV,		"<file1> <file2> - move file",
    "mkdir",	CMD_MKDIR,	"<dir> - make remote directory",
    "rmdir",	CMD_RMDIR,	"<dir> - remove remote directory",
    "chmod",	CMD_CHMOD,	"<mode> <file> - change mode",
    "chown",	CMD_CHOWN,	"<uid>[.<gid>] <file> -  change owner",
    "put",	CMD_PUT,	"<local-file> [<remote-file>] - put file",
    "mount",	CMD_MOUNT,	"[-u] [-p] [<host>:]<path> - mount file system",
    "umount",	CMD_UMOUNT,	"- umount remote file system",
    "umountall",CMD_UMOUNTALL,	"- umount all remote file systems",
    "export",	CMD_EXPORT,	"- show all exported file systems",
    "dump",	CMD_DUMP,	"- show all remote mounted file systems",
    "status",	CMD_STATUS,	"- general status report",
    "help",	CMD_HELP,	"- this help message",
    "quit",	CMD_QUIT,	"- name says it all",
    "handle",	CMD_HANDLE,	"[<handle>] - get/set directory file handle",
    "mknod",	CMD_MKNOD,	"mknod <file> <major> <minor> - make devices",
    "mroot", 	CMD_MROOT,	"- attempt to masquerade as root"
};

/* run-time settable flags */
int verbose = 1;		/* verbosity flag */
int interact = 1;		/* interactive mode */

/* user provided credentials */
int uid = -2;			/* remote user id (initialy nobody) */
int gid = -2;			/* remote group id (initialy nobody) */

/* server information (also used as state information) */
 char *mountpath;		/* remote mount path */ char *remotehost;		/* remote host name */ struct sockaddr_in server_addr;	/* remote server address information */ struct sockaddr_in mntserver_addr; /* remote mount server address */
struct sockaddr_in nfsserver_addr; /* remote mount server address */
CLIENT *mntclient;		/* mount RPC client */
CLIENT *nfsclient;		/* nfs RPC client */
fhstatus *mountpoint;		/* remote mount point */
fhandle directory_handle;	/* current directory handle */
struct timeval timeout = { 60, 0 }; /* default time out */
int transfersize;		/* NFS default transfer size */

/* interrupt environments */
jmp_buf intenv;			/* where to go in interrupts */

fhstatus *pmap_mnt();

char *malloc();
char *calloc();
char *realloc();
char *strsave();
char *strchr();
char *nfs_error();
char *getenv();
void interrupt();

main(argc, argv)
    int argc;
    char **argv;
{
    int opt, cmd, argcount;
    char *argvec[NARGVEC];
    char buffer[BUFSIZ];

    /* command line option processing */
    while ((opt = getopt(argc, argv, "vi")) != EOF) {
	switch (opt) {
	case 'v':
	    verbose = 0;
	    break;
	case 'i':
	    interact = 0;
	    break;
	default:
	    fprintf(stderr, "Usage: %s [-v]\n", argv[0]);
	    exit(1);
	}
    }

    signal(SIGINT, interrupt);

    /* interpreter's main command loop */
    if (setjmp(intenv)) putchar('\n');
    while (getline(buffer, BUFSIZ, &argcount, argvec, NARGVEC)) {
	if (argcount == 0) continue;
	if ((cmd = command(argvec[0])) == CMD_QUIT)
	    break;
	else switch (cmd) {
	case CMD_HOST:
	    do_host(argcount, argvec);
	    break;
	case CMD_UID:
	    do_setid(&uid, argcount, argvec);
	    break;
	case CMD_GID:
	    do_setid(&gid, argcount, argvec);
	    break;
	case CMD_CD:
	    do_cd(argcount, argvec);
	    break;
	case CMD_LCD:
	    do_lcd(argcount, argvec);
	    break;
	case CMD_CAT:
	    do_cat(argcount, argvec);
	    break;
	case CMD_LS:
	    do_ls(argcount, argvec);
	    break;
	case CMD_GET:
	    do_get(argcount, argvec);
	    break;
	case CMD_DF:
	    do_df(argcount, argvec);
	    break;
	case CMD_RM:
	    do_rm(argcount, argvec);
	    break;
	case CMD_LN:
	    do_ln(argcount, argvec);
	    break;
	case CMD_MV:
	    do_mv(argcount, argvec);
	    break;
	case CMD_MKDIR:
	    do_mkdir(argcount, argvec);
	    break;
	case CMD_RMDIR:
	    do_rmdir(argcount, argvec);
	    break;
	case CMD_CHMOD:
	    do_chmod(argcount, argvec);
	    break;
	case CMD_CHOWN:
	    do_chown(argcount, argvec);
	    break;
	case CMD_PUT:
	    do_put(argcount, argvec);
	    break;
	case CMD_HANDLE:
	    do_handle(argcount, argvec);
	    break;
	case CMD_MKNOD:
	    do_mknod(argcount, argvec);
	    break;
	case CMD_MROOT:
	    do_mroot(&uid, argcount, argvec);
	    break;
	case CMD_MOUNT:
	    do_mount(argcount, argvec);
	    break;
	case CMD_UMOUNT:
	    do_umount(argcount, argvec);
	    break;
	case CMD_UMOUNTALL:
	    do_umountall(argcount, argvec);
	    break;
	case CMD_EXPORT:
	    do_export(argcount, argvec);
	    break;
	case CMD_DUMP:
	    do_dump(argcount, argvec);
	    break;
	case CMD_STATUS:
	    do_status(argcount, argvec);
	    break;
	case CMD_HELP:
	    do_help(argcount, argvec);
	    break;
	case CMD_UNKNOWN:
	    if (buffer[0] == '!') {
		system(buffer + 1);
		printf("!\n");
	    } else
	        fprintf(stderr, "%s: unrecognized command\n", argvec[0]);
	    break;
	default:
	    fprintf(stderr, "internal error: '%s' not is case\n", argvec[0]);
	    break;
	}
    }
    if (remotehost) close_mount();
    exit(0);
}

void
interrupt()
{
    longjmp(intenv, 1);
}

/*
 * Read a line from standard input and break
 * it up into an argument vector.
 */
int
getline(buf, bufsize, argc, argv, argvsize)
    char *buf, **argv;
    int bufsize, *argc, argvsize;
{
    register char *p;

    if (interact) printf("nfs> ");
    if (fgets(buf, bufsize, stdin) == NULL)
	return 0;
    *argc = 0;
    for (p = buf; *p == ' ' || *p == '\t'; p++)
	/* skip white spaces */;
    while (*p != ' ' && *p != '\t' && *p != '\n' && *p != '\0') {
	if (*argc > argvsize) break;
	argv[(*argc)++] = p;
	for (; *p != ' ' && *p != '\t' && *p != '\n' && *p != '\0'; p++)
	    /* skip word */;
	if (*p != '\0') *p++ ='\0';
	for (; *p == ' ' || *p == '\t'; p++)
	   /* skip white spaces */;
    }
    return 1;
}

/*
 * Search for command in keyword table
 */
int
command(cmd)
    char *cmd;
{
    register int i;

    for (i = 0; i < sizeof(keyword)/sizeof(struct keyword); i++)
	if (strcmp(keyword[i].kw_command, cmd) == 0)
	    return keyword[i].kw_value;
    return CMD_UNKNOWN;
}

/*
 * Set remote host and initialize RPC channel
 * to mount daemon.
 */
do_host(argc, argv)
    int argc;
    char **argv;
{
    if (argc != 2)
	fprintf(stderr, "Usage: host <host>\n");
    else
	open_mount(argv[1]);
}

/*
 * Set user or group id (updating RPC authentication info)
 */
do_setid(var, argc, argv)
    int *var, argc;
    char **argv;
{
    *var = argc == 2 ? atoi(argv[1]) : -2;
    if (nfsclient && nfsclient->cl_auth) {
	auth_destroy(nfsclient->cl_auth);
	nfsclient->cl_auth = authunix_create_default(uid, gid);
    }
}

/*
 * Change remote working directory
 */
do_cd(argc, argv)
    int argc;
    char **argv;
{
    register char *p;
    char *component;
    diropargs args;
    diropres *res;
    fhandle handle;

    if (mountpath == NULL) {
	fprintf(stderr, "cd: no remote file system mounted\n");
	return;
    }

    /* easy case: cd to root */
    if (argc == 1) {
	bcopy(mountpoint->fhstatus_u.fhs_fhandle, directory_handle, NFS_FHSIZE);
	return;
    }

    /* if a directory start with '/', we search from the root */
    if (*(p = argv[1]) == '/') {
	bcopy(mountpoint->fhstatus_u.fhs_fhandle, handle, NFS_FHSIZE);
	p++;
    } else
	bcopy(directory_handle, handle, NFS_FHSIZE);

    /*
     * Break path up into directory components and check every
     * component for its validity.
     */
    for (;;) {
	if (*p == '\0') break;
	for (component = p; *p != '/' && *p != '\0'; p++)
	    /* do nothing */;
	*p++ = '\0';
	args.name = component;
	bcopy(handle, &args.dir, NFS_FHSIZE);
	if ((res = nfsproc_lookup_2(&args, nfsclient)) == NULL) {
	    clnt_perror(nfsclient, "nfsproc_lookup");
	    return;
	}
	if (res->status != NFS_OK) {
	    fprintf(stderr, "%s: %s\n", component, nfs_error(res->status));
	    return;
	}
	if (res->diropres_u.diropres.attributes.type != NFDIR) {
	    fprintf(stderr, "%s: is not a directory\n", component);
	    return;
	}
	bcopy(&res->diropres_u.diropres.file, handle, NFS_FHSIZE);
    }
    bcopy(handle, directory_handle, NFS_FHSIZE);
}

/*
 * Change local working directory
 */
do_lcd(argc, argv)
    int argc;
    char **argv;
{
    if (argc == 1) {
	char *home = getenv("HOME");
	if (home != NULL)
	    if (chdir(home) != 0) perror("lcd");
    } else
	if (chdir(argv[1]) != 0) perror("lcd");
}

/*
 * Display a remote file
 */
do_cat(argc, argv)
    int argc;
    char **argv;
{
    diropargs dargs;
    diropres *dres;
    readargs rargs;
    readres *rres;
    long offset;

    if (mountpath == NULL) {
	fprintf(stderr, "cat: no remote file system mounted\n");
	return;
    }
    if (argc != 2) {
	fprintf(stderr, "Usage: cat <filespec>\n");
	return;
    }

    /* lookup name in current directory */
    dargs.name = argv[1];
    bcopy(directory_handle, &dargs.dir, NFS_FHSIZE);
    if ((dres = nfsproc_lookup_2(&dargs, nfsclient)) == NULL) {
	clnt_perror(nfsclient, "nfsproc_lookup");
	return;
    }
    if (dres->status != NFS_OK) {
	fprintf(stderr, "%s: %s\n", argv[1], nfs_error(dres->status));
	return;
    }
    if (dres->diropres_u.diropres.attributes.type != NFREG) {
	fprintf(stderr, "%s: is not a regular file\n", argv[1]);
	return;
    }
    bcopy(&dres->diropres_u.diropres.file, &rargs.file, NFS_FHSIZE);
    for (offset = 0; offset < dres->diropres_u.diropres.attributes.size; ) {
	rargs.offset = offset;
	rargs.count = rargs.totalcount = transfersize;
	if ((rres = nfsproc_read_2(&rargs, nfsclient)) == NULL) {
	    clnt_perror(nfsclient, "nfsproc_read_2");
	    break;
        }
	if (rres->status != NFS_OK) {
	    fprintf(stderr, "%s: %s\n", argv[1], nfs_error(rres->status));
	    break;
	}
        fwrite(rres->readres_u.reply.data.data_val,
            rres->readres_u.reply.data.data_len, 1, stdout);
	offset += transfersize;
    }
}

/*
 * List remote directory
 */
do_ls(argc, argv)
    int argc;
    char **argv;
{
    char **table, **ptr, **p;
    int lflag = 0;

    argv++; argc--;
    if (mountpath == NULL) {
	fprintf(stderr, "ls: no remote file system mounted\n");
	return;
    }
    if (argc >= 1 && strcmp(argv[0], "-l") == 0) {
	argv++; argc--;
	lflag = 1;
    }

    if (!getdirentries(directory_handle, &table, &ptr, 20))
	return;
    for (p = table; p < ptr; p++) {
	if (!match(*p, argc, argv)) continue;
	if (lflag == 1)
	    printfilestatus(*p);
	else
	    printf("%s\n", *p);
	free(*p);
    }
    free(table);
}

/*
 * Print long listing of a files, much in the way ``ls -l'' does
 */
printfilestatus(name)
    char *name;
{
    diropargs args;
    diropres *res;
    int mode;

    args.name = name;
    bcopy(directory_handle, &args.dir, NFS_FHSIZE);

    if ((res = nfsproc_lookup_2(&args, nfsclient)) == NULL) {
	clnt_perror(nfsclient, "nfsproc_lookup");
	return;
    }
    if (res->status != NFS_OK) {
	fprintf(stderr, "Lookup failed: %s\n", nfs_error(res->status));
	return;
    }

    switch (res->diropres_u.diropres.attributes.type) {
	case NFNON:
	    putchar('s');
	    break;
	case NFREG:
	    putchar('-');
	    break;
	case NFDIR:
	    putchar('d');
	    break;
	case NFBLK:
	    putchar('b');
	    break;
	case NFCHR:
	    putchar('c');
	    break;
	case NFLNK:
	    putchar('l');
	    break;
	default:
	    putchar('?');
	    break;
    }
    mode = res->diropres_u.diropres.attributes.mode;
    if (mode & 0400) putchar('r'); else putchar('-');
    if (mode & 0200) putchar('w'); else putchar('-');
    if (mode & 0100)
	if (mode & 04000) putchar('s'); else putchar('x');
    else
	if (mode & 04000) putchar('S'); else putchar('-');
    if (mode & 040) putchar('r'); else putchar('-');
    if (mode & 020) putchar('w'); else putchar('-');
    if (mode & 010)
	if (mode & 02000) putchar('s'); else putchar('x');
    else
	if (mode & 02000) putchar('S'); else putchar('-');
    if (mode & 04) putchar('r'); else putchar('-');
    if (mode & 02) putchar('w'); else putchar('-');
    if (mode & 01)
	if (mode & 01000) putchar('t'); else putchar('x');
    else
	if (mode & 01000) putchar('T'); else putchar('-');
    printf("%3d%9d%6d%10d ",
	res->diropres_u.diropres.attributes.nlink,
	res->diropres_u.diropres.attributes.uid,
	res->diropres_u.diropres.attributes.gid,
	res->diropres_u.diropres.attributes.size);
    writefiledate(res->diropres_u.diropres.attributes.ctime.seconds);
    printf(" %s", name);
    if (res->diropres_u.diropres.attributes.type == NFLNK) {
	readlinkres *rlres;
	nfs_fh rlargs;

	bcopy(&res->diropres_u.diropres.file, &rlargs, NFS_FHSIZE);
	if ((rlres = nfsproc_readlink_2(&rlargs, nfsclient)) == NULL) {
	    clnt_perror(nfsclient, "nfsproc_readlink");
	    return;
	}
	if (res->status != NFS_OK) {
	    fprintf(stderr, "Lookup failed: %s\n", nfs_error(res->status));
	    return;
	}
	printf(" -> %s\n", rlres->readlinkres_u.data);
    } else
	putchar('\n');
}

int
writefiledate(d)
    time_t d;
{
    time_t now, sixmonthsago, onehourfromnow;
    char *cp;

    (void) time(&now);
    sixmonthsago = now - 6L*30L*24L*60L*60L;
    onehourfromnow = now + 60L*60L;
    cp = ctime(&d);
    if ((d < sixmonthsago) || (d > onehourfromnow))
	return printf(" %-7.7s %-4.4s ", cp+4, cp+20);
    else
	return printf(" %-12.12s ", cp+4);
}

/*
 * Get remote files
 */
do_get(argc, argv)
    int argc;
    char **argv;
{
    char **table, **ptr, **p;
    char answer[512];
    diropargs args;
    diropres *res;
    readargs rargs;
    readres *rres;
    int iflag = 0;
    long offset;
    FILE *fp;

    argv++; argc--;
    if (mountpath == NULL) {
	fprintf(stderr, "get: no remote file system mounted\n");
	return;
    }
    if (argc >= 1 && strcmp(argv[0], "-i") == 0) {
	argv++; argc--;
	iflag = 1;
    }

    if (!getdirentries(directory_handle, &table, &ptr, 20))
	return;
    for (p = table; p < ptr; p++) {
	/* match before going over the wire */
	if (!match(*p, argc, argv)) continue;

	/* only regular files can be transfered */
	args.name = *p;
	bcopy(directory_handle, &args.dir, NFS_FHSIZE);
	if ((res = nfsproc_lookup_2(&args, nfsclient)) == NULL) {
	    clnt_perror(nfsclient, "nfsproc_lookup");
	    return;
	}
	if (res->status != NFS_OK) {
	    fprintf(stderr, "Lookup failed: %s\n", nfs_error(res->status));
	    return;
	}
	if (res->diropres_u.diropres.attributes.type != NFREG)
	    continue;

	/* ask for confirmation */
	printf("%s? ", *p);
	if (!iflag) {
	    gets(answer);
	    if (answer[0] != 'y' && answer[0] != 'Y') continue;
	} else
	    printf("Yes\n");

	/* get actual file */
	if ((fp = fopen(*p, "w")) == NULL) {
	    fprintf(stderr, "get: cannot create %s\n", *p);
	    continue;
	}
	bcopy(&res->diropres_u.diropres.file, &rargs.file, NFS_FHSIZE);
	for (offset = 0; offset < res->diropres_u.diropres.attributes.size; ) {
	    rargs.offset = offset;
	    rargs.count = rargs.totalcount = transfersize;
	    if ((rres = nfsproc_read_2(&rargs, nfsclient)) == NULL) {
		clnt_perror(nfsclient, "nfsproc_read");
		break;
            }
	    if (rres->status != NFS_OK) {
		fprintf(stderr, "%s: %s\n", argv[1], nfs_error(rres->status));
		break;
	    }
	    fwrite(rres->readres_u.reply.data.data_val,
		rres->readres_u.reply.data.data_len, 1, fp);
	    offset += transfersize;
	}
	fclose(fp);
	free(*p);
    }
    free(table);
}

/*
 * Show file system information
 */
/* ARGUSED */
do_df(argc, argv)
    int argc;
    char **argv;
{
    statfsres *res;

    if (mountpath == NULL) {
	fprintf(stderr, "df: no remote file system mounted\n");
	return;
    }
    if (argc != 1) {
	fprintf(stderr, "Usage: df\n");
	return;
    }
    if ((res = nfsproc_statfs_2(directory_handle, nfsclient)) == NULL) {
	clnt_perror(nfsclient, "nfsproc_statfs");
	return;
    }
    if (res->status != NFS_OK) {
	fprintf(stderr, "Df failed: %s\n", nfs_error(res->status));
	return;
    }

#define x res->statfsres_u.reply
    printf("%s:%s    %dK, %dK used, %dK free (%dK useable).\n",
	remotehost, mountpath, x.blocks, (x.blocks-x.bfree), x.bfree, x.bavail);

    if (x.bsize != 1024)
	printf("Warning: This filesystem has a blocksize of %d bytes.\n",
	    x.bsize);
#undef x
}

/*
 * Delete a remote file
 */
do_rm(argc, argv)
    int argc;
    char **argv;
{
    diropargs args;
    nfsstat *res;

    if (mountpath == NULL) {
	fprintf(stderr, "rm: no remote file system mounted\n");
	return;
    }
    if (argc != 2) {
	fprintf(stderr, "Usage: rm <file>\n");
	return;
    }
    args.name = argv[1];
    bcopy(directory_handle, &args.dir, NFS_FHSIZE);
    if ((res = nfsproc_remove_2(&args, nfsclient)) == NULL) {
	clnt_perror(nfsclient, "nfsproc_remove");
	return;
    }
    if (*res != NFS_OK) {
	fprintf(stderr, "Remove failed: %s\n", nfs_error(*res));
	return;
    }
}

/*
 * Link a file
 */
do_ln(argc, argv)
    int argc;
    char **argv;
{
    diropargs dargs;
    linkargs largs;
    diropres *dres;
    nfsstat *lres;

    if (mountpath == NULL) {
	fprintf(stderr, "ln: no remote file system mounted\n");
	return;
    }
    if (argc != 3) {
	fprintf(stderr, "Usage: ln <file1> <file2>\n");
	return;
    }

    dargs.name = argv[1];
    bcopy(directory_handle, &dargs.dir, NFS_FHSIZE);
    if ((dres = nfsproc_lookup_2(&dargs, nfsclient)) == NULL) {
	clnt_perror(nfsclient, "nfsproc_lookup");
	return;
    }
    if (dres->status != NFS_OK) {
	fprintf(stderr, "%s: %s\n", argv[1], nfs_error(dres->status));
	return;
    }

    bcopy(&dres->diropres_u.diropres.file, &largs.from, NFS_FHSIZE);
    largs.to.name = argv[2];
    bcopy(directory_handle, &largs.to.dir, NFS_FHSIZE);

    if ((lres = nfsproc_link_2(&largs, nfsclient)) == NULL) {
	clnt_perror(nfsclient, "nfsproc_link");
	return;
    }
    if (*lres != NFS_OK) {
	fprintf(stderr, "Link failed: %s\n", nfs_error(*lres));
	return;
    }
}

/*
 * Move a file or directory
 */
do_mv(argc, argv)
    int argc;
    char **argv;
{
    renameargs args;
    nfsstat *res;

    if (mountpath == NULL) {
	fprintf(stderr, "mv: no remote file system mounted\n");
	return;
    }
    if (argc != 3) {
	fprintf(stderr, "Usage: mv <file1> <file2>\n");
	return;
    }
    args.from.name = argv[1];
    bcopy(directory_handle, &args.from.dir, NFS_FHSIZE);
    args.to.name = argv[2];
    bcopy(directory_handle, &args.to.dir, NFS_FHSIZE);
    if ((res = nfsproc_rename_2(&args, nfsclient)) == NULL) {
	clnt_perror(nfsclient, "nfsproc_rename");
	return;
    }
    if (*res != NFS_OK) {
	fprintf(stderr, "Rename failed: %s\n", nfs_error(*res));
	return;
    }
}

/*
 * Make remote directory
 */
do_mkdir(argc, argv)
    int argc;
    char **argv;
{
    createargs args;
    diropres *res;

    if (mountpath == NULL) {
	fprintf(stderr, "mkdir: no remote file system mounted\n");
	return;
    }
    if (argc != 2) {
	fprintf(stderr, "Usage: mkdir <directory>\n");
	return;
    }

    args.where.name = argv[1];
    bcopy(directory_handle, &args.where.dir, NFS_FHSIZE);
    args.attributes.mode = 040755;
    args.attributes.uid = uid;
    args.attributes.gid = gid;
    args.attributes.size = -1;
    args.attributes.atime.seconds = -1;
    args.attributes.atime.useconds = -1;
    args.attributes.mtime.seconds = -1;
    args.attributes.mtime.useconds = -1;

    if ((res = nfsproc_mkdir_2(&args, nfsclient)) == NULL) {
	clnt_perror(nfsclient, "nfsproc_mkdir");
	return;
    }
    if (res->status != NFS_OK) {
	fprintf(stderr, "Make directory failed: %s\n", nfs_error(res->status));
	return;
    }
}

/*
 * Remove remote directory
 */
do_rmdir(argc, argv)
    int argc;
    char **argv;
{
    diropargs args;
    nfsstat *res;

    if (mountpath == NULL) {
	fprintf(stderr, "rmdir: no remote file system mounted\n");
	return;
    }
    if (argc != 2) {
	fprintf(stderr, "Usage: rmdir <directory>\n");
	return;
    }

    args.name = argv[1];
    bcopy(directory_handle, &args.dir, NFS_FHSIZE);
    if ((res = nfsproc_rmdir_2(&args, nfsclient)) == NULL) {
	clnt_perror(nfsclient, "nfsproc_rmdir");
	return;
    }
    if (*res != NFS_OK) {
	fprintf(stderr, "Remove directory failed: %s\n", nfs_error(*res));
	return;
    }
}

/*
 * Change mode of remote file or directory
 */
do_chmod(argc, argv)
    int argc;
    char **argv;
{
    sattrargs aargs;
    diropargs dargs;
    attrstat *ares;
    diropres *dres;
    int mode;

    if (mountpath == NULL) {
	fprintf(stderr, "chmod: no remote file system mounted\n");
	return;
    }
    if (argc != 3) {
	fprintf(stderr, "Usage: chmod <mode> <file>\n");
	return;
    }
    if (sscanf(argv[1], "%o", &mode) != 1) {
	fprintf(stderr, "chmod: invalid mode\n");
	return;
    }

    dargs.name = argv[2];
    bcopy(directory_handle, &dargs.dir, NFS_FHSIZE);
    if ((dres = nfsproc_lookup_2(&dargs, nfsclient)) == NULL) {
	clnt_perror(nfsclient, "nfsproc_lookup");
	return;
    }
    if (dres->status != NFS_OK) {
	fprintf(stderr, "%s: %s\n", argv[2], nfs_error(dres->status));
	return;
    }

    bcopy(&dres->diropres_u.diropres.file, &aargs.file, NFS_FHSIZE);
    aargs.attributes.mode = mode;
    aargs.attributes.uid = -1;
    aargs.attributes.gid = -1;
    aargs.attributes.size = -1;
    aargs.attributes.atime.seconds = -1;
    aargs.attributes.atime.useconds = -1;
    aargs.attributes.mtime.seconds = -1;
    aargs.attributes.mtime.useconds = -1;

    if ((ares = nfsproc_setattr_2(&aargs, nfsclient)) == NULL) {
	clnt_perror(nfsclient, "nfsproc_setattr");
	return;
    }
    if (ares->status != NFS_OK) {
	fprintf(stderr, "Set attributes failed: %s\n", nfs_error(ares->status));
	return;
    }
}

/*
 * Change owner (and group) of remote file or directory
 */
do_chown(argc, argv)
    int argc;
    char **argv;
{
    sattrargs aargs;
    diropargs dargs;
    attrstat *ares;
    diropres *dres;
    int own_uid, own_gid;

    if (mountpath == NULL) {
	fprintf(stderr, "chown: no remote file system mounted\n");
	return;
    }
    if (argc != 3) {
	fprintf(stderr, "Usage: chown <uid>[.<gid>] <file>\n");
	return;
    }
    if (sscanf(argv[1], "%d.%d", &own_uid, &own_gid) != 2) {
	own_gid = -1;
	if (sscanf(argv[1], "%d", &own_uid) != 1) {
	    fprintf(stderr, "chown: invalid uid[.gid]\n");
	    return;
	}
    }

    dargs.name = argv[2];
    bcopy(directory_handle, &dargs.dir, NFS_FHSIZE);
    if ((dres = nfsproc_lookup_2(&dargs, nfsclient)) == NULL) {
	clnt_perror(nfsclient, "nfsproc_lookup");
	return;
    }
    if (dres->status != NFS_OK) {
	fprintf(stderr, "%s: %s\n", argv[2], nfs_error(dres->status));
	return;
    }

    bcopy(&dres->diropres_u.diropres.file, &aargs.file, NFS_FHSIZE);
    aargs.attributes.mode = -1;
    aargs.attributes.uid = own_uid;
    aargs.attributes.gid = own_gid;
    aargs.attributes.size = -1;
    aargs.attributes.atime.seconds = -1;
    aargs.attributes.atime.useconds = -1;
    aargs.attributes.mtime.seconds = -1;
    aargs.attributes.mtime.useconds = -1;

    if ((ares = nfsproc_setattr_2(&aargs, nfsclient)) == NULL) {
	clnt_perror(nfsclient, "nfsproc_setattr");
	return;
    }
    if (ares->status != NFS_OK) {
	fprintf(stderr, "Set attributes failed: %s\n", nfs_error(ares->status));
	return;
    }
}

/*
 * Put file from local to remote
 */
do_put(argc, argv)
    int argc;
    char **argv;
{
    createargs cargs;
    diropargs dargs;
    diropres *cres;
    diropres *dres;
    char buf[BUFSIZ];
    fhandle handle;
    FILE *fp;
    int n;
    long offset;

    if (mountpath == NULL) {
	fprintf(stderr, "put: no remote file system mounted\n");
	return;
    }
    if (argc != 2 && argc != 3) {
	fprintf(stderr, "Usage: put <local-file> [<remote-file>]\n");
	return;
    }

    if ((fp = fopen(argv[1], "r")) == NULL) {
	fprintf(stderr, "put: cannot open %s\n", argv[1]);
	return;
    }

    /*
     * Create remote file name
     */
    cargs.where.name = argc == 3 ? argv[2] : argv[1];
    bcopy(directory_handle, &cargs.where.dir, NFS_FHSIZE);
    cargs.attributes.mode = 0666;
    cargs.attributes.uid = uid;
    cargs.attributes.gid = gid;
    cargs.attributes.size = -1;
    cargs.attributes.atime.seconds = -1;
    cargs.attributes.atime.useconds = -1;
    cargs.attributes.mtime.seconds = -1;
    cargs.attributes.mtime.useconds = -1;
    if ((cres = nfsproc_create_2(&cargs, nfsclient)) == NULL) {
	clnt_perror(nfsclient, "nfsproc_create");
	fclose(fp);
	return;
    }
    if (cres->status != NFS_OK)
	fprintf(stderr, "WARNING: Create failed: %s\n", nfs_error(*cres));

    /*
     * Look up remote file name, to get its handle
     */
    dargs.name = argc == 3 ? argv[2] : argv[1];
    bcopy(directory_handle, &dargs.dir, NFS_FHSIZE);
    if ((dres = nfsproc_lookup_2(&dargs, nfsclient)) == NULL) {
	clnt_perror(nfsclient, "nfsproc_lookup");
	fclose(fp);
	return;
    }
    if (dres->status != NFS_OK) {
	fprintf(stderr, "%s: %s\n", argv[1], nfs_error(dres->status));
	fclose(fp);
	return;
    }
    bcopy(&dres->diropres_u.diropres.file, handle, NFS_FHSIZE);

    for (offset = 0; (n = fread(buf, 1, sizeof(buf), fp)) > 0; offset += n) {
	writeargs wargs;
	attrstat *wres;

	bcopy(handle, &wargs.file, NFS_FHSIZE);
	wargs.beginoffset = wargs.offset = offset;
	wargs.totalcount = n;
	wargs.data.data_len = n;
	wargs.data.data_val = buf;
	if ((wres = nfsproc_write_2(&wargs, nfsclient)) == NULL) {
	    clnt_perror(nfsclient, "nfsproc_write");
	    fclose(fp);
	    return;
	}
	if (wres->status != NFS_OK) {
	    fprintf(stderr, "Write failed: %s\n", nfs_error(wres->status));
	    fclose(fp);
	    return;
	}
    }
    fclose(fp);
}

/*
 * Get/set file handle
 */
do_handle(argc, argv)
    int argc;
    char **argv;
{
    register char *p;
    register int i;
    int sock;

    if (argc <= 1) {
	if (mountpath == NULL) {
	    fprintf(stderr, "handle: no remote file system mounted\n");
	    return;
	}
	printf("%s:", mountpath);
	for (i = 0, p = (char *)directory_handle; i < NFS_FHSIZE; i++)
	    printf(" %02x", *p++ & 0xFF);
	printf("\n");
	return;
    }

    if (argc != NFS_FHSIZE + 1) {
	fprintf(stderr, "Usage: handle [<file handle>]\n");
	return;
    }

    /* umount previous mounted remote file system */
    if (mountpath != NULL)
	close_nfs();

    /* setup communication channel with NFS server */
    sock = privileged();
    nfsserver_addr = server_addr;
    if ((nfsclient = clntudp_create(&nfsserver_addr, NFS_PROGRAM,
      NFS_VERSION, timeout, &sock)) == NULL) {
        clnt_pcreateerror("nfs clntudp_create");
        return 0;
    }
    clnt_control(nfsclient, CLSET_TIMEOUT, &timeout);
    nfsclient->cl_auth = authunix_create_default(uid, gid);

    /* provide some generic name for it */
    if ((mountpath = malloc(8 + 1)) == NULL) {
	fprintf(stderr, "internal error: no more core for mountpath\n");
	return;
    }
    strcpy(mountpath, "<handle>");

    /* copy handle from command line argument */
    for (i = 0, p = (char *)directory_handle; i < NFS_FHSIZE; i++)
	*p++ = (char) xtoa(argv[i + 1]);

    /* get transfer size */
    if ((transfersize = determine_transfersize()) <= 0) {
	fprintf(stderr, "Handle failed: Unable to obtain transfer size\n");
	return;
    }
}

/*
 * Create a character device
 */
do_mknod(argc, argv)
    int argc;
    char **argv;

{
    createargs cargs;
    diropargs dargs;
    diropres *cres;
    diropres *dres;
    char buf[BUFSIZ];
    fhandle handle;
    FILE *fp;
    int n;
    long offset;

    if (mountpath == NULL) {
	fprintf(stderr, "mknod: no remote file system mounted\n");
	return;
    }
    if (argc != 4) {
	fprintf(stderr, "Usage: mknod <file> <major> <minor>]\n");
	return;
    }
printf("%s\n", argv[1]);

    cargs.where.name=argv[1];
    bcopy(directory_handle, &cargs.where.dir, NFS_FHSIZE);
    cargs.attributes.mode = IFCHR;
    cargs.attributes.uid = uid;
    cargs.attributes.gid = gid;
    cargs.attributes.size = makedev(0, 0);
    cargs.attributes.atime.seconds = -1;
    cargs.attributes.atime.useconds = -1;
    cargs.attributes.mtime.seconds = -1;
    cargs.attributes.mtime.useconds = -1;
    if ((cres = nfsproc_create_2(&cargs, nfsclient)) == NULL) {
	clnt_perror(nfsclient, "nfsproc_create");
	return;
    }
}

/*
 * Attempt to masquerade as root (and update RPC authenticaton info)
 */
do_mroot(var, argc, argv)
    int *var, argc;
    char **argv;

{
    *var = 0x00100000;
    if (nfsclient && nfsclient->cl_auth) {
	auth_destroy(nfsclient->cl_auth);
	nfsclient->cl_auth = authunix_create_default(uid, gid);
    }
/*
 * Put in some type of code here
 * to find a directory owned by root
 * and write a zero byte file to it.
 * once written, it would be possible
 * to test to make sure this method
 * works and tell the person upon
 * startup.
 */
}

int
xtoa(s)
    char *s;
{
    register int n;

    for (n = 0; *s; s++) {
	n <<= 4;
	if (*s >= '0' && *s <= '9')
	    n += *s - '0';
	else if (*s >= 'a' && *s <= 'f')
	    n += *s - 'a' + 10;
	else if (*s >= 'A' && *s <= 'F')
	    n += *s - 'A' + 10;
	else
	    break;
    }
    return n;
}

/*
 * Set up a channel to the NFS server and
 * mount remote file system.
 */
do_mount(argc, argv)
    int argc;
    char **argv;
{
    char *host, *path;
    int umount = 0, portmap = 0;

    argv++; argc--;
    if (argc >= 1 && strcmp(argv[0], "-u") == 0) {
	argv++; argc--;
	umount = 1;
    }
    if (argc >= 1 && strcmp(argv[0], "-p") == 0) {
	argv++; argc--;
	portmap = 1;
    }
    if (argc == 0) {
	fprintf(stderr, "Usage: mount [-u] [-p] [<host>:]<path>\n");
	return;
    }
    if ((path = strchr(host = argv[0], ':')) != NULL) {
	*path++ = '\0';
	/* this heuristic is far too simple */
	if (remotehost && strcmp(remotehost, host) == 0) {
	    if (mountpath) close_nfs();
	} else if (!open_mount(host))
	    return;
    } else
	path = argv[0];
    if (remotehost == NULL) {
	fprintf(stderr, "mount: no host specified\n");
	return;
    }
    open_nfs(path, umount, portmap);
}

/*
 * Unmount remote file system, and close
 * RPC channel.
 */
/* ARGUSED */
do_umount(argc, argv)
    int argc;
    char **argv;
{
    if (argc != 1) {
	fprintf(stderr, "Usage: umount\n");
	return;
    }
    if (mountpath == NULL)
	fprintf(stderr, "umount: no remote file system mounted\n");
    else
	close_nfs();
}

/*
 * Unmount all remote file system from this host
 */
/* ARGUSED */
do_umountall(argc, argv)
    int argc;
    char **argv;
{
    if (argc != 1) {
	fprintf(stderr, "Usage: umountall\n");
	return;
    }
    if (remotehost == NULL) {
	fprintf(stderr, "umountall: no host specified\n");
	return;
    }
    if (mountpath != NULL) close_nfs();
    (void) mountproc_umntall_1(NULL, mntclient);
}

/*
 * Display all exported file systems on remote system
 */
/* ARGUSED */
do_export(argc, argv)
    int argc;
    char **argv;
{
    exports ex, *exp;
    groups gr;

    if (argc != 1) {
	fprintf(stderr, "Usage: export\n");
	return;
    }
    if (remotehost == NULL) {
	fprintf(stderr, "export: no host specified\n");
	return;
    }
    if ((exp = mountproc_export_1(NULL, mntclient)) == NULL) {
	clnt_perror(mntclient, "mountproc_export");
	return;
    }
    printf("Export list for %s:\n", remotehost);
    for (ex = *exp; ex != NULL; ex = ex->ex_next) {
	printf("%-25s", ex->ex_dir);
	if (strlen(ex->ex_dir) >= 25)
	    printf("\n                    ");
	if ((gr = ex->ex_groups) == NULL)
	    printf("everyone");
	while (gr) {
	    printf("%s ", gr->gr_name);
	    gr = gr->gr_next;
	}
	putchar('\n');
    }
}

/*
 * Display all remote mounted file systems
 */
/* ARGUSED */
do_dump(argc, argv)
    int argc;
    char **argv;
{
    mountlist ml, *mlp;

    if (argc != 1) {
	fprintf(stderr, "Usage: dump\n");
	return;
    }
    if (remotehost == NULL) {
	fprintf(stderr, "dump: no host specified\n");
	return;
    }
    if ((mlp = mountproc_dump_1(NULL, mntclient)) == NULL) {
	clnt_perror(mntclient, "mountproc_dump");
	return;
    }
    for (ml = *mlp; ml != NULL; ml = ml->ml_next)
	printf("%s:%s\n", ml->ml_hostname, ml->ml_directory);
}

/*
 * Generic status report
 */
/* ARGUSED */
do_status(argc, argv)
    int argc;
    char **argv;
{
    if (argc != 1) {
	fprintf(stderr, "Usage: status\n");
	return;
    }
    printf("User id      : %d\n", uid);
    printf("Group id     : %d\n", gid);
    if (remotehost)
	printf("Remote host  : `%s'\n", remotehost);
    if (mountpath)
	printf("Mount path   : `%s'\n", mountpath);
    printf("Transfer size: %d\n", transfersize);
}

/*
 * Simple on-line help facility
 */
/* ARGUSED */
do_help(argc, argv)
    int argc;
    char **argv;
{
    register int i;

    for (i = 0; i < sizeof(keyword)/sizeof(struct keyword); i++) {
	if (argc == 2 && strcmp(keyword[i].kw_command, argv[1]) != 0)
	    continue;
	printf("%s %s\n", keyword[i].kw_command, keyword[i].kw_help);
    }
}

/*
 * Open a channel to remote Mount daemon,
 * possibly closing an already open connection.
 */
int
open_mount(host)
    char *host;
{
    int sock = privileged();

    /* close previous mounted host */
    if (remotehost != NULL)
	close_mount();

    /* convert hostname to IP address */
    if (isdigit(*host)) {
        server_addr.sin_addr.s_addr = inet_addr(host);
    } else {
        struct hostent *hp = gethostbyname(host);
        if (hp == NULL) {
            fprintf(stderr, "%s: unknown host\n", host);
	    return 0;
        }
        bcopy(hp->h_addr, &server_addr.sin_addr.s_addr, hp->h_length);
        host = hp->h_name;
    }
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = 0;

    /* set host name */
    if ((remotehost = malloc(strlen(host) + 1)) == NULL) {
	fprintf(stderr, "internal error: no more core for host\n");
	return 0;
    }
    strcpy(remotehost, host);

    /* setup communication channel with mount daemon */
    mntserver_addr = server_addr;
    if ((mntclient = clntudp_create(&mntserver_addr, MOUNTPROG,
      MOUNTVERS, timeout, &sock)) == NULL) {
        clnt_pcreateerror("mount clntudp_create");
        return 0;
    }
    clnt_control(mntclient, CLSET_TIMEOUT, &timeout);
    mntclient->cl_auth = authunix_create_default(0, 0);
    if (verbose)
	printf("Open %s (%s)\n",
	    remotehost, inet_ntoa(server_addr.sin_addr));
    return 1;
}

/*
 * Close channel to mount daemon,
 * possibly umounting a NFS file system.
 */
close_mount()
{
    if (mountpath) close_nfs();
    if (verbose) printf("Close `%s'\n", remotehost);
    free(remotehost);
    remotehost = NULL;
    if (mntclient) {
	auth_destroy(mntclient->cl_auth);
	clnt_destroy(mntclient);
    }
}

/*
 * Mount an NFS file system, perhaps closing
 * a previous mounted one. The umount option
 * fools the accounting system.
 */
int
open_nfs(path, umount, portmap)
    char *path;
    int umount, portmap;
{
    int sock = privileged();

    /* umount previous mounted remote file system */
    if (mountpath != NULL)
	close_nfs();

    /* setup communication channel with NFS server */
    nfsserver_addr = server_addr;
    if ((nfsclient = clntudp_create(&nfsserver_addr, NFS_PROGRAM,
      NFS_VERSION, timeout, &sock)) == NULL) {
        clnt_pcreateerror("nfs clntudp_create");
        return 0;
    }
    clnt_control(nfsclient, CLSET_TIMEOUT, &timeout);
    nfsclient->cl_auth = authunix_create_default(uid, gid);

    /*
     * Get file handle for this path from the mount daemon. There
     * are two ways to get it, either ask it directly or get it
     * through the port mapper.
     */
    if (portmap) {
	if ((mountpoint = pmap_mnt(&path, &mntserver_addr)) == NULL)
	    return 0;
    } else if ((mountpoint = mountproc_mnt_1(&path, mntclient)) == NULL) {
        clnt_perror(mntclient, "mountproc_mnt");
	return 0;
    }
    if (mountpoint->fhs_status != NFS_OK) {
        fprintf(stderr, "Mount failed: %s\n",
            nfs_error(mountpoint->fhs_status));
	return 0;
    }
    bcopy(mountpoint->fhstatus_u.fhs_fhandle, directory_handle, NFS_FHSIZE);
    if (umount) (void) mountproc_umnt_1(&path, mntclient);

    /* get transfer size */
    if ((transfersize = determine_transfersize()) <= 0) {
	fprintf(stderr, "Mount failed: Unable to obtain transfer size\n");
	(void) mountproc_umnt_1(&path, mntclient);
	return 0;
    }

    /* set mount path */
    if ((mountpath = malloc(strlen(path) + 1)) == NULL) {
	fprintf(stderr, "internal error: no more core for mountpath\n");
	return 0;
    }
    strcpy(mountpath, path);
    if (verbose)
	printf("Mount `%s'%s, transfer size %d bytes.\n",
	    mountpath, umount ? " (unmount)":"", transfersize);
    return 1;
}

/*
 * Make a mount call via the port mapper
 */
fhstatus *
pmap_mnt(argp, server_addr)
    dirpath *argp;
    struct sockaddr_in *server_addr;
{
    enum clnt_stat stat;
    static fhstatus res;
    u_long port;

    bzero((char *)&res, sizeof(res));
    if ((stat = pmap_rmtcall(server_addr, MOUNTPROG, MOUNTVERS, MOUNTPROC_MNT,
      xdr_dirpath, argp, xdr_fhstatus, &res, timeout, &port)) != RPC_SUCCESS) {
	clnt_perrno(stat);
	return NULL;
    }
    return &res;
}

/*
 * Determine NFS server's transfer size
 */
static int
determine_transfersize()
{
    statfsres *res;

    if ((res = nfsproc_statfs_2(directory_handle, nfsclient)) == NULL)
	return 0;
    if (res->status != NFS_OK)
	return 0;
    return res->statfsres_u.reply.tsize;
}

/*
 * Acquire a privileged port when possible
 */
int
privileged()
{
    int lport = IPPORT_RESERVED - 1;
    struct sockaddr_in sin;
    int s, dontblock = 1;

    sin.sin_family = AF_INET;
    sin.sin_addr.s_addr = 0;
    s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s < 0)
	return RPC_ANYSOCK;
    for (;;) {
	sin.sin_port = htons((u_short)lport);
	if (bind(s, (caddr_t)&sin, sizeof (sin), 0) >= 0) {
	    (void)ioctl(s, FIONBIO, (char *) &dontblock);
	    if (verbose)
		fprintf(stderr, "Using a privileged port (%d)\n", lport);
	    fflush(stderr);
	    return s;
	}
	if (errno != EADDRINUSE && errno != EADDRNOTAVAIL)
	    return RPC_ANYSOCK;
	(lport)--;
	if (lport == IPPORT_RESERVED/2) {
	    fprintf(stderr, "privileged socket: All ports in use\n");
	    return RPC_ANYSOCK;
	}
    }
}

/*
 * Close an NFS mounted file system
 */
close_nfs()
{
    if (mountpath == NULL) return;
    if (verbose) printf("Unmount `%s'\n", mountpath);
    (void) mountproc_umnt_1(&mountpath, mntclient);
    free(mountpath);
    mountpath = NULL;
    if (nfsclient) {
	auth_destroy(nfsclient->cl_auth);
	clnt_destroy(nfsclient);
    }
}

/*
 * Returns an auth handle with parameters determined by doing lots of
 * syscalls.
 */
AUTH *
authunix_create_default(uid, gid)
    int uid, gid;
{
    char machname[MAX_MACHINE_NAME + 1];
    int gids[1];

    if (gethostname(machname, MAX_MACHINE_NAME) == -1) {
	fprintf(stderr, "authunix_create_default: cannot get hostname\n");
	exit(1);
    }
    machname[MAX_MACHINE_NAME] = 0;
    gids[0] = gid;
    return (authunix_create(machname, uid, gid, 1, gids));
}

/*
 * Read all entries (names) in directory 'dirhandle' into
 * a dynamically build table. It is up to the caller to free
 * this table.
 */
getdirentries(dirhandle, table, ptr, nentries)
    fhandle *dirhandle;
    register char ***table;
    register char ***ptr;
    int nentries;
{
    readdirargs args;
    readdirres *res;
    entry *ep;
    int dircmp();
    char **last;

    *ptr = *table = (char **) calloc(nentries, sizeof(char *));
    last = *ptr + nentries;
    if (*ptr == NULL) {
	fprintf(stderr, "getdirentries: out of memory\n");
	return 0;
    }

    bcopy(*dirhandle, &args.dir, NFS_FHSIZE);
    bzero(args.cookie, NFS_COOKIESIZE);
    args.count = 1000;
    for (;;) {
        if ((res = nfsproc_readdir_2(&args, nfsclient)) == NULL) {
            clnt_perror(nfsclient, "nfsproc_readdir");
            break;
        }
        if (res->status != NFS_OK) {
            fprintf(stderr, "Readdir failed: %s\n", nfs_error(res->status));
            break;
        }

        ep = res->readdirres_u.reply.entries;
        while (ep != NULL) {
	    if (*ptr == last) {
		*table = (char **)realloc(*table, 2*nentries*sizeof(char *));
		if (*table == NULL) {
		    fprintf(stderr, "getdirentries: out of memory\n");
		    exit(1);
		}
		*ptr = *table + nentries;
		last = *ptr + nentries;
		nentries *= 2;
	    }
	    if ((*(*ptr)++ = strsave(ep->name)) == NULL)
		return 0;

            if (ep->nextentry == NULL)
                break;
            ep = ep->nextentry;
        }
        if (res->readdirres_u.reply.eof)
            break;
        bcopy(ep->cookie, args.cookie, NFS_COOKIESIZE);
    }
    qsort(*table, *ptr - *table, sizeof(char **), dircmp);
    return 1;
}

char *
strsave(str)
    char *str;
{
    char *cp = malloc(strlen(str) + 1);

    if (cp == NULL) {
	fprintf(stderr, "strsave: out of memory\n");
	return NULL;
    }
    strcpy(cp, str);
    return cp;
}

int
dircmp(p, q)
    char **p, **q;
{
    return strcmp(*p, *q);
}

/*
 * Match string against a normal shell pattern (*?[])
 */
int
match(s, argc, argv)
    char *s;
    int argc;
    char **argv;
{
    register int i;

    if (argc == 0) return 1;
    for (i = 0; i < argc; i++)
	if (matchpattern(s, argv[i]))
	    return 1;
    return 0;
}

int
matchpattern(s, p)
    char *s, *p;
{
    if (*s == '.' && *p != '.')
	return 0;
    return amatchpattern(s, p);
}

int
amatchpattern(s, p)
    register char *s, *p;
{
    register int scc;
    int c, cc, ok, lc;

    if (scc = *s++)
	if ((scc &= 0177) == 0)
	    scc = 0200;

    switch (c = *p++) {
    case '[':
	ok = 0;
	lc = 077777;
	while (cc = *p++) {
	    if (cc == ']') {
		if (ok)
		    return amatchpattern(s, p);
		else
		    return 0;
	    } else if (cc == '-') {
		if (lc <= scc && scc <= (c = *p++))
		    ok++;
	    } else
		if (scc == (lc = cc))
		    ok++;
	}
	return 0;
    case '*':
	return umatchpattern(--s, p);
    case '\0':
	return !scc;
    default:
	if (c != scc)
	    return 0;
    case '?':
	if (scc)
	    return amatchpattern(s, p);
	return 0;
    }
}

int
umatchpattern(s, p)
    register char *s, *p;
{
    if (*p == '\0')
	return 1;
    while (*s != '\0')
	if (amatchpattern(s++, p))
	    return 1;
    return 0;
}

/*
 * NFS errors
 */
char *
nfs_error(nfsstat)
    enum nfsstat nfsstat;
{
    switch (nfsstat) {
    case NFS_OK:
	return "No error";
    case NFSERR_PERM:
	return "Not owner";
    case NFSERR_NOENT:
	return "No such file or directory";
    case NFSERR_IO:
	return "I/O error";
    case NFSERR_NXIO:
	return "No such device or address";
    case NFSERR_ACCES:
	return "Permission denied";
    case NFSERR_EXIST:
	return "File exists";
    case NFSERR_NODEV:
	return "No such device";
    case NFSERR_NOTDIR:
	return "Not a directory";
    case NFSERR_ISDIR:
	return "Is a directory";
    case NFSERR_FBIG:
	return "File too large";
    case NFSERR_NOSPC:
	return "No space left on device";
    case NFSERR_ROFS:
	return "Read-only file system";
    case NFSERR_NAMETOOLONG:
	return "File name too long";
    case NFSERR_NOTEMPTY:
	return "Directory not empty";
    case NFSERR_DQUOT:
	return "Disc quota exceeded";
    case NFSERR_STALE:
	return "Stale NFS file handle";
    case NFSERR_WFLUSH:
	return "Write cache flushed";
    default:
	return "UKNOWN NFS ERROR";
    }
}

