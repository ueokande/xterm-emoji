#define RES_OFFSET(field)	XtOffsetOf(XTERM_RESOURCE, field)

#include <xterm.h>
#include <version.h>
#include <graphics.h>

#include <X11/cursorfont.h>
#include <X11/Xlocale.h>

#include <pwd.h>
#include <ctype.h>

#include <data.h>
#include <error.h>
#include <menu.h>
#include <main.h>
#include <xstrings.h>
#include <xtermcap.h>
#include <xterm_io.h>

static void Syntax(char *) GCC_NORETURN;
static void HsSysError(int) GCC_NORETURN;

#include <sys/stat.h>

#include <stdio.h>

#define UTMP_STR utmp

#include <signal.h>


static int get_pty(int *pty, char *from);
static void resize_termcap(XtermWidget xw);
static void set_owner(char *device, unsigned uid, unsigned gid, unsigned mode);

static Bool added_utmp_entry = False;

static uid_t save_ruid;
static gid_t save_rgid;

static char *explicit_shname = NULL;

/*
** Ordinarily it should be okay to omit the assignment in the following
** statement. Apparently the c89 compiler on AIX 4.1.3 has a bug, or does
** it? Without the assignment though the compiler will init command_to_exec
** to 0xffffffff instead of NULL; and subsequent usage, e.g. in spawnXTerm() to
** SEGV.
*/
static char **command_to_exec = NULL;


#define TERMCAP_ERASE "kb"
#define VAL_INITIAL_ERASE A2E(8)

/* choose a nice default value for speed - if we make it too low, users who
 * mistakenly use $TERM set to vt100 will get padding delays.  Setting it to a
 * higher value is not useful since legacy applications (termcap) that care
 * about padding generally store the code in a short, which does not have
 * enough bits for the extended values.
 */
#define VAL_LINE_SPEED B9600

/*
 * SYSV has the termio.c_cc[V] and ltchars; BSD has tchars and ltchars;
 * SVR4 has only termio.c_cc, but it includes everything from ltchars.
 * POSIX termios has termios.c_cc, which is similar to SVR4.
 */
#define TTYMODE(name) { name, sizeof(name)-1, 0, 0 }
static Boolean override_tty_modes = False;
/* *INDENT-OFF* */
static struct _xttymodes {
    const char *name;
    size_t len;
    int set;
    int value;
} ttymodelist[] = {
    TTYMODE("intr"),		/* tchars.t_intrc ; VINTR */
#define XTTYMODE_intr	0
    TTYMODE("quit"),		/* tchars.t_quitc ; VQUIT */
#define XTTYMODE_quit	1
    TTYMODE("erase"),		/* sgttyb.sg_erase ; VERASE */
#define XTTYMODE_erase	2
    TTYMODE("kill"),		/* sgttyb.sg_kill ; VKILL */
#define XTTYMODE_kill	3
    TTYMODE("eof"),		/* tchars.t_eofc ; VEOF */
#define XTTYMODE_eof	4
    TTYMODE("eol"),		/* VEOL */
#define XTTYMODE_eol	5
    TTYMODE("swtch"),		/* VSWTCH */
#define XTTYMODE_swtch	6
    TTYMODE("start"),		/* tchars.t_startc ; VSTART */
#define XTTYMODE_start	7
    TTYMODE("stop"),		/* tchars.t_stopc ; VSTOP */
#define XTTYMODE_stop	8
    TTYMODE("brk"),		/* tchars.t_brkc */
#define XTTYMODE_brk	9
    TTYMODE("susp"),		/* ltchars.t_suspc ; VSUSP */
#define XTTYMODE_susp	10
    TTYMODE("dsusp"),		/* ltchars.t_dsuspc ; VDSUSP */
#define XTTYMODE_dsusp	11
    TTYMODE("rprnt"),		/* ltchars.t_rprntc ; VREPRINT */
#define XTTYMODE_rprnt	12
    TTYMODE("flush"),		/* ltchars.t_flushc ; VDISCARD */
#define XTTYMODE_flush	13
    TTYMODE("weras"),		/* ltchars.t_werasc ; VWERASE */
#define XTTYMODE_weras	14
    TTYMODE("lnext"),		/* ltchars.t_lnextc ; VLNEXT */
#define XTTYMODE_lnext	15
    TTYMODE("status"),		/* VSTATUS */
#define XTTYMODE_status	16
    TTYMODE("erase2"),		/* VERASE2 */
#define XTTYMODE_erase2	17
    TTYMODE("eol2"),		/* VEOL2 */
#define XTTYMODE_eol2	18
    { NULL,	0, 0, '\0' },	/* end of data */
};

#define validTtyChar(data, n) \
	    (known_ttyChars[n].sysMode >= 0 && \
	     known_ttyChars[n].sysMode < (int) XtNumber(data.c_cc))

static const struct {
    int sysMode;
    int myMode;
    int myDefault;
} known_ttyChars[] = {
};
/* *INDENT-ON* */

#define TMODE(ind,var) if (ttymodelist[ind].set) var = (cc_t) ttymodelist[ind].value

static int parse_tty_modes(char *s, struct _xttymodes *modelist);

/*
 * Some people with 4.3bsd /bin/login seem to like to use login -p -f user
 * to implement xterm -ls.  They can turn on USE_LOGIN_DASH_P and turn off
 * WTMP and USE_LASTLOG.
 */
static char noPassedPty[2];
static char *passedPty = noPassedPty;	/* name if pty if slave */

static sigjmp_buf env;

#define SetUtmpHost(dst, screen) \
	{ \
	    char host[sizeof(dst) + 1]; \
	    strncpy(host, DisplayString(screen->display), sizeof(host)); \
	    TRACE(("DisplayString(%s)\n", host)); \
	    if (!resource.utmpDisplayId) { \
		char *endptr = strrchr(host, ':'); \
		if (endptr) { \
		    TRACE(("trimming display-id '%s'\n", host)); \
		    *endptr = '\0'; \
		} \
	    } \
	    copy_filled(dst, host, sizeof(dst)); \
	}

/* used by VT (charproc.c) */

static XtResource application_resources[] =
{
    Sres("iconGeometry", "IconGeometry", icon_geometry, NULL),
    Sres(XtNtitle, XtCTitle, title, NULL),
    Sres(XtNiconHint, XtCIconHint, icon_hint, NULL),
    Sres(XtNiconName, XtCIconName, icon_name, NULL),
    Sres("termName", "TermName", term_name, NULL),
    Sres("ttyModes", "TtyModes", tty_modes, NULL),
    Bres("hold", "Hold", hold_screen, False),
    Bres("utmpInhibit", "UtmpInhibit", utmpInhibit, False),
    Bres("utmpDisplayId", "UtmpDisplayId", utmpDisplayId, True),
    Bres("messages", "Messages", messages, True),
    Ires("minBufSize", "MinBufSize", minBufSize, 4096),
    Ires("maxBufSize", "MaxBufSize", maxBufSize, 32768),
    Sres("menuLocale", "MenuLocale", menuLocale, DEF_MENU_LOCALE),
    Sres("omitTranslation", "OmitTranslation", omitTranslation, NULL),
    Sres("keyboardType", "KeyboardType", keyboardType, "unknown"),
    Bres("useInsertMode", "UseInsertMode", useInsertMode, False),
};

static String fallback_resources[] =
{
    "*SimpleMenu*menuLabel.vertSpace: 100",
    "*SimpleMenu*HorizontalMargins: 16",
    "*SimpleMenu*Sme.height: 16",
    "*SimpleMenu*Cursor: left_ptr",
    "*mainMenu.Label:  Main Options (no app-defaults)",
    "*vtMenu.Label:  VT Options (no app-defaults)",
    "*fontMenu.Label:  VT Fonts (no app-defaults)",
    NULL
};

/* Command line options table.  Only resources are entered here...there is a
   pass over the remaining options after XrmParseCommand is let loose. */
/* *INDENT-OFF* */
static XrmOptionDescRec optionDescList[] = {
{"-geometry",	"*vt100.geometry",XrmoptionSepArg,	(XPointer) NULL},
{"-132",	"*c132",	XrmoptionNoArg,		(XPointer) "on"},
{"+132",	"*c132",	XrmoptionNoArg,		(XPointer) "off"},
{"-ah",		"*alwaysHighlight", XrmoptionNoArg,	(XPointer) "on"},
{"+ah",		"*alwaysHighlight", XrmoptionNoArg,	(XPointer) "off"},
{"-aw",		"*autoWrap",	XrmoptionNoArg,		(XPointer) "on"},
{"+aw",		"*autoWrap",	XrmoptionNoArg,		(XPointer) "off"},
{"-b",		"*internalBorder",XrmoptionSepArg,	(XPointer) NULL},
{"-bc",		"*cursorBlink",	XrmoptionNoArg,		(XPointer) "on"},
{"+bc",		"*cursorBlink",	XrmoptionNoArg,		(XPointer) "off"},
{"-bcf",	"*cursorOffTime",XrmoptionSepArg,	(XPointer) NULL},
{"-bcn",	"*cursorOnTime",XrmoptionSepArg,	(XPointer) NULL},
{"-bdc",	"*colorBDMode",	XrmoptionNoArg,		(XPointer) "off"},
{"+bdc",	"*colorBDMode",	XrmoptionNoArg,		(XPointer) "on"},
{"-cb",		"*cutToBeginningOfLine", XrmoptionNoArg, (XPointer) "off"},
{"+cb",		"*cutToBeginningOfLine", XrmoptionNoArg, (XPointer) "on"},
{"-cc",		"*charClass",	XrmoptionSepArg,	(XPointer) NULL},
{"-cm",		"*colorMode",	XrmoptionNoArg,		(XPointer) "off"},
{"+cm",		"*colorMode",	XrmoptionNoArg,		(XPointer) "on"},
{"-cn",		"*cutNewline",	XrmoptionNoArg,		(XPointer) "off"},
{"+cn",		"*cutNewline",	XrmoptionNoArg,		(XPointer) "on"},
{"-cr",		"*cursorColor",	XrmoptionSepArg,	(XPointer) NULL},
{"-cu",		"*curses",	XrmoptionNoArg,		(XPointer) "on"},
{"+cu",		"*curses",	XrmoptionNoArg,		(XPointer) "off"},
{"-dc",		"*dynamicColors",XrmoptionNoArg,	(XPointer) "off"},
{"+dc",		"*dynamicColors",XrmoptionNoArg,	(XPointer) "on"},
{"-fb",		"*boldFont",	XrmoptionSepArg,	(XPointer) NULL},
{"-fbb",	"*freeBoldBox", XrmoptionNoArg,		(XPointer)"off"},
{"+fbb",	"*freeBoldBox", XrmoptionNoArg,		(XPointer)"on"},
{"-fbx",	"*forceBoxChars", XrmoptionNoArg,	(XPointer)"off"},
{"+fbx",	"*forceBoxChars", XrmoptionNoArg,	(XPointer)"on"},
{"-hold",	"*hold",	XrmoptionNoArg,		(XPointer) "on"},
{"+hold",	"*hold",	XrmoptionNoArg,		(XPointer) "off"},
{"-j",		"*jumpScroll",	XrmoptionNoArg,		(XPointer) "on"},
{"+j",		"*jumpScroll",	XrmoptionNoArg,		(XPointer) "off"},
{"-kt",		"*keyboardType", XrmoptionSepArg,	(XPointer) NULL},
/* parse logging options anyway for compatibility */
{"-l",		"*logging",	XrmoptionNoArg,		(XPointer) "on"},
{"+l",		"*logging",	XrmoptionNoArg,		(XPointer) "off"},
{"-lf",		"*logFile",	XrmoptionSepArg,	(XPointer) NULL},
{"-ls",		"*loginShell",	XrmoptionNoArg,		(XPointer) "on"},
{"+ls",		"*loginShell",	XrmoptionNoArg,		(XPointer) "off"},
{"-mb",		"*marginBell",	XrmoptionNoArg,		(XPointer) "on"},
{"+mb",		"*marginBell",	XrmoptionNoArg,		(XPointer) "off"},
{"-mc",		"*multiClickTime", XrmoptionSepArg,	(XPointer) NULL},
{"-mesg",	"*messages",	XrmoptionNoArg,		(XPointer) "off"},
{"+mesg",	"*messages",	XrmoptionNoArg,		(XPointer) "on"},
{"-ms",		"*pointerColor",XrmoptionSepArg,	(XPointer) NULL},
{"-nb",		"*nMarginBell",	XrmoptionSepArg,	(XPointer) NULL},
{"-nul",	"*underLine",	XrmoptionNoArg,		(XPointer) "off"},
{"+nul",	"*underLine",	XrmoptionNoArg,		(XPointer) "on"},
{"-pc",		"*boldColors",	XrmoptionNoArg,		(XPointer) "on"},
{"+pc",		"*boldColors",	XrmoptionNoArg,		(XPointer) "off"},
{"-rw",		"*reverseWrap",	XrmoptionNoArg,		(XPointer) "on"},
{"+rw",		"*reverseWrap",	XrmoptionNoArg,		(XPointer) "off"},
{"-s",		"*multiScroll",	XrmoptionNoArg,		(XPointer) "on"},
{"+s",		"*multiScroll",	XrmoptionNoArg,		(XPointer) "off"},
{"-sb",		"*scrollBar",	XrmoptionNoArg,		(XPointer) "on"},
{"+sb",		"*scrollBar",	XrmoptionNoArg,		(XPointer) "off"},
{"-rvc",	"*colorRVMode",	XrmoptionNoArg,		(XPointer) "off"},
{"+rvc",	"*colorRVMode",	XrmoptionNoArg,		(XPointer) "on"},
{"-sf",		"*sunFunctionKeys", XrmoptionNoArg,	(XPointer) "on"},
{"+sf",		"*sunFunctionKeys", XrmoptionNoArg,	(XPointer) "off"},
{"-sh",		"*scaleHeight", XrmoptionSepArg,	(XPointer) NULL},
{"-si",		"*scrollTtyOutput", XrmoptionNoArg,	(XPointer) "off"},
{"+si",		"*scrollTtyOutput", XrmoptionNoArg,	(XPointer) "on"},
{"-sk",		"*scrollKey",	XrmoptionNoArg,		(XPointer) "on"},
{"+sk",		"*scrollKey",	XrmoptionNoArg,		(XPointer) "off"},
{"-sl",		"*saveLines",	XrmoptionSepArg,	(XPointer) NULL},
{"-ti",		"*decTerminalID",XrmoptionSepArg,	(XPointer) NULL},
{"-tm",		"*ttyModes",	XrmoptionSepArg,	(XPointer) NULL},
{"-tn",		"*termName",	XrmoptionSepArg,	(XPointer) NULL},
{"-uc",		"*cursorUnderLine", XrmoptionNoArg,	(XPointer) "on"},
{"+uc",		"*cursorUnderLine", XrmoptionNoArg,	(XPointer) "off"},
{"-ulc",	"*colorULMode",	XrmoptionNoArg,		(XPointer) "off"},
{"+ulc",	"*colorULMode",	XrmoptionNoArg,		(XPointer) "on"},
{"-ulit",       "*italicULMode", XrmoptionNoArg,        (XPointer) "off"},
{"+ulit",       "*italicULMode", XrmoptionNoArg,        (XPointer) "on"},
{"-ut",		"*utmpInhibit",	XrmoptionNoArg,		(XPointer) "on"},
{"+ut",		"*utmpInhibit",	XrmoptionNoArg,		(XPointer) "off"},
{"-im",		"*useInsertMode", XrmoptionNoArg,	(XPointer) "on"},
{"+im",		"*useInsertMode", XrmoptionNoArg,	(XPointer) "off"},
{"-vb",		"*visualBell",	XrmoptionNoArg,		(XPointer) "on"},
{"+vb",		"*visualBell",	XrmoptionNoArg,		(XPointer) "off"},
{"-pob",	"*popOnBell",	XrmoptionNoArg,		(XPointer) "on"},
{"+pob",	"*popOnBell",	XrmoptionNoArg,		(XPointer) "off"},
{"-wf",		"*waitForMap",	XrmoptionNoArg,		(XPointer) "on"},
{"+wf",		"*waitForMap",	XrmoptionNoArg,		(XPointer) "off"},
/* options that we process ourselves */
{"-help",	NULL,		XrmoptionSkipNArgs,	(XPointer) NULL},
{"-version",	NULL,		XrmoptionSkipNArgs,	(XPointer) NULL},
{"-class",	NULL,		XrmoptionSkipArg,	(XPointer) NULL},
{"-e",		NULL,		XrmoptionSkipLine,	(XPointer) NULL},
{"-into",	NULL,		XrmoptionSkipArg,	(XPointer) NULL},
/* bogus old compatibility stuff for which there are
   standard XtOpenApplication options now */
{"%",		"*tekGeometry",	XrmoptionStickyArg,	(XPointer) NULL},
{"#",		".iconGeometry",XrmoptionStickyArg,	(XPointer) NULL},
{"-T",		".title",	XrmoptionSepArg,	(XPointer) NULL},
{"-n",		"*iconName",	XrmoptionSepArg,	(XPointer) NULL},
{"-r",		"*reverseVideo",XrmoptionNoArg,		(XPointer) "on"},
{"+r",		"*reverseVideo",XrmoptionNoArg,		(XPointer) "off"},
{"-rv",		"*reverseVideo",XrmoptionNoArg,		(XPointer) "on"},
{"+rv",		"*reverseVideo",XrmoptionNoArg,		(XPointer) "off"},
{"-w",		".borderWidth", XrmoptionSepArg,	(XPointer) NULL},
};

static OptionHelp xtermOptions[] = {
{ "-version",              "print the version number" },
{ "-help",                 "print out this message" },
{ "-display displayname",  "X server to contact" },
{ "-geometry geom",        "size (in characters) and position" },
{ "-/+rv",                 "turn on/off reverse video" },
{ "-bg color",             "background color" },
{ "-fg color",             "foreground color" },
{ "-bd color",             "border color" },
{ "-bw number",            "border width in pixels" },
{ "-fn fontname",          "normal text font" },
{ "-fb fontname",          "bold text font" },
{ "-/+fbb",                "turn on/off normal/bold font comparison inhibit"},
{ "-/+fbx",                "turn off/on linedrawing characters"},
{ "-b number",             "internal border in pixels" },
{ "-/+bc",                 "turn on/off text cursor blinking" },
{ "-bcf milliseconds",     "time text cursor is off when blinking"},
{ "-bcn milliseconds",     "time text cursor is on when blinking"},
{ "-/+bdc",                "turn off/on display of bold as color"},
{ "-/+cb",                 "turn on/off cut-to-beginning-of-line inhibit" },
{ "-cc classrange",        "specify additional character classes" },
{ "-/+cm",                 "turn off/on ANSI color mode" },
{ "-/+cn",                 "turn on/off cut newline inhibit" },
{ "-cr color",             "text cursor color" },
{ "-/+cu",                 "turn on/off curses emulation" },
{ "-/+dc",                 "turn off/on dynamic color selection" },
{ "-/+hold",               "turn on/off logic that retains window after exit" },
{ "-/+im",                 "use insert mode for TERMCAP" },
{ "-/+j",                  "turn on/off jump scroll" },
{ "-kt keyboardtype",      "set keyboard type:" KEYBOARD_TYPES },
{ "-/+l",                  "turn on/off logging (not supported)" },
{ "-lf filename",          "logging filename (not supported)" },
{ "-/+ls",                 "turn on/off login shell" },
{ "-/+mb",                 "turn on/off margin bell" },
{ "-mc milliseconds",      "multiclick time in milliseconds" },
{ "-/+mesg",               "forbid/allow messages" },
{ "-ms color",             "pointer color" },
{ "-nb number",            "margin bell in characters from right end" },
{ "-/+nul",                "turn off/on display of underlining" },
{ "-/+aw",                 "turn on/off auto wraparound" },
{ "-/+pc",                 "turn on/off PC-style bold colors" },
{ "-/+rw",                 "turn on/off reverse wraparound" },
{ "-/+s",                  "turn on/off multiscroll" },
{ "-/+sb",                 "turn on/off scrollbar" },
{ "-/+rvc",                "turn off/on display of reverse as color" },
{ "-/+sf",                 "turn on/off Sun Function Key escape codes" },
{ "-sh number",            "scale line-height values by the given number" },
{ "-/+si",                 "turn on/off scroll-on-tty-output inhibit" },
{ "-/+sk",                 "turn on/off scroll-on-keypress" },
{ "-sl number",            "number of scrolled lines to save" },
{ "-ti termid",            "terminal identifier" },
{ "-tm string",            "terminal mode keywords and characters" },
{ "-tn name",              "TERM environment variable name" },
{ "-/+uc",                 "turn on/off underline cursor" },
{ "-/+ulc",                "turn off/on display of underline as color" },
{ "-/+ulit",               "turn off/on display of underline as italics" },
{ "-/+ut",                 "turn on/off utmp support (not available)" },
{ "-/+vb",                 "turn on/off visual bell" },
{ "-/+pob",                "turn on/off pop on bell" },
{ "-/+wf",                 "turn on/off wait for map before command exec" },
{ "-e command args ...",   "command to execute" },
{ "#geom",                 "icon window geometry" },
{ "-T string",             "title name for window" },
{ "-n string",             "icon name for window" },
{ "-C",                    "intercept console messages (not supported)" },
{ "-Sccn",                 "slave mode on \"ttycc\", file descriptor \"n\"" },
{ "-into windowId",        "use the window id given to -into as the parent window rather than the default root window" },
{ NULL, NULL }};
/* *INDENT-ON* */

static const char *const message[] =
{
    "Fonts should be fixed width and, if both normal and bold are specified, should",
    "have the same size.  If only a normal font is specified, it will be used for",
    "both normal and bold text (by doing overstriking).  The -e option, if given,",
    "must appear at the end of the command line, otherwise the user's default shell",
    "will be started.  Options that start with a plus sign (+) restore the default.",
    NULL};

/*
 * Decode a key-definition.  This combines the termcap and ttyModes, for
 * comparison.  Note that octal escapes in ttyModes are done by the normal
 * resource translation.  Also, ttyModes allows '^-' as a synonym for disabled.
 */
static int
decode_keyvalue(char **ptr, int termcap)
{
    char *string = *ptr;
    int value = -1;

    TRACE(("decode_keyvalue '%s'\n", string));
    if (*string == '^') {
	switch (*++string) {
	case '?':
	    value = A2E(ANSI_DEL);
	    break;
	case '-':
	    if (!termcap) {
		errno = 0;
		break;
	    }
	    /* FALLTHRU */
	default:
	    value = CONTROL(*string);
	    break;
	}
	++string;
    } else if (termcap && (*string == '\\')) {
	char *d;
	int temp = (int) strtol(string + 1, &d, 8);
	if (temp > 0 && d != string) {
	    value = temp;
	    string = d;
	}
    } else {
	value = CharOf(*string);
	++string;
    }
    *ptr = string;
    TRACE(("...decode_keyvalue %#x\n", value));
    return value;
}

static int
matchArg(XrmOptionDescRec * table, const char *param)
{
    int result = -1;
    int n;
    int ch;

    for (n = 0; (ch = table->option[n]) != '\0'; ++n) {
	if (param[n] == ch) {
	    result = n;
	} else {
	    if (param[n] != '\0')
		result = -1;
	    break;
	}
    }

    return result;
}

/* return the number of argv[] entries which constitute arguments of option */
static int
countArg(XrmOptionDescRec * item)
{
    int result = 0;

    switch (item->argKind) {
    case XrmoptionNoArg:
	/* FALLTHRU */
    case XrmoptionIsArg:
	/* FALLTHRU */
    case XrmoptionStickyArg:
	break;
    case XrmoptionSepArg:
	/* FALLTHRU */
    case XrmoptionResArg:
	/* FALLTHRU */
    case XrmoptionSkipArg:
	result = 1;
	break;
    case XrmoptionSkipLine:
	break;
    case XrmoptionSkipNArgs:
	result = (int) (long) (item->value);
	break;
    }
    return result;
}

#define isOption(string) (Boolean)((string)[0] == '-' || (string)[0] == '+')

/*
 * Parse the argument list, more/less as XtInitialize, etc., would do, so we
 * can find our own "-help" and "-version" options reliably.  Improve on just
 * doing that, by detecting ambiguous options (things that happen to match the
 * abbreviated option we are examining), and making it smart enough to handle
 * "-d" as an abbreviation for "-display".  Doing this requires checking the
 * standard table (something that the X libraries should do).
 */
static XrmOptionDescRec *
parseArg(int *num, char **argv, char **valuep)
{
    /* table adapted from XtInitialize, used here to improve abbreviations */
    /* *INDENT-OFF* */
#define DATA(option,kind) { option, NULL, kind, (XtPointer) NULL }
    static XrmOptionDescRec opTable[] = {
	DATA("+synchronous",	   XrmoptionNoArg),
	DATA("-background",	   XrmoptionSepArg),
	DATA("-bd",		   XrmoptionSepArg),
	DATA("-bg",		   XrmoptionSepArg),
	DATA("-bordercolor",	   XrmoptionSepArg),
	DATA("-borderwidth",	   XrmoptionSepArg),
	DATA("-bw",		   XrmoptionSepArg),
	DATA("-display",	   XrmoptionSepArg),
	DATA("-fg",		   XrmoptionSepArg),
	DATA("-fn",		   XrmoptionSepArg),
	DATA("-font",		   XrmoptionSepArg),
	DATA("-foreground",	   XrmoptionSepArg),
	DATA("-iconic",		   XrmoptionNoArg),
	DATA("-name",		   XrmoptionSepArg),
	DATA("-reverse",	   XrmoptionNoArg),
	DATA("-selectionTimeout",  XrmoptionSepArg),
	DATA("-synchronous",	   XrmoptionNoArg),
	DATA("-title",		   XrmoptionSepArg),
	DATA("-xnllanguage",	   XrmoptionSepArg),
	DATA("-xrm",		   XrmoptionResArg),
	DATA("-xtsessionID",	   XrmoptionSepArg),
	/* These xterm options are processed after XtOpenApplication */
	DATA("-S",		   XrmoptionStickyArg),
	DATA("-D",		   XrmoptionNoArg),
    };
#undef DATA
    /* *INDENT-ON* */

    XrmOptionDescRec *result = 0;
    Cardinal inlist;
    Cardinal limit = XtNumber(optionDescList) + XtNumber(opTable);
    int atbest = -1;
    int best = -1;
    int test;
    Boolean exact = False;
    int ambiguous1 = -1;
    int ambiguous2 = -1;
    char *option;
    char *value;

#define ITEM(n) ((Cardinal)(n) < XtNumber(optionDescList) \
		 ? &optionDescList[n] \
		 : &opTable[(Cardinal)(n) - XtNumber(optionDescList)])

    if ((option = argv[*num]) != 0) {
	Boolean need_value;
	Boolean have_value = False;

	TRACE(("parseArg %s\n", option));
	if ((value = argv[(*num) + 1]) != 0) {
	    have_value = (Boolean) !isOption(value);
	}
	for (inlist = 0; inlist < limit; ++inlist) {
	    XrmOptionDescRec *check = ITEM(inlist);

	    test = matchArg(check, option);
	    if (test < 0)
		continue;

	    /* check for exact match */
	    if ((test + 1) == (int) strlen(check->option)) {
		if (check->argKind == XrmoptionStickyArg) {
		    if (strlen(option) > strlen(check->option)) {
			exact = True;
			atbest = (int) inlist;
			break;
		    }
		} else if ((test + 1) == (int) strlen(option)) {
		    exact = True;
		    atbest = (int) inlist;
		    break;
		}
	    }

	    need_value = (Boolean) (test > 0 && countArg(check) > 0);

	    if (need_value && value != 0) {
		;
	    } else if (need_value ^ have_value) {
		TRACE(("...skipping, need %d vs have %d\n", need_value, have_value));
		continue;
	    }

	    /* special-case for our own options - always allow abbreviation */
	    if (test > 0
		&& ITEM(inlist)->argKind >= XrmoptionSkipArg) {
		atbest = (int) inlist;
		if (ITEM(inlist)->argKind == XrmoptionSkipNArgs) {
		    /* in particular, silence a warning about ambiguity */
		    exact = 1;
		}
		break;
	    }
	    if (test > best) {
		best = test;
		atbest = (int) inlist;
	    } else if (test == best) {
		if (atbest >= 0) {
		    if (atbest > 0) {
			ambiguous1 = (int) inlist;
			ambiguous2 = (int) atbest;
		    }
		    atbest = -1;
		}
	    }
	}
    }

    *valuep = 0;
    if (atbest >= 0) {
	result = ITEM(atbest);
	if (!exact) {
	    if (ambiguous1 >= 0 && ambiguous2 >= 0) {
		xtermWarning("ambiguous option \"%s\" vs \"%s\"\n",
			     ITEM(ambiguous1)->option,
			     ITEM(ambiguous2)->option);
	    } else if (strlen(option) > strlen(result->option)) {
		result = 0;
	    }
	}
	if (result != 0) {
	    TRACE(("...result %s\n", result->option));
	    /* expand abbreviations */
	    if (result->argKind != XrmoptionStickyArg) {
		if (strcmp(argv[*num], result->option)) {
		    argv[*num] = x_strdup(result->option);
		}
	    }

	    /* adjust (*num) to skip option value */
	    (*num) += countArg(result);
	    TRACE(("...next %s\n", NonNull(argv[*num])));
	    if (result->argKind == XrmoptionSkipArg) {
		*valuep = argv[*num];
		TRACE(("...parameter %s\n", NonNull(*valuep)));
	    }
	}
    }
#undef ITEM
    return result;
}

static void
Syntax(char *badOption)
{
    OptionHelp *opt;
    OptionHelp *list = sortedOpts(xtermOptions, optionDescList, XtNumber(optionDescList));
    int col;

    TRACE(("Syntax error at %s\n", badOption));
    xtermWarning("bad command line option \"%s\"\r\n\n", badOption);

    fprintf(stderr, "usage:  %s", ProgramName);
    col = 8 + (int) strlen(ProgramName);
    for (opt = list; opt->opt; opt++) {
	int len = 3 + (int) strlen(opt->opt);	/* space [ string ] */
	if (col + len > 79) {
	    fprintf(stderr, "\r\n   ");		/* 3 spaces */
	    col = 3;
	}
	fprintf(stderr, " [%s]", opt->opt);
	col += len;
    }

    fprintf(stderr, "\r\n\nType %s -help for a full description.\r\n\n",
	    ProgramName);
    exit(1);
}

static void
Version(void)
{
    printf("%s\n", xtermVersion());
    fflush(stdout);
}

static void
Help(void)
{
    OptionHelp *opt;
    OptionHelp *list = sortedOpts(xtermOptions, optionDescList, XtNumber(optionDescList));
    const char *const *cpp;

    printf("%s usage:\n    %s [-options ...] [-e command args]\n\n",
	   xtermVersion(), ProgramName);
    printf("where options include:\n");
    for (opt = list; opt->opt; opt++) {
	printf("    %-28s %s\n", opt->opt, opt->desc);
    }

    putchar('\n');
    for (cpp = message; *cpp; cpp++)
	puts(*cpp);
    putchar('\n');
    fflush(stdout);
}


/*
 * DeleteWindow(): Action proc to implement ICCCM delete_window.
 */
/* ARGSUSED */
static void
DeleteWindow(Widget w,
	     XEvent *event GCC_UNUSED,
	     String *params GCC_UNUSED,
	     Cardinal *num_params GCC_UNUSED)
{
	do_hangup(w, (XtPointer) 0, (XtPointer) 0);
}

/* ARGSUSED */
static void
KeyboardMapping(Widget w GCC_UNUSED,
		XEvent *event,
		String *params GCC_UNUSED,
		Cardinal *num_params GCC_UNUSED)
{
    switch (event->type) {
    case MappingNotify:
	XRefreshKeyboardMapping(&event->xmapping);
	break;
    }
}

static XtActionsRec actionProcs[] =
{
    {"DeleteWindow", DeleteWindow},
    {"KeyboardMapping", KeyboardMapping},
};

/*
 * Some platforms use names such as /dev/tty01, others /dev/pts/1.  Parse off
 * the "tty01" or "pts/1" portion, and return that for use as an identifier for
 * utmp.
 */
static char *
my_pty_name(char *device)
{
    size_t len = strlen(device);
    Bool name = False;

    while (len != 0) {
	int ch = device[len - 1];
	if (isdigit(ch)) {
	    len--;
	} else if (ch == '/') {
	    if (name)
		break;
	    len--;
	} else if (isalpha(ch)) {
	    name = True;
	    len--;
	} else {
	    break;
	}
    }
    TRACE(("my_pty_name(%s) -> '%s'\n", device, device + len));
    return device + len;
}

/*
 * If the name contains a '/', it is a "pts/1" case.  Otherwise, return the
 * last few characters for a utmp identifier.
 */
static char *
my_pty_id(char *device)
{
    char *name = my_pty_name(device);
    char *leaf = x_basename(name);

    if (name == leaf) {		/* no '/' in the name */
	int len = (int) strlen(leaf);
	if (PTYCHARLEN < len)
	    leaf = leaf + (len - PTYCHARLEN);
    }
    TRACE(("my_pty_id  (%s) -> '%s'\n", device, leaf));
    return leaf;
}

/*
 * Set the tty/pty identifier
 */
static void
set_pty_id(char *device, char *id)
{
    char *name = my_pty_name(device);
    char *leaf = x_basename(name);

    if (name == leaf) {
	strcpy(my_pty_id(device), id);
    } else {
	strcpy(leaf, id);
    }
    TRACE(("set_pty_id(%s) -> '%s'\n", id, device));
}

/*
 * The original -S option accepts two characters to identify the pty, and a
 * file-descriptor (assumed to be nonzero).  That is not general enough, so we
 * check first if the option contains a '/' to delimit the two fields, and if
 * not, fall-thru to the original logic.
 */
static Bool
ParseSccn(char *option)
{
    char *leaf = x_basename(option);
    Bool code = False;

    passedPty = x_strdup(option);
    if (leaf != option) {
	if (leaf - option > 0
	    && isdigit(CharOf(*leaf))
	    && sscanf(leaf, "%d", &am_slave) == 1) {
	    size_t len = (size_t) (leaf - option - 1);
	    /*
	     * If we have a slash, we only care about the part after the slash,
	     * which is a file-descriptor.  The part before the slash can be
	     * the /dev/pts/XXX value, but since we do not need to reopen it,
	     * it is useful mainly for display in a "ps -ef".
	     */
	    passedPty[len] = 0;
	    code = True;
	}
    } else {
	code = (sscanf(option, "%c%c%d",
		       passedPty, passedPty + 1, &am_slave) == 3);
	passedPty[2] = '\0';
    }
    TRACE(("ParseSccn(%s) = '%s' %d (%s)\n", option,
	   passedPty, am_slave, code ? "OK" : "ERR"));
    return code;
}

#define disableSetUid()		/* nothing */

#define disableSetGid()		/* nothing */

int
main(int argc, char *argv[]ENVP_ARG)
{
    Widget form_top, menu_top;
    Dimension menu_high;
    TScreen *screen;
    int mode;
    char *my_class = x_strdup(DEFCLASS);
    Window winToEmbedInto = None;

    ProgramName = argv[0];


    save_ruid = getuid();
    save_rgid = getgid();

    /* extra length in case longer tty name like /dev/ttyq255 */
    ttydev = TypeMallocN(char, sizeof(TTYDEV) + 80);
    if (!ttydev)
    {
	xtermWarning("unable to allocate memory for ttydev or ptydev\n");
	exit(1);
    }
    strcpy(ttydev, TTYDEV);

    /* Do these first, since we may not be able to open the display */
    TRACE_OPTS(xtermOptions, optionDescList, XtNumber(optionDescList));
    TRACE_ARGV("Before XtOpenApplication", argv);
    if (argc > 1) {
	XrmOptionDescRec *option_ptr;
	char *option_value;
	int n;
	Bool quit = False;

	for (n = 1; n < argc; n++) {
	    if ((option_ptr = parseArg(&n, argv, &option_value)) == 0) {
		if (argv[n] == 0) {
		    break;
		} else if (isOption(argv[n])) {
		    Syntax(argv[n]);
		} else if (explicit_shname != 0) {
		    xtermWarning("Explicit shell already was %s\n", explicit_shname);
		    Syntax(argv[n]);
		}
		explicit_shname = xtermFindShell(argv[n], True);
		if (explicit_shname == 0)
		    exit(0);
		TRACE(("...explicit shell %s\n", explicit_shname));
	    } else if (!strcmp(option_ptr->option, "-e")) {
		command_to_exec = (argv + n + 1);
		if (!command_to_exec[0])
		    Syntax(argv[n]);
		break;
	    } else if (!strcmp(option_ptr->option, "-version")) {
		Version();
		quit = True;
	    } else if (!strcmp(option_ptr->option, "-help")) {
		Help();
		quit = True;
	    } else if (!strcmp(option_ptr->option, "-class")) {
		free(my_class);
		if ((my_class = x_strdup(option_value)) == 0) {
		    Help();
		    quit = True;
		}
	    } else if (!strcmp(option_ptr->option, "-into")) {
		char *endPtr;
		winToEmbedInto = (Window) strtol(option_value, &endPtr, 0);
	    }
	}
	if (quit)
	    exit(0);
	/*
	 * If there is anything left unparsed, and we're not using "-e",
	 * then give up.
	 */
	if (n < argc && !command_to_exec) {
	    Syntax(argv[n]);
	}
    }

    /* This dumped core on HP-UX 9.05 with X11R5 */

    /* Init the Toolkit. */
    {
	init_colored_cursor();

	toplevel = xtermOpenApplication(&app_con,
					my_class,
					optionDescList,
					XtNumber(optionDescList),
					&argc, (String *) argv,
					fallback_resources,
					sessionShellWidgetClass,
					NULL, 0);

	XtGetApplicationResources(toplevel, (XtPointer) &resource,
				  application_resources,
				  XtNumber(application_resources), NULL, 0);
	TRACE_XRES();
	VTInitTranslations();
    }

    /*
     * ICCCM delete_window.
     */
    XtAppAddActions(app_con, actionProcs, XtNumber(actionProcs));

    /*
     * fill in terminal modes
     */
    if (resource.tty_modes) {
	int n = parse_tty_modes(resource.tty_modes, ttymodelist);
	if (n < 0) {
	    xtermWarning("bad tty modes \"%s\"\n", resource.tty_modes);
	} else if (n > 0) {
	    override_tty_modes = True;
	}
    }
    initZIconBeep();
    hold_screen = resource.hold_screen ? 1 : 0;
    if (resource.icon_geometry != NULL) {
	int scr, junk;
	int ix, iy;
	Arg args[2];

	for (scr = 0;		/* yyuucchh */
	     XtScreen(toplevel) != ScreenOfDisplay(XtDisplay(toplevel), scr);
	     scr++) ;

	args[0].name = XtNiconX;
	args[1].name = XtNiconY;
	XGeometry(XtDisplay(toplevel), scr, resource.icon_geometry, "",
		  0, 0, 0, 0, 0, &ix, &iy, &junk, &junk);
	args[0].value = (XtArgVal) ix;
	args[1].value = (XtArgVal) iy;
	XtSetValues(toplevel, args, 2);
    }

    XtSetValues(toplevel, ourTopLevelShellArgs,
		number_ourTopLevelShellArgs);

    /* Parse the rest of the command line */
    TRACE_ARGV("After XtOpenApplication", argv);
    for (argc--, argv++; argc > 0; argc--, argv++) {
	if (!isOption(*argv)) {
	    if (argc > 1)
		Syntax(*argv);
	    continue;
	}

	TRACE(("parsing %s\n", argv[0]));
	switch (argv[0][1]) {
	case 'C':
	    continue;
	case 'S':
	    if (!ParseSccn(*argv + 2))
		Syntax(*argv);
	    continue;
	case 'c':
	    if (strcmp(argv[0], "-class"))
		Syntax(*argv);
	    argc--, argv++;
	    continue;
	case 'e':
	    if (strcmp(argv[0], "-e"))
		Syntax(*argv);
	    command_to_exec = (argv + 1);
	    break;
	case 'i':
	    if (strcmp(argv[0], "-into"))
		Syntax(*argv);
	    argc--, argv++;
	    continue;

	default:
	    Syntax(*argv);
	}
	break;
    }

    SetupMenus(toplevel, &form_top, &menu_top, &menu_high);

    term = (XtermWidget) XtVaCreateManagedWidget("vt100", xtermWidgetClass,
						 form_top,
						 (XtPointer) 0);
    decode_keyboard_type(term, &resource);

    screen = TScreenOf(term);
    screen->inhibit = 0;

    if (term->misc.signalInhibit)
	screen->inhibit |= I_SIGNAL;

    /*
     * Start the toolbar at this point, after the first window has been setup.
     */

    xtermOpenSession();

    /*
     * Set title and icon name if not specified
     */
    if (command_to_exec) {
	Arg args[2];

	if (!resource.title) {
	    if (command_to_exec) {
		resource.title = x_basename(command_to_exec[0]);
	    }			/* else not reached */
	}

	if (!resource.icon_name)
	    resource.icon_name = resource.title;
	XtSetArg(args[0], XtNtitle, resource.title);
	XtSetArg(args[1], XtNiconName, resource.icon_name);

	TRACE(("setting:\n\ttitle \"%s\"\n\ticon \"%s\"\n\thint \"%s\"\n\tbased on command \"%s\"\n",
	       resource.title,
	       resource.icon_name,
	       NonNull(resource.icon_hint),
	       *command_to_exec));

	XtSetValues(toplevel, args, 2);
    }

    spawnXTerm(term);

    XSetErrorHandler(xerror);
    XSetIOErrorHandler(xioerror);

    initPtyData(&VTbuffer);

    xtermEmbedWindow(winToEmbedInto);

    for (;;) {
	    VTRun(term);
    }
}

/*
 * This function opens up a pty master and stuffs its value into pty.
 *
 * If it finds one, it returns a value of 0.  If it does not find one,
 * it returns a value of !0.  This routine is designed to be re-entrant,
 * so that if a pty master is found and later, we find that the slave
 * has problems, we can re-enter this function and get another one.
 */
static int
get_pty(int *pty, char *from GCC_UNUSED)
{
    int result = 1;

    result = pty_search(pty);

    TRACE(("get_pty(ttydev=%s, ptydev=%s) %s fd=%d\n",
	   ttydev != 0 ? ttydev : "?",
	   ptydev != 0 ? ptydev : "?",
	   result ? "FAIL" : "OK",
	   pty != 0 ? *pty : -1));
    return result;
}

static void
set_pty_permissions(uid_t uid, unsigned gid, unsigned mode)
{
    TRACE_IDS;
    set_owner(ttydev, uid, gid, mode);
}

/*
 * The only difference in /etc/termcap between 4014 and 4015 is that
 * the latter has support for switching character sets.  We support the
 * 4015 protocol, but ignore the character switches.  Therefore, we
 * choose 4014 over 4015.
 *
 * Features of the 4014 over the 4012: larger (19") screen, 12-bit
 * graphics addressing (compatible with 4012 10-bit addressing),
 * special point plot mode, incremental plot mode (not implemented in
 * later Tektronix terminals), and 4 character sizes.
 * All of these are supported by xterm.
 */

/* The VT102 is a VT100 with the Advanced Video Option included standard.
 * It also adds Escape sequences for insert/delete character/line.
 * The VT220 adds 8-bit character sets, selective erase.
 * The VT320 adds a 25th status line, terminal state interrogation.
 * The VT420 has up to 48 lines on the screen.
 */

static const char *const vtterm[] =
{
    DFT_TERMTYPE,		/* for people who want special term name */
    "xterm",			/* the prefered name, should be fastest */
    "vt102",
    "vt100",
    "ansi",
    "dumb",
    0
};

/* ARGSUSED */
static void
hungtty(int i GCC_UNUSED)
{
    DEBUG_MSG("handle:hungtty\n");
    siglongjmp(env, 1);
}

/*
 * temporary hack to get xterm working on att ptys
 */
static void
HsSysError(int error)
{
    xtermWarning("fatal pty error %d (errno=%d) on tty %s\n",
		 error, errno, ttydev);
    exit(error);
}

/*
 * Does a non-blocking wait for a child process.  If the system
 * doesn't support non-blocking wait, do nothing.
 * Returns the pid of the child, or 0 or -1 if none or error.
 */
int
nonblocking_wait(void)
{
    union wait status;
    int pid;

    pid = wait3(&status, WNOHANG, (struct rusage *) NULL);
    return pid;
}

static void
remove_termcap_entry(char *buf, const char *str)
{
    char *base = buf;
    char *first = base;
    int count = 0;
    size_t len = strlen(str);

    TRACE(("*** remove_termcap_entry('%s', '%s')\n", str, buf));

    while (*buf != 0) {
	if (!count && !strncmp(buf, str, len)) {
	    while (*buf != 0) {
		if (*buf == '\\')
		    buf++;
		else if (*buf == ':')
		    break;
		if (*buf != 0)
		    buf++;
	    }
	    while ((*first++ = *buf++) != 0) {
		;
	    }
	    TRACE(("...removed_termcap_entry('%s', '%s')\n", str, base));
	    return;
	} else if (*buf == '\\') {
	    buf++;
	} else if (*buf == ':') {
	    first = buf;
	    count = 0;
	} else if (!isspace(CharOf(*buf))) {
	    count++;
	}
	if (*buf != 0)
	    buf++;
    }
    TRACE(("...cannot remove\n"));
}

/*
 * parse_tty_modes accepts lines of the following form:
 *
 *         [SETTING] ...
 *
 * where setting consists of the words in the modelist followed by a character
 * or ^char.
 */
static int
parse_tty_modes(char *s, struct _xttymodes *modelist)
{
    struct _xttymodes *mp;
    int c;
    int count = 0;

    TRACE(("parse_tty_modes\n"));
    for (;;) {
	size_t len;

	while (*s && isascii(CharOf(*s)) && isspace(CharOf(*s)))
	    s++;
	if (!*s)
	    return count;

	for (len = 0; isalnum(CharOf(s[len])); ++len) ;
	for (mp = modelist; mp->name; mp++) {
	    if (len == mp->len
		&& strncmp(s, mp->name, mp->len) == 0)
		break;
	}
	if (!mp->name)
	    return -1;

	s += mp->len;
	while (*s && isascii(CharOf(*s)) && isspace(CharOf(*s)))
	    s++;
	if (!*s)
	    return -1;

	if ((c = decode_keyvalue(&s, False)) != -1) {
	    mp->value = c;
	    mp->set = 1;
	    count++;
	    TRACE(("...parsed #%d: %s=%#x\n", count, mp->name, c));
	}
    }
}

/* Utility function to try to hide system differences from
   everybody who used to call killpg() */

int
kill_process_group(int pid, int sig)
{
    TRACE(("kill_process_group(pid=%d, sig=%d)\n", pid, sig));
    return killpg(pid, sig);
}
