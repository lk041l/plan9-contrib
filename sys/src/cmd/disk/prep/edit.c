/*
 * disk partition editor
 */
#include <u.h>
#include <libc.h>
#include <bio.h>
#include <ctype.h>
#include <disk.h>
#include "edit.h"

char*
getline(Edit *edit)
{
	static char line[100];
	char *p;
	int n;

	n = read(0, line, sizeof(line)-1);
	if(n <= 0) {
		if(edit->changed)
			fprint(2, "?warning: changes not written\n");
		exits(0);
	}

	line[n-1] = '\0';
	p = line;
	while(isspace(*p))
		p++;
	return p;
}

Part*
findpart(Edit *edit, char *name)
{
	int i;

	for(i=0; i<edit->npart; i++)
		if(strcmp(edit->part[i]->name, name) == 0)
			return edit->part[i];
	return nil;
}

static char*
okname(Edit *edit, char *name)
{
	int i;
	static char msg[100];

	if(name[0] == '\0')
		return "partition has no name";

	if(strlen(name) >= NAMELEN)
		return "name too long";

	for(i=0; i<edit->npart; i++) {
		if(strcmp(name, edit->part[i]->name) == 0) {
			sprint(msg, "already have partition with name \"%s\"", name);
			return msg;
		}
	}
	return nil;
}

char*
addpart(Edit *edit, Part *p)
{
	int i;
	static char msg[100];
	char *err;

	if(err = okname(edit, p->name))
		return err;

	for(i=0; i<edit->npart; i++) {
		if(p->start < edit->part[i]->end && edit->part[i]->start < p->end) {
			sprint(msg, "\"%s\" overlaps with \"%s\"", p->name, edit->part[i]->name);
			return msg;
		}
	}

	if(edit->npart >= nelem(edit->part))	
		return "too many partitions";

	edit->part[i=edit->npart++] = p;
	for(; i > 0 && p->start < edit->part[i-1]->start; i--) {
		edit->part[i] = edit->part[i-1];
		edit->part[i-1] = p;
	}

	if(p->changed)
		edit->changed = 1;
	return nil;
}

char*
delpart(Edit *edit, Part *p)
{
	int i;

	for(i=0; i<edit->npart; i++)
		if(edit->part[i] == p)
			break;
	assert(i < edit->npart);
	edit->npart--;
	for(; i<edit->npart; i++)
		edit->part[i] = edit->part[i+1];

	edit->changed = 1;
	return nil;
}

static char*
editdot(Edit *edit, int argc, char **argv)
{
	char *err;
	vlong ndot;

	if(argc == 1) {
		print("\t. %lld\n", edit->dot);
		return nil;
	}

	if(argc > 2)
		return "args";

	if(err = parseexpr(argv[1], edit->dot, edit->disk->secs, edit->disk->secs, &ndot))
		return err;

	edit->dot = ndot;
	return nil;
}

static char*
editadd(Edit *edit, int argc, char **argv)
{
	char *name, *err, *q;
	static char msg[100];
	vlong start, end, maxend;
	int i;

	if(argc < 2)
		return "args";

	name = estrdup(argv[1]);
	if((err = okname(edit, name)) || (err = edit->okname(edit, name)))
		return err;

	if(argc >= 3)
		q = argv[2];
	else {
		fprint(2, "start sector: ");
		q = getline(edit);
	}
	if(err = parseexpr(q, edit->dot, edit->disk->secs, edit->disk->secs, &start))
		return err;

	if(start < 0 || start >= edit->disk->secs)
		return "start sector out of range";

	for(i=0; i < edit->npart; i++) {
		if(edit->part[i]->start <= start && start < edit->part[i]->end) {
			sprint(msg, "start sector in partition \"%s\"", edit->part[i]->name);
			return msg;
		}
	}

	maxend = edit->disk->secs;
	for(i=0; i < edit->npart; i++)
		if(start < edit->part[i]->start && edit->part[i]->start < maxend)
			maxend = edit->part[i]->start;

	if(argc >= 4)
		q = argv[3];
	else {
		fprint(2, "end [%lld..%lld] ", start, maxend);
		q = getline(edit);
	}
	if(err = parseexpr(q, edit->dot, maxend, edit->disk->secs, &end))
		return err;

	if(start == end)
		return "size zero partition";

	if(end <= start || end > maxend)
		return "end sector out of range";

	if(err = edit->add(edit, name, start, end))
		return err;

	edit->dot = end;
	return nil;
}

static char*
editdel(Edit *edit, int argc, char **argv)
{
	Part *p;

	if(argc != 2)
		return "args";

	if((p = findpart(edit, argv[1])) == nil)
		return "no such partition";

	return edit->del(edit, p);
}

static char *helptext = 
	". [newdot] - display or set value of dot\n"
	"a name [start [end]] - add partition\n"
	"d name - delete partition\n"
	"h - print help message\n"
	"p - print partition table\n"
	"P - print commands to update sd(3) device\n"
	"w - write partition table\n"
	"q - quit\n";

static char*
edithelp(Edit *edit, int, char**)
{
	print("%s", helptext);
	if(edit->help)
		return edit->help(edit);
	return nil;
}

static char*
editprint(Edit *edit, int argc, char**)
{
	vlong lastend;
	int i;
	Part **part;

	if(argc != 1)
		return "args";

	lastend = 0;
	part = edit->part;
	for(i=0; i<edit->npart; i++) {
		if(lastend < part[i]->start)
			edit->sum(edit, nil, lastend, part[i]->start);
		edit->sum(edit, part[i], part[i]->start, part[i]->end);
		lastend = part[i]->end;
	}
	if(lastend < edit->disk->secs)
		edit->sum(edit, nil, lastend, edit->disk->secs);
	return nil;
}

char*
editwrite(Edit *edit, int argc, char**)
{
	int i;
	char *err;

	if(argc != 1)
		return "args";

	if(edit->disk->rdonly)
		return "read only";

	err = edit->write(edit);
	if(err)
		return err;
	for(i=0; i<edit->npart; i++)
		edit->part[i]->changed = 0;
	edit->changed = 0;
	return nil;
}

static char*
editquit(Edit *edit, int argc, char**)
{
	static int warned;

	if(argc != 1) {
		warned = 0;
		return "args";
	}

	if(edit->changed && (!edit->warned || edit->lastcmd != 'q')) {
		edit->warned = 1;
		return "changes unwritten";
	}

	exits(0);
	return nil;	/* not reached */
}

char*
editctlprint(Edit *edit, int argc, char **)
{
	if(argc != 1)
		return "args";

	if(edit->printctl)
		edit->printctl(edit, 1);
	else
		ctldiff(edit, 1);
	return nil;
}

typedef struct Cmd Cmd;
struct Cmd {
	char c;
	char *(*fn)(Edit*, int ,char**);
};

Cmd cmds[] = {
	'.',	editdot,
	'a',	editadd,
	'd',	editdel,
	'?',	edithelp,
	'h',	edithelp,
	'P',	editctlprint,
	'p',	editprint,
	'w',	editwrite,
	'q',	editquit,
};

void
runcmd(Edit *edit, char *cmd)
{
	char *f[10], *err;
	int i, nf;

	while(*cmd && isspace(*cmd))
		cmd++;

	nf = tokenize(cmd, f, nelem(f));
	if(nf >= 10) {
		fprint(2, "?\n");
		return;
	}

	if(strlen(f[0]) != 1) {
		fprint(2, "?\n");
		return;
	}

	err = nil;
	for(i=0; i<nelem(cmds); i++) {
		if(cmds[i].c == f[0][0]) {
			err = cmds[i].fn(edit, nf, f);
			break;
		}
	}
	if(i == nelem(cmds))
		err = edit->ext(edit, nf, f);
	if(err) 
		fprint(2, "?%s\n", err);
	edit->lastcmd = f[0][0];
}

static Part*
ctlmkpart(char *name, vlong start, vlong end, int changed)
{
	Part *p;

	p = emalloc(sizeof(*p));
	strcpy(p->name, name);
	strcpy(p->ctlname, name);
	p->start = start;
	p->end = end;
	p->changed = changed;
	return p;
}

static void
rdctlpart(Edit *edit)
{
	int i, nline, nf;
	char *line[128];
	char buf[4096];
	vlong a, b;
	char *f[5];
	Disk *disk;

	disk = edit->disk;
	edit->nctlpart = 0;
	seek(disk->ctlfd, 0, 0);
	if(readn(disk->ctlfd, buf, sizeof buf) <= 0) {
		return;
	}

	nline = getfields(buf, line, nelem(line), 1, "\n");
	for(i=0; i<nline; i++){
		if(strncmp(line[i], "part ", 5) != 0)
			continue;

		nf = getfields(line[i], f, nelem(f), 1, " \t\r");
		if(nf != 4 || strcmp(f[0], "part") != 0)
			break;

		a = strtoll(f[2], 0, 0);
		b = strtoll(f[3], 0, 0);

		if(a >= b)
			break;

		/* only gather partitions contained in the disk partition we are editing */
		if(a < disk->offset ||  disk->offset+disk->secs < b)
			continue;

		a -= disk->offset;
		b -= disk->offset;

		/* the partition we are editing does not count */
		if(strcmp(f[1], disk->part) == 0)
			continue;

		if(edit->nctlpart >= nelem(edit->ctlpart)) {
			fprint(2, "?too many partitions in ctl file\n");
			exits("ctlpart");
		}
		edit->ctlpart[edit->nctlpart++] = ctlmkpart(f[1], a, b, 0);
	}
}

static int
areequiv(Part *p, Part *q)
{
	return strcmp(p->ctlname, q->ctlname) == 0
			&& p->start == q->start && p->end == q->end;
}

static void
unchange(Edit *edit, Part *p)
{
	int i;
	Part *q;

	for(i=0; i<edit->nctlpart; i++) {
		q = edit->ctlpart[i];
		if(p->start <= q->start && q->end <= p->end) {
			q->changed = 0;
		}
	}
assert(p->changed == 0);
}

int
ctldiff(Edit *edit, int ctlfd)
{
	int i, j, waserr;
	Part *p;
	vlong offset;

	rdctlpart(edit);

	/* everything is bogus until we prove otherwise */
	for(i=0; i<edit->nctlpart; i++)
		edit->ctlpart[i]->changed = 1;

	/*
	 * partitions with same info have not changed,
	 * and neither have partitions inside them.
	 */
	for(i=0; i<edit->nctlpart; i++)
		for(j=0; j<edit->npart; j++)
			if(areequiv(edit->ctlpart[i], edit->part[j])) {
				unchange(edit, edit->ctlpart[i]);
				break;
			}

	/*
	 * delete all the changed partitions except data (we'll add them back if necessary) 
	 */
	for(i=0; i<edit->nctlpart; i++) {
		p = edit->ctlpart[i];
		if(p->changed)
			fprint(ctlfd, "delpart %s\n", p->ctlname);
	}

	/*
	 * add all the partitions from the real list;
	 * this is okay since adding a parition with
	 * information identical to what is there is a no-op.
	 */
	waserr = 0;
	offset = edit->disk->offset;
	for(i=0; i<edit->npart; i++) {
		p = edit->part[i];
		if(p->ctlname[0])
			if(fprint(ctlfd, "part %s %lld %lld\n", p->ctlname, offset+p->start, offset+p->end) < 0)
				waserr = -1;
	}
	return waserr;
}
