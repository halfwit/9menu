#include <u.h>
#include <libc.h>
#include <bio.h>
#include <draw.h>
#include <keyboard.h>
#include <mouse.h>
#include <thread.h>

// TODO: accept keyboard input to narrow items list shown

enum {
	Off = 3,
};

// Background, altback, text
static Image *cback, *aback, *ctext;
static int wctl, hlitem, nin;
static int nitems, fitems;
static char *prompt, *pos = "rb", *items[1024], *filtered[1024];
static Font *f;
static char input[256];

static void
inputproc(void *c)
{
	Biobuf b;
	char *s;

	threadsetname("input");
	Binit(&b, 0, OREAD);
	for(;;){
		s = Brdstr(&b, '\n', 1);
		sendp(c, s ? s : strdup(""));
		if(s == nil)
			break;
	}
	Bterm(&b);
}

static void
redraw(void)
{
	static char s[256];
	static char in[256];
	Rectangle r;
	Point p;
	int i;

	r = screen->r;
	p.x = r.min.x + Off;
	p.y = r.min.y + Off;
	draw(screen, r, cback, nil, ZP);
	snprint(in, 256, "%s > %s", prompt, input);
	string(screen, p, ctext, ZP, f, in);
	p.y+=f->height;
	for(i = 0; i < fitems; i++){
		snprint(s, sizeof(s), "%s", filtered[i]);
		string(screen, p, ctext, ZP, f, s);

		if(i == hlitem){
			replclipr(screen, 0, r);
			stringbg(screen, p, aback, ZP, f, s, ctext, ZP);
			replclipr(screen, 0, screen->r);
		}
		p.y+=f->height;
	}

	flushimage(display, 1);
	
}

static void
filter(void)
{
	int i;

	fitems = 0;
	for(i = 0; i < nitems; i++){
		if(strstr(items[i], input)){
			filtered[fitems] = items[i];
			fitems++;
		}
	}
}

static void
clicked(int y, int buttons)
{
	Rectangle r;
	int offset;

	r = screen->r;
	offset = (y - r.min.y - f->height) / f->height;
	if(offset < 0 || offset > fitems)
		sysfatal("Selection out of bounds");

	fprint(1, "%s\n", filtered[offset]);
	if(buttons == 1)
		threadexitsall(nil);
}

static void
usage(void)
{
	fprint(2, "usage: %s\n", argv0);
	threadexitsall("usage");
}

void
threadmain(int argc, char **argv)
{
	Keyboardctl *kctl;
	Mousectl *mctl;
	Biobuf *b;
	Rune key;
	Mouse m;
	char *s, *v[3];
	int oldbuttons;
	u32int argb, brgb, crgb;
	enum {
		Emouse,
		Eresize,
		Ekeyboard,
		Einput,
		Eend,
	};
	Alt a[] = {
		[Emouse] = { nil, &m, CHANRCV },
		[Eresize] = { nil, nil, CHANRCV },
		[Ekeyboard] = { nil, &key, CHANRCV },
		[Einput] = { nil, &s, CHANRCV },
		[Eend] = { nil, nil, CHANEND },
	};
	ARGBEGIN{
	case 'p':
		prompt = EARGF(usage());
		break;
	default:
		usage();
	}ARGEND
	argb = DPalebluegreen;
	brgb = DPalegreygreen;
	crgb = DBlack;
	if((b = Bopen("/dev/theme", OREAD)) != nil){
		while((s = Brdline(b, '\n')) != nil){
			s[Blinelen(b)-1] = 0;
			if(tokenize(s, v, nelem(v)) > 1){
				if(strcmp(v[0], "menuback") == 0){
					brgb = strtoul(v[1], nil, 16)<<8 | 0xff;
					break;
				}
				if(strcmp(v[0], "menubord") == 0){
					argb = strtoul(v[1], nil, 16)<<8 | 0xff;
					break;
				}
				if(strcmp(v[0], "menutext") == 0){
					crgb = strtoul(v[1], nil, 16)<<8 | 0xff;
					break;
				}
			}
		}
		Bterm(b);
	}

	if((wctl = open("/dev/wctl", ORDWR)) < 0)
		sysfatal("%r");
	if(initdraw(nil, nil, "menu") < 0)
		sysfatal("initdraw: %r");
	f = display->defaultfont;

	if((mctl = initmouse(nil, screen)) == nil)
		sysfatal("initmouse: %r");
	if((kctl = initkeyboard(nil)) == nil)
		sysfatal("keyboard:  %r");

	aback = allocimage(display, Rect(0,0,1,1), RGB24, 1, argb);
	cback = allocimage(display, Rect(0,0,1,1), RGB24, 1, brgb);
	ctext = allocimage(display, Rect(0,0,1,1), RGB24, 1, crgb);

	a[Emouse].c = mctl->c;
	a[Eresize].c = mctl->resizec;
	a[Ekeyboard].c = kctl->c;
	a[Einput].c = chancreate(sizeof(s), 0);

	hlitem = -1;
	nin = 0;
	redraw();

	proccreate(inputproc, a[Einput].c, 16384);
	m.buttons = 0;
	for(;;){
		oldbuttons = m.buttons;

		switch(alt(a)){
		case Ekeyboard:
			switch(key){
			case Kdel:
				close(wctl);
				threadexitsall(nil);
			case 8: // backspace
				if (nin > 0){
					nin--;
					input[nin] = 0;
				}
				filter();
				break;
			case 10: // enter
				fprint(1, "%s\n", filtered[0]);
				break;
			default:
				input[nin++] = key;
				input[nin] = 0;
				filter();
			}
			break;
		case Emouse:
			hlitem = (m.xy.y - screen->r.min.y - f->height) / f->height;
			if(m.buttons == oldbuttons)
				break;
			clicked(m.xy.y, m.buttons);
			break;
		case Eresize:
			if(getwindow(display, Refnone) < 0)
				threadexitsall(nil);
			break;
		case Einput:
			if(s != nil)
				// TODO: String split via seperator
				items[nitems++] = s;
				filtered[fitems++] = s;
			break;
		}
		redraw();
	}
}
