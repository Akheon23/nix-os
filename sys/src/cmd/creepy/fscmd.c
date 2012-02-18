#include <u.h>
#include <libc.h>
#include <thread.h>
#include <bio.h>
#include <fcall.h>
#include <error.h>

#include "conf.h"
#include "dbg.h"
#include "dk.h"
#include "fns.h"

enum 
{
	Nels = 64
};

static char *fsdir;
static int verb;

/*
 * Walks elems starting at f.
 * Ok if nelems is 0.
 */
static Memblk*
walkpath(Memblk *f, char *elems[], int nelems)
{
	int i;
	Memblk *f0, *nf;

	isfile(f);
	f0 = f;
	for(i = 0; i < nelems; i++){
		if((f->mf->mode&DMDIR) == 0)
			error("not a directory");
		rwlock(f, Rd);
		if(catcherror()){
			if(f != f0)
				mbput(f);
			rwunlock(f, Rd);
			error("walk: %r");
		}
		nf = dfwalk(f, elems[i], 0);
		rwunlock(f, Rd);
		if(f != f0)
			mbput(f);
		f = nf;
		USED(&f);	/* in case of error() */
		noerror();
	}
	if(f == f0)
		incref(f);
	return f;
}

static char*
fsname(char *p)
{
	if(p[0] == '/')
		return strdup(p);
	if(fsdir)
		return smprint("%s/%s", fsdir, p);
	return strdup(p);
}

static Memblk*
walkto(char *a, char **lastp)
{
	char *els[Nels], *path;
	int nels;
	Memblk *f;

	path = fsname(a);
	nels = gettokens(path, els, Nels, "/");
	if(nels < 1){
		free(path);
		error("invalid path");
	}
	if(catcherror()){
		free(path);
		error("walkpath: %r");
	}
	if(lastp != nil){
		f = walkpath(fs->root, els, nels-1);
		*lastp = a + strlen(a) - strlen(els[nels-1]);
	}else
		f = walkpath(fs->root, els, nels);
	free(path);
	noerror();
	if(verb)
		print("walked to %H\n", f);
	return f;
}

static void
fscd(int, char *argv[])
{
	free(fsdir);
	fsdir = strdup(argv[1]);
}

static void
fsput(int, char *argv[])
{
	int fd;
	char *fn;
	Memblk *m, *f;
	Dir *d;
	char buf[4096];
	uvlong off;
	long nw, nr;

	fd = open(argv[1], OREAD);
	if(fd < 0)
		error("open: %r\n");
	d = dirfstat(fd);
	if(d == nil){
		error("dirfstat: %r\n");
	}
	if(catcherror()){
		close(fd);
		free(d);
		error(nil);
	}
	m = walkto(argv[2], &fn);
	m = dfmelt(m);
	if(catcherror()){
		rwunlock(m, Wr);
		mbput(m);
		error(nil);
	}
	f = dfcreate(m, fn, d->uid, d->mode&(DMDIR|0777));
	rwlock(f, Wr);
	if(catcherror()){
		rwunlock(f, Wr);
		mbput(f);
		error(nil);
	}
	if((d->mode&DMDIR) == 0){
		off = 0;
		for(;;){
			nr = read(fd, buf, sizeof buf);
			if(nr <= 0)
				break;
			nw = dfpwrite(f, buf, nr, off);
			dDprint("wrote %ld of %ld bytes\n", nw, nr);
			off += nr;
		}
	}
	noerror();
	noerror();
	noerror();
	if(verb)
		print("created %H\nat %H\n", f, m);
	rwunlock(f, Wr);
	rwunlock(m, Wr);
	mbput(m);
	mbput(f);
	close(fd);
	free(d);
}

static void
fscat(int, char *argv[])
{
	Memblk *f;
	Mfile *m;
	char buf[4096];
	uvlong off;
	long nr;

	f = walkto(argv[2], nil);
	rwlock(f, Rd);
	if(catcherror()){
		rwunlock(f, Rd);
		mbput(f);
		error(nil);
	}
	m = f->mf;
	print("cat %-30s\t%M\t%5ulld\t%s %ulld refs\n",
		m->name, (ulong)m->mode, m->length, m->uid, dbgetref(f->addr));
	if((m->mode&DMDIR) == 0){
		off = 0;
		for(;;){
			nr = dfpread(f, buf, sizeof buf, off);
			if(nr <= 0)
				break;
			write(1, buf, nr);
			off += nr;
		}
	}
	noerror();
	rwunlock(f, Rd);
	mbput(f);
}

static void
fsget(int, char *argv[])
{
	Memblk *f;
	Mfile *m;
	char buf[4096];
	uvlong off;
	long nr;
	int fd;

	fd = create(argv[1], OWRITE, 0664);
	if(fd < 0)
		error("create: %r\n");
	if(catcherror()){
		close(fd);
		error(nil);
	}
	f = walkto(argv[2], nil);
	rwlock(f, Rd);
	if(catcherror()){
		rwunlock(f, Rd);
		mbput(f);
		error(nil);
	}
	m = f->mf;
	print("get %-30s\t%M\t%5ulld\t%s %ulld refs\n",
		m->name, (ulong)m->mode, m->length, m->uid, dbgetref(f->addr));
	if((m->mode&DMDIR) == 0){
		off = 0;
		for(;;){
			nr = dfpread(f, buf, sizeof buf, off);
			if(nr <= 0)
				break;
			if(write(fd, buf, nr) != nr){
				fprint(2, "%s: error: %r\n", argv[0]);
				break;
			}
			off += nr;
		}
	}
	close(fd);
	rwunlock(f, Rd);
	noerror();
	noerror();
	mbput(f);
}

static void
fsls(int, char**)
{
	if(verb)
		fsdump(1);
	else
		fslist();
}

static void
fssnap(int, char**)
{
	fssync();
}

static void
fsrcl(int, char**)
{
	fsreclaim();
}

static void
fsdmp(int, char**)
{
	fsdump(0);
}

static void
fsdmpall(int, char**)
{
	fsdump(1);
}

static void
fsdbg(int, char *argv[])
{
	dbg['D'] = atoi(argv[1]);
}

static void
fsout(int, char*[])
{
	fslowmem();
}

static void
fsrm(int, char *argv[])
{
	Memblk *f, *p;

	f = walkto(argv[1], nil);
	if(catcherror()){
		mbput(f);
		error(nil);
	}
	f->mf->parent = dfmelt(f->mf->parent);
	p = f->mf->parent;
	rwlock(f, Wr);
	if(catcherror()){
		rwunlock(f, Wr);
		rwunlock(p, Wr);
		error(nil);
	}
	dfremove(p, f);
	noerror();
	noerror();
	rwunlock(p, Wr);
}

static void
usage(void)
{
	fprint(2, "usage: %s [-DFLAGS] [-dv] [-f disk] cmd...\n", argv0);
	exits("usage");
}

static struct
{
	char *name;
	void (*f)(int, char**);
	int nargs;
	char *usage;
} cmds[] =
{
	{"cd",	fscd,	2, "cd!where"},
	{"put",	fsput,	3, "put!src!dst"},
	{"get",	fsget,	3, "get!dst!src"},
	{"cat",	fscat,	3, "cat!what"},
	{"ls",	fsls,	1, "ls"},
	{"dump",	fsdmp,	1, "dump"},
	{"dumpall",	fsdmpall, 1, "dumpall"},
	{"snap", fssnap, 1, "snap"},
	{"rcl",	fsrcl,	1, "rcl"},
	{"dbg", fsdbg,	2, "dbg!n"},
	{"out", fsout, 1, "out"},
	{"rm",	fsrm,	2, "rm!what"},
};

void
threadmain(int argc, char *argv[])
{
	char *dev;
	char *args[Nels];
	int i, j, nargs;

	dev = "disk";
	ARGBEGIN{
	case 'v':
		verb++;
		break;
	case 'f':
		dev = EARGF(usage());
		break;
	default:
		if(ARGC() >= 'A' && ARGC() <= 'Z'){
			dbg['d'] = 1;
			dbg[ARGC()] = 1;
		}else
			usage();
	}ARGEND;
	if(argc == 0)
		usage();
	fmtinstall('H', mbfmt);
	fmtinstall('M', dirmodefmt);
	errinit(Errstack);
	if(catcherror())
		fatal("error: %r");
	fsopen(dev);
	for(i = 0; i < argc; i++){
		if(catcherror())
			fatal("cmd %s: %r", argv[i]);
		if(verb>1)
			fsdump(0);
		print("%% %s\n", argv[i]);
		nargs = gettokens(argv[i], args, Nels, "!");
		for(j = 0; j < nelem(cmds); j++){
			if(strcmp(cmds[j].name, argv[i]) != 0)
				continue;
			if(cmds[j].nargs != 0 && cmds[j].nargs != nargs)
				print("usage: %s\n", cmds[j].usage);
			else{
				cmds[j].f(nargs, args);
			}
			break;
		}
		noerror();
		if(j == nelem(cmds)){
			print("no such command\n");
			for(j = 0; j < nelem(cmds); j++)
				print("\t%s\n", cmds[j].usage);
			break;
		}
	}
	if(verb>1)
		fsdump(0);
	noerror();
	exits(nil);
}
