#if	defined(sun) && !defined(__GNUC__)
#define	const
#else
#define	ANSI
#endif

#define	LINUX

/*
 * Load average stuff.
 * There doesn't seem to be any sort of standard include file including
 * structures and filenames for this. I think there should be.
 */
/*#define	RWHOD	"/usr/spool/rwho/whod."*/
/*#define	RWHODBYTEOFFS	44L	/* byte offset of load average in RWHOD file */
#define	RUP	"/usr/bin/rup"	/* use rup to find machines */
	/* RUP needs rstatd and rwhod on all machines */
#define	WORKFILE "/etc/hosts.lav"	/* contains machine names */

/*
 * Obviously BNET has restricted uses. Don't try to do anything
 * where the output isn't totally to the screen, and input must
 * be redirected using "<".
 * You must use BNET if you have NFS - it will copy the file to the
 * local machine and execute it here - so it's of no use!
 */
#define	BNET	"/usr/bin/rsh"	/* rsh or remsh */
/*#define	NEWC	"/net"			/* entry in $PATH */

/*
 * net - automatic load balancing program.
 *
 * argv[0] contains the program to load balance.
 * #define BNET - use rsh or remsh to do it
 * #define NEWC - use newcastle connexion to do it.
 *
 * net -s doesn't do anything, it just sits around
 * for cache purposes. Put it into /etc/rc.
 */
#include	<stdio.h>
#include	<ctype.h>
#include	<signal.h>
#include	<fcntl.h>
#include	<sys/types.h>
#include	<sys/stat.h>
#include	<malloc.h>
#include	<stdlib.h>
#include	<unistd.h>

#ifdef	RUP
#include	<string.h>
#endif

/*#define	DEBUG*/

struct	where {
	const	char	*machine;
	const	char	*username;
};

static	struct	where	*getmc(const char *hosts);
static	void	err();

static	char	hostname[32];

int
main(argc, argv)
register char **argv;
register int argc;
{
	register const char *machine;
	register int calledlb;
	const char *hosts = NULL;
#ifdef	BNET
	register const char **cmd;
	register int i, j;
	register struct where *w;
#endif	BNET
	int c;
	char **v;
#ifdef	NEWC
	char *getenv();
#endif	NEWC

	/*
	 * ignore argv[0] if it's "lb".
	 */
	if(strcmp(argv[0], "lb") == 0) {
#ifdef	RUP
		if(argc == 1)
			err("Usage: lb [ -h host1,host2] command [args]");
#else
		if(argc == 1)
			err("Usage: lb command [args]");
#endif
		if(strcmp(argv[1], "-s") == 0) {
			if(fork())
				return(0);
			else
				for(;;) {
					signal(SIGINT, SIG_IGN);
					signal(SIGQUIT, SIG_IGN);
					pause();
				}
		}
		calledlb = 1;
	} else
		calledlb = 0;

	c = argc;
	v = argv;

	while((--c > 0) && (*++v)[0] == '-') {
		const char *s;

		argc--;
		argv++;
		for(s = v[0] + 1; *s; s++)
			switch(*s) {
				case 'h':
					hosts = v[1];
					argc--;
					argv++;
					break;
				default:
					fprintf(stderr, "%s: unknown option %c\n",
						argv[0], *s);
					return 1;
			}
	}

	/*
	 * Don't do it if Root.
	 */
	if(geteuid() == 0)
		err("No permission to use network");

	if(calledlb)
		argv++, argc--;
	gethostname(hostname, sizeof(hostname));
	w = getmc(hosts);
	machine = w->machine;
	/*
	 * Fast check for loopback.
	 */
	if(calledlb &&
	  ((strcmp(machine, hostname) == 0) || (strcmp(machine, "localhost") == 0))) {
#ifdef	DEBUG
		puts("Executing locally");
#endif
		execvp(argv[0], argv);
		err("%s: not found", argv[0]);
	}
#ifdef	DEBUG
	printf("machine is %s\n", machine);
#endif	DEBUG
#ifdef	NEWC
	doit(machine, argv, getenv("PATH"));
#endif	NEWC
#ifdef	BNET
	/*
	 * Later this will be in it's own doit().
	 */
	cmd = (const char **)malloc((argc + 4) * sizeof (char *));
	/*cmd[0] = BNET;*/
	cmd[0] = machine;
	if(w->username) {
		cmd[1] = "-l";
		cmd[2] = w->username;
		j = 3;
	} else
		j = 1;

	for(i = 0; i < argc; i++)
		cmd[i + j] = argv[i];
	cmd[i + j] = (char *)NULL;

#ifdef	DEBUG
	for(i = 0; cmd[i]; i++)
		printf("%d: %s\n", i, cmd[i]);
#endif

	execv(BNET, cmd);
	perror(BNET);
#endif	BNET
	/*NOTREACHED*/
	return 1;
}

#ifdef	NEWC
static void
doit(mc, argv, path)
char *mc, **argv, *path;
{
	register char *dir;
	register char *this = path;
	register len;
	register char *args;	/* arg list built up */
	char *strtok();

	for(;;) {
		dir = strtok(this, ":");
		this = (char *)NULL;
		if(dir == (char *)NULL) {
			fprintf(stderr, "%s not found on machine %s\n",
				argv[0], mc);
			exit(1);
		}
		if(strcmp(dir, NEWC) == 0)
			continue;	/* avoid loops */
		len = 0;
		len += strlen(mc) + 1;
		len += strlen(dir) + 2;
		len += strlen(argv[0]);
		args = malloc(len + 3);
		if(args == (char *)NULL)
			err("Not enough memory");
		strcpy(args, "/");
		strcat(args, mc);
		strcat(args, dir);
		strcat(args, "/");
		strcat(args, argv[0]);
		fprintf(stderr, "%s\n", args);
		execv(args, argv);
	}
}
#endif	NEWC

#ifdef	RWHOD
static char *
getmc(const char *hosts)
{
	register j, pidindex;
	register FILE *fp;
	register char *cp;
	register fd;
	register minload, ownload;
	register rwhodlen;
	char sysname[100][100];
	int pid[100];
	char filename[32];
	time_t now;
	struct stat stats;

	if ((fp = fopen(WORKFILE, "r")) == NULL) {
#ifdef	DEBUG
		perror(WORKFILE);
#endif
		return(hostname);
	}
	strcpy(filename, RWHOD);
	rwhodlen = strlen(RWHOD);
	time(&now);
	pidindex = 0;
	while (fgets(cp = sysname[pidindex], 100, fp) != NULL) {
		if (!isalpha(*cp))
			continue;
		/*
		 * This doesn't work if the input line is >100 characters!
		 */
		while(*cp++ != '\n')
			;
		*--cp = '\0';
		strcpy(&filename[rwhodlen], sysname[pidindex]);
		if(stat(filename, &stats) < 0) {
#ifdef	DEBUG
			perror(filename);
#endif
			continue;
		}
		/*
		 * If it hasn't been changed in the last 5 minutes,
		 * the machine is down.
		 */
		if((now - stats.st_mtime) >= 5*60L) {
#ifdef	DEBUG
			printf("%s down\n", sysname[pidindex]);
#endif	DEBUG
			continue;
		}
#ifdef	DEBUG
		printf("open %s\n", filename);
#endif	DEBUG
		fd = open(filename, O_RDONLY);
		if(fd == -1)
			continue;
		if(lseek(fd, RWHODBYTEOFFS, 0) != RWHODBYTEOFFS)
			continue;
		read(fd, &pid[pidindex], sizeof(int));
		close(fd);
		if(strcmp(sysname[pidindex], hostname) == 0)
			ownload = pid[pidindex];
#ifdef DEBUG
		printf("lav=%f %s\n", pid[pidindex]/100.0, sysname[pidindex]);
#endif DEBUG
		pidindex++;
	}
	fclose(fp);	/* put in a sensible place - NJH */
	if (pidindex == 0)
		return(hostname);
	minload = pid[0];
	for(j = 1; j < pidindex; j++)
		if(pid[j] < minload)
			minload = pid[j];
	/*
	 * If there's no lightly loaded machine, don't bother to offload
	 * it anywhere.
	 */
	if(minload > 500) {
#ifdef	DEBUG
		printf("All machines heavy\n");
#endif	DEBUG
		return(hostname);
	}
	/*
	 * If we're very lightly loaded or the least loaded machine
	 * isn't very much less loaded than us, don't offload either.
	 */
	if(ownload <= 30) {
#ifdef	DEBUG
		printf("Own machine light\n");
#endif	DEBUG
		return(hostname);
	}
	if((ownload - minload) < 50) {
#ifdef	DEBUG
		printf("All machines equal own %d minimum %d\n", ownload, minload);
#endif	DEBUG
		return(hostname);
	}
	for(j = 0; j < pidindex; j++)
		if(pid[j] == minload)
			return(strdup(sysname[j]));
	/*NOTREACHED*/
}
#endif	/*RWHOD*/

#ifdef	RUP
static struct where *
getmc(const char *hosts)
{
	register int i;
	register FILE *fp, *pp;
	register char *cp, *end;
	char sysname[100], host[100];
	static struct where w;

	w.username = NULL;
	w.machine = hostname;

	if(hosts) {
		char *copy = strdup(hosts);

		for(cp = copy; *cp; cp++)
			if(*cp == ',')
				*cp = ' ';
		sprintf(sysname, "%s -l %s ", RUP, copy);
		fp = NULL;
		free(copy);
	} else {
		if(cp = getenv("HOME")) {
			sprintf(host, "%s/.rhosts", cp);
			fp = fopen(host, "r");
			if(fp == NULL)
				if ((fp = fopen(WORKFILE, "r")) == NULL) {
#ifdef	DEBUG
					perror(WORKFILE);
#endif
					return(&w);
				}
		} else if ((fp = fopen(WORKFILE, "r")) == NULL) {
#ifdef	DEBUG
			perror(WORKFILE);
#endif
			return(&w);
		}
		i = 0;
		sprintf(sysname, "%s -l ", RUP);
		while (fgets(host, sizeof(host), fp) != NULL) {
			cp = host;
			if(!isalnum(*cp))
				continue;
			/*
			 * This doesn't work if the input line is >100
			 * characters!
			 */
			while(isalnum(*cp++))
				;
			*--cp = '\0';
			strcat(sysname, host);
			strcat(sysname, " ");
			i++;
		}
		if (i == 0) {
			fclose(fp);
			return(&w);
		}
	}
#ifndef	LINUX
	/*
	 * BUG in rup: it doesn't sort by load average if you give it a
	 * list of machine names
	 */
	strcat(sysname, "| sort -t: +2n ");
#endif
	strcat(sysname, "2> /dev/null");

#ifdef	DEBUG
	printf("Running '%s'\n", sysname);
#endif

	pp = popen(sysname, "r");

	if(pp == NULL) {
		perror(sysname);
		if(fp)
			fclose(fp);
		return(&w);
	}

	if(fp)
		rewind(fp);	/* whilst we're waiting for rup to start */

#ifdef	LINUX
	/* ignore the collecting... line */
	fgets(host, sizeof(host), pp);
#endif

	cp = fgets(host, sizeof(host), pp);
	pclose(pp);
	if(cp == NULL) {
		if(fp)
			fclose(fp);
		return(&w);
	}
	while(isspace(*cp))
		cp++;
	end = cp;
	while(!isspace(*end))
		end++;
	*end = '\0';
	w.machine = strdup(cp);
#ifdef	DEBUG
	printf("Using %s\n", w.machine);
#endif
	if(fp) {
		while (fgets(host, sizeof(host), fp) != NULL)
			if(strncmp(host, w.machine, strlen(w.machine)) == 0) {
				if(end = strchr(host, ' ')) {
					w.username = strdup(++end);
					if(end = strchr(w.username, '\n'))
						*end = '\0';
				}
				break;
			}
		fclose(fp);
	}

#ifdef	DEBUG
	if(w.username)
		printf("As %s\n", w.username);
	else
		puts("As self");
#endif

	return(&w);
}
#endif	/*RUP*/

/*VARARGS1*/
static void
err(s, a)
const char *s;
int a;
{
	fprintf(stderr, s, a);
	putc('\n', stderr);
	exit(1);
}
