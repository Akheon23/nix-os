#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"

#include "../port/error.h"

#include "/sys/src/libc/9syscall/sys.h"
#include "linuxsystab.h"

#include <tos.h>
#include "amd64.h"
#include "ureg.h"

/* linux calling convention is callee-save. We are caller-save. 
 * But that issue is covered in trap, which saves everything. 
 * so we only need to know the calling conventions. 
 * when we call a(1,2,3,4,5,6). NO on-stack params.
	movl	$6, %r9d
	movl	$5, %r8d
	movl	$4, %r10
	movl	$3, %edx
	movl	$2, %esi
	movl	$1, %edi
 * syscall is in %ax however. 
 * return is in %ax
 */

void
linuxsyscall(unsigned int, Ureg* ureg)
{
	void noted(Ureg*, uintptr);
	void arch_prctl(Ar0 *ar, Ureg *ureg, va_list list);
	unsigned int scallnr;
	void notify(Ureg *);
	char *e;
	uintptr	sp;
	int s;
	Ar0 ar0;
	static Ar0 zar0;
	int i;
	uintptr linuxargs[6];

//print("linuxsyscall: wrong %d\n", wrong);
//dumpstack();
	if(!userureg(ureg))
		panic("syscall: cs %#llux\n", ureg->cs);

	cycles(&up->kentry);

	m->syscall++;
	up->nsyscall++;
	up->nqsyscall++;
	up->insyscall = 1;
	up->pc = ureg->ip;
	up->dbgreg = ureg;

	if(up->procctl == Proc_tracesyscall){
		up->procctl = Proc_stopme;
		procctl(up);
	}
	scallnr = ureg->ax;
//print("# %d\n", scallnr);
	up->scallnr = scallnr;

	if(scallnr == 56)
		fpusysrfork(ureg);
	spllo();

	sp = ureg->sp;
	up->nerrlab = 0;
	ar0 = zar0;
	if(!waserror()){
		int printarg;
		char *name = scallnr < nlinuxsyscall ? linuxsystab[scallnr].n : "Unknown";
		if(scallnr >= nlinuxsyscall || linuxsystab[scallnr].f == nil){
			pprint("bad linux sys call number %d(%s) pc %#ullx max %d\n",
				scallnr, name, ureg->ip, nlinuxsyscall);
			postnote(up, 1, "sys: bad sys call", NDebug);
			error(Ebadarg);
		}

		if(sp < (USTKTOP-BIGPGSZ) || sp > (USTKTOP-sizeof(up->arg)-BY2SE))
			validaddr(UINT2PTR(sp), sizeof(up->arg)+BY2SE, 0);

		up->psstate = linuxsystab[scallnr].n;

		/* note: arch_prctl needs ureg. Unless someone thinks of a better way.
		 * one way is to change the way we construct linuxargs, 
		 * and add ureg is scallnr == 158. The current if below is a hack, 
		 * I know.
		 */
		linuxargs[0] = ureg->di;
		linuxargs[1] = ureg->si;
		linuxargs[2] = ureg->dx;
		linuxargs[3] = ureg->r10;
		linuxargs[4] = ureg->r8;
		linuxargs[5] = ureg->r9;

		if (up->linux & 16) {print("%d:linux: %s: pc %#p ", up->pid, linuxsystab[scallnr].n,(void *)ureg->ip);
			for(printarg = 0; printarg < linuxsystab[scallnr].narg; printarg++)
				print("%p ", (void *)linuxargs[printarg]);
			print("\n");
		}
		if (up->linux&32) dumpregs(ureg);
		/* this one is special .. sigh */
		if (scallnr == 158)
			arch_prctl(&ar0, ureg, (va_list)linuxargs);
		else
			linuxsystab[scallnr].f(&ar0, (va_list)linuxargs);
		if (up->linux & 64){print("AFTER: ");dumpregs(ureg);}
		poperror();
	}else{
		/* failure: save the error buffer for errstr */
		if (up->linux & 16){
			int i;
			print("Error path in linuxsyscall: %#ux, %s\n", scallnr, up->syserrstr ? up->syserrstr : "no errstr");
			for(i = 0; i < nelem(linuxargs); i++)
				print("%d: %#p\n", i, linuxargs[i]);
			dumpregs(ureg);
		}
		e = up->syserrstr;
		up->syserrstr = up->errstr;
		up->errstr = e;
		if (scallnr < nlinuxsyscall)
			ar0 = linuxsystab[scallnr].r;
		else
			ar0.i = -1;
	}

	/* normal amd64 kernel does not have this; remove? */
	if(up->nerrlab){
		print("bad errstack [%d]: %d extra\n", scallnr, up->nerrlab);
		for(i = 0; i < NERR; i++)
			print("sp=%#ullx pc=%#ullx\n",
				up->errlab[i].sp, up->errlab[i].pc);
		panic("error stack");
	}

	/*
	 * NIX: for the execac() syscall, what follows is done within
	 * the system call, because it never returns.
	 * See acore.c:/^retfromsyscall
	 */

	noerrorsleft();
	/*
	 * Put return value in frame.
	 */
	ureg->ax = ar0.p;
	if (up->linux & 16)print("%d:Ret from syscall %#lx\n", up->pid, (unsigned long) ar0.p);
	if(up->procctl == Proc_tracesyscall){
		up->procctl = Proc_stopme;
		s = splhi();
		procctl(up);
		splx(s);
	}else if(up->procctl == Proc_totc || up->procctl == Proc_toac)
		procctl(up);


	up->insyscall = 0;
	up->psstate = 0;

	if(scallnr == NOTED)
		noted(ureg, *(uintptr*)(sp+BY2SE));

	splhi();

	if(scallnr != 56 && (up->procctl || up->nnote))
		notify(ureg);

	/* if we delayed sched because we held a lock, sched now */
	if(up->delaysched){
		sched();
		splhi();
	}
	kexit(ureg);
}

void*
linuxsysexecregs(uintptr entry, ulong ssize, ulong nargs)
{
	int i;
	uvlong *l;
	Ureg *ureg;
	uintptr *sp;

	if(!up->linux)
		panic("linuxsysexecregs: up->linux %d\n", up->linux);

	/* need to figure out linux exec conventions :-( */
	sp = (uintptr*)(USTKTOP - ssize);
	*--sp = nargs;

	ureg = up->dbgreg;
	l = &ureg->bp;
	print("Starting linux proc pc %#ullx sp %p nargs %ld\n",
		ureg->ip, sp+1, nargs);

	/* set up registers for linux */
	/* we are dying in getenv. */
	/* because glibc does not follow the PPC ABI. */
	/* you have to push the env, then the args. */
	/* so to do this, well, we'll push an empty env on stack, i.e. shift
	 * the args down one. stack grows down. We already made space
	 * when we pushed nargs. 
	 */
	memmove(sp, sp+1, nargs * sizeof(*sp));
	sp[nargs] = 0;
	*--sp = nargs;
	for(i = 7; i < 16; i++)
		*l++ = 0xdeadbeef + (i*0x110);

	ureg->sp = PTR2UINT(sp);
	ureg->ip = entry;
	print("Starting linux proc pc %#ullx\n", ureg->ip);

	/*
	 */
	return UINT2PTR(nargs);
}

void
linuxsysrforkchild(Proc* child, Proc* parent)
{
	Ureg *cureg;

	/* don't clear linux any more. linux procs can now fork */
	child->linuxexec = 0;
	/*
	 * Add 3*BY2SE to the stack to account for
	 *  - the return PC
	 *  - trap's arguments (syscallnr, ureg)
	 */
	child->sched.sp = PTR2UINT(child->kstack+KSTACK-(sizeof(Ureg)+3*BY2SE));
	child->sched.pc = PTR2UINT(sysrforkret);

	cureg = (Ureg*)(child->sched.sp+3*BY2SE);
	memmove(cureg, parent->dbgreg, sizeof(Ureg));

	/* Things from bottom of syscall which were never executed */
	child->psstate = 0;
	child->insyscall = 0;

	cureg->ax = 0;
	child->hang = 1;

	dumpregs(cureg);
	fpusysrforkchild(child, parent);
}
