#include <version.h>
#include <xterm.h>

#include <X11/Xatom.h>
#include <X11/Xutil.h>
#include <X11/cursorfont.h>
#include <X11/Xmu/Atoms.h>
#include <X11/Xmu/CharSet.h>
#include <X11/Xmu/Converters.h>

#include <stdio.h>
#include <ctype.h>
#include <assert.h>

#include <VTparse.h>
#include <data.h>
#include <error.h>
#include <menu.h>
#include <main.h>
#include <fontutils.h>
#include <charclass.h>
#include <xstrings.h>
#include <graphics.h>

typedef int (*BitFunc) (unsigned * /* p */ ,
			unsigned /* mask */ );

static IChar doinput(void);
static int set_character_class(char * /*s */ );
static void FromAlternate(XtermWidget /* xw */ );
static void ReallyReset(XtermWidget /* xw */ ,
			Bool /* full */ ,
			Bool /* saved */ );
static void RequestResize(XtermWidget /* xw */ ,
			  int /* rows */ ,
			  int /* cols */ ,
			  Bool /* text */ );
static void SwitchBufs(XtermWidget /* xw */ ,
		       int /* toBuf */ ,
		       Bool /* clearFirst */ );
static void ToAlternate(XtermWidget /* xw */ ,
			Bool /* clearFirst */ );
static void ansi_modes(XtermWidget termw,
		       BitFunc /* func */ );
static int bitclr(unsigned *p, unsigned mask);
static int bitcpy(unsigned *p, unsigned q, unsigned mask);
static int bitset(unsigned *p, unsigned mask);
static void dpmodes(XtermWidget /* xw */ ,
		    BitFunc /* func */ );
static void restoremodes(XtermWidget /* xw */ );
static void savemodes(XtermWidget /* xw */ );
static void window_ops(XtermWidget /* xw */ );

#define DoStartBlinking(s) ((s)->cursor_blink ^ (s)->cursor_blink_esc)

#define StartBlinking(screen)	/* nothing */
#define StopBlinking(screen)	/* nothing */

#define	DEFAULT		-1
#define BELLSUPPRESSMSEC 200

static ANSI reply;
static PARAMS parms;

#define nparam parms.count

#define InitParams()  parms.count = parms.is_sub[0] = parms.has_subparams = 0
#define GetParam(n)   parms.params[(n)]
#define SetParam(n,v) parms.params[(n)] = v
#define ParamPair(n)  nparam - (n), parms.params + (n)
#define ParamsDone()  InitParams()

static jmp_buf vtjmpbuf;

/* event handlers */
static void HandleBell PROTO_XT_ACTIONS_ARGS;
static void HandleIgnore PROTO_XT_ACTIONS_ARGS;
static void HandleKeymapChange PROTO_XT_ACTIONS_ARGS;
static void HandleVisualBell PROTO_XT_ACTIONS_ARGS;
/*
 * NOTE: VTInitialize zeros out the entire ".screen" component of the
 * XtermWidget, so make sure to add an assignment statement in VTInitialize()
 * for each new ".screen" field added to this resource list.
 */

/* Defaults */

static String _Font_Selected_ = "yes";	/* string is arbitrary */

static const char *defaultTranslations;
/* *INDENT-OFF* */
static XtActionsRec actionsList[] = {
    { "allow-send-events",	HandleAllowSends },
    { "bell",			HandleBell },
    { "clear-saved-lines",	HandleClearSavedLines },
    { "copy-selection",		HandleCopySelection },
    { "create-menu",		HandleCreateMenu },
    { "delete-is-del",		HandleDeleteIsDEL },
    { "dired-button",		DiredButton },
    { "hard-reset",		HandleHardReset },
    { "ignore",			HandleIgnore },
    { "insert",			HandleKeyPressed },  /* alias for insert-seven-bit */
    { "insert-eight-bit",	HandleEightBitKeyPressed },
    { "insert-selection",	HandleInsertSelection },
    { "insert-seven-bit",	HandleKeyPressed },
    { "interpret",		HandleInterpret },
    { "keymap",			HandleKeymapChange },
    { "popup-menu",		HandlePopupMenu },
    { "print",			HandlePrintScreen },
    { "print-everything",	HandlePrintEverything },
    { "print-redir",		HandlePrintControlMode },
    { "quit",			HandleQuit },
    { "redraw",			HandleRedraw },
    { "scroll-back",		HandleScrollBack },
    { "scroll-forw",		HandleScrollForward },
    { "secure",			HandleSecure },
    { "select-cursor-end",	HandleKeyboardSelectEnd },
    { "select-cursor-extend",   HandleKeyboardSelectExtend },
    { "select-cursor-start",	HandleKeyboardSelectStart },
    { "select-end",		HandleSelectEnd },
    { "select-extend",		HandleSelectExtend },
    { "select-set",		HandleSelectSet },
    { "select-start",		HandleSelectStart },
    { "send-signal",		HandleSendSignal },
    { "set-8-bit-control",	Handle8BitControl },
    { "set-allow132",		HandleAllow132 },
    { "set-altscreen",		HandleAltScreen },
    { "set-appcursor",		HandleAppCursor },
    { "set-appkeypad",		HandleAppKeypad },
    { "set-autolinefeed",	HandleAutoLineFeed },
    { "set-autowrap",		HandleAutoWrap },
    { "set-backarrow",		HandleBackarrow },
    { "set-bellIsUrgent",	HandleBellIsUrgent },
    { "set-cursesemul",		HandleCursesEmul },
    { "set-jumpscroll",		HandleJumpscroll },
    { "set-keep-selection",	HandleKeepSelection },
    { "set-marginbell",		HandleMarginBell },
    { "set-old-function-keys",	HandleOldFunctionKeys },
    { "set-pop-on-bell",	HandleSetPopOnBell },
    { "set-reverse-video",	HandleReverseVideo },
    { "set-reversewrap",	HandleReverseWrap },
    { "set-scroll-on-key",	HandleScrollKey },
    { "set-scroll-on-tty-output", HandleScrollTtyOutput },
    { "set-scrollbar",		HandleScrollbar },
    { "set-select",		HandleSetSelect },
    { "set-sun-keyboard",	HandleSunKeyboard },
    { "set-titeInhibit",	HandleTiteInhibit },
    { "set-visual-bell",	HandleSetVisualBell },
    { "set-vt-font",		HandleSetFont },
    { "soft-reset",		HandleSoftReset },
    { "start-cursor-extend",	HandleKeyboardStartExtend },
    { "start-extend",		HandleStartExtend },
    { "string",			HandleStringEvent },
    { "vi-button",		ViButton },
    { "visual-bell",		HandleVisualBell },
};
/* *INDENT-ON* */

#define SPS screen.printer_state

static XtResource xterm_resources[] =
{
    Bres(XtNallowPasteControls, XtCAllowPasteControls,
	 screen.allowPasteControl0, False),
    Bres(XtNallowSendEvents, XtCAllowSendEvents, screen.allowSendEvent0, False),
    Bres(XtNallowColorOps, XtCAllowColorOps, screen.allowColorOp0, DEF_ALLOW_COLOR),
    Bres(XtNallowFontOps, XtCAllowFontOps, screen.allowFontOp0, DEF_ALLOW_FONT),
    Bres(XtNallowTcapOps, XtCAllowTcapOps, screen.allowTcapOp0, DEF_ALLOW_TCAP),
    Bres(XtNallowTitleOps, XtCAllowTitleOps, screen.allowTitleOp0, DEF_ALLOW_TITLE),
    Bres(XtNallowWindowOps, XtCAllowWindowOps, screen.allowWindowOp0, DEF_ALLOW_WINDOW),
    Bres(XtNaltIsNotMeta, XtCAltIsNotMeta, screen.alt_is_not_meta, False),
    Bres(XtNaltSendsEscape, XtCAltSendsEscape, screen.alt_sends_esc, DEF_ALT_SENDS_ESC),
    Bres(XtNallowBoldFonts, XtCAllowBoldFonts, screen.allowBoldFonts, True),
    Bres(XtNalwaysBoldMode, XtCAlwaysBoldMode, screen.always_bold_mode, False),
    Bres(XtNalwaysHighlight, XtCAlwaysHighlight, screen.always_highlight, False),
    Bres(XtNappcursorDefault, XtCAppcursorDefault, misc.appcursorDefault, False),
    Bres(XtNappkeypadDefault, XtCAppkeypadDefault, misc.appkeypadDefault, False),
    Bres(XtNalternateScroll, XtCScrollCond, screen.alternateScroll, False),
    Bres(XtNautoWrap, XtCAutoWrap, misc.autoWrap, True),
    Bres(XtNawaitInput, XtCAwaitInput, screen.awaitInput, False),
    Bres(XtNfreeBoldBox, XtCFreeBoldBox, screen.free_bold_box, False),
    Bres(XtNbackarrowKey, XtCBackarrowKey, screen.backarrow_key, DEF_BACKARO_BS),
    Bres(XtNbellIsUrgent, XtCBellIsUrgent, screen.bellIsUrgent, False),
    Bres(XtNbellOnReset, XtCBellOnReset, screen.bellOnReset, True),
    Bres(XtNboldMode, XtCBoldMode, screen.bold_mode, True),
    Bres(XtNbrokenSelections, XtCBrokenSelections, screen.brokenSelections, False),
    Bres(XtNc132, XtCC132, screen.c132, False),
    Bres(XtNcdXtraScroll, XtCCdXtraScroll, misc.cdXtraScroll, False),
    Bres(XtNcurses, XtCCurses, screen.curses, False),
    Bres(XtNcutNewline, XtCCutNewline, screen.cutNewline, True),
    Bres(XtNcutToBeginningOfLine, XtCCutToBeginningOfLine,
	 screen.cutToBeginningOfLine, True),
    Bres(XtNdeleteIsDEL, XtCDeleteIsDEL, screen.delete_is_del, DEFDELETE_DEL),
    Bres(XtNdynamicColors, XtCDynamicColors, misc.dynamicColors, True),
    Bres(XtNeightBitControl, XtCEightBitControl, screen.control_eight_bits, False),
    Bres(XtNeightBitInput, XtCEightBitInput, screen.input_eight_bits, True),
    Bres(XtNeightBitOutput, XtCEightBitOutput, screen.output_eight_bits, True),
    Bres(XtNhighlightSelection, XtCHighlightSelection,
	 screen.highlight_selection, False),
    Bres(XtNshowWrapMarks, XtCShowWrapMarks, screen.show_wrap_marks, False),
    Bres(XtNhpLowerleftBugCompat, XtCHpLowerleftBugCompat, screen.hp_ll_bc, False),
    Bres(XtNi18nSelections, XtCI18nSelections, screen.i18nSelections, True),
    Bres(XtNfastScroll, XtCFastScroll, screen.fastscroll, False),
    Bres(XtNjumpScroll, XtCJumpScroll, screen.jumpscroll, True),
    Bres(XtNkeepSelection, XtCKeepSelection, screen.keepSelection, True),
    Bres(XtNloginShell, XtCLoginShell, misc.login_shell, False),
    Bres(XtNmarginBell, XtCMarginBell, screen.marginbell, False),
    Bres(XtNmetaSendsEscape, XtCMetaSendsEscape, screen.meta_sends_esc, DEF_META_SENDS_ESC),
    Bres(XtNmultiScroll, XtCMultiScroll, screen.multiscroll, False),
    Bres(XtNoldXtermFKeys, XtCOldXtermFKeys, screen.old_fkeys, False),
    Bres(XtNpopOnBell, XtCPopOnBell, screen.poponbell, False),
    Bres(XtNprinterAutoClose, XtCPrinterAutoClose, SPS.printer_autoclose, False),
    Bres(XtNprinterExtent, XtCPrinterExtent, SPS.printer_extent, False),
    Bres(XtNprinterFormFeed, XtCPrinterFormFeed, SPS.printer_formfeed, False),
    Bres(XtNprinterNewLine, XtCPrinterNewLine, SPS.printer_newline, True),
    Bres(XtNquietGrab, XtCQuietGrab, screen.quiet_grab, False),
    Bres(XtNreverseVideo, XtCReverseVideo, misc.re_verse, False),
    Bres(XtNreverseWrap, XtCReverseWrap, misc.reverseWrap, False),
    Bres(XtNscrollBar, XtCScrollBar, misc.scrollbar, False),
    Bres(XtNscrollKey, XtCScrollCond, screen.scrollkey, False),
    Bres(XtNscrollTtyOutput, XtCScrollCond, screen.scrollttyoutput, True),
    Bres(XtNselectToClipboard, XtCSelectToClipboard,
	 screen.selectToClipboard, False),
    Bres(XtNsignalInhibit, XtCSignalInhibit, misc.signalInhibit, False),
    Bres(XtNtiteInhibit, XtCTiteInhibit, misc.titeInhibit, False),
    Bres(XtNtiXtraScroll, XtCTiXtraScroll, misc.tiXtraScroll, False),
    Bres(XtNtrimSelection, XtCTrimSelection, screen.trim_selection, False),
    Bres(XtNunderLine, XtCUnderLine, screen.underline, True),
    Bres(XtNvisualBell, XtCVisualBell, screen.visualbell, False),
    Bres(XtNvisualBellLine, XtCVisualBellLine, screen.flash_line, False),

    Dres(XtNscaleHeight, XtCScaleHeight, screen.scale_height, "1.0"),

    Ires(XtNbellSuppressTime, XtCBellSuppressTime, screen.bellSuppressTime, BELLSUPPRESSMSEC),
    Ires(XtNfontWarnings, XtCFontWarnings, misc.fontWarnings, fwResource),
    Ires(XtNinternalBorder, XtCBorderWidth, screen.border, DEFBORDER),
    Ires(XtNlimitResize, XtCLimitResize, misc.limit_resize, 1),
    Ires(XtNmultiClickTime, XtCMultiClickTime, screen.multiClickTime, MULTICLICKTIME),
    Ires(XtNnMarginBell, XtCColumn, screen.nmarginbell, N_MARGINBELL),
    Ires(XtNpointerMode, XtCPointerMode, screen.pointer_mode, DEF_POINTER_MODE),
    Ires(XtNprinterControlMode, XtCPrinterControlMode,
	 SPS.printer_controlmode, 0),
    Ires(XtNtitleModes, XtCTitleModes, screen.title_modes, DEF_TITLE_MODES),
    Ires(XtNvisualBellDelay, XtCVisualBellDelay, screen.visualBellDelay, 100),
    Ires(XtNsaveLines, XtCSaveLines, screen.savelines, SAVELINES),
    Ires(XtNscrollBarBorder, XtCScrollBarBorder, screen.scrollBarBorder, 1),
    Ires(XtNscrollLines, XtCScrollLines, screen.scrolllines, SCROLLLINES),

    Sres(XtNinitialFont, XtCInitialFont, screen.initial_font, NULL),
    Sres(XtNfont1, XtCFont1, screen.MenuFontName(fontMenu_font1), NULL),
    Sres(XtNfont2, XtCFont2, screen.MenuFontName(fontMenu_font2), NULL),
    Sres(XtNfont3, XtCFont3, screen.MenuFontName(fontMenu_font3), NULL),
    Sres(XtNfont4, XtCFont4, screen.MenuFontName(fontMenu_font4), NULL),
    Sres(XtNfont5, XtCFont5, screen.MenuFontName(fontMenu_font5), NULL),
    Sres(XtNfont6, XtCFont6, screen.MenuFontName(fontMenu_font6), NULL),

    Sres(XtNanswerbackString, XtCAnswerbackString, screen.answer_back, ""),
    Sres(XtNboldFont, XtCBoldFont, misc.default_font.f_b, DEFBOLDFONT),
    Sres(XtNcharClass, XtCCharClass, screen.charClass, NULL),
    Sres(XtNdecTerminalID, XtCDecTerminalID, screen.term_id, DFT_DECID),
    Sres(XtNdefaultString, XtCDefaultString, screen.default_string, "#"),
    Sres(XtNdisallowedColorOps, XtCDisallowedColorOps,
	 screen.disallowedColorOps, DEF_DISALLOWED_COLOR),
    Sres(XtNdisallowedFontOps, XtCDisallowedFontOps,
	 screen.disallowedFontOps, DEF_DISALLOWED_FONT),
    Sres(XtNdisallowedTcapOps, XtCDisallowedTcapOps,
	 screen.disallowedTcapOps, DEF_DISALLOWED_TCAP),
    Sres(XtNdisallowedWindowOps, XtCDisallowedWindowOps,
	 screen.disallowedWinOps, DEF_DISALLOWED_WINDOW),
    Sres(XtNeightBitMeta, XtCEightBitMeta, screen.eight_bit_meta_s, DEF_8BIT_META),
    Sres(XtNeightBitSelectTypes, XtCEightBitSelectTypes,
	 screen.eightbit_select_types, NULL),
    Sres(XtNfont, XtCFont, misc.default_font.f_n, DEFFONT),
    Sres(XtNgeometry, XtCGeometry, misc.geo_metry, NULL),
    Sres(XtNkeyboardDialect, XtCKeyboardDialect, screen.keyboard_dialect, DFT_KBD_DIALECT),
    Sres(XtNprinterCommand, XtCPrinterCommand, SPS.printer_command, ""),
    Sres(XtNtekGeometry, XtCGeometry, misc.T_geometry, NULL),

    Tres(XtNcursorColor, XtCCursorColor, TEXT_CURSOR, XtDefaultForeground),
    Tres(XtNforeground, XtCForeground, TEXT_FG, XtDefaultForeground),
    Tres(XtNpointerColor, XtCPointerColor, MOUSE_FG, XtDefaultForeground),
    Tres(XtNbackground, XtCBackground, TEXT_BG, XtDefaultBackground),
    Tres(XtNpointerColorBackground, XtCBackground, MOUSE_BG, XtDefaultBackground),

    {XtNresizeGravity, XtCResizeGravity, XtRGravity, sizeof(XtGravity),
     XtOffsetOf(XtermWidgetRec, misc.resizeGravity),
     XtRImmediate, (XtPointer) SouthWestGravity},

    {XtNpointerShape, XtCCursor, XtRCursor, sizeof(Cursor),
     XtOffsetOf(XtermWidgetRec, screen.pointer_cursor),
     XtRString, (XtPointer) "xterm"},

    Bres(XtNcursorUnderLine, XtCCursorUnderLine, screen.cursor_underline, False),

    CLICK_RES("2", screen.onClick[1], "word"),
    CLICK_RES("3", screen.onClick[2], "line"),
    CLICK_RES("4", screen.onClick[3], 0),
    CLICK_RES("5", screen.onClick[4], 0),

};

static Boolean VTSetValues(Widget cur, Widget request, Widget new_arg,
			   ArgList args, Cardinal *num_args);
static void VTClassInit(void);
static void VTDestroy(Widget w);
static void VTExpose(Widget w, XEvent *event, Region region);
static void VTInitialize(Widget wrequest, Widget new_arg, ArgList args,
			 Cardinal *num_args);
static void VTRealize(Widget w, XtValueMask * valuemask,
		      XSetWindowAttributes * values);
static void VTResize(Widget w);


static
WidgetClassRec xtermClassRec =
{
    {
	/* core_class fields */
	(WidgetClass) & widgetClassRec,		/* superclass   */
	"VT100",		/* class_name                   */
	sizeof(XtermWidgetRec),	/* widget_size                  */
	VTClassInit,		/* class_initialize             */
	NULL,			/* class_part_initialize        */
	False,			/* class_inited                 */
	VTInitialize,		/* initialize                   */
	NULL,			/* initialize_hook              */
	VTRealize,		/* realize                      */
	actionsList,		/* actions                      */
	XtNumber(actionsList),	/* num_actions                  */
	xterm_resources,	/* resources                    */
	XtNumber(xterm_resources),	/* num_resources        */
	NULLQUARK,		/* xrm_class                    */
	True,			/* compress_motion              */
	False,			/* compress_exposure            */
	True,			/* compress_enterleave          */
	False,			/* visible_interest             */
	VTDestroy,		/* destroy                      */
	VTResize,		/* resize                       */
	VTExpose,		/* expose                       */
	VTSetValues,		/* set_values                   */
	NULL,			/* set_values_hook              */
	XtInheritSetValuesAlmost,	/* set_values_almost    */
	NULL,			/* get_values_hook              */
	NULL,			/* accept_focus                 */
	XtVersion,		/* version                      */
	NULL,			/* callback_offsets             */
	0,			/* tm_table                     */
	XtInheritQueryGeometry,	/* query_geometry               */
	XtInheritDisplayAccelerator,	/* display_accelerator  */
	NULL			/* extension                    */
    }
};

WidgetClass xtermWidgetClass = (WidgetClass) & xtermClassRec;

/*
 * Add input-actions for widgets that are overlooked (scrollbar and toolbar):
 *
 *	a) Sometimes the scrollbar passes through translations, sometimes it
 *	   doesn't.  We add the KeyPress translations here, just to be sure.
 *	b) In the normal (non-toolbar) configuration, the xterm widget covers
 *	   almost all of the window.  With a toolbar, there's a relatively
 *	   large area that the user would expect to enter keystrokes since the
 *	   program can get the focus.
 */
void
xtermAddInput(Widget w)
{
    /* *INDENT-OFF* */
    XtActionsRec input_actions[] = {
	{ "insert",		    HandleKeyPressed }, /* alias */
	{ "insert-eight-bit",	    HandleEightBitKeyPressed },
	{ "insert-seven-bit",	    HandleKeyPressed },
	{ "secure",		    HandleSecure },
	{ "string",		    HandleStringEvent },
	{ "scroll-back",	    HandleScrollBack },
	{ "scroll-forw",	    HandleScrollForward },
	{ "select-cursor-end",	    HandleKeyboardSelectEnd },
	{ "select-cursor-extend",   HandleKeyboardSelectExtend },
	{ "select-cursor-start",    HandleKeyboardSelectStart },
	{ "insert-selection",	    HandleInsertSelection },
	{ "select-start",	    HandleSelectStart },
	{ "select-extend",	    HandleSelectExtend },
	{ "start-extend",	    HandleStartExtend },
	{ "select-end",		    HandleSelectEnd },
	{ "clear-saved-lines",	    HandleClearSavedLines },
	{ "popup-menu",		    HandlePopupMenu },
	{ "bell",		    HandleBell },
	{ "ignore",		    HandleIgnore },
    };
    /* *INDENT-ON* */

    TRACE_TRANS("BEFORE", w);
    XtAppAddActions(app_con, input_actions, XtNumber(input_actions));
    XtAugmentTranslations(w, XtParseTranslationTable(defaultTranslations));
    TRACE_TRANS("AFTER:", w);

}

void
resetCharsets(TScreen *screen)
{
    TRACE(("resetCharsets\n"));

    screen->gsets[0] = nrc_ASCII;
    screen->gsets[1] = nrc_ASCII;
    screen->gsets[2] = nrc_ASCII;
    screen->gsets[3] = nrc_ASCII;

    screen->curgl = 0;		/* G0 => GL.            */
    screen->curgr = 2;		/* G2 => GR.            */
    screen->curss = 0;		/* No single shift.     */

}

static void
modified_DECNRCM(XtermWidget xw)
{
}

/*
 * VT300 and up support three ANSI conformance levels, defined according to
 * the dpANSI X3.134.1 standard.  DEC's manuals equate levels 1 and 2, and
 * are unclear.  This code is written based on the manuals.
 */
static void
set_ansi_conformance(TScreen *screen, int level)
{
    TRACE(("set_ansi_conformance(%d) dec_level %d:%d, ansi_level %d\n",
	   level,
	   screen->vtXX_level * 100,
	   screen->terminal_id,
	   screen->ansi_level));
    if (screen->vtXX_level >= 3) {
	switch (screen->ansi_level = level) {
	case 1:
	    /* FALLTHRU */
	case 2:
	    screen->gsets[0] = nrc_ASCII;	/* G0 is ASCII */
	    screen->gsets[1] = nrc_ASCII;	/* G1 is ISO Latin-1 */
	    screen->curgl = 0;
	    screen->curgr = 1;
	    break;
	case 3:
	    screen->gsets[0] = nrc_ASCII;	/* G0 is ASCII */
	    screen->curgl = 0;
	    break;
	}
    }
}

/*
 * Set scrolling margins.  VTxxx terminals require that the top/bottom are
 * different, so we have at least two lines in the scrolling region.
 */
void
set_tb_margins(TScreen *screen, int top, int bottom)
{
    TRACE(("set_tb_margins %d..%d, prior %d..%d\n",
	   top, bottom,
	   screen->top_marg,
	   screen->bot_marg));
    if (bottom > top) {
	screen->top_marg = top;
	screen->bot_marg = bottom;
    }
    if (screen->top_marg > screen->max_row)
	screen->top_marg = screen->max_row;
    if (screen->bot_marg > screen->max_row)
	screen->bot_marg = screen->max_row;
}

void
set_lr_margins(TScreen *screen, int left, int right)
{
    TRACE(("set_lr_margins %d..%d, prior %d..%d\n",
	   left, right,
	   screen->lft_marg,
	   screen->rgt_marg));
    if (right > left) {
	screen->lft_marg = left;
	screen->rgt_marg = right;
    }
    if (screen->lft_marg > screen->max_col)
	screen->lft_marg = screen->max_col;
    if (screen->rgt_marg > screen->max_col)
	screen->rgt_marg = screen->max_col;
}

#define reset_tb_margins(screen) set_tb_margins(screen, 0, screen->max_row)
#define reset_lr_margins(screen) set_lr_margins(screen, 0, screen->max_col)

static void
reset_margins(TScreen *screen)
{
    reset_tb_margins(screen);
    reset_lr_margins(screen);
}

void
set_max_col(TScreen *screen, int cols)
{
    TRACE(("set_max_col %d, prior %d\n", cols, screen->max_col));
    if (cols < 0)
	cols = 0;
    screen->max_col = cols;
}

void
set_max_row(TScreen *screen, int rows)
{
    TRACE(("set_max_row %d, prior %d\n", rows, screen->max_row));
    if (rows < 0)
	rows = 0;
    screen->max_row = rows;
}

#define DumpParams()		/* nothing */

	/* allocate larger buffer if needed/possible */
#define SafeAlloc(type, area, used, size) \
		type *new_string = area; \
		size_t new_length = size; \
		if (new_length == 0) { \
		    new_length = 256; \
		    new_string = TypeMallocN(type, new_length); \
		} else if (used+1 >= new_length) { \
		    new_length = size * 2; \
		    new_string = TypeMallocN(type, new_length); \
		    if (new_string != 0 \
		     && area != 0 \
		     && used != 0) \
			memcpy(new_string, area, used * sizeof(type)); \
		}

#define WriteNow() {						\
	    unsigned single = 0;				\
								\
	    if (screen->curss) {				\
		dotext(xw,					\
		       screen->gsets[(int) (screen->curss)],	\
		       sp->print_area,				\
		       (Cardinal) 1);				\
		screen->curss = 0;				\
		single++;					\
	    }							\
	    if (sp->print_used > single) {			\
		dotext(xw,					\
		       screen->gsets[(int) (screen->curgl)],	\
		       sp->print_area + single,			\
		       (Cardinal) (sp->print_used - single));	\
	    }							\
	    sp->print_used = 0;					\
	}							\

struct ParseState {
    Const PARSE_T *groundtable;
    Const PARSE_T *parsestate;
    int scstype;
    int scssize;
    Bool private_function;	/* distinguish private-mode from standard */
    int string_mode;		/* nonzero iff we're processing a string */
    int lastchar;		/* positive iff we had a graphic character */
    int nextstate;
    /* Buffer for processing printable text */
    IChar *print_area;
    size_t print_size;
    size_t print_used;
    /* Buffer for processing strings (e.g., OSC ... ST) */
    Char *string_area;
    size_t string_size;
    size_t string_used;
};

static struct ParseState myState;

static void
init_groundtable(TScreen *screen, struct ParseState *sp)
{
    (void) screen;

    {
	sp->groundtable = ansi_table;
    }
}

static void
select_charset(struct ParseState *sp, int type, int size)
{
    TRACE(("select_charset %d %d\n", type, size));
    sp->scstype = type;
    sp->scssize = size;
    if (size == 94) {
	sp->parsestate = scstable;
    } else {
	sp->parsestate = scs96table;
    }
}

static void
decode_scs(XtermWidget xw, int which, int prefix, int suffix)
{
    /* *INDENT-OFF* */
    static struct {
	DECNRCM_codes result;
	int prefix;
	int suffix;
	int min_level;
	int max_level;
	int need_nrc;
    } table[] = {
	{ nrc_ASCII,             0,   'B', 1, 9, 0 },
	{ nrc_British,           0,   'A', 1, 9, 0 },
	{ nrc_DEC_Spec_Graphic,  0,   '0', 1, 9, 0 },
	{ nrc_DEC_Alt_Chars,     0,   '1', 1, 1, 0 },
	{ nrc_DEC_Alt_Graphics,  0,   '2', 1, 1, 0 },
	/* VT2xx */
	{ nrc_DEC_Supp,          0,   '<', 2, 9, 0 },
	{ nrc_Dutch,             0,   '4', 2, 9, 1 },
	{ nrc_Finnish,           0,   '5', 2, 9, 1 },
	{ nrc_Finnish2,          0,   'C', 2, 9, 1 },
	{ nrc_French,            0,   'R', 2, 9, 1 },
	{ nrc_French2,           0,   'f', 2, 9, 1 },
	{ nrc_French_Canadian,   0,   'Q', 2, 9, 1 },
	{ nrc_German,            0,   'K', 2, 9, 1 },
	{ nrc_Italian,           0,   'Y', 2, 9, 1 },
	{ nrc_Norwegian_Danish2, 0,   'E', 2, 9, 1 },
	{ nrc_Norwegian_Danish3, 0,   '6', 2, 9, 1 },
	{ nrc_Spanish,           0,   'Z', 2, 9, 1 },
	{ nrc_Swedish,           0,   '7', 2, 9, 1 },
	{ nrc_Swedish2,          0,   'H', 2, 9, 1 },
	{ nrc_Swiss,             0,   '=', 2, 9, 1 },
	/* VT3xx */
	{ nrc_British_Latin_1,   0,   'A', 3, 9, 1 },
	{ nrc_DEC_Supp_Graphic,  '%', '5', 3, 9, 0 },
	{ nrc_DEC_Technical,     0,   '>', 3, 9, 0 },
	{ nrc_French_Canadian2,  0,   '9', 3, 9, 1 },
	{ nrc_Norwegian_Danish,  0,   '`', 3, 9, 1 },
	{ nrc_Portugese,         '%', '6', 3, 9, 1 },
    };
    /* *INDENT-ON* */

    TScreen *screen = TScreenOf(xw);
    Cardinal n;
    DECNRCM_codes result = nrc_Unknown;

    suffix &= 0x7f;
    for (n = 0; n < XtNumber(table); ++n) {
	if (prefix == table[n].prefix
	    && suffix == table[n].suffix
	    && screen->vtXX_level >= table[n].min_level
	    && screen->vtXX_level <= table[n].max_level
	    && (table[n].need_nrc == 0 || (xw->flags & NATIONAL) != 0)) {
	    result = table[n].result;
	    break;
	}
    }
    if (result != nrc_Unknown) {
	screen->gsets[which] = result;
	TRACE(("setting G%d to %s\n", which, visibleScsCode((int) result)));
    } else {
	TRACE(("...unknown GSET\n"));
    }
}

/*
 * Given a parameter number, and subparameter (starting in each case from zero)
 * return the corresponding index into the parameter array.  If the combination
 * is not found, return -1.
 */
static int
subparam_index(int p, int s)
{
    int result = -1;
    int j, p2, s2;

    for (j = p2 = 0; j < nparam; ++j, ++p2) {
	if (parms.is_sub[j]) {
	    s2 = 0;

	    do {
		if ((p == p2) && (s == s2)) {
		    result = j;
		    break;
		}
		++s2;
	    } while ((++j < nparam) && (parms.is_sub[j - 1] < parms.is_sub[j]));

	    if (result >= 0)
		break;

	    --j;		/* undo the last "while" */
	} else if (p == p2) {
	    if (s == 0) {
		result = j;
	    }
	    break;
	}
    }
    TRACE2(("...subparam_index %d.%d = %d\n", p + 1, s + 1, result));
    return result;
}

/*
 * Check if the given item in the parameter array has subparameters.
 * If so, return the number of subparameters to use as a loop limit, etc.
 */
static int
param_has_subparams(int item)
{
    int result = 0;
    if (parms.has_subparams) {
	int n = subparam_index(item, 0);
	if (n >= 0 && parms.is_sub[n]) {
	    while (n++ < nparam && parms.is_sub[n - 1] < parms.is_sub[n]) {
		result++;
	    }
	}
    }
    TRACE(("...param_has_subparams(%d) ->%d\n", item, result));
    return result;
}

static int
optional_param(int which)
{
    return (nparam > which) ? GetParam(which) : DEFAULT;
}

static int
zero_if_default(int which)
{
    int result = (nparam > which) ? GetParam(which) : 0;
    if (result <= 0)
	result = 0;
    return result;
}

static int
one_if_default(int which)
{
    int result = (nparam > which) ? GetParam(which) : 0;
    if (result <= 0)
	result = 1;
    return result;
}

/*
 * Color palette changes using the OSC controls require a repaint of the
 * screen - but not immediately.  Do the repaint as soon as we detect a
 * state which will not lead to another color palette change.
 */
static void
repaintWhenPaletteChanged(XtermWidget xw, struct ParseState *sp)
{
    Boolean ignore = False;

    switch (sp->nextstate) {
    case CASE_ESC:
	ignore = ((sp->parsestate == ansi_table) ||
		  (sp->parsestate == sos_table));
	break;
    case CASE_OSC:
	ignore = ((sp->parsestate == ansi_table) ||
		  (sp->parsestate == esc_table));
	break;
    case CASE_IGNORE:
	ignore = (sp->parsestate == sos_table);
	break;
    case CASE_ST:
	ignore = ((sp->parsestate == esc_table) ||
		  (sp->parsestate == sos_table));
	break;
    case CASE_ESC_DIGIT:
	ignore = (sp->parsestate == csi_table);
	break;
    case CASE_ESC_SEMI:
	ignore = (sp->parsestate == csi2_table);
	break;
    }

    if (!ignore) {
	TRACE(("repaintWhenPaletteChanged\n"));
	xw->misc.palette_changed = False;
	xtermRepaint(xw);
    }
}

#define ParseSOS(screen) 0

#define ResetState(sp) ParamsDone(), (sp)->parsestate = (sp)->groundtable

static void
illegal_parse(XtermWidget xw, unsigned c, struct ParseState *sp)
{
    ResetState(sp);
    sp->nextstate = sp->parsestate[E2A(c)];
    Bell(xw, XkbBI_MinorError, 0);
}

static void
init_parser(XtermWidget xw, struct ParseState *sp)
{
    TScreen *screen = TScreenOf(xw);

    memset(sp, 0, sizeof(*sp));
    sp->scssize = 94;		/* number of printable/nonspace ASCII */
    sp->lastchar = -1;		/* not a legal IChar */
    sp->nextstate = -1;		/* not a legal state */

    init_groundtable(screen, sp);
    ResetState(sp);
}

static void
init_reply(unsigned type)
{
    memset(&reply, 0, sizeof(reply));
    reply.a_type = (Char) type;
}

static Boolean
doparsing(XtermWidget xw, unsigned c, struct ParseState *sp)
{
    TScreen *screen = TScreenOf(xw);
    int item;
    int count;
    int value;
    int laststate;
    int thischar = -1;
    XTermRect myRect;

    do {

	/* Intercept characters for printer controller mode */
	if (PrinterOf(screen).printer_controlmode == 2) {
	    if ((c = (unsigned) xtermPrinterControl(xw, (int) c)) == 0)
		continue;
	}

	/*
	 * VT52 is a little ugly in the one place it has a parameterized
	 * control sequence, since the parameter falls after the character
	 * that denotes the type of sequence.
	 */

	laststate = sp->nextstate;
	if (c == ANSI_DEL
	    && sp->parsestate == sp->groundtable
	    && sp->scssize == 96
	    && sp->scstype != 0) {
	    /*
	     * Handle special case of shifts for 96-character sets by checking
	     * if we have a DEL.  The other special case for SPACE will always
	     * be printable.
	     */
	    sp->nextstate = CASE_PRINT;
	} else
	    sp->nextstate = sp->parsestate[E2A(c)];

	/*
	 * Accumulate string for printable text.  This may be 8/16-bit
	 * characters.
	 */
	if (sp->nextstate == CASE_PRINT) {
	    SafeAlloc(IChar, sp->print_area, sp->print_used, sp->print_size);
	    if (new_string == 0) {
		xtermWarning("Cannot allocate %lu bytes for printable text\n",
			     (unsigned long) new_length);
		continue;
	    }
	    sp->print_area = new_string;
	    sp->print_size = new_length;
	    sp->print_area[sp->print_used++] = (IChar) c;
	    sp->lastchar = thischar = (int) c;
	    if (morePtyData(screen, VTbuffer)) {
		continue;
	    }
	}

	if (sp->nextstate == CASE_PRINT
	    || (laststate == CASE_PRINT && sp->print_used)) {
	    WriteNow();
	}

	/*
	 * Accumulate string for APC, DCS, PM, OSC, SOS controls
	 * This should always be 8-bit characters.
	 */
	if (sp->parsestate == sos_table) {
	    SafeAlloc(Char, sp->string_area, sp->string_used, sp->string_size);
	    if (new_string == 0) {
		xtermWarning("Cannot allocate %lu bytes for string mode %d\n",
			     (unsigned long) new_length, sp->string_mode);
		continue;
	    }
	    if (sp->string_area != new_string) {
		free(sp->string_area);
	    }
	    sp->string_area = new_string;
	    sp->string_size = new_length;
	    sp->string_area[(sp->string_used)++] = CharOf(c);
	} else if (sp->parsestate != esc_table) {
	    /* if we were accumulating, we're not any more */
	    sp->string_mode = 0;
	    sp->string_used = 0;
	}

	DumpParams();
	TRACE(("parse %04X -> %d %s (used=%lu)\n",
	       c, sp->nextstate,
	       which_table(sp->parsestate),
	       (unsigned long) sp->string_used));

	/*
	 * If the parameter list has subparameters (tokens separated by ":")
	 * reject any controls that do not accept subparameters.
	 */
	if (parms.has_subparams) {
	    switch (sp->nextstate) {
	    case CASE_GROUND_STATE:
	    case CASE_CSI_IGNORE:
		/* FALLTHRU */

	    case CASE_ESC_DIGIT:
	    case CASE_ESC_SEMI:
	    case CASE_ESC_COLON:
		/* these states are required to parse parameter lists */
		break;

	    case CASE_SGR:
		TRACE(("...possible subparam usage\n"));
		break;

	    case CASE_CSI_DEC_DOLLAR_STATE:
	    case CASE_CSI_DOLLAR_STATE:
	    case CASE_CSI_EX_STATE:
	    case CASE_CSI_QUOTE_STATE:
	    case CASE_CSI_SPACE_STATE:
	    case CASE_CSI_STAR_STATE:
	    case CASE_CSI_TICK_STATE:
	    case CASE_DEC2_STATE:
	    case CASE_DEC3_STATE:
	    case CASE_DEC_STATE:
		/* use this branch when we do not yet have the final character */
		TRACE(("...unexpected subparam usage\n"));
		ParamsDone();
		sp->nextstate = CASE_CSI_IGNORE;
		break;

	    default:
		/* use this branch for cases where we have the final character
		 * in the table that processed the parameter list.
		 */
		TRACE(("...unexpected subparam usage\n"));
		ResetState(sp);
		continue;
	    }
	}

	if (xw->misc.palette_changed) {
	    repaintWhenPaletteChanged(xw, sp);
	}

	switch (sp->nextstate) {
	case CASE_PRINT:
	    TRACE(("CASE_PRINT - printable characters\n"));
	    break;

	case CASE_GROUND_STATE:
	    TRACE(("CASE_GROUND_STATE - exit ignore mode\n"));
	    ResetState(sp);
	    break;

	case CASE_IGNORE:
	    TRACE(("CASE_IGNORE - Ignore character %02X\n", c));
	    break;

	case CASE_ENQ:
	    TRACE(("CASE_ENQ - answerback\n"));
	    for (count = 0; screen->answer_back[count] != 0; count++)
		unparseputc(xw, screen->answer_back[count]);
	    unparse_end(xw);
	    break;

	case CASE_BELL:
	    TRACE(("CASE_BELL - bell\n"));
	    if (sp->string_mode == ANSI_OSC) {
		if (sp->string_used)
		    sp->string_area[--(sp->string_used)] = '\0';
		do_osc(xw, sp->string_area, sp->string_used, (int) c);
		ResetState(sp);
	    } else {
		/* bell */
		Bell(xw, XkbBI_TerminalBell, 0);
	    }
	    break;

	case CASE_BS:
	    TRACE(("CASE_BS - backspace\n"));
	    CursorBack(xw, 1);
	    break;

	case CASE_CR:
	    TRACE(("CASE_CR\n"));
	    CarriageReturn(xw);
	    break;

	case CASE_ESC:
	    if_OPT_VT52_MODE(screen, {
		sp->parsestate = vt52_esc_table;
		break;
	    });
	    sp->parsestate = esc_table;
	    break;


	case CASE_VMOT:
	    TRACE(("CASE_VMOT\n"));
	    /*
	     * form feed, line feed, vertical tab
	     */
	    xtermAutoPrint(xw, c);
	    xtermIndex(xw, 1);
	    if (xw->flags & LINEFEED)
		CarriageReturn(xw);
	    else
		do_xevents();
	    break;

	case CASE_CBT:
	    TRACE(("CASE_CBT\n"));
	    /* cursor backward tabulation */
	    count = one_if_default(0);
	    while ((count-- > 0)
		   && (TabToPrevStop(xw))) ;
	    ResetState(sp);
	    break;

	case CASE_CHT:
	    TRACE(("CASE_CHT\n"));
	    /* cursor forward tabulation */
	    count = one_if_default(0);
	    while ((count-- > 0)
		   && (TabToNextStop(xw))) ;
	    ResetState(sp);
	    break;

	case CASE_TAB:
	    /* tab */
	    TabToNextStop(xw);
	    break;

	case CASE_SI:
	    screen->curgl = 0;
	    if_OPT_VT52_MODE(screen, {
		ResetState(sp);
	    });
	    break;

	case CASE_SO:
	    screen->curgl = 1;
	    if_OPT_VT52_MODE(screen, {
		ResetState(sp);
	    });
	    break;

	case CASE_DECDHL:
	    xterm_DECDHL(xw, c == '3');
	    ResetState(sp);
	    break;

	case CASE_DECSWL:
	    xterm_DECSWL(xw);
	    ResetState(sp);
	    break;

	case CASE_DECDWL:
	    xterm_DECDWL(xw);
	    ResetState(sp);
	    break;

	case CASE_SCR_STATE:
	    /* enter scr state */
	    sp->parsestate = scrtable;
	    break;

	case CASE_SCS0_STATE:
	    /* enter scs state 0 */
	    select_charset(sp, 0, 94);
	    break;

	case CASE_SCS1_STATE:
	    /* enter scs state 1 */
	    select_charset(sp, 1, 94);
	    break;

	case CASE_SCS2_STATE:
	    /* enter scs state 2 */
	    select_charset(sp, 2, 94);
	    break;

	case CASE_SCS3_STATE:
	    /* enter scs state 3 */
	    select_charset(sp, 3, 94);
	    break;

	case CASE_SCS1A_STATE:
	    /* enter scs state 1 */
	    select_charset(sp, 1, 96);
	    break;

	case CASE_SCS2A_STATE:
	    /* enter scs state 2 */
	    select_charset(sp, 2, 96);
	    break;

	case CASE_SCS3A_STATE:
	    /* enter scs state 3 */
	    select_charset(sp, 3, 96);
	    break;

	case CASE_ESC_IGNORE:
	    /* unknown escape sequence */
	    sp->parsestate = eigtable;
	    break;

	case CASE_ESC_DIGIT:
	    /* digit in csi or dec mode */
	    if (nparam > 0) {
		value = zero_if_default(nparam - 1);
		SetParam(nparam - 1, (10 * value) + ((int) c - '0'));
		if (GetParam(nparam - 1) > 65535)
		    SetParam(nparam - 1, 65535);
		if (sp->parsestate == csi_table)
		    sp->parsestate = csi2_table;
	    }
	    break;

	case CASE_ESC_SEMI:
	    /* semicolon in csi or dec mode */
	    if (nparam < NPARAM) {
		parms.is_sub[nparam] = 0;
		SetParam(nparam++, DEFAULT);
	    }
	    if (sp->parsestate == csi_table)
		sp->parsestate = csi2_table;
	    break;

	    /*
	     * A _few_ commands accept colon-separated subparameters.
	     * Mark the parameter list so that we can exclude (most) bogus
	     * commands with simple/fast checks.
	     */
	case CASE_ESC_COLON:
	    if (nparam < NPARAM) {
		parms.has_subparams = 1;
		if (nparam == 0) {
		    parms.is_sub[nparam] = 1;
		    SetParam(nparam++, DEFAULT);
		} else if (parms.is_sub[nparam - 1] == 0) {
		    parms.is_sub[nparam - 1] = 1;
		    parms.is_sub[nparam] = 2;
		    parms.params[nparam] = 0;
		    ++nparam;
		} else {
		    parms.is_sub[nparam] = 1 + parms.is_sub[nparam - 1];
		    parms.params[nparam] = 0;
		    ++nparam;
		}
	    }
	    break;

	case CASE_DEC_STATE:
	    /* enter dec mode */
	    sp->parsestate = dec_table;
	    break;

	case CASE_DEC2_STATE:
	    /* enter dec2 mode */
	    sp->parsestate = dec2_table;
	    break;

	case CASE_DEC3_STATE:
	    /* enter dec3 mode */
	    sp->parsestate = dec3_table;
	    break;

	case CASE_ICH:
	    TRACE(("CASE_ICH - insert char\n"));
	    InsertChar(xw, (unsigned) one_if_default(0));
	    ResetState(sp);
	    break;

	case CASE_CUU:
	    TRACE(("CASE_CUU - cursor up\n"));
	    CursorUp(screen, one_if_default(0));
	    ResetState(sp);
	    break;

	case CASE_CUD:
	    TRACE(("CASE_CUD - cursor down\n"));
	    CursorDown(screen, one_if_default(0));
	    ResetState(sp);
	    break;

	case CASE_CUF:
	    TRACE(("CASE_CUF - cursor forward\n"));
	    CursorForward(xw, one_if_default(0));
	    ResetState(sp);
	    break;

	case CASE_CUB:
	    TRACE(("CASE_CUB - cursor backward\n"));
	    CursorBack(xw, one_if_default(0));
	    ResetState(sp);
	    break;

	case CASE_CUP:
	    TRACE(("CASE_CUP - cursor position\n"));
	    if_OPT_XMC_GLITCH(screen, {
		Jump_XMC(xw);
	    });
	    CursorSet(screen, one_if_default(0) - 1, one_if_default(1) - 1, xw->flags);
	    ResetState(sp);
	    break;

	case CASE_VPA:
	    TRACE(("CASE_VPA - vertical position absolute\n"));
	    CursorSet(screen, one_if_default(0) - 1, CursorCol(xw), xw->flags);
	    ResetState(sp);
	    break;

	case CASE_HPA:
	    TRACE(("CASE_HPA - horizontal position absolute\n"));
	    CursorSet(screen, CursorRow(xw), one_if_default(0) - 1, xw->flags);
	    ResetState(sp);
	    break;

	case CASE_VPR:
	    TRACE(("CASE_VPR - vertical position relative\n"));
	    CursorSet(screen,
		      CursorRow(xw) + one_if_default(0),
		      CursorCol(xw),
		      xw->flags);
	    ResetState(sp);
	    break;

	case CASE_HPR:
	    TRACE(("CASE_HPR - horizontal position relative\n"));
	    CursorSet(screen,
		      CursorRow(xw),
		      CursorCol(xw) + one_if_default(0),
		      xw->flags);
	    ResetState(sp);
	    break;

	case CASE_HP_BUGGY_LL:
	    TRACE(("CASE_HP_BUGGY_LL\n"));
	    /* Some HP-UX applications have the bug that they
	       assume ESC F goes to the lower left corner of
	       the screen, regardless of what terminfo says. */
	    if (screen->hp_ll_bc)
		CursorSet(screen, screen->max_row, 0, xw->flags);
	    ResetState(sp);
	    break;

	case CASE_ED:
	    TRACE(("CASE_ED - erase display\n"));
	    do_cd_xtra_scroll(xw);
	    do_erase_display(xw, zero_if_default(0), OFF_PROTECT);
	    ResetState(sp);
	    break;

	case CASE_EL:
	    TRACE(("CASE_EL - erase line\n"));
	    do_erase_line(xw, zero_if_default(0), OFF_PROTECT);
	    ResetState(sp);
	    break;

	case CASE_ECH:
	    TRACE(("CASE_ECH - erase char\n"));
	    /* ECH */
	    ClearRight(xw, one_if_default(0));
	    ResetState(sp);
	    break;

	case CASE_IL:
	    TRACE(("CASE_IL - insert line\n"));
	    set_cur_col(screen, ScrnLeftMargin(xw));
	    InsertLine(xw, one_if_default(0));
	    ResetState(sp);
	    break;

	case CASE_DL:
	    TRACE(("CASE_DL - delete line\n"));
	    set_cur_col(screen, ScrnLeftMargin(xw));
	    DeleteLine(xw, one_if_default(0));
	    ResetState(sp);
	    break;

	case CASE_DCH:
	    TRACE(("CASE_DCH - delete char\n"));
	    DeleteChar(xw, (unsigned) one_if_default(0));
	    ResetState(sp);
	    break;

	case CASE_TRACK_MOUSE:
	    /*
	     * A single parameter other than zero is always scroll-down.
	     * A zero-parameter is used to reset the mouse mode, and is
	     * not useful for scrolling anyway.
	     */
	    if (nparam > 1 || GetParam(0) == 0) {
		CELL start;

		TRACE(("CASE_TRACK_MOUSE\n"));
		/* Track mouse as long as in window and between
		 * specified rows
		 */
		start.row = one_if_default(2) - 1;
		start.col = GetParam(1) - 1;
		TrackMouse(xw,
			   GetParam(0),
			   &start,
			   GetParam(3) - 1, GetParam(4) - 2);
	    } else {
		TRACE(("CASE_SD - scroll down\n"));
		/* SD */
		RevScroll(xw, one_if_default(0));
		do_xevents();
	    }
	    ResetState(sp);
	    break;

	case CASE_DECID:
	    TRACE(("CASE_DECID\n"));
	    if_OPT_VT52_MODE(screen, {
		unparseputc(xw, ANSI_ESC);
		unparseputc(xw, '/');
		unparseputc(xw, 'Z');
		unparse_end(xw);
		ResetState(sp);
		break;
	    });
	    SetParam(0, DEFAULT);	/* Default ID parameter */
	    /* FALLTHRU */
	case CASE_DA1:
	    TRACE(("CASE_DA1\n"));
	    if (GetParam(0) <= 0) {	/* less than means DEFAULT */
		count = 0;
		init_reply(ANSI_CSI);
		reply.a_pintro = '?';

		/*
		 * The first parameter corresponds to the highest operating
		 * level (i.e., service level) of the emulation.  A DEC
		 * terminal can be setup to respond with a different DA
		 * response, but there's no control sequence that modifies
		 * this.  We set it via a resource.
		 */
		if (screen->terminal_id < 200) {
		    switch (screen->terminal_id) {
		    case 125:
			reply.a_param[count++] = 12;	/* VT125 */
			reply.a_param[count++] = 0 | 2 | 0;	/* no STP, AVO, no GPO (ReGIS) */
			reply.a_param[count++] = 0;	/* no printer */
			reply.a_param[count++] = XTERM_PATCH;	/* ROM version */
			break;
		    case 102:
			reply.a_param[count++] = 6;	/* VT102 */
			break;
		    case 101:
			reply.a_param[count++] = 1;	/* VT101 */
			reply.a_param[count++] = 0;	/* no options */
			break;
		    default:	/* VT100 */
			reply.a_param[count++] = 1;	/* VT100 */
			reply.a_param[count++] = 0 | 2 | 0;	/* no STP, AVO, no GPO (ReGIS) */
			break;
		    }
		} else {
		    reply.a_param[count++] = (ParmType) (60
							 + screen->terminal_id
							 / 100);
		    reply.a_param[count++] = 1;		/* 132-columns */
		    reply.a_param[count++] = 2;		/* printer */
		    reply.a_param[count++] = 6;		/* selective-erase */
			reply.a_param[count++] = 8;	/* user-defined-keys */
		    reply.a_param[count++] = 9;		/* national replacement charsets */
		    reply.a_param[count++] = 15;	/* technical characters */
		    if (screen->terminal_id >= 400) {
			reply.a_param[count++] = 18;	/* windowing capability */
			reply.a_param[count++] = 21;	/* horizontal scrolling */
		    }
		    if_OPT_ISO_COLORS(screen, {
			reply.a_param[count++] = 22;	/* ANSI color, VT525 */
		    });
		}
		reply.a_nparam = (ParmType) count;
		reply.a_inters = 0;
		reply.a_final = 'c';
		unparseseq(xw, &reply);
	    }
	    ResetState(sp);
	    break;

	case CASE_DA2:
	    TRACE(("CASE_DA2\n"));
	    if (GetParam(0) <= 0) {	/* less than means DEFAULT */
		count = 0;
		init_reply(ANSI_CSI);
		reply.a_pintro = '>';

		if (screen->terminal_id >= 200) {
		    switch (screen->terminal_id) {
		    case 220:
		    default:
			reply.a_param[count++] = 1;	/* VT220 */
			break;
		    case 240:
			/* http://www.decuslib.com/DECUS/vax87a/gendyn/vt200_kind.lis */
			reply.a_param[count++] = 2;	/* VT240 */
			break;
		    case 320:
			/* http://www.vt100.net/docs/vt320-uu/appendixe.html */
			reply.a_param[count++] = 24;	/* VT320 */
			break;
		    case 330:
			reply.a_param[count++] = 18;	/* VT330 */
			break;
		    case 340:
			reply.a_param[count++] = 19;	/* VT340 */
			break;
		    case 420:
			reply.a_param[count++] = 41;	/* VT420 */
			break;
		    case 510:
			/* http://www.vt100.net/docs/vt510-rm/DA2 */
			reply.a_param[count++] = 61;	/* VT510 */
			break;
		    case 520:
			reply.a_param[count++] = 64;	/* VT520 */
			break;
		    case 525:
			reply.a_param[count++] = 65;	/* VT525 */
			break;
		    }
		} else {
		    reply.a_param[count++] = 0;		/* VT100 (nonstandard) */
		}
		reply.a_param[count++] = XTERM_PATCH;	/* Version */
		reply.a_param[count++] = 0;	/* options (none) */
		reply.a_nparam = (ParmType) count;
		reply.a_inters = 0;
		reply.a_final = 'c';
		unparseseq(xw, &reply);
	    }
	    ResetState(sp);
	    break;

	case CASE_DECRPTUI:
	    TRACE(("CASE_DECRPTUI\n"));
	    if ((screen->vtXX_level >= 4)
		&& (GetParam(0) <= 0)) {	/* less than means DEFAULT */
		unparseputc1(xw, ANSI_DCS);
		unparseputc(xw, '!');
		unparseputc(xw, '|');
		unparseputc(xw, '0');
		unparseputc1(xw, ANSI_ST);
		unparse_end(xw);
	    }
	    ResetState(sp);
	    break;

	case CASE_TBC:
	    TRACE(("CASE_TBC - tab clear\n"));
	    if ((value = GetParam(0)) <= 0)	/* less than means default */
		TabClear(xw->tabs, screen->cur_col);
	    else if (value == 3)
		TabZonk(xw->tabs);
	    ResetState(sp);
	    break;

	case CASE_SET:
	    TRACE(("CASE_SET - set mode\n"));
	    ansi_modes(xw, bitset);
	    ResetState(sp);
	    break;

	case CASE_RST:
	    TRACE(("CASE_RST - reset mode\n"));
	    ansi_modes(xw, bitclr);
	    ResetState(sp);
	    break;

	case CASE_SGR:
	    for (item = 0; item < nparam; ++item) {
		int op = GetParam(item);

		if_OPT_XMC_GLITCH(screen, {
		    Mark_XMC(xw, op);
		});
		TRACE(("CASE_SGR %d\n", op));

		/*
		 * Only SGR 38/48 accept subparameters, and in those cases
		 * the values will not be seen at this point.
		 */
		if (param_has_subparams(item)) {
		    switch (op) {
		    case 38:
			/* FALLTHRU */
		    case 48:
			if_OPT_ISO_COLORS(screen, {
			    break;
			});
		    default:
			TRACE(("...unexpected subparameter in SGR\n"));
			op = 9999;
			ResetState(sp);
			break;
		    }
		}

		switch (op) {
		case DEFAULT:
		    /* FALLTHRU */
		case 0:
		    UIntClr(xw->flags,
			    (SGR_MASK | SGR_MASK2 | INVISIBLE));
		    if_OPT_ISO_COLORS(screen, {
			reset_SGR_Colors(xw);
		    });
		    break;
		case 1:	/* Bold                 */
		    UIntSet(xw->flags, BOLD);
		    if_OPT_ISO_COLORS(screen, {
			setExtendedFG(xw);
		    });
		    break;
		case 4:	/* Underscore           */
		    UIntSet(xw->flags, UNDERLINE);
		    if_OPT_ISO_COLORS(screen, {
			setExtendedFG(xw);
		    });
		    break;
		case 5:	/* Blink                */
		    UIntSet(xw->flags, BLINK);
		    StartBlinking(screen);
		    if_OPT_ISO_COLORS(screen, {
			setExtendedFG(xw);
		    });
		    break;
		case 7:
		    UIntSet(xw->flags, INVERSE);
		    if_OPT_ISO_COLORS(screen, {
			setExtendedBG(xw);
		    });
		    break;
		case 8:
		    UIntSet(xw->flags, INVISIBLE);
		    break;
		case 22:	/* reset 'bold' */
		    UIntClr(xw->flags, BOLD);
		    if_OPT_ISO_COLORS(screen, {
			setExtendedFG(xw);
		    });
		    break;
		case 24:
		    UIntClr(xw->flags, UNDERLINE);
		    if_OPT_ISO_COLORS(screen, {
			setExtendedFG(xw);
		    });
		    break;
		case 25:	/* reset 'blink' */
		    UIntClr(xw->flags, BLINK);
		    if_OPT_ISO_COLORS(screen, {
			setExtendedFG(xw);
		    });
		    break;
		case 27:
		    UIntClr(xw->flags, INVERSE);
		    if_OPT_ISO_COLORS(screen, {
			setExtendedBG(xw);
		    });
		    break;
		case 28:
		    UIntClr(xw->flags, INVISIBLE);
		    break;
		case 30:
		    /* FALLTHRU */
		case 31:
		    /* FALLTHRU */
		case 32:
		    /* FALLTHRU */
		case 33:
		    /* FALLTHRU */
		case 34:
		    /* FALLTHRU */
		case 35:
		    /* FALLTHRU */
		case 36:
		    /* FALLTHRU */
		case 37:
		    if_OPT_ISO_COLORS(screen, {
			xw->sgr_foreground = (op - 30);
			xw->sgr_extended = False;
			setExtendedFG(xw);
		    });
		    break;
		case 38:
		    /* This is more complicated than I'd like, but it should
		     * properly eat all the parameters for unsupported modes.
		     */
		    if_OPT_ISO_COLORS(screen, {
			if (parse_extended_colors(xw, &value, &item)) {
			    xw->sgr_foreground = value;
			    xw->sgr_extended = True;
			    setExtendedFG(xw);
			}
		    });
		    break;
		case 39:
		    if_OPT_ISO_COLORS(screen, {
			reset_SGR_Foreground(xw);
		    });
		    break;
		case 40:
		    /* FALLTHRU */
		case 41:
		    /* FALLTHRU */
		case 42:
		    /* FALLTHRU */
		case 43:
		    /* FALLTHRU */
		case 44:
		    /* FALLTHRU */
		case 45:
		    /* FALLTHRU */
		case 46:
		    /* FALLTHRU */
		case 47:
		    if_OPT_ISO_COLORS(screen, {
			xw->sgr_background = (op - 40);
			setExtendedBG(xw);
		    });
		    break;
		case 48:
		    if_OPT_ISO_COLORS(screen, {
			if (parse_extended_colors(xw, &value, &item)) {
			    xw->sgr_background = value;
			    setExtendedBG(xw);
			}
		    });
		    break;
		case 49:
		    if_OPT_ISO_COLORS(screen, {
			reset_SGR_Background(xw);
		    });
		    break;
		case 90:
		    /* FALLTHRU */
		case 91:
		    /* FALLTHRU */
		case 92:
		    /* FALLTHRU */
		case 93:
		    /* FALLTHRU */
		case 94:
		    /* FALLTHRU */
		case 95:
		    /* FALLTHRU */
		case 96:
		    /* FALLTHRU */
		case 97:
		    if_OPT_AIX_COLORS(screen, {
			xw->sgr_foreground = (op - 90 + 8);
			xw->sgr_extended = False;
			setExtendedFG(xw);
		    });
		    break;
		case 100:
		case 101:
		    /* FALLTHRU */
		case 102:
		    /* FALLTHRU */
		case 103:
		    /* FALLTHRU */
		case 104:
		    /* FALLTHRU */
		case 105:
		    /* FALLTHRU */
		case 106:
		    /* FALLTHRU */
		case 107:
		    if_OPT_AIX_COLORS(screen, {
			xw->sgr_background = (op - 100 + 8);
			setExtendedBG(xw);
		    });
		    break;
		}
	    }
	    ResetState(sp);
	    break;

	    /* DSR (except for the '?') is a superset of CPR */
	case CASE_DSR:
	    sp->private_function = True;

	    /* FALLTHRU */
	case CASE_CPR:
	    TRACE(("CASE_DSR - device status report\n"));
	    count = 0;
	    init_reply(ANSI_CSI);
	    reply.a_pintro = CharOf(sp->private_function ? '?' : 0);
	    reply.a_inters = 0;
	    reply.a_final = 'n';

	    switch (GetParam(0)) {
	    case 5:
		TRACE(("...request operating status\n"));
		/* operating status */
		reply.a_param[count++] = 0;	/* (no malfunction ;-) */
		break;
	    case 6:
		TRACE(("...request %s\n",
		       (sp->private_function
			? "DECXCPR"
			: "CPR")));
		/* CPR */
		/* DECXCPR (with page=1) */
		value = (screen->cur_row + 1);
		if ((xw->flags & ORIGIN) != 0) {
		    value -= screen->top_marg;
		}
		reply.a_param[count++] = (ParmType) value;

		value = (screen->cur_col + 1);
		if ((xw->flags & ORIGIN) != 0) {
		    value -= screen->lft_marg;
		}
		reply.a_param[count++] = (ParmType) value;

		if (sp->private_function
		    && screen->vtXX_level >= 4) {	/* VT420 */
		    reply.a_param[count++] = 1;
		}
		reply.a_final = 'R';
		break;
	    case 15:
		TRACE(("...request printer status\n"));
		if (sp->private_function
		    && screen->vtXX_level >= 2) {	/* VT220 */
		    reply.a_param[count++] = 13;	/* no printer detected */
		}
		break;
	    case 25:
		TRACE(("...request UDK status\n"));
		if (sp->private_function
		    && screen->vtXX_level >= 2) {	/* VT220 */
		    reply.a_param[count++] = 20;	/* UDK always unlocked */
		}
		break;
	    case 26:
		TRACE(("...request keyboard status\n"));
		if (sp->private_function
		    && screen->vtXX_level >= 2) {	/* VT220 */
		    reply.a_param[count++] = 27;
		    reply.a_param[count++] = 1;		/* North American */
		    if (screen->vtXX_level >= 4) {	/* VT420 */
			reply.a_param[count++] = 0;	/* ready */
			reply.a_param[count++] = 0;	/* LK201 */
		    }
		}
		break;
	    case 53:		/* according to existing xterm handling */
		/* FALLTHRU */
	    case 55:		/* according to the VT330/VT340 Text Programming Manual */
		TRACE(("...request locator status\n"));
		if (sp->private_function
		    && screen->vtXX_level >= 2) {	/* VT220 */
		    reply.a_param[count++] = 53;	/* no locator */
		}
		break;
	    case 56:
		TRACE(("...request locator type\n"));
		if (sp->private_function
		    && screen->vtXX_level >= 3) {	/* VT330 (FIXME: what about VT220?) */
		    reply.a_param[count++] = 57;
		    reply.a_param[count++] = 0;		/* unknown */
		}
		break;
	    case 62:
		TRACE(("...request DECMSR - macro space\n"));
		if (sp->private_function
		    && screen->vtXX_level >= 4) {	/* VT420 */
		    reply.a_pintro = 0;
		    reply.a_radix[count] = 16;	/* no data */
		    reply.a_param[count++] = 0;		/* no space for macros */
		    reply.a_inters = '*';
		    reply.a_final = L_CURL;
		}
		break;
	    case 63:
		TRACE(("...request DECCKSR - memory checksum\n"));
		/* DECCKSR - Memory checksum */
		if (sp->private_function
		    && screen->vtXX_level >= 4) {	/* VT420 */
		    init_reply(ANSI_DCS);
		    reply.a_param[count++] = (ParmType) GetParam(1);	/* PID */
		    reply.a_delim = "!~";	/* delimiter */
		    reply.a_radix[count] = 16;	/* use hex */
		    reply.a_param[count++] = 0;		/* no data */
		}
		break;
	    case 75:
		TRACE(("...request data integrity\n"));
		if (sp->private_function
		    && screen->vtXX_level >= 4) {	/* VT420 */
		    reply.a_param[count++] = 70;	/* no errors */
		}
		break;
	    case 85:
		TRACE(("...request multi-session configuration\n"));
		if (sp->private_function
		    && screen->vtXX_level >= 4) {	/* VT420 */
		    reply.a_param[count++] = 83;	/* not configured */
		}
		break;
	    default:
		break;
	    }

	    if ((reply.a_nparam = (ParmType) count) != 0)
		unparseseq(xw, &reply);

	    ResetState(sp);
	    sp->private_function = False;
	    break;

	case CASE_MC:
	    TRACE(("CASE_MC - media control\n"));
	    xtermMediaControl(xw, GetParam(0), False);
	    ResetState(sp);
	    break;

	case CASE_DEC_MC:
	    TRACE(("CASE_DEC_MC - DEC media control\n"));
	    xtermMediaControl(xw, GetParam(0), True);
	    ResetState(sp);
	    break;

	case CASE_HP_MEM_LOCK:
	    /* FALLTHRU */
	case CASE_HP_MEM_UNLOCK:
	    TRACE(("%s\n", ((sp->parsestate[c] == CASE_HP_MEM_LOCK)
			    ? "CASE_HP_MEM_LOCK"
			    : "CASE_HP_MEM_UNLOCK")));
	    if (screen->scroll_amt)
		FlushScroll(xw);
	    if (sp->parsestate[c] == CASE_HP_MEM_LOCK)
		set_tb_margins(screen, screen->cur_row, screen->bot_marg);
	    else
		set_tb_margins(screen, 0, screen->bot_marg);
	    ResetState(sp);
	    break;

	case CASE_DECSTBM:
	    TRACE(("CASE_DECSTBM - set scrolling region\n"));
	    {
		int top;
		int bot;
		top = one_if_default(0);
		if (nparam < 2 || (bot = GetParam(1)) == DEFAULT
		    || bot > MaxRows(screen)
		    || bot == 0)
		    bot = MaxRows(screen);
		if (bot > top) {
		    if (screen->scroll_amt)
			FlushScroll(xw);
		    set_tb_margins(screen, top - 1, bot - 1);
		    CursorSet(screen, 0, 0, xw->flags);
		}
		ResetState(sp);
	    }
	    break;

	case CASE_DECREQTPARM:
	    TRACE(("CASE_DECREQTPARM\n"));
	    if (screen->terminal_id < 200) {	/* VT102 */
		value = zero_if_default(0);
		if (value == 0 || value == 1) {
		    init_reply(ANSI_CSI);
		    reply.a_pintro = 0;
		    reply.a_nparam = 7;
		    reply.a_param[0] = (ParmType) (value + 2);
		    reply.a_param[1] = 1;	/* no parity */
		    reply.a_param[2] = 1;	/* eight bits */
		    reply.a_param[3] = 128;	/* transmit 38.4k baud */
		    reply.a_param[4] = 128;	/* receive 38.4k baud */
		    reply.a_param[5] = 1;	/* clock multiplier ? */
		    reply.a_param[6] = 0;	/* STP flags ? */
		    reply.a_inters = 0;
		    reply.a_final = 'x';
		    unparseseq(xw, &reply);
		}
	    }
	    ResetState(sp);
	    break;

	case CASE_DECSET:
	    /* DECSET */
		dpmodes(xw, bitset);
	    ResetState(sp);
	    break;

	case CASE_DECRST:
	    /* DECRST */
	    dpmodes(xw, bitclr);
	    init_groundtable(screen, sp);
	    ResetState(sp);
	    break;

	case CASE_DECALN:
	    TRACE(("CASE_DECALN - alignment test\n"));
	    if (screen->cursor_state)
		HideCursor();
	    reset_margins(screen);
	    CursorSet(screen, 0, 0, xw->flags);
	    xtermParseRect(xw, 0, 0, &myRect);
	    ScrnFillRectangle(xw, &myRect, 'E', 0, False);
	    ResetState(sp);
	    break;

	case CASE_GSETS:
	    TRACE(("CASE_GSETS(%d) = '%c'\n", sp->scstype, c));
	    decode_scs(xw, sp->scstype, 0, (int) c);
	    ResetState(sp);
	    break;

	case CASE_ANSI_SC:
	    if (IsLeftRightMode(xw)) {
		int left;
		int right;

		TRACE(("CASE_DECSLRM - set left and right margin\n"));
		left = one_if_default(0);
		if (nparam < 2 || (right = GetParam(1)) == DEFAULT
		    || right > MaxCols(screen)
		    || right == 0)
		    right = MaxCols(screen);
		if (right > left) {
		    set_lr_margins(screen, left - 1, right - 1);
		    CursorSet(screen, 0, 0, xw->flags);
		}
	    } else {
		TRACE(("CASE_ANSI_SC - save cursor\n"));
		CursorSave(xw);
	    }
	    ResetState(sp);
	    break;

	case CASE_DECSC:
	    TRACE(("CASE_DECSC - save cursor\n"));
	    CursorSave(xw);
	    ResetState(sp);
	    break;

	case CASE_ANSI_RC:
	    /* FALLTHRU */
	case CASE_DECRC:
	    TRACE(("CASE_%sRC - restore cursor\n",
		   (sp->nextstate == CASE_DECRC) ? "DEC" : "ANSI_"));
	    CursorRestore(xw);
	    if_OPT_ISO_COLORS(screen, {
		setExtendedFG(xw);
	    });
	    ResetState(sp);
	    break;

	case CASE_DECKPAM:
	    TRACE(("CASE_DECKPAM\n"));
	    xw->keyboard.flags |= MODE_DECKPAM;
	    update_appkeypad();
	    ResetState(sp);
	    break;

	case CASE_DECKPNM:
	    TRACE(("CASE_DECKPNM\n"));
	    UIntClr(xw->keyboard.flags, MODE_DECKPAM);
	    update_appkeypad();
	    ResetState(sp);
	    break;

	case CASE_CSI_QUOTE_STATE:
	    sp->parsestate = csi_quo_table;
	    break;

	case CASE_ANSI_LEVEL_1:
	    TRACE(("CASE_ANSI_LEVEL_1\n"));
	    set_ansi_conformance(screen, 1);
	    ResetState(sp);
	    break;

	case CASE_ANSI_LEVEL_2:
	    TRACE(("CASE_ANSI_LEVEL_2\n"));
	    set_ansi_conformance(screen, 2);
	    ResetState(sp);
	    break;

	case CASE_ANSI_LEVEL_3:
	    TRACE(("CASE_ANSI_LEVEL_3\n"));
	    set_ansi_conformance(screen, 3);
	    ResetState(sp);
	    break;

	case CASE_DECSCL:
	    TRACE(("CASE_DECSCL(%d,%d)\n", GetParam(0), GetParam(1)));
	    /*
	     * This changes the emulation level, and is not recognized by
	     * VT100s.
	     */
	    if (screen->terminal_id >= 200) {
		/*
		 * Disallow unrecognized parameters, as well as attempts to set
		 * the operating level higher than the given terminal-id.
		 */
		if (GetParam(0) >= 61
		    && GetParam(0) <= 60 + (screen->terminal_id / 100)) {
		    int new_vtXX_level = GetParam(0) - 60;
		    int case_value = zero_if_default(1);
		    /*
		     * VT300, VT420, VT520 manuals claim that DECSCL does a
		     * hard reset (RIS).  VT220 manual states that it is a soft
		     * reset.  Perhaps both are right (unlikely).  Kermit says
		     * it's soft.
		     */
		    ReallyReset(xw, False, False);
		    init_parser(xw, sp);
		    screen->vtXX_level = new_vtXX_level;
		    if (new_vtXX_level > 1) {
			switch (case_value) {
			case 1:
			    show_8bit_control(False);
			    break;
			case 0:
			case 2:
			    show_8bit_control(True);
			    break;
			}
		    }
		}
	    }
	    ResetState(sp);
	    break;

	case CASE_DECSCA:
	    TRACE(("CASE_DECSCA\n"));
	    screen->protected_mode = DEC_PROTECT;
	    if (GetParam(0) <= 0 || GetParam(0) == 2) {
		UIntClr(xw->flags, PROTECTED);
		TRACE(("...clear PROTECTED\n"));
	    } else if (GetParam(0) == 1) {
		xw->flags |= PROTECTED;
		TRACE(("...set PROTECTED\n"));
	    }
	    ResetState(sp);
	    break;

	case CASE_DECSED:
	    TRACE(("CASE_DECSED\n"));
	    do_erase_display(xw, zero_if_default(0), DEC_PROTECT);
	    ResetState(sp);
	    break;

	case CASE_DECSEL:
	    TRACE(("CASE_DECSEL\n"));
	    do_erase_line(xw, zero_if_default(0), DEC_PROTECT);
	    ResetState(sp);
	    break;

	case CASE_GRAPHICS_ATTRIBUTES:
	    ResetState(sp);
	    break;

	case CASE_ST:
	    TRACE(("CASE_ST: End of String (%lu bytes) (mode=%d)\n",
		   (unsigned long) sp->string_used,
		   sp->string_mode));
	    ResetState(sp);
	    if (!sp->string_used)
		break;
	    sp->string_area[--(sp->string_used)] = '\0';
	    switch (sp->string_mode) {
	    case ANSI_APC:
		/* ignored */
		break;
	    case ANSI_DCS:
		do_dcs(xw, sp->string_area, sp->string_used);
		break;
	    case ANSI_OSC:
		do_osc(xw, sp->string_area, sp->string_used, ANSI_ST);
		break;
	    case ANSI_PM:
		/* ignored */
		break;
	    case ANSI_SOS:
		/* ignored */
		break;
	    default:
		TRACE(("unknown mode\n"));
		break;
	    }
	    break;

	case CASE_SOS:
	    TRACE(("CASE_SOS: Start of String\n"));
	    if (ParseSOS(screen)) {
		sp->string_mode = ANSI_SOS;
		sp->parsestate = sos_table;
	    } else {
		illegal_parse(xw, c, sp);
	    }
	    break;

	case CASE_PM:
	    TRACE(("CASE_PM: Privacy Message\n"));
	    if (ParseSOS(screen)) {
		sp->string_mode = ANSI_PM;
		sp->parsestate = sos_table;
	    } else {
		illegal_parse(xw, c, sp);
	    }
	    break;

	case CASE_DCS:
	    TRACE(("CASE_DCS: Device Control String\n"));
	    sp->string_mode = ANSI_DCS;
	    sp->parsestate = sos_table;
	    break;

	case CASE_APC:
	    TRACE(("CASE_APC: Application Program Command\n"));
	    if (ParseSOS(screen)) {
		sp->string_mode = ANSI_APC;
		sp->parsestate = sos_table;
	    } else {
		illegal_parse(xw, c, sp);
	    }
	    break;

	case CASE_SPA:
	    TRACE(("CASE_SPA - start protected area\n"));
	    screen->protected_mode = ISO_PROTECT;
	    xw->flags |= PROTECTED;
	    ResetState(sp);
	    break;

	case CASE_EPA:
	    TRACE(("CASE_EPA - end protected area\n"));
	    UIntClr(xw->flags, PROTECTED);
	    ResetState(sp);
	    break;

	case CASE_SU:
	    TRACE(("CASE_SU - scroll up\n"));
	    xtermScroll(xw, one_if_default(0));
	    ResetState(sp);
	    break;

	case CASE_SL:		/* ISO 6429, non-DEC */
	    TRACE(("CASE_SL - scroll left\n"));
	    xtermScrollLR(xw, one_if_default(0), True);
	    ResetState(sp);
	    break;

	case CASE_SR:		/* ISO 6429, non-DEC */
	    TRACE(("CASE_SR - scroll right\n"));
	    xtermScrollLR(xw, one_if_default(0), False);
	    ResetState(sp);
	    break;

	case CASE_DECDC:
	    TRACE(("CASE_DC - delete column\n"));
	    if (screen->vtXX_level >= 4) {
		xtermColScroll(xw, one_if_default(0), True, screen->cur_col);
	    }
	    ResetState(sp);
	    break;

	case CASE_DECIC:
	    TRACE(("CASE_IC - insert column\n"));
	    if (screen->vtXX_level >= 4) {
		xtermColScroll(xw, one_if_default(0), False, screen->cur_col);
	    }
	    ResetState(sp);
	    break;

	case CASE_DECBI:
	    TRACE(("CASE_BI - back index\n"));
	    if (screen->vtXX_level >= 4) {
		xtermColIndex(xw, True);
	    }
	    ResetState(sp);
	    break;

	case CASE_DECFI:
	    TRACE(("CASE_FI - forward index\n"));
	    if (screen->vtXX_level >= 4) {
		xtermColIndex(xw, False);
	    }
	    ResetState(sp);
	    break;

	case CASE_IND:
	    TRACE(("CASE_IND - index\n"));
	    xtermIndex(xw, 1);
	    do_xevents();
	    ResetState(sp);
	    break;

	case CASE_CPL:
	    TRACE(("CASE_CPL - cursor prev line\n"));
	    CursorPrevLine(xw, one_if_default(0));
	    ResetState(sp);
	    break;

	case CASE_CNL:
	    TRACE(("CASE_CNL - cursor next line\n"));
	    CursorNextLine(xw, one_if_default(0));
	    ResetState(sp);
	    break;

	case CASE_NEL:
	    TRACE(("CASE_NEL\n"));
	    xtermIndex(xw, 1);
	    CarriageReturn(xw);
	    ResetState(sp);
	    break;

	case CASE_HTS:
	    TRACE(("CASE_HTS - horizontal tab set\n"));
	    TabSet(xw->tabs, screen->cur_col);
	    ResetState(sp);
	    break;

	case CASE_RI:
	    TRACE(("CASE_RI - reverse index\n"));
	    RevIndex(xw, 1);
	    ResetState(sp);
	    break;

	case CASE_SS2:
	    TRACE(("CASE_SS2\n"));
	    screen->curss = 2;
	    ResetState(sp);
	    break;

	case CASE_SS3:
	    TRACE(("CASE_SS3\n"));
	    screen->curss = 3;
	    ResetState(sp);
	    break;

	case CASE_CSI_STATE:
	    /* enter csi state */
	    InitParams();
	    SetParam(nparam++, DEFAULT);
	    sp->parsestate = csi_table;
	    break;

	case CASE_ESC_SP_STATE:
	    /* esc space */
	    sp->parsestate = esc_sp_table;
	    break;

	case CASE_CSI_EX_STATE:
	    /* csi exclamation */
	    sp->parsestate = csi_ex_table;
	    break;

	case CASE_CSI_TICK_STATE:
	    /* csi tick (') */
	    sp->parsestate = csi_tick_table;
	    break;


	case CASE_CSI_DOLLAR_STATE:
	    /* csi dollar ($) */
	    sp->parsestate = eigtable;
	    break;

	case CASE_CSI_STAR_STATE:
	    /* csi dollar (*) */
	    sp->parsestate = eigtable;
	    break;

	case CASE_CSI_DEC_DOLLAR_STATE:
	    /* csi ? dollar ($) */
	    sp->parsestate = eigtable;
	    break;

	case CASE_S7C1T:
	    TRACE(("CASE_S7C1T\n"));
	    if (screen->vtXX_level >= 2) {
		show_8bit_control(False);
		ResetState(sp);
	    }
	    break;

	case CASE_S8C1T:
	    TRACE(("CASE_S8C1T\n"));
	    if (screen->vtXX_level >= 2) {
		show_8bit_control(True);
		ResetState(sp);
	    }
	    break;

	case CASE_OSC:
	    TRACE(("CASE_OSC: Operating System Command\n"));
	    sp->parsestate = sos_table;
	    sp->string_mode = ANSI_OSC;
	    break;

	case CASE_RIS:
	    TRACE(("CASE_RIS\n"));
	    VTReset(xw, True, True);
	    ResetState(sp);
	    break;

	case CASE_DECSTR:
	    TRACE(("CASE_DECSTR\n"));
	    VTReset(xw, False, False);
	    ResetState(sp);
	    break;

	case CASE_REP:
	    TRACE(("CASE_REP\n"));
	    if (sp->lastchar >= 0 &&
		sp->lastchar < 256 &&
		sp->groundtable[E2A(sp->lastchar)] == CASE_PRINT) {
		IChar repeated[2];
		count = one_if_default(0);
		repeated[0] = (IChar) sp->lastchar;
		while (count-- > 0) {
		    dotext(xw,
			   screen->gsets[(int) (screen->curgl)],
			   repeated, 1);
		}
	    }
	    ResetState(sp);
	    break;

	case CASE_LS2:
	    TRACE(("CASE_LS2\n"));
	    screen->curgl = 2;
	    ResetState(sp);
	    break;

	case CASE_LS3:
	    TRACE(("CASE_LS3\n"));
	    screen->curgl = 3;
	    ResetState(sp);
	    break;

	case CASE_LS3R:
	    TRACE(("CASE_LS3R\n"));
	    screen->curgr = 3;
	    ResetState(sp);
	    break;

	case CASE_LS2R:
	    TRACE(("CASE_LS2R\n"));
	    screen->curgr = 2;
	    ResetState(sp);
	    break;

	case CASE_LS1R:
	    TRACE(("CASE_LS1R\n"));
	    screen->curgr = 1;
	    ResetState(sp);
	    break;

	case CASE_XTERM_SAVE:
	    savemodes(xw);
	    ResetState(sp);
	    break;

	case CASE_XTERM_RESTORE:
	    restoremodes(xw);
	    ResetState(sp);
	    break;

	case CASE_XTERM_WINOPS:
	    TRACE(("CASE_XTERM_WINOPS\n"));
	    window_ops(xw);
	    ResetState(sp);
	    break;
	case CASE_HIDE_POINTER:
	    TRACE(("CASE_HIDE_POINTER\n"));
	    if (nparam >= 1 && GetParam(0) != DEFAULT) {
		screen->pointer_mode = GetParam(0);
	    } else {
		screen->pointer_mode = DEF_POINTER_MODE;
	    }
	    break;

	case CASE_SM_TITLE:
	    TRACE(("CASE_SM_TITLE\n"));
	    if (nparam >= 1) {
		int n;
		for (n = 0; n < nparam; ++n) {
		    if (GetParam(n) != DEFAULT)
			screen->title_modes |= (1 << GetParam(n));
		}
	    } else {
		screen->title_modes = DEF_TITLE_MODES;
	    }
	    TRACE(("...title_modes %#x\n", screen->title_modes));
	    break;

	case CASE_RM_TITLE:
	    TRACE(("CASE_RM_TITLE\n"));
	    if (nparam >= 1) {
		int n;
		for (n = 0; n < nparam; ++n) {
		    if (GetParam(n) != DEFAULT)
			screen->title_modes &= ~(1 << GetParam(n));
		}
	    } else {
		screen->title_modes = DEF_TITLE_MODES;
	    }
	    TRACE(("...title_modes %#x\n", screen->title_modes));
	    break;

	case CASE_CSI_IGNORE:
	    sp->parsestate = cigtable;
	    break;

	case CASE_DECSWBV:
	    TRACE(("CASE_DECSWBV\n"));
	    switch (zero_if_default(0)) {
	    case 2:
		/* FALLTHRU */
	    case 3:
		/* FALLTHRU */
	    case 4:
		screen->warningVolume = bvLow;
		break;
	    case 5:
		/* FALLTHRU */
	    case 6:
		/* FALLTHRU */
	    case 7:
		/* FALLTHRU */
	    case 8:
		screen->warningVolume = bvHigh;
		break;
	    default:
		screen->warningVolume = bvOff;
		break;
	    }
	    TRACE(("...warningVolume %d\n", screen->warningVolume));
	    ResetState(sp);
	    break;

	case CASE_DECSMBV:
	    TRACE(("CASE_DECSMBV\n"));
	    switch (zero_if_default(0)) {
	    case 2:
		/* FALLTHRU */
	    case 3:
		/* FALLTHRU */
	    case 4:
		screen->marginVolume = bvLow;
		break;
	    case 0:
		/* FALLTHRU */
	    case 5:
		/* FALLTHRU */
	    case 6:
		/* FALLTHRU */
	    case 7:
		/* FALLTHRU */
	    case 8:
		screen->marginVolume = bvHigh;
		break;
	    default:
		screen->marginVolume = bvOff;
		break;
	    }
	    TRACE(("...marginVolume %d\n", screen->marginVolume));
	    ResetState(sp);
	    break;
	}
	if (sp->parsestate == sp->groundtable)
	    sp->lastchar = thischar;
    } while (0);


    return True;
}

static void
VTparse(XtermWidget xw)
{
    /* We longjmp back to this point in VTReset() */
    (void) setjmp(vtjmpbuf);
    init_parser(xw, &myState);

    do {
    } while (doparsing(xw, doinput(), &myState));
}

static Char *v_buffer;		/* pointer to physical buffer */
static Char *v_bufstr = NULL;	/* beginning of area to write */
static Char *v_bufptr;		/* end of area to write */
static Char *v_bufend;		/* end of physical buffer */

/* Write data to the pty as typed by the user, pasted with the mouse,
   or generated by us in response to a query ESC sequence. */

void
v_write(int f, const Char *data, unsigned len)
{
    int riten;

    TRACE2(("v_write(%d:%s)\n", len, visibleChars(data, len)));
    if (v_bufstr == NULL) {
	if (len > 0) {
	    v_buffer = (Char *) XtMalloc((Cardinal) len);
	    v_bufstr = v_buffer;
	    v_bufptr = v_buffer;
	    v_bufend = v_buffer + len;
	}
	if (v_bufstr == NULL) {
	    return;
	}
    }
    if (!FD_ISSET(f, &pty_mask)) {
	IGNORE_RC(write(f, (const char *) data, (size_t) len));
	return;
    }

    /*
     * Append to the block we already have.
     * Always doing this simplifies the code, and
     * isn't too bad, either.  If this is a short
     * block, it isn't too expensive, and if this is
     * a long block, we won't be able to write it all
     * anyway.
     */

    if (len > 0) {
	if (v_bufend < v_bufptr + len) {	/* we've run out of room */
	    if (v_bufstr != v_buffer) {
		/* there is unused space, move everything down */
		/* possibly overlapping memmove here */
		memmove(v_buffer, v_bufstr, (size_t) (v_bufptr - v_bufstr));
		v_bufptr -= v_bufstr - v_buffer;
		v_bufstr = v_buffer;
	    }
	    if (v_bufend < v_bufptr + len) {
		/* still won't fit: get more space */
		/* Don't use XtRealloc because an error is not fatal. */
		unsigned size = (unsigned) (v_bufptr - v_buffer);
		v_buffer = TypeRealloc(Char, size + len, v_buffer);
		if (v_buffer) {
		    v_bufstr = v_buffer;
		    v_bufptr = v_buffer + size;
		    v_bufend = v_bufptr + len;
		} else {
		    /* no memory: ignore entire write request */
		    xtermWarning("cannot allocate buffer space\n");
		    v_buffer = v_bufstr;	/* restore clobbered pointer */
		}
	    }
	}
	if (v_bufend >= v_bufptr + len) {
	    /* new stuff will fit */
	    memmove(v_bufptr, data, (size_t) len);
	    v_bufptr += len;
	}
    }

    /*
     * Write out as much of the buffer as we can.
     * Be careful not to overflow the pty's input silo.
     * We are conservative here and only write
     * a small amount at a time.
     *
     * If we can't push all the data into the pty yet, we expect write
     * to return a non-negative number less than the length requested
     * (if some data written) or -1 and set errno to EAGAIN,
     * EWOULDBLOCK, or EINTR (if no data written).
     *
     * (Not all systems do this, sigh, so the code is actually
     * a little more forgiving.)
     */

#define MAX_PTY_WRITE 128	/* 1/2 POSIX minimum MAX_INPUT */

    if (v_bufptr > v_bufstr) {
	riten = (int) write(f, v_bufstr,
			    (size_t) ((v_bufptr - v_bufstr <= MAX_PTY_WRITE)
				      ? v_bufptr - v_bufstr
				      : MAX_PTY_WRITE));
	if (riten < 0)
	{
	    riten = 0;
	}
	v_bufstr += riten;
	if (v_bufstr >= v_bufptr)	/* we wrote it all */
	    v_bufstr = v_bufptr = v_buffer;
    }

    /*
     * If we have lots of unused memory allocated, return it
     */
    if (v_bufend - v_bufptr > 1024) {	/* arbitrary hysteresis */
	/* save pointers across realloc */
	int start = (int) (v_bufstr - v_buffer);
	int size = (int) (v_bufptr - v_buffer);
	unsigned allocsize = (unsigned) (size ? size : 1);

	v_buffer = TypeRealloc(Char, allocsize, v_buffer);
	if (v_buffer) {
	    v_bufstr = v_buffer + start;
	    v_bufptr = v_buffer + size;
	    v_bufend = v_buffer + allocsize;
	} else {
	    /* should we print a warning if couldn't return memory? */
	    v_buffer = v_bufstr - start;	/* restore clobbered pointer */
	}
    }
}

static void
updateCursor(TScreen *screen)
{
    if (screen->cursor_set != screen->cursor_state) {
	if (screen->cursor_set)
	    ShowCursor();
	else
	    HideCursor();
    }
}

static void
in_put(XtermWidget xw)
{
    static PtySelect select_mask;
    static PtySelect write_mask;

    TScreen *screen = TScreenOf(xw);
    int i, time_select;
    int size;
    int update = VTbuffer->update;

    static struct timeval select_timeout;


    for (;;) {
	if (screen->eventMode == NORMAL
	    && (size = readPtyData(xw, &select_mask, VTbuffer)) != 0) {
	    if (screen->scrollWidget
		&& screen->scrollttyoutput
		&& screen->topline < 0)
		WindowScroll(xw, 0, False);	/* Scroll to bottom */
	    /* stop speed reading at some point to look for X stuff */
	    TRACE(("VTbuffer uses %ld/%d\n",
		   (long) (VTbuffer->last - VTbuffer->buffer),
		   BUF_SIZE));
	    if ((VTbuffer->last - VTbuffer->buffer) > BUF_SIZE) {
		FD_CLR(screen->respond, &select_mask);
		break;
	    }
	    (void) size;	/* unused in this branch */
	    break;
	}
	/* update the screen */
	if (screen->scroll_amt)
	    FlushScroll(xw);
	if (screen->cursor_set && CursorMoved(screen)) {
	    if (screen->cursor_state)
		HideCursor();
	    ShowCursor();
	} else {
	    updateCursor(screen);
	}

	XFlush(screen->display);	/* always flush writes before waiting */

	/* Update the masks and, unless X events are already in the queue,
	   wait for I/O to be possible. */
	XFD_COPYSET(&Select_mask, &select_mask);
	/* in selection mode xterm does not read pty */
	if (screen->eventMode != NORMAL)
	    FD_CLR(screen->respond, &select_mask);

	if (v_bufptr > v_bufstr) {
	    XFD_COPYSET(&pty_mask, &write_mask);
	} else
	    FD_ZERO(&write_mask);
	select_timeout.tv_sec = 0;
	time_select = 0;

	/*
	 * if there's either an XEvent or an XtTimeout pending, just take
	 * a quick peek, i.e. timeout from the select() immediately.  If
	 * there's nothing pending, let select() block a little while, but
	 * for a shorter interval than the arrow-style scrollbar timeout.
	 * The blocking is optional, because it tends to increase the load
	 * on the host.
	 */
	if (xtermAppPending()) {
	    select_timeout.tv_usec = 0;
	    time_select = 1;
	} else if (screen->awaitInput) {
	    select_timeout.tv_usec = 50000;
	    time_select = 1;
	}
	if (need_cleanup)
	    NormalExit();
	i = Select(max_plus1, &select_mask, &write_mask, 0,
		   (time_select ? &select_timeout : 0));
	if (i < 0) {
	    if (errno != EINTR)
		SysError(ERROR_SELECT);
	    continue;
	}

	/* if there is room to write more data to the pty, go write more */
	if (FD_ISSET(screen->respond, &write_mask)) {
	    v_write(screen->respond, (Char *) 0, 0);	/* flush buffer */
	}

	/* if there are X events already in our queue, it
	   counts as being readable */
	if (xtermAppPending() ||
	    FD_ISSET(ConnectionNumber(screen->display), &select_mask)) {
	    xevents();
	    if (VTbuffer->update != update)	/* HandleInterpret */
		break;
	}

    }
}

static IChar
doinput(void)
{
    TScreen *screen = TScreenOf(term);

    while (!morePtyData(screen, VTbuffer))
	in_put(term);
    return nextPtyData(screen, VTbuffer);
}

static void
WrapLine(XtermWidget xw)
{
    TScreen *screen = TScreenOf(xw);
    LineData *ld = getLineData(screen, screen->cur_row);

    if (ld != 0) {
	/* mark that we had to wrap this line */
	LineSetFlag(ld, LINEWRAPPED);
	if (screen->show_wrap_marks) {
	    ShowWrapMarks(xw, screen->cur_row, ld);
	}
	xtermAutoPrint(xw, '\n');
	xtermIndex(xw, 1);
	set_cur_col(screen, ScrnLeftMargin(xw));
    }
}

/*
 * process a string of characters according to the character set indicated
 * by charset.  worry about end of line conditions (wraparound if selected).
 */
void
dotext(XtermWidget xw,
       int charset,
       IChar *buf,		/* start of characters to process */
       Cardinal len)		/* end */
{
    TScreen *screen = TScreenOf(xw);
    int next_col, last_col, this_col;	/* must be signed */
    Cardinal offset;
    int right = ScrnRightMargin(xw);

    /*
     * It is possible to use CUP, etc., to move outside margins.  In that
     * case, the right-margin is ineffective.
     */
    if (screen->cur_col > right) {
	right = screen->max_col;
    }
	if (!xtermCharSetOut(xw, buf, buf + len, charset))
	    return;

    if_OPT_XMC_GLITCH(screen, {
	Cardinal n;
	if (charset != '?') {
	    for (n = 0; n < len; n++) {
		if (buf[n] == XMC_GLITCH)
		    buf[n] = XMC_GLITCH + 1;
	    }
	}
    });

    for (offset = 0; offset < len; offset += (Cardinal) this_col) {

	last_col = LineMaxCol(screen, ld);
	if (last_col > (right + 1))
	    last_col = right + 1;
	this_col = last_col - screen->cur_col + 1;
	if (this_col <= 1) {
	    if (screen->do_wrap) {
		screen->do_wrap = False;
		if ((xw->flags & WRAPAROUND)) {
		    WrapLine(xw);
		}
	    }
	    this_col = 1;
	}
	if (offset + (Cardinal) this_col > len) {
	    this_col = (int) (len - offset);
	}
	next_col = screen->cur_col + this_col;

	WriteText(xw, buf + offset, (unsigned) this_col);

	/*
	 * The call to WriteText updates screen->cur_col.
	 * If screen->cur_col is less than next_col, we must have
	 * hit the right margin - so set the do_wrap flag.
	 */
	screen->do_wrap = (Boolean) (screen->cur_col < next_col);
    }
}

/*
 * process ANSI modes set, reset
 */
static void
ansi_modes(XtermWidget xw, BitFunc func)
{
    int i;

    for (i = 0; i < nparam; ++i) {
	switch (GetParam(i)) {
	case 2:		/* KAM (if set, keyboard locked */
	    (*func) (&xw->keyboard.flags, MODE_KAM);
	    break;

	case 4:		/* IRM                          */
	    (*func) (&xw->flags, INSERT);
	    break;

	case 12:		/* SRM (if set, local echo      */
	    (*func) (&xw->keyboard.flags, MODE_SRM);
	    break;

	case 20:		/* LNM                          */
	    (*func) (&xw->flags, LINEFEED);
	    update_autolinefeed();
	    break;
	}
    }
}

#define IsSM() (func == bitset)

#define set_bool_mode(flag) \
	flag = (Boolean) IsSM()

static void
really_set_mousemode(XtermWidget xw,
		     Bool enabled,
		     XtermMouseModes mode)
{
    TScreenOf(xw)->send_mouse_pos = enabled ? mode : MOUSE_OFF;
    if (TScreenOf(xw)->send_mouse_pos != MOUSE_OFF)
	xtermShowPointer(xw, True);
}

#define set_mousemode(mode) really_set_mousemode(xw, IsSM(), mode)


/*
 * process DEC private modes set, reset
 */
static void
dpmodes(XtermWidget xw, BitFunc func)
{
    TScreen *screen = TScreenOf(xw);
    int i, j;
    unsigned myflags;

    TRACE(("changing %d DEC private modes\n", nparam));
    for (i = 0; i < nparam; ++i) {
	int code = GetParam(i);

	TRACE(("%s %d\n", IsSM()? "DECSET" : "DECRST", code));
	switch ((DECSET_codes) code) {
	case srm_DECCKM:
	    (*func) (&xw->keyboard.flags, MODE_DECCKM);
	    update_appcursor();
	    break;
	case srm_DECANM:	/* ANSI/VT52 mode      */
	    if (IsSM()) {	/* ANSI (VT100) */
		/*
		 * Setting DECANM should have no effect, since this function
		 * cannot be reached from vt52 mode.
		 */
		/* EMPTY */ ;
	    }
	    break;
	case srm_DECCOLM:
	    if (screen->c132) {
		if (!(xw->flags & NOCLEAR_COLM))
		    ClearScreen(xw);
		CursorSet(screen, 0, 0, xw->flags);
		if ((j = IsSM()? 132 : 80) !=
		    ((xw->flags & IN132COLUMNS) ? 132 : 80) ||
		    j != MaxCols(screen))
		    RequestResize(xw, -1, j, True);
		(*func) (&xw->flags, IN132COLUMNS);
		if (xw->flags & IN132COLUMNS) {
		    UIntClr(xw->flags, LEFT_RIGHT);
		    reset_lr_margins(screen);
		}
	    }
	    break;
	case srm_DECSCLM:	/* (slow scroll)        */
	    if (IsSM()) {
		screen->jumpscroll = 0;
		if (screen->scroll_amt)
		    FlushScroll(xw);
	    } else
		screen->jumpscroll = 1;
	    (*func) (&xw->flags, SMOOTHSCROLL);
	    update_jumpscroll();
	    break;
	case srm_DECSCNM:
	    myflags = xw->flags;
	    (*func) (&xw->flags, REVERSE_VIDEO);
	    if ((xw->flags ^ myflags) & REVERSE_VIDEO)
		ReverseVideo(xw);
	    /* update_reversevideo done in RevVid */
	    break;

	case srm_DECOM:
	    (*func) (&xw->flags, ORIGIN);
	    CursorSet(screen, 0, 0, xw->flags);
	    break;

	case srm_DECAWM:
	    (*func) (&xw->flags, WRAPAROUND);
	    update_autowrap();
	    break;
	case srm_DECARM:
	    /* ignore autorepeat
	     * XAutoRepeatOn() and XAutoRepeatOff() can do this, but only
	     * for the whole display - not limited to a given window.
	     */
	    break;
	case srm_X10_MOUSE:	/* MIT bogus sequence           */
	    MotionOff(screen, xw);
	    set_mousemode(X10_MOUSE);
	    break;
	case srm_DECPFF:	/* print form feed */
	    set_bool_mode(PrinterOf(screen).printer_formfeed);
	    break;
	case srm_DECPEX:	/* print extent */
	    set_bool_mode(PrinterOf(screen).printer_extent);
	    break;
	case srm_DECTCEM:	/* Show/hide cursor (VT200) */
	    set_bool_mode(screen->cursor_set);
	    break;
	case srm_RXVT_SCROLLBAR:
	    if (screen->fullVwin.sb_info.width != (IsSM()? ON : OFF))
		ToggleScrollBar(xw);
	    break;
	case srm_132COLS:	/* 132 column mode              */
	    set_bool_mode(screen->c132);
	    update_allow132();
	    break;
	case srm_CURSES_HACK:
	    set_bool_mode(screen->curses);
	    update_cursesemul();
	    break;
	case srm_DECNRCM:	/* national charset (VT220) */
	    if (screen->vtXX_level >= 2) {
		if ((*func) (&xw->flags, NATIONAL)) {
		    modified_DECNRCM(xw);
		}
	    }
	    break;
	case srm_MARGIN_BELL:	/* margin bell                  */
	    set_bool_mode(screen->marginbell);
	    if (!screen->marginbell)
		screen->bellArmed = -1;
	    update_marginbell();
	    break;
	case srm_REVERSEWRAP:	/* reverse wraparound   */
	    (*func) (&xw->flags, REVERSEWRAP);
	    update_reversewrap();
	    break;
	case srm_OPT_ALTBUF_CURSOR:	/* alternate buffer & cursor */
	    if (!xw->misc.titeInhibit) {
		if (IsSM()) {
		    CursorSave(xw);
		    ToAlternate(xw, True);
		    ClearScreen(xw);
		} else {
		    FromAlternate(xw);
		    CursorRestore(xw);
		}
	    } else if (IsSM()) {
		do_ti_xtra_scroll(xw);
	    }
	    break;
	case srm_OPT_ALTBUF:
	    /* FALLTHRU */
	case srm_ALTBUF:	/* alternate buffer */
	    if (!xw->misc.titeInhibit) {
		if (IsSM()) {
		    ToAlternate(xw, False);
		} else {
		    if (screen->whichBuf
			&& (code == 1047))
			ClearScreen(xw);
		    FromAlternate(xw);
		}
	    } else if (IsSM()) {
		do_ti_xtra_scroll(xw);
	    }
	    break;
	case srm_DECNKM:
	    (*func) (&xw->keyboard.flags, MODE_DECKPAM);
	    update_appkeypad();
	    break;
	case srm_DECBKM:
	    /* back-arrow mapped to backspace or delete(D) */
	    (*func) (&xw->keyboard.flags, MODE_DECBKM);
	    TRACE(("DECSET DECBKM %s\n",
		   BtoS(xw->keyboard.flags & MODE_DECBKM)));
	    update_decbkm();
	    break;
	case srm_DECLRMM:
	    if (screen->vtXX_level >= 4) {	/* VT420 */
		(*func) (&xw->flags, LEFT_RIGHT);
		if (IsLeftRightMode(xw)) {
		    xterm_ResetDouble(xw);
		} else {
		    reset_lr_margins(screen);
		}
	    }
	    break;
	case srm_DECNCSM:
	    if (screen->vtXX_level >= 5) {	/* VT510 */
		(*func) (&xw->flags, NOCLEAR_COLM);
	    }
	    break;
	case srm_VT200_MOUSE:	/* xterm bogus sequence         */
	    MotionOff(screen, xw);
	    set_mousemode(VT200_MOUSE);
	    break;
	case srm_VT200_HIGHLIGHT_MOUSE:	/* xterm sequence w/hilite tracking */
	    MotionOff(screen, xw);
	    set_mousemode(VT200_HIGHLIGHT_MOUSE);
	    break;
	case srm_BTN_EVENT_MOUSE:
	    MotionOff(screen, xw);
	    set_mousemode(BTN_EVENT_MOUSE);
	    break;
	case srm_ANY_EVENT_MOUSE:
	    set_mousemode(ANY_EVENT_MOUSE);
	    if (screen->send_mouse_pos == MOUSE_OFF) {
		MotionOff(screen, xw);
	    } else {
		MotionOn(screen, xw);
	    }
	    break;
	case srm_EXT_MODE_MOUSE:
	    /* FALLTHRU */
	case srm_SGR_EXT_MODE_MOUSE:
	    /* FALLTHRU */
	case srm_URXVT_EXT_MODE_MOUSE:
	    /*
	     * Rather than choose an arbitrary precedence among the coordinate
	     * modes, they are mutually exclusive.  For consistency, a reset is
	     * only effective against the matching mode.
	     */
	    if (IsSM()) {
		screen->extend_coords = code;
	    } else if (screen->extend_coords == code) {
		screen->extend_coords = 0;
	    }
	    break;
	case srm_ALTERNATE_SCROLL:
	    set_bool_mode(screen->alternateScroll);
	    break;
	case srm_RXVT_SCROLL_TTY_OUTPUT:
	    set_bool_mode(screen->scrollttyoutput);
	    update_scrollttyoutput();
	    break;
	case srm_RXVT_SCROLL_TTY_KEYPRESS:
	    set_bool_mode(screen->scrollkey);
	    update_scrollkey();
	    break;
	case srm_EIGHT_BIT_META:
	    if (screen->eight_bit_meta != ebNever) {
		set_bool_mode(screen->eight_bit_meta);
	    }
	    break;
	case srm_DELETE_IS_DEL:
	    set_bool_mode(screen->delete_is_del);
	    update_delete_del();
	    break;
	case srm_KEEP_SELECTION:
	    set_bool_mode(screen->keepSelection);
	    update_keepSelection();
	    break;
	case srm_SELECT_TO_CLIPBOARD:
	    set_bool_mode(screen->selectToClipboard);
	    update_selectToClipboard();
	    break;
	case srm_BELL_IS_URGENT:
	    set_bool_mode(screen->bellIsUrgent);
	    update_bellIsUrgent();
	    break;
	case srm_POP_ON_BELL:
	    set_bool_mode(screen->poponbell);
	    update_poponbell();
	    break;
	case srm_TITE_INHIBIT:
	    if (!xw->misc.titeInhibit) {
		if (IsSM())
		    CursorSave(xw);
		else
		    CursorRestore(xw);
	    }
	    break;
	case srm_LEGACY_FKEYS:
	    set_keyboard_type(xw, keyboardIsLegacy, IsSM());
	    break;
	default:
	    TRACE(("DATA_ERROR: unknown private code %d\n", code));
	    break;
	}
    }
}

/*
 * process xterm private modes save
 */
static void
savemodes(XtermWidget xw)
{
    TScreen *screen = TScreenOf(xw);
    int i;

    for (i = 0; i < nparam; i++) {
	int code = GetParam(i);

	TRACE(("savemodes %d\n", code));
	switch ((DECSET_codes) code) {
	case srm_DECCKM:
	    DoSM(DP_DECCKM, xw->keyboard.flags & MODE_DECCKM);
	    break;
	case srm_DECANM:	/* ANSI/VT52 mode      */
	    /* no effect */
	    break;
	case srm_DECCOLM:
	    if (screen->c132)
		DoSM(DP_DECCOLM, xw->flags & IN132COLUMNS);
	    break;
	case srm_DECSCLM:	/* (slow scroll)        */
	    DoSM(DP_DECSCLM, xw->flags & SMOOTHSCROLL);
	    break;
	case srm_DECSCNM:
	    DoSM(DP_DECSCNM, xw->flags & REVERSE_VIDEO);
	    break;
	case srm_DECOM:
	    DoSM(DP_DECOM, xw->flags & ORIGIN);
	    break;
	case srm_DECAWM:
	    DoSM(DP_DECAWM, xw->flags & WRAPAROUND);
	    break;
	case srm_DECARM:
	    /* ignore autorepeat */
	    break;
	case srm_X10_MOUSE:	/* mouse bogus sequence */
	    DoSM(DP_X_X10MSE, screen->send_mouse_pos);
	    break;
	case srm_DECPFF:	/* print form feed */
	    DoSM(DP_PRN_FORMFEED, PrinterOf(screen).printer_formfeed);
	    break;
	case srm_DECPEX:	/* print extent */
	    DoSM(DP_PRN_EXTENT, PrinterOf(screen).printer_extent);
	    break;
	case srm_DECTCEM:	/* Show/hide cursor (VT200) */
	    DoSM(DP_CRS_VISIBLE, screen->cursor_set);
	    break;
	case srm_RXVT_SCROLLBAR:
	    DoSM(DP_RXVT_SCROLLBAR, (screen->fullVwin.sb_info.width != 0));
	    break;
	case srm_132COLS:	/* 132 column mode              */
	    DoSM(DP_X_DECCOLM, screen->c132);
	    break;
	case srm_CURSES_HACK:	/* curses hack                  */
	    DoSM(DP_X_MORE, screen->curses);
	    break;
	case srm_DECNRCM:	/* national charset (VT220) */
	    if (screen->vtXX_level >= 2) {
		DoSM(DP_DECNRCM, xw->flags & NATIONAL);
	    }
	    break;
	case srm_MARGIN_BELL:	/* margin bell                  */
	    DoSM(DP_X_MARGIN, screen->marginbell);
	    break;
	case srm_REVERSEWRAP:	/* reverse wraparound   */
	    DoSM(DP_X_REVWRAP, xw->flags & REVERSEWRAP);
	    break;
	case srm_OPT_ALTBUF_CURSOR:
	    /* FALLTHRU */
	case srm_OPT_ALTBUF:
	    /* FALLTHRU */
	case srm_ALTBUF:	/* alternate buffer             */
	    DoSM(DP_X_ALTSCRN, screen->whichBuf);
	    break;
	case srm_DECNKM:
	    DoSM(DP_DECKPAM, xw->keyboard.flags & MODE_DECKPAM);
	    break;
	case srm_DECBKM:	/* backarrow mapping */
	    DoSM(DP_DECBKM, xw->keyboard.flags & MODE_DECBKM);
	    break;
	case srm_DECLRMM:	/* left-right */
	    DoSM(DP_X_LRMM, LEFT_RIGHT);
	    break;
	case srm_DECNCSM:	/* noclear */
	    DoSM(DP_X_NCSM, NOCLEAR_COLM);
	    break;
	case srm_VT200_MOUSE:	/* mouse bogus sequence         */
	    /* FALLTHRU */
	case srm_VT200_HIGHLIGHT_MOUSE:
	    /* FALLTHRU */
	case srm_BTN_EVENT_MOUSE:
	    /* FALLTHRU */
	case srm_ANY_EVENT_MOUSE:
	    DoSM(DP_X_MOUSE, screen->send_mouse_pos);
	    break;
	case srm_EXT_MODE_MOUSE:
	    /* FALLTHRU */
	case srm_SGR_EXT_MODE_MOUSE:
	    /* FALLTHRU */
	case srm_URXVT_EXT_MODE_MOUSE:
	    DoSM(DP_X_EXT_MOUSE, screen->extend_coords);
	    break;
	case srm_ALTERNATE_SCROLL:
	    DoSM(DP_ALTERNATE_SCROLL, screen->alternateScroll);
	    break;
	case srm_RXVT_SCROLL_TTY_OUTPUT:
	    DoSM(DP_RXVT_SCROLL_TTY_OUTPUT, screen->scrollttyoutput);
	    break;
	case srm_RXVT_SCROLL_TTY_KEYPRESS:
	    DoSM(DP_RXVT_SCROLL_TTY_KEYPRESS, screen->scrollkey);
	    break;
	case srm_EIGHT_BIT_META:
	    DoSM(DP_EIGHT_BIT_META, screen->eight_bit_meta);
	    break;
	case srm_DELETE_IS_DEL:
	    DoSM(DP_DELETE_IS_DEL, screen->delete_is_del);
	    break;
	case srm_KEEP_SELECTION:
	    DoSM(DP_KEEP_SELECTION, screen->keepSelection);
	    break;
	case srm_SELECT_TO_CLIPBOARD:
	    DoSM(DP_SELECT_TO_CLIPBOARD, screen->selectToClipboard);
	    break;
	case srm_BELL_IS_URGENT:
	    DoSM(DP_BELL_IS_URGENT, screen->bellIsUrgent);
	    break;
	case srm_POP_ON_BELL:
	    DoSM(DP_POP_ON_BELL, screen->poponbell);
	    break;
	case srm_LEGACY_FKEYS:
	    DoSM(DP_KEYBOARD_TYPE, xw->keyboard.type);
	    break;
	case srm_TITE_INHIBIT:
	    if (!xw->misc.titeInhibit) {
		CursorSave(xw);
	    }
	    break;
	}
    }
}

/*
 * process xterm private modes restore
 */
static void
restoremodes(XtermWidget xw)
{
    TScreen *screen = TScreenOf(xw);
    int i, j;

    for (i = 0; i < nparam; i++) {
	int code = GetParam(i);

	TRACE(("restoremodes %d\n", code));
	switch ((DECSET_codes) code) {
	case srm_DECCKM:
	    bitcpy(&xw->keyboard.flags,
		   screen->save_modes[DP_DECCKM], MODE_DECCKM);
	    update_appcursor();
	    break;
	case srm_DECANM:	/* ANSI/VT52 mode      */
	    /* no effect */
	    break;
	case srm_DECCOLM:
	    if (screen->c132) {
		if (!(xw->flags & NOCLEAR_COLM))
		    ClearScreen(xw);
		CursorSet(screen, 0, 0, xw->flags);
		if ((j = (screen->save_modes[DP_DECCOLM] & IN132COLUMNS)
		     ? 132 : 80) != ((xw->flags & IN132COLUMNS)
				     ? 132 : 80) || j != MaxCols(screen))
		    RequestResize(xw, -1, j, True);
		bitcpy(&xw->flags,
		       screen->save_modes[DP_DECCOLM],
		       IN132COLUMNS);
	    }
	    break;
	case srm_DECSCLM:	/* (slow scroll)        */
	    if (screen->save_modes[DP_DECSCLM] & SMOOTHSCROLL) {
		screen->jumpscroll = 0;
		if (screen->scroll_amt)
		    FlushScroll(xw);
	    } else
		screen->jumpscroll = 1;
	    bitcpy(&xw->flags, screen->save_modes[DP_DECSCLM], SMOOTHSCROLL);
	    update_jumpscroll();
	    break;
	case srm_DECSCNM:
	    if ((screen->save_modes[DP_DECSCNM] ^ xw->flags) & REVERSE_VIDEO) {
		bitcpy(&xw->flags, screen->save_modes[DP_DECSCNM], REVERSE_VIDEO);
		ReverseVideo(xw);
		/* update_reversevideo done in RevVid */
	    }
	    break;
	case srm_DECOM:
	    bitcpy(&xw->flags, screen->save_modes[DP_DECOM], ORIGIN);
	    CursorSet(screen, 0, 0, xw->flags);
	    break;

	case srm_DECAWM:
	    bitcpy(&xw->flags, screen->save_modes[DP_DECAWM], WRAPAROUND);
	    update_autowrap();
	    break;
	case srm_DECARM:
	    /* ignore autorepeat */
	    break;
	case srm_X10_MOUSE:	/* MIT bogus sequence           */
	    DoRM0(DP_X_X10MSE, screen->send_mouse_pos);
	    really_set_mousemode(xw,
				 screen->send_mouse_pos != MOUSE_OFF,
				 (XtermMouseModes) screen->send_mouse_pos);
	    break;
	case srm_DECPFF:	/* print form feed */
	    DoRM(DP_PRN_FORMFEED, PrinterOf(screen).printer_formfeed);
	    break;
	case srm_DECPEX:	/* print extent */
	    DoRM(DP_PRN_EXTENT, PrinterOf(screen).printer_extent);
	    break;
	case srm_DECTCEM:	/* Show/hide cursor (VT200) */
	    DoRM(DP_CRS_VISIBLE, screen->cursor_set);
	    break;
	case srm_RXVT_SCROLLBAR:
	    if ((screen->fullVwin.sb_info.width != 0) !=
		screen->save_modes[DP_RXVT_SCROLLBAR]) {
		ToggleScrollBar(xw);
	    }
	    break;
	case srm_132COLS:	/* 132 column mode              */
	    DoRM(DP_X_DECCOLM, screen->c132);
	    update_allow132();
	    break;
	case srm_CURSES_HACK:	/* curses hack                  */
	    DoRM(DP_X_MORE, screen->curses);
	    update_cursesemul();
	    break;
	case srm_DECNRCM:	/* national charset (VT220) */
	    if (screen->vtXX_level >= 2) {
		if (bitcpy(&xw->flags, screen->save_modes[DP_DECNRCM], NATIONAL))
		    modified_DECNRCM(xw);
	    }
	    break;
	case srm_MARGIN_BELL:	/* margin bell                  */
	    if ((DoRM(DP_X_MARGIN, screen->marginbell)) == 0)
		screen->bellArmed = -1;
	    update_marginbell();
	    break;
	case srm_REVERSEWRAP:	/* reverse wraparound   */
	    bitcpy(&xw->flags, screen->save_modes[DP_X_REVWRAP], REVERSEWRAP);
	    update_reversewrap();
	    break;
	case srm_OPT_ALTBUF_CURSOR:	/* alternate buffer & cursor */
	    /* FALLTHRU */
	case srm_OPT_ALTBUF:
	    /* FALLTHRU */
	case srm_ALTBUF:	/* alternate buffer */
	    if (!xw->misc.titeInhibit) {
		if (screen->save_modes[DP_X_ALTSCRN])
		    ToAlternate(xw, False);
		else
		    FromAlternate(xw);
		/* update_altscreen done by ToAlt and FromAlt */
	    } else if (screen->save_modes[DP_X_ALTSCRN]) {
		do_ti_xtra_scroll(xw);
	    }
	    break;
	case srm_DECNKM:
	    bitcpy(&xw->flags, screen->save_modes[DP_DECKPAM], MODE_DECKPAM);
	    update_appkeypad();
	    break;
	case srm_DECBKM:	/* backarrow mapping */
	    bitcpy(&xw->flags, screen->save_modes[DP_DECBKM], MODE_DECBKM);
	    update_decbkm();
	    break;
	case srm_DECLRMM:	/* left-right */
	    bitcpy(&xw->flags, screen->save_modes[DP_X_LRMM], LEFT_RIGHT);
	    if (IsLeftRightMode(xw)) {
		xterm_ResetDouble(xw);
	    } else {
		reset_lr_margins(screen);
	    }
	    break;
	case srm_DECNCSM:	/* noclear */
	    bitcpy(&xw->flags, screen->save_modes[DP_X_NCSM], NOCLEAR_COLM);
	    break;
	case srm_VT200_MOUSE:	/* mouse bogus sequence         */
	    /* FALLTHRU */
	case srm_VT200_HIGHLIGHT_MOUSE:
	    /* FALLTHRU */
	case srm_BTN_EVENT_MOUSE:
	    /* FALLTHRU */
	case srm_ANY_EVENT_MOUSE:
	    DoRM0(DP_X_MOUSE, screen->send_mouse_pos);
	    really_set_mousemode(xw,
				 screen->send_mouse_pos != MOUSE_OFF,
				 (XtermMouseModes) screen->send_mouse_pos);
	    break;
	case srm_EXT_MODE_MOUSE:
	    /* FALLTHRU */
	case srm_SGR_EXT_MODE_MOUSE:
	    /* FALLTHRU */
	case srm_URXVT_EXT_MODE_MOUSE:
	    DoRM(DP_X_EXT_MOUSE, screen->extend_coords);
	    break;
	case srm_TITE_INHIBIT:
	    if (!xw->misc.titeInhibit) {
		CursorRestore(xw);
	    }
	    break;
	case srm_ALTERNATE_SCROLL:
	    DoRM(DP_ALTERNATE_SCROLL, screen->alternateScroll);
	    break;
	case srm_RXVT_SCROLL_TTY_OUTPUT:
	    DoRM(DP_RXVT_SCROLL_TTY_OUTPUT, screen->scrollttyoutput);
	    update_scrollttyoutput();
	    break;
	case srm_RXVT_SCROLL_TTY_KEYPRESS:
	    DoRM(DP_RXVT_SCROLL_TTY_KEYPRESS, screen->scrollkey);
	    update_scrollkey();
	    break;
	case srm_EIGHT_BIT_META:
	    DoRM(DP_EIGHT_BIT_META, screen->eight_bit_meta);
	    break;
	case srm_DELETE_IS_DEL:
	    DoRM(DP_DELETE_IS_DEL, screen->delete_is_del);
	    update_delete_del();
	    break;
	case srm_KEEP_SELECTION:
	    DoRM(DP_KEEP_SELECTION, screen->keepSelection);
	    update_keepSelection();
	    break;
	case srm_SELECT_TO_CLIPBOARD:
	    DoRM(DP_SELECT_TO_CLIPBOARD, screen->selectToClipboard);
	    update_selectToClipboard();
	    break;
	case srm_BELL_IS_URGENT:
	    DoRM(DP_BELL_IS_URGENT, screen->bellIsUrgent);
	    update_bellIsUrgent();
	    break;
	case srm_POP_ON_BELL:
	    DoRM(DP_POP_ON_BELL, screen->poponbell);
	    update_poponbell();
	    break;
	case srm_LEGACY_FKEYS:
	    xw->keyboard.type = (xtermKeyboardType) screen->save_modes[DP_KEYBOARD_TYPE];
	    break;
	}
    }
}

/*
 * Convert an XTextProperty to a string.
 *
 * This frees the data owned by the XTextProperty, and returns in its place the
 * string, which must be freed by the caller.
 */
static char *
property_to_string(XtermWidget xw, XTextProperty * text)
{
    TScreen *screen = TScreenOf(xw);
    Display *dpy = screen->display;
    char *result = 0;
    char **list;
    int length = 0;
    int rc;

    TRACE(("property_to_string value %p, encoding %s, format %d, nitems %ld\n",
	   text->value,
	   XGetAtomName(dpy, text->encoding),
	   text->format,
	   text->nitems));

	if ((rc = XmbTextPropertyToTextList(dpy, text, &list, &length)) < 0)
	    rc = XTextPropertyToStringList(text, &list, &length);

    if (rc >= 0) {
	int n, c, pass;
	size_t need = 0;

	for (pass = 0; pass < 2; ++pass) {
	    for (n = 0, need = 0; n < length; n++) {
		char *s = list[n];
		while ((c = *s++) != '\0') {
		    if (pass)
			result[need] = (char) c;
		    ++need;
		}
	    }
	    if (pass)
		result[need] = '\0';
	    else
		result = malloc(need + 1);
	    if (result == 0)
		break;
	}
	XFreeStringList(list);
    }
    if (text->value != 0)
	XFree(text->value);

    return result;
}

static char *
get_icon_label(XtermWidget xw)
{
    XTextProperty text;
    char *result = 0;

    if (XGetWMIconName(TScreenOf(xw)->display, VShellWindow(xw), &text)) {
	result = property_to_string(xw, &text);
    }
    return result;
}

static char *
get_window_label(XtermWidget xw)
{
    XTextProperty text;
    char *result = 0;

    if (XGetWMName(TScreenOf(xw)->display, VShellWindow(xw), &text)) {
	result = property_to_string(xw, &text);
    }
    return result;
}

/*
 * Report window label (icon or title) in dtterm protocol
 * ESC ] code label ESC backslash
 */
static void
report_win_label(XtermWidget xw,
		 int code,
		 char *text)
{
    unparseputc(xw, ANSI_ESC);
    unparseputc(xw, ']');
    unparseputc(xw, code);

    if (text != 0) {
	int copy = IsTitleMode(xw, tmGetBase16);
	if (copy) {
	    TRACE(("Encoding hex:%s\n", text));
	    text = x_encode_hex(text);
	}
	unparseputs(xw, text);
	if (copy)
	    free(text);
    }

    unparseputc(xw, ANSI_ESC);
    unparseputc(xw, '\\');	/* should be ST */
    unparse_end(xw);
}

/*
 * Window operations (from CDE dtterm description, as well as extensions).
 * See also "allowWindowOps" resource.
 */
static void
window_ops(XtermWidget xw)
{
    TScreen *screen = TScreenOf(xw);
    XWindowChanges values;
    XWindowAttributes win_attrs;
    unsigned value_mask;
    int code = zero_if_default(0);
    char *label;

    TRACE(("window_ops %d\n", code));
    switch (code) {
    case ewRestoreWin:		/* Restore (de-iconify) window */
	if (AllowWindowOps(xw, ewRestoreWin)) {
	    TRACE(("...de-iconify window\n"));
	    XMapWindow(screen->display,
		       VShellWindow(xw));
	}
	break;

    case ewMinimizeWin:	/* Minimize (iconify) window */
	if (AllowWindowOps(xw, ewMinimizeWin)) {
	    TRACE(("...iconify window\n"));
	    XIconifyWindow(screen->display,
			   VShellWindow(xw),
			   DefaultScreen(screen->display));
	}
	break;

    case ewSetWinPosition:	/* Move the window to the given position */
	if (AllowWindowOps(xw, ewSetWinPosition)) {
	    values.x = zero_if_default(1);
	    values.y = zero_if_default(2);
	    TRACE(("...move window to %d,%d\n", values.x, values.y));
	    value_mask = (CWX | CWY);
	    XReconfigureWMWindow(screen->display,
				 VShellWindow(xw),
				 DefaultScreen(screen->display),
				 value_mask,
				 &values);
	}
	break;

    case ewSetWinSizePixels:	/* Resize the window to given size in pixels */
	if (AllowWindowOps(xw, ewSetWinSizePixels)) {
	    RequestResize(xw, optional_param(1), optional_param(2), False);
	}
	break;

    case ewRaiseWin:		/* Raise the window to the front of the stack */
	if (AllowWindowOps(xw, ewRaiseWin)) {
	    TRACE(("...raise window\n"));
	    XRaiseWindow(screen->display, VShellWindow(xw));
	}
	break;

    case ewLowerWin:		/* Lower the window to the bottom of the stack */
	if (AllowWindowOps(xw, ewLowerWin)) {
	    TRACE(("...lower window\n"));
	    XLowerWindow(screen->display, VShellWindow(xw));
	}
	break;

    case ewRefreshWin:		/* Refresh the window */
	if (AllowWindowOps(xw, ewRefreshWin)) {
	    TRACE(("...redraw window\n"));
	    Redraw();
	}
	break;

    case ewSetWinSizeChars:	/* Resize the text-area, in characters */
	if (AllowWindowOps(xw, ewSetWinSizeChars)) {
	    RequestResize(xw, optional_param(1), optional_param(2), True);
	}
	break;


    case ewGetWinState:	/* Report the window's state */
	if (AllowWindowOps(xw, ewGetWinState)) {
	    TRACE(("...get window attributes\n"));
	    xtermGetWinAttrs(screen->display,
			     VWindow(screen),
			     &win_attrs);
	    init_reply(ANSI_CSI);
	    reply.a_pintro = 0;
	    reply.a_nparam = 1;
	    reply.a_param[0] = (ParmType) ((win_attrs.map_state == IsViewable)
					   ? 1
					   : 2);
	    reply.a_inters = 0;
	    reply.a_final = 't';
	    unparseseq(xw, &reply);
	}
	break;

    case ewGetWinPosition:	/* Report the window's position */
	if (AllowWindowOps(xw, ewGetWinPosition)) {
	    TRACE(("...get window position\n"));
	    xtermGetWinAttrs(screen->display,
			     WMFrameWindow(xw),
			     &win_attrs);
	    init_reply(ANSI_CSI);
	    reply.a_pintro = 0;
	    reply.a_nparam = 3;
	    reply.a_param[0] = 3;
	    reply.a_param[1] = (ParmType) win_attrs.x;
	    reply.a_param[2] = (ParmType) win_attrs.y;
	    reply.a_inters = 0;
	    reply.a_final = 't';
	    unparseseq(xw, &reply);
	}
	break;

    case ewGetWinSizePixels:	/* Report the window's size in pixels */
	if (AllowWindowOps(xw, ewGetWinSizePixels)) {
	    TRACE(("...get window size in pixels\n"));
	    init_reply(ANSI_CSI);
	    reply.a_pintro = 0;
	    reply.a_nparam = 3;
	    reply.a_param[0] = 4;
	    reply.a_param[1] = (ParmType) Height(screen);
	    reply.a_param[2] = (ParmType) Width(screen);
	    reply.a_inters = 0;
	    reply.a_final = 't';
	    unparseseq(xw, &reply);
	}
	break;

    case ewGetWinSizeChars:	/* Report the text's size in characters */
	if (AllowWindowOps(xw, ewGetWinSizeChars)) {
	    TRACE(("...get window size in characters\n"));
	    init_reply(ANSI_CSI);
	    reply.a_pintro = 0;
	    reply.a_nparam = 3;
	    reply.a_param[0] = 8;
	    reply.a_param[1] = (ParmType) MaxRows(screen);
	    reply.a_param[2] = (ParmType) MaxCols(screen);
	    reply.a_inters = 0;
	    reply.a_final = 't';
	    unparseseq(xw, &reply);
	}
	break;

    case ewGetIconTitle:	/* Report the icon's label */
	if (AllowWindowOps(xw, ewGetIconTitle)) {
	    TRACE(("...get icon's label\n"));
	    report_win_label(xw, 'L', label = get_icon_label(xw));
	    free(label);
	}
	break;

    case ewGetWinTitle:	/* Report the window's title */
	if (AllowWindowOps(xw, ewGetWinTitle)) {
	    TRACE(("...get window's label\n"));
	    report_win_label(xw, 'l', label = get_window_label(xw));
	    free(label);
	}
	break;

    case ewPushTitle:		/* save the window's title(s) on stack */
	if (AllowWindowOps(xw, ewPushTitle)) {
	    SaveTitle *last = screen->save_title;
	    SaveTitle *item = TypeCalloc(SaveTitle);

	    TRACE(("...push title onto stack\n"));
	    if (item != 0) {
		switch (zero_if_default(1)) {
		case 0:
		    item->iconName = get_icon_label(xw);
		    item->windowName = get_window_label(xw);
		    break;
		case 1:
		    item->iconName = get_icon_label(xw);
		    break;
		case 2:
		    item->windowName = get_window_label(xw);
		    break;
		}
		item->next = last;
		if (item->iconName == 0) {
		    item->iconName = ((last == 0)
				      ? get_icon_label(xw)
				      : x_strdup(last->iconName));
		}
		if (item->windowName == 0) {
		    item->windowName = ((last == 0)
					? get_window_label(xw)
					: x_strdup(last->windowName));
		}
		screen->save_title = item;
	    }
	}
	break;

    case ewPopTitle:		/* restore the window's title(s) from stack */
	if (AllowWindowOps(xw, ewPopTitle)) {
	    SaveTitle *item = screen->save_title;

	    TRACE(("...pop title off stack\n"));
	    if (item != 0) {
		switch (zero_if_default(1)) {
		case 0:
		    ChangeIconName(xw, item->iconName);
		    ChangeTitle(xw, item->windowName);
		    break;
		case 1:
		    ChangeIconName(xw, item->iconName);
		    break;
		case 2:
		    ChangeTitle(xw, item->windowName);
		    break;
		}
		screen->save_title = item->next;
		free(item->iconName);
		free(item->windowName);
		free(item);
	    }
	}
	break;

    default:			/* DECSLPP (24, 25, 36, 48, 72, 144) */
	if (AllowWindowOps(xw, ewSetWinLines)) {
	    if (code >= 24)
		RequestResize(xw, code, -1, True);
	}
	break;
    }
}

/*
 * set a bit in a word given a pointer to the word and a mask.
 */
static int
bitset(unsigned *p, unsigned mask)
{
    unsigned before = *p;
    *p |= mask;
    return (before != *p);
}

/*
 * clear a bit in a word given a pointer to the word and a mask.
 */
static int
bitclr(unsigned *p, unsigned mask)
{
    unsigned before = *p;
    *p &= ~mask;
    return (before != *p);
}

/*
 * Copy bits from one word to another, given a mask
 */
static int
bitcpy(unsigned *p, unsigned q, unsigned mask)
{
    unsigned before = *p;
    bitclr(p, mask);
    bitset(p, q & mask);
    return (before != *p);
}

void
unparseputc1(XtermWidget xw, int c)
{
    if (c >= 0x80 && c <= 0x9F) {
	if (!TScreenOf(xw)->control_eight_bits) {
	    unparseputc(xw, A2E(ANSI_ESC));
	    c = A2E(c - 0x40);
	}
    }
    unparseputc(xw, c);
}

void
unparseseq(XtermWidget xw, ANSI *ap)
{
    int c;
    int i;
    int inters;

    unparseputc1(xw, c = ap->a_type);
    if (c == ANSI_ESC
	|| c == ANSI_DCS
	|| c == ANSI_CSI
	|| c == ANSI_OSC
	|| c == ANSI_PM
	|| c == ANSI_APC
	|| c == ANSI_SS3) {
	if (ap->a_pintro != 0)
	    unparseputc(xw, ap->a_pintro);
	for (i = 0; i < ap->a_nparam; ++i) {
	    if (i != 0) {
		if (ap->a_delim) {
		    unparseputs(xw, ap->a_delim);
		} else {
		    unparseputc(xw, ';');
		}
	    }
	    if (ap->a_radix[i]) {
		char temp[8];
		sprintf(temp, "%04X", ap->a_param[i] & 0xffff);
		unparseputs(xw, temp);
	    } else {
		unparseputn(xw, (unsigned int) ap->a_param[i]);
	    }
	}
	if ((inters = ap->a_inters) != 0) {
	    for (i = 3; i >= 0; --i) {
		c = CharOf(inters >> (8 * i));
		if (c != 0)
		    unparseputc(xw, c);
	    }
	}
	switch (ap->a_type) {
	case ANSI_DCS:
	    /* FALLTHRU */
	case ANSI_OSC:
	    /* FALLTHRU */
	case ANSI_PM:
	    /* FALLTHRU */
	case ANSI_APC:
	    unparseputc1(xw, ANSI_ST);
	    break;
	default:
	    unparseputc(xw, (char) ap->a_final);
	    break;
	}
    }
    unparse_end(xw);
}

void
unparseputn(XtermWidget xw, unsigned int n)
{
    unsigned int q;

    q = n / 10;
    if (q != 0)
	unparseputn(xw, q);
    unparseputc(xw, (char) ('0' + (n % 10)));
}

void
unparseputs(XtermWidget xw, const char *s)
{
    if (s != 0) {
	while (*s)
	    unparseputc(xw, *s++);
    }
}

void
unparseputc(XtermWidget xw, int c)
{
    TScreen *screen = TScreenOf(xw);
    IChar *buf = screen->unparse_bfr;
    unsigned len;

    if ((screen->unparse_len + 2) >= sizeof(screen->unparse_bfr) / sizeof(IChar))
	  unparse_end(xw);

    len = screen->unparse_len;

    if ((buf[len++] = (IChar) c) == '\r' && (xw->flags & LINEFEED)) {
	buf[len++] = '\n';
    }

    screen->unparse_len = len;

    /* If send/receive mode is reset, we echo characters locally */
    if ((xw->keyboard.flags & MODE_SRM) == 0) {
	(void) doparsing(xw, (unsigned) c, &myState);
    }
}

void
unparse_end(XtermWidget xw)
{
    TScreen *screen = TScreenOf(xw);

    if (screen->unparse_len) {
	writePtyData(screen->respond, screen->unparse_bfr, screen->unparse_len);
	screen->unparse_len = 0;
    }
}

void
ToggleAlternate(XtermWidget xw)
{
    if (TScreenOf(xw)->whichBuf)
	FromAlternate(xw);
    else
	ToAlternate(xw, False);
}

static void
ToAlternate(XtermWidget xw, Bool clearFirst)
{
    TScreen *screen = TScreenOf(xw);

    if (screen->whichBuf == 0) {
	TRACE(("ToAlternate\n"));
	if (!screen->editBuf_index[1])
	    screen->editBuf_index[1] = allocScrnBuf(xw,
						    (unsigned) MaxRows(screen),
						    (unsigned) MaxCols(screen),
						    &screen->editBuf_data[1]);
	SwitchBufs(xw, 1, clearFirst);
	update_altscreen();
    }
}

static void
FromAlternate(XtermWidget xw)
{
    TScreen *screen = TScreenOf(xw);

    if (screen->whichBuf != 0) {
	TRACE(("FromAlternate\n"));
	if (screen->scroll_amt)
	    FlushScroll(xw);
	SwitchBufs(xw, 0, False);
	update_altscreen();
    }
}

static void
SwitchBufs(XtermWidget xw, int toBuf, Bool clearFirst)
{
    TScreen *screen = TScreenOf(xw);
    int rows, top;

    screen->whichBuf = toBuf;
    if (screen->cursor_state)
	HideCursor();

    rows = MaxRows(screen);
    SwitchBufPtrs(screen, toBuf);

    if ((top = INX2ROW(screen, 0)) < rows) {
	if (screen->scroll_amt) {
	    FlushScroll(xw);
	}
	XClearArea(screen->display,
		   VWindow(screen),
		   (int) OriginX(screen),
		   (int) top * FontHeight(screen) + screen->border,
		   (unsigned) Width(screen),
		   (unsigned) ((rows - top) * FontHeight(screen)),
		   False);
	if (clearFirst) {
	    ClearBufRows(xw, top, rows);
	}
    }
    ScrnUpdate(xw, 0, 0, rows, MaxCols(screen), False);
}

Bool
CheckBufPtrs(TScreen *screen)
{
    return (screen->visbuf != 0
	    && screen->editBuf_index[1] != 0);
}

/*
 * Swap buffer line pointers between alternate and regular screens.
 */
void
SwitchBufPtrs(TScreen *screen, int toBuf GCC_UNUSED)
{
    if (CheckBufPtrs(screen)) {
	size_t len = ScrnPointers(screen, (size_t) MaxRows(screen));

	memcpy(screen->save_ptr, screen->visbuf, len);
	memcpy(screen->visbuf, screen->editBuf_index[1], len);
	memcpy(screen->editBuf_index[1], screen->save_ptr, len);
    }
}

void
VTRun(XtermWidget xw)
{
    TScreen *screen = TScreenOf(xw);

    TRACE(("VTRun ...\n"));

    if (!screen->Vshow) {
	set_vt_visibility(True);
    }
    update_vttekmode();
    update_vtshow();
    update_tekshow();
    set_vthide_sensitivity();

    ScrnAllocBuf(xw);

    screen->cursor_state = OFF;
    screen->cursor_set = ON;

    screen->is_running = True;
    if (screen->embed_high && screen->embed_wide) {
	ScreenResize(xw, screen->embed_wide, screen->embed_high, &(xw->flags));
    }
    if (!setjmp(VTend))
	VTparse(xw);
    StopBlinking(screen);
    HideCursor();
    screen->cursor_set = OFF;
    TRACE(("... VTRun\n"));
}

/*ARGSUSED*/
static void
VTExpose(Widget w GCC_UNUSED,
	 XEvent *event,
	 Region region GCC_UNUSED)
{
    DEBUG_MSG("Expose\n");
    if (event->type == Expose)
	HandleExposure(term, event);
}

static void
VTGraphicsOrNoExpose(XEvent *event)
{
    TScreen *screen = TScreenOf(term);
    if (screen->incopy <= 0) {
	screen->incopy = 1;
	if (screen->scrolls > 0)
	    screen->scrolls--;
    }
    if (event->type == GraphicsExpose)
	if (HandleExposure(term, event))
	    screen->cursor_state = OFF;
    if ((event->type == NoExpose)
	|| ((XGraphicsExposeEvent *) event)->count == 0) {
	if (screen->incopy <= 0 && screen->scrolls > 0)
	    screen->scrolls--;
	if (screen->scrolls)
	    screen->incopy = -1;
	else
	    screen->incopy = 0;
    }
}

/*ARGSUSED*/
static void
VTNonMaskableEvent(Widget w GCC_UNUSED,
		   XtPointer closure GCC_UNUSED,
		   XEvent *event,
		   Boolean *cont GCC_UNUSED)
{
    switch (event->type) {
    case GraphicsExpose:
	/* FALLTHRU */
    case NoExpose:
	VTGraphicsOrNoExpose(event);
	break;
    }
}

static void
VTResize(Widget w)
{
    if (XtIsRealized(w)) {
	XtermWidget xw = (XtermWidget) w;
	ScreenResize(xw, xw->core.width, xw->core.height, &xw->flags);
    }
}

#define okDimension(src,dst) ((src <= 32767) \
			  && ((dst = (Dimension) src) == src))

static void
RequestResize(XtermWidget xw, int rows, int cols, Bool text)
{
    TScreen *screen = TScreenOf(xw);
    unsigned long value;
    Dimension replyWidth, replyHeight;
    Dimension askedWidth, askedHeight;
    XtGeometryResult status;
    XWindowAttributes attrs;

    TRACE(("RequestResize(rows=%d, cols=%d, text=%d)\n", rows, cols, text));

    if ((int) (askedWidth = (Dimension) cols) < cols
	|| (int) (askedHeight = (Dimension) rows) < rows)
	return;

    if (askedHeight == 0
	|| askedWidth == 0
	|| xw->misc.limit_resize > 0) {
	xtermGetWinAttrs(XtDisplay(xw),
			 RootWindowOfScreen(XtScreen(xw)), &attrs);
    }

    if (text) {
	if ((value = (unsigned long) rows) != 0) {
	    if (rows < 0)
		value = (unsigned long) MaxRows(screen);
	    value *= (unsigned long) FontHeight(screen);
	    value += (unsigned long) (2 * screen->border);
	    if (!okDimension(value, askedHeight))
		return;
	}

	if ((value = (unsigned long) cols) != 0) {
	    if (cols < 0)
		value = (unsigned long) MaxCols(screen);
	    value *= (unsigned long) FontWidth(screen);
	    value += (unsigned long) ((2 * screen->border)
				      + ScrollbarWidth(screen));
	    if (!okDimension(value, askedWidth))
		return;
	}

    } else {
	if (rows < 0)
	    askedHeight = FullHeight(screen);
	if (cols < 0)
	    askedWidth = FullWidth(screen);
    }

    if (rows == 0)
	askedHeight = (Dimension) attrs.height;
    if (cols == 0)
	askedWidth = (Dimension) attrs.width;

    if (xw->misc.limit_resize > 0) {
	Dimension high = (Dimension) (xw->misc.limit_resize * attrs.height);
	Dimension wide = (Dimension) (xw->misc.limit_resize * attrs.width);
	if ((int) high < attrs.height)
	    high = (Dimension) attrs.height;
	if (askedHeight > high)
	    askedHeight = high;
	if ((int) wide < attrs.width)
	    wide = (Dimension) attrs.width;
	if (askedWidth > wide)
	    askedWidth = wide;
    }

    TRACE(("...requesting resize %dx%d\n", askedHeight, askedWidth));
    status = REQ_RESIZE((Widget) xw,
			askedWidth, askedHeight,
			&replyWidth, &replyHeight);

    if (status == XtGeometryYes ||
	status == XtGeometryDone) {
	ScreenResize(xw, replyWidth, replyHeight, &xw->flags);
    }

    XSync(screen->display, False);	/* synchronize */
    if (xtermAppPending())
	xevents();

    TRACE(("...RequestResize done\n"));
}

static String xterm_trans =
"<ClientMessage>WM_PROTOCOLS: DeleteWindow()\n\
     <MappingNotify>: KeyboardMapping()\n";

int
VTInit(XtermWidget xw)
{
    Widget vtparent = SHELL_OF(xw);

    TRACE(("VTInit {{\n"));

    XtRealizeWidget(vtparent);
    XtOverrideTranslations(vtparent, XtParseTranslationTable(xterm_trans));
    (void) XSetWMProtocols(XtDisplay(vtparent), XtWindow(vtparent),
			   &wm_delete_window, 1);
    TRACE_TRANS("shell", vtparent);
    TRACE_TRANS("vt100", (Widget) (xw));

    ScrnAllocBuf(xw);

    TRACE(("...}} VTInit\n"));
    return (1);
}

static void
VTClassInit(void)
{
    XtAddConverter(XtRString, XtRGravity, XmuCvtStringToGravity,
		   (XtConvertArgList) NULL, (Cardinal) 0);
}

#define fill_Tres(target, source, offset) \
	TScreenOf(target)->Tcolors[offset] = TScreenOf(source)->Tcolors[offset]
#define repairColors(target)	/* nothing */


void
lookupSelectUnit(XtermWidget xw, Cardinal item, String value)
{
    /* *INDENT-OFF* */
    static const struct {
	const char *	name;
	SelectUnit	code;
    } table[] = {
    	{ "char",	Select_CHAR },
    	{ "word",	Select_WORD },
    	{ "line",	Select_LINE },
    	{ "group",	Select_GROUP },
    	{ "page",	Select_PAGE },
    	{ "all",	Select_ALL },
    };
    /* *INDENT-ON* */

    TScreen *screen = TScreenOf(xw);
    String next = x_skip_nonblanks(value);
    Cardinal n;

    screen->selectMap[item] = NSELECTUNITS;
    for (n = 0; n < XtNumber(table); ++n) {
	if (!x_strncasecmp(table[n].name, value, (unsigned) (next - value))) {
	    screen->selectMap[item] = table[n].code;
	    break;
	}
    }
}

static void
ParseOnClicks(XtermWidget wnew, XtermWidget wreq, Cardinal item)
{
    lookupSelectUnit(wnew, item, TScreenOf(wreq)->onClick[item]);
}

/*
 * Parse a comma-separated list, returning a string which the caller must
 * free, and updating the source pointer.
 */
static char *
ParseList(const char **source)
{
    const char *base = *source;
    const char *next;
    size_t size;
    char *value = 0;
    char *result;

    /* ignore empty values */
    while (*base == ',')
	++base;
    if (*base != '\0') {
	next = base;
	while (*next != '\0' && *next != ',')
	    ++next;
	size = (size_t) (1 + next - base);
	value = malloc(size);
	if (value != 0) {
	    memcpy(value, base, size);
	    value[size - 1] = '\0';
	}
	*source = next;
    } else {
	*source = base;
    }
    result = x_strtrim(value);
    free(value);
    return result;
}

static void
set_flags_from_list(char *target,
		    const char *source,
		    const FlagList * list,
		    Cardinal limit)
{
    Cardinal n;
    int value = -1;

    while (!IsEmpty(source)) {
	char *next = ParseList(&source);
	Boolean found = False;

	if (next == 0)
	    break;
	if (isdigit(CharOf(*next))) {
	    char *temp;

	    value = (int) strtol(next, &temp, 0);
	    if (!IsEmpty(temp)) {
		xtermWarning("Expected a number: %s\n", next);
	    } else {
		for (n = 0; n < limit; ++n) {
		    if (list[n].code == value) {
			target[value] = 1;
			found = True;
			break;
		    }
		}
	    }
	} else {
	    for (n = 0; n < limit; ++n) {
		if (!x_strcasecmp(next, list[n].name)) {
		    value = list[n].code;
		    target[value] = 1;
		    found = True;
		    break;
		}
	    }
	}
	if (!found) {
	    xtermWarning("Unrecognized keyword: %s\n", next);
	} else {
	    TRACE(("...found %s (%d)\n", next, value));
	}
	free(next);
    }
}


static void
initializeKeyboardType(XtermWidget xw)
{
    xw->keyboard.type = TScreenOf(xw)->old_fkeys
	? keyboardIsLegacy
	: keyboardIsDefault;
}

#define InitCursorShape(target, source) \
    target->cursor_shape = source->cursor_underline \
	? CURSOR_UNDERLINE \
	: CURSOR_BLOCK

/* ARGSUSED */
static void
VTInitialize(Widget wrequest,
	     Widget new_arg,
	     ArgList args GCC_UNUSED,
	     Cardinal *num_args GCC_UNUSED)
{
#define Kolor(name) TScreenOf(wnew)->name.resource
#define TxtFg(name) !x_strcasecmp(Kolor(Tcolors[TEXT_FG]), Kolor(name))
#define TxtBg(name) !x_strcasecmp(Kolor(Tcolors[TEXT_BG]), Kolor(name))
#define DftFg(name) isDefaultForeground(Kolor(name))
#define DftBg(name) isDefaultBackground(Kolor(name))

#define DATA(name) { #name, ec##name }
    static const FlagList tblColorOps[] =
    {
	DATA(SetColor)
	,DATA(GetColor)
	,DATA(GetAnsiColor)
    };
#undef DATA

#define DATA(name) { #name, ef##name }
    static const FlagList tblFontOps[] =
    {
	DATA(SetFont)
	,DATA(GetFont)
    };
#undef DATA

#define DATA(name) { #name, et##name }
    static const FlagList tblTcapOps[] =
    {
	DATA(SetTcap)
	,DATA(GetTcap)
    };
#undef DATA

#define DATA(name) { #name, ew##name }
    static const FlagList tblWindowOps[] =
    {
	DATA(RestoreWin)
	,DATA(MinimizeWin)
	,DATA(SetWinPosition)
	,DATA(SetWinSizePixels)
	,DATA(RaiseWin)
	,DATA(LowerWin)
	,DATA(RefreshWin)
	,DATA(SetWinSizeChars)
	,DATA(GetWinState)
	,DATA(GetWinPosition)
	,DATA(GetWinSizePixels)
	,DATA(GetWinSizeChars)
	,DATA(GetIconTitle)
	,DATA(GetWinTitle)
	,DATA(PushTitle)
	,DATA(PopTitle)
	,DATA(SetWinLines)
	,DATA(SetXprop)
	,DATA(GetSelection)
	,DATA(SetSelection)
    };
#undef DATA




#define DATA(name) { #name, eb##name }
    static const FlagList tbl8BitMeta[] =
    {
	DATA(Never)
	,DATA(Locale)
    };
#undef DATA

    XtermWidget request = (XtermWidget) wrequest;
    XtermWidget wnew = (XtermWidget) new_arg;
    Widget my_parent = SHELL_OF(wnew);
    int i;
    const char *s;




    TRACE(("VTInitialize wnew %p, %d / %d resources\n",
	   (void *) wnew, XtNumber(xterm_resources), MAXRESOURCES));
    assert(XtNumber(xterm_resources) < MAXRESOURCES);

    /* Zero out the entire "screen" component of "wnew" widget, then do
     * field-by-field assignment of "screen" fields that are named in the
     * resource list.
     */
    memset(TScreenOf(wnew), 0, sizeof(wnew->screen));

    /* DESCO Sys#67660
     * Zero out the entire "keyboard" component of "wnew" widget.
     */
    memset(&wnew->keyboard, 0, sizeof(wnew->keyboard));

    /*
     * The workspace has no resources - clear it.
     */
    memset(&wnew->work, 0, sizeof(wnew->work));

    /* dummy values so that we don't try to Realize the parent shell with height
     * or width of 0, which is illegal in X.  The real size is computed in the
     * xtermWidget's Realize proc, but the shell's Realize proc is called first,
     * and must see a valid size.
     */
    wnew->core.height = wnew->core.width = 1;

    /*
     * The definition of -rv now is that it changes the definition of
     * XtDefaultForeground and XtDefaultBackground.  So, we no longer
     * need to do anything special.
     */
    TScreenOf(wnew)->display = wnew->core.screen->display;

    /* prep getVisualInfo() */
    wnew->visInfo = 0;
    wnew->numVisuals = 0;
    (void) getVisualInfo(wnew);

    /*
     * We use the default foreground/background colors to compare/check if a
     * color-resource has been set.
     */
#define MyBlackPixel(dpy) BlackPixel(dpy,DefaultScreen(dpy))
#define MyWhitePixel(dpy) WhitePixel(dpy,DefaultScreen(dpy))

    if (request->misc.re_verse) {
	wnew->dft_foreground = MyWhitePixel(TScreenOf(wnew)->display);
	wnew->dft_background = MyBlackPixel(TScreenOf(wnew)->display);
    } else {
	wnew->dft_foreground = MyBlackPixel(TScreenOf(wnew)->display);
	wnew->dft_background = MyWhitePixel(TScreenOf(wnew)->display);
    }

    init_Tres(TEXT_FG);
    init_Tres(TEXT_BG);
    repairColors(wnew);

    wnew->old_foreground = T_COLOR(TScreenOf(wnew), TEXT_FG);
    wnew->old_background = T_COLOR(TScreenOf(wnew), TEXT_BG);

    TRACE(("Color resource initialization:\n"));
    TRACE(("   Default foreground 0x%06lx\n", wnew->dft_foreground));
    TRACE(("   Default background 0x%06lx\n", wnew->dft_background));
    TRACE(("   Screen foreground  0x%06lx\n", T_COLOR(TScreenOf(wnew), TEXT_FG)));
    TRACE(("   Screen background  0x%06lx\n", T_COLOR(TScreenOf(wnew), TEXT_BG)));
    TRACE(("   Actual  foreground 0x%06lx\n", wnew->old_foreground));
    TRACE(("   Actual  background 0x%06lx\n", wnew->old_background));

    TScreenOf(wnew)->mouse_button = 0;
    TScreenOf(wnew)->mouse_row = -1;
    TScreenOf(wnew)->mouse_col = -1;

    init_Bres(screen.free_bold_box);
    init_Bres(screen.allowBoldFonts);

    init_Bres(screen.c132);
    init_Bres(screen.curses);
    init_Bres(screen.hp_ll_bc);
    init_Bres(screen.cursor_underline);
    /* resources allow for underline or block, not (yet) bar */
    InitCursorShape(TScreenOf(wnew), TScreenOf(request));
    TRACE(("cursor_shape:%d blinks:%s\n",
	   TScreenOf(wnew)->cursor_shape,
	   BtoS(TScreenOf(wnew)->cursor_blink)));
    init_Ires(screen.border);
    init_Bres(screen.jumpscroll);
    init_Bres(screen.fastscroll);

    init_Bres(screen.old_fkeys);
    wnew->screen.old_fkeys0 = wnew->screen.old_fkeys;

    init_Mres(screen.delete_is_del);
    initializeKeyboardType(wnew);
    init_Bres(screen.bellIsUrgent);
    init_Bres(screen.bellOnReset);
    init_Bres(screen.marginbell);
    init_Bres(screen.multiscroll);
    init_Ires(screen.nmarginbell);
    init_Ires(screen.savelines);
    init_Ires(screen.scrollBarBorder);
    init_Ires(screen.scrolllines);
    init_Bres(screen.alternateScroll);
    init_Bres(screen.scrollttyoutput);
    init_Bres(screen.scrollkey);

    init_Dres(screen.scale_height);
    if (TScreenOf(wnew)->scale_height < 0.9)
	TScreenOf(wnew)->scale_height = (float) 0.9;
    if (TScreenOf(wnew)->scale_height > 1.5)
	TScreenOf(wnew)->scale_height = (float) 1.5;

    init_Bres(misc.autoWrap);
    init_Bres(misc.login_shell);
    init_Bres(misc.reverseWrap);
    init_Bres(misc.scrollbar);
    init_Sres(misc.geo_metry);
    init_Sres(misc.T_geometry);

    init_Sres(screen.term_id);
    for (s = TScreenOf(request)->term_id; *s; s++) {
	if (!isalpha(CharOf(*s)))
	    break;
    }
    TScreenOf(wnew)->terminal_id = atoi(s);
    if (TScreenOf(wnew)->terminal_id < MIN_DECID)
	TScreenOf(wnew)->terminal_id = MIN_DECID;
    if (TScreenOf(wnew)->terminal_id > MAX_DECID)
	TScreenOf(wnew)->terminal_id = MAX_DECID;
    TRACE(("term_id '%s' -> terminal_id %d\n",
	   TScreenOf(wnew)->term_id,
	   TScreenOf(wnew)->terminal_id));

    TScreenOf(wnew)->vtXX_level = (TScreenOf(wnew)->terminal_id / 100);

    init_Ires(screen.title_modes);
    wnew->screen.title_modes0 = wnew->screen.title_modes;

    init_Bres(screen.visualbell);
    init_Bres(screen.flash_line);
    init_Ires(screen.visualBellDelay);
    init_Bres(screen.poponbell);
    init_Ires(misc.limit_resize);

    wnew->misc.re_verse0 = request->misc.re_verse;
    init_Bres(misc.re_verse);
    init_Ires(screen.multiClickTime);
    init_Ires(screen.bellSuppressTime);
    init_Sres(screen.charClass);

    init_Bres(screen.always_highlight);
    init_Bres(screen.brokenSelections);
    init_Bres(screen.cutNewline);
    init_Bres(screen.cutToBeginningOfLine);
    init_Bres(screen.highlight_selection);
    init_Bres(screen.show_wrap_marks);
    init_Bres(screen.i18nSelections);
    init_Bres(screen.keepSelection);
    init_Bres(screen.selectToClipboard);
    init_Bres(screen.trim_selection);

    TScreenOf(wnew)->pointer_cursor = TScreenOf(request)->pointer_cursor;
    init_Ires(screen.pointer_mode);
    wnew->screen.pointer_mode0 = wnew->screen.pointer_mode;

    init_Sres(screen.answer_back);

    wnew->SPS.printer_checked = False;
    init_Sres(SPS.printer_command);
    init_Bres(SPS.printer_autoclose);
    init_Bres(SPS.printer_extent);
    init_Bres(SPS.printer_formfeed);
    init_Bres(SPS.printer_newline);
    init_Ires(SPS.printer_controlmode);
    init_Sres(screen.keyboard_dialect);

    init_Bres(screen.input_eight_bits);
    init_Bres(screen.output_eight_bits);
    init_Bres(screen.control_eight_bits);
    init_Bres(screen.backarrow_key);
    init_Bres(screen.alt_is_not_meta);
    init_Bres(screen.alt_sends_esc);
    init_Bres(screen.meta_sends_esc);

    init_Bres(screen.allowPasteControl0);
    init_Bres(screen.allowSendEvent0);
    init_Bres(screen.allowColorOp0);
    init_Bres(screen.allowFontOp0);
    init_Bres(screen.allowTcapOp0);
    init_Bres(screen.allowTitleOp0);
    init_Bres(screen.allowWindowOp0);
    init_Sres(screen.disallowedColorOps);

    set_flags_from_list(TScreenOf(wnew)->disallow_color_ops,
			TScreenOf(wnew)->disallowedColorOps,
			tblColorOps,
			ecLAST);

    init_Sres(screen.disallowedFontOps);

    set_flags_from_list(TScreenOf(wnew)->disallow_font_ops,
			TScreenOf(wnew)->disallowedFontOps,
			tblFontOps,
			efLAST);

    init_Sres(screen.disallowedTcapOps);

    set_flags_from_list(TScreenOf(wnew)->disallow_tcap_ops,
			TScreenOf(wnew)->disallowedTcapOps,
			tblTcapOps,
			etLAST);

    init_Sres(screen.disallowedWinOps);

    set_flags_from_list(TScreenOf(wnew)->disallow_win_ops,
			TScreenOf(wnew)->disallowedWinOps,
			tblWindowOps,
			ewLAST);

    init_Sres(screen.default_string);
    init_Sres(screen.eightbit_select_types);

    /* make a copy so that editres cannot change the resource after startup */
    TScreenOf(wnew)->allowPasteControls = TScreenOf(wnew)->allowPasteControl0;
    TScreenOf(wnew)->allowSendEvents = TScreenOf(wnew)->allowSendEvent0;
    TScreenOf(wnew)->allowColorOps = TScreenOf(wnew)->allowColorOp0;
    TScreenOf(wnew)->allowFontOps = TScreenOf(wnew)->allowFontOp0;
    TScreenOf(wnew)->allowTcapOps = TScreenOf(wnew)->allowTcapOp0;
    TScreenOf(wnew)->allowTitleOps = TScreenOf(wnew)->allowTitleOp0;
    TScreenOf(wnew)->allowWindowOps = TScreenOf(wnew)->allowWindowOp0;

    init_Bres(screen.quiet_grab);
    init_Bres(misc.signalInhibit);
    init_Bres(misc.titeInhibit);
    init_Bres(misc.tiXtraScroll);
    init_Bres(misc.cdXtraScroll);
    init_Bres(misc.dynamicColors);
    for (i = fontMenu_font1; i <= fontMenu_lastBuiltin; i++) {
	init_Sres2(screen.MenuFontName, i);
    }
    init_Ires(misc.fontWarnings);
#define DefaultFontNames TScreenOf(wnew)->menu_font_names[fontMenu_default]
    init_Sres(misc.default_font.f_n);
    init_Sres(misc.default_font.f_b);
    DefaultFontNames[fNorm] = x_strdup(wnew->misc.default_font.f_n);
    DefaultFontNames[fBold] = x_strdup(wnew->misc.default_font.f_b);
    TScreenOf(wnew)->EscapeFontName() = NULL;
    TScreenOf(wnew)->SelectFontName() = NULL;

    TScreenOf(wnew)->menu_font_number = fontMenu_default;
    init_Sres(screen.initial_font);
    if (TScreenOf(wnew)->initial_font != 0) {
	int result = xtermGetFont(TScreenOf(wnew)->initial_font);
	if (result >= 0)
	    TScreenOf(wnew)->menu_font_number = result;
    }

    /*
     * Decode the resources that control the behavior on multiple mouse clicks.
     * A single click is always bound to normal character selection, but the
     * other flavors can be changed.
     */
    for (i = 0; i < NSELECTUNITS; ++i) {
	int ck = (i + 1);
	TScreenOf(wnew)->maxClicks = ck;
	if (i == Select_CHAR)
	    TScreenOf(wnew)->selectMap[i] = Select_CHAR;
	else if (TScreenOf(request)->onClick[i] != 0)
	    ParseOnClicks(wnew, request, (unsigned) i);
	else if (i <= Select_LINE)
	    TScreenOf(wnew)->selectMap[i] = (SelectUnit) i;
	else
	    break;
	TRACE(("on%dClicks %s=%d\n", ck,
	       NonNull(TScreenOf(request)->onClick[i]),
	       TScreenOf(wnew)->selectMap[i]));
	if (TScreenOf(wnew)->selectMap[i] == NSELECTUNITS)
	    break;
    }
    TRACE(("maxClicks %d\n", TScreenOf(wnew)->maxClicks));

    init_Tres(MOUSE_FG);
    init_Tres(MOUSE_BG);
    init_Tres(TEXT_CURSOR);
    init_Sres(screen.eight_bit_meta_s);
    wnew->screen.eight_bit_meta =
	extendedBoolean(request->screen.eight_bit_meta_s, tbl8BitMeta, uLast);
    if (wnew->screen.eight_bit_meta == ebLocale) {
	{
	    wnew->screen.eight_bit_meta = ebTrue;
	    TRACE(("...eightBitMeta is true due to locale\n"));
	}
    }

    init_Bres(screen.always_bold_mode);
    init_Bres(screen.bold_mode);
    init_Bres(screen.underline);

    wnew->cur_foreground = 0;
    wnew->cur_background = 0;

    wnew->keyboard.flags = MODE_SRM;

    if (TScreenOf(wnew)->backarrow_key)
	wnew->keyboard.flags |= MODE_DECBKM;
    TRACE(("initialized DECBKM %s\n",
	   BtoS(wnew->keyboard.flags & MODE_DECBKM)));

    /* look for focus related events on the shell, because we need
     * to care about the shell's border being part of our focus.
     */
    TRACE(("adding event handlers for my_parent %p\n", (void *) my_parent));
    XtAddEventHandler(my_parent, EnterWindowMask, False,
		      HandleEnterWindow, (Opaque) NULL);
    XtAddEventHandler(my_parent, LeaveWindowMask, False,
		      HandleLeaveWindow, (Opaque) NULL);
    XtAddEventHandler(my_parent, FocusChangeMask, False,
		      HandleFocusChange, (Opaque) NULL);
    XtAddEventHandler((Widget) wnew, 0L, True,
		      VTNonMaskableEvent, (Opaque) NULL);
    XtAddEventHandler((Widget) wnew, PropertyChangeMask, False,
		      HandleBellPropertyChange, (Opaque) NULL);


    TScreenOf(wnew)->bellInProgress = False;

    set_character_class(TScreenOf(wnew)->charClass);

    /* create it, but don't realize it */
    ScrollBarOn(wnew, True);

    /* make sure that the resize gravity acceptable */
    if (!GravityIsNorthWest(wnew) &&
	!GravityIsSouthWest(wnew)) {
	char value[80];
	String temp[2];
	Cardinal nparams = 1;

	sprintf(value, "%d", wnew->misc.resizeGravity);
	temp[0] = value;
	temp[1] = 0;
	XtAppWarningMsg(app_con, "rangeError", "resizeGravity", "XTermError",
			"unsupported resizeGravity resource value (%s)",
			temp, &nparams);
	wnew->misc.resizeGravity = SouthWestGravity;
    }

    if (TScreenOf(wnew)->savelines < 0)
	TScreenOf(wnew)->savelines = 0;

    init_Bres(screen.awaitInput);

    wnew->flags = 0;
    if (!TScreenOf(wnew)->jumpscroll)
	wnew->flags |= SMOOTHSCROLL;
    if (wnew->misc.reverseWrap)
	wnew->flags |= REVERSEWRAP;
    if (wnew->misc.autoWrap)
	wnew->flags |= WRAPAROUND;
    if (wnew->misc.re_verse != wnew->misc.re_verse0)
	wnew->flags |= REVERSE_VIDEO;
    if (TScreenOf(wnew)->c132)
	wnew->flags |= IN132COLUMNS;

    wnew->initflags = wnew->flags;


    init_Ires(misc.appcursorDefault);
    if (wnew->misc.appcursorDefault)
	wnew->keyboard.flags |= MODE_DECCKM;

    init_Ires(misc.appkeypadDefault);
    if (wnew->misc.appkeypadDefault)
	wnew->keyboard.flags |= MODE_DECKPAM;

    initLineData(wnew);
    return;
}

void
releaseCursorGCs(XtermWidget xw)
{
    TScreen *screen = TScreenOf(xw);
    VTwin *win = WhichVWin(screen);
    int n;

    for_each_curs_gc(n) {
	freeCgs(xw, win, (CgsEnum) n);
    }
}

void
releaseWindowGCs(XtermWidget xw, VTwin *win)
{
    int n;

    for_each_text_gc(n) {
	freeCgs(xw, win, (CgsEnum) n);
    }
}

#define TRACE_FREE_LEAK(name) \
	if (name) { \
	    TRACE(("freed " #name ": %p\n", (const void *) name)); \
	    free((void *) name); \
	    name = 0; \
	}

#define FREE_LEAK(name) \
	if (name) { \
	    free((void *) name); \
	    name = 0; \
	}


static void
VTDestroy(Widget w GCC_UNUSED)
{
}

/*ARGSUSED*/
static void
VTRealize(Widget w,
	  XtValueMask * valuemask,
	  XSetWindowAttributes * values)
{
    XtermWidget xw = (XtermWidget) w;
    TScreen *screen = TScreenOf(xw);

    const VTFontNames *myfont;
    struct Xinerama_geometry pos;
    int pr;
    Atom pid_atom;
    int i;

    TRACE(("VTRealize\n"));


    TabReset(xw->tabs);

    if (screen->menu_font_number == fontMenu_default) {
	myfont = &(xw->misc.default_font);
    } else {
	myfont = xtermFontName(screen->MenuFontName(screen->menu_font_number));
    }
    memset(screen->fnts, 0, sizeof(screen->fnts));

    if (!xtermLoadFont(xw,
		       myfont,
		       False,
		       screen->menu_font_number)) {
	if (XmuCompareISOLatin1(myfont->f_n, DEFFONT) != 0) {
	    char *use_font = x_strdup(DEFFONT);
	    xtermWarning("unable to open font \"%s\", trying \"%s\"....\n",
			 myfont->f_n, use_font);
	    (void) xtermLoadFont(xw,
				 xtermFontName(use_font),
				 False,
				 screen->menu_font_number);
	    screen->MenuFontName(screen->menu_font_number) = use_font;
	}
    }

    /* really screwed if we couldn't open default font */
    if (!screen->fnts[fNorm].fs) {
	xtermWarning("unable to locate a suitable font\n");
	Exit(1);
    }

    /* making cursor */
    if (!screen->pointer_cursor) {
	screen->pointer_cursor =
	    make_colored_cursor(XC_xterm,
				T_COLOR(screen, MOUSE_FG),
				T_COLOR(screen, MOUSE_BG));
    } else {
	recolor_cursor(screen,
		       screen->pointer_cursor,
		       T_COLOR(screen, MOUSE_FG),
		       T_COLOR(screen, MOUSE_BG));
    }

    /* set defaults */
    pos.x = 1;
    pos.y = 1;
    pos.w = 80;
    pos.h = 24;

    TRACE(("parsing geo_metry %s\n", NonNull(xw->misc.geo_metry)));
    pr = XParseXineramaGeometry(screen->display, xw->misc.geo_metry, &pos);
    TRACE(("... position %d,%d size %dx%d\n", pos.y, pos.x, pos.h, pos.w));

    set_max_col(screen, (int) (pos.w - 1));	/* units in character cells */
    set_max_row(screen, (int) (pos.h - 1));	/* units in character cells */
    xtermUpdateFontInfo(xw, False);

    pos.w = screen->fullVwin.fullwidth;
    pos.h = screen->fullVwin.fullheight;

    TRACE(("... border widget %d parent %d shell %d\n",
	   BorderWidth(xw),
	   BorderWidth(XtParent(xw)),
	   BorderWidth(SHELL_OF(xw))));

    if ((pr & XValue) && (XNegative & pr)) {
	pos.x = (Position) (pos.x + (pos.scr_w
				     - (int) pos.w
				     - (BorderWidth(XtParent(xw)) * 2)));
    }
    if ((pr & YValue) && (YNegative & pr)) {
	pos.y = (Position) (pos.y + (pos.scr_h
				     - (int) pos.h
				     - (BorderWidth(XtParent(xw)) * 2)));
    }
    pos.x = (Position) (pos.x + pos.scr_x);
    pos.y = (Position) (pos.y + pos.scr_y);

    /* set up size hints for window manager; min 1 char by 1 char */
    getXtermSizeHints(xw);
    xtermSizeHints(xw, (xw->misc.scrollbar
			? (screen->scrollWidget->core.width
			   + BorderWidth(screen->scrollWidget))
			: 0));

    xw->hints.x = pos.x;
    xw->hints.y = pos.y;
    if ((XValue & pr) || (YValue & pr)) {
	xw->hints.flags |= USSize | USPosition;
	xw->hints.flags |= PWinGravity;
	switch (pr & (XNegative | YNegative)) {
	case 0:
	    xw->hints.win_gravity = NorthWestGravity;
	    break;
	case XNegative:
	    xw->hints.win_gravity = NorthEastGravity;
	    break;
	case YNegative:
	    xw->hints.win_gravity = SouthWestGravity;
	    break;
	default:
	    xw->hints.win_gravity = SouthEastGravity;
	    break;
	}
    } else {
	/* set a default size, but do *not* set position */
	xw->hints.flags |= PSize;
    }
    xw->hints.height = xw->hints.base_height
	+ xw->hints.height_inc * MaxRows(screen);
    xw->hints.width = xw->hints.base_width
	+ xw->hints.width_inc * MaxCols(screen);

    if ((WidthValue & pr) || (HeightValue & pr))
	xw->hints.flags |= USSize;
    else
	xw->hints.flags |= PSize;

    /*
     * Note that the size-hints are for the shell, while the resize-request
     * is for the vt100 widget.  They are not the same size.
     */
    (void) REQ_RESIZE((Widget) xw,
		      (Dimension) pos.w, (Dimension) pos.h,
		      &xw->core.width, &xw->core.height);

    /* XXX This is bogus.  We are parsing geometries too late.  This
     * is information that the shell widget ought to have before we get
     * realized, so that it can do the right thing.
     */
    if (xw->hints.flags & USPosition)
	XMoveWindow(XtDisplay(xw), VShellWindow(xw),
		    xw->hints.x, xw->hints.y);

    TRACE(("%s@%d -- ", __FILE__, __LINE__));
    TRACE_HINTS(&xw->hints);
    XSetWMNormalHints(XtDisplay(xw), VShellWindow(xw), &xw->hints);
    TRACE(("%s@%d -- ", __FILE__, __LINE__));
    TRACE_WM_HINTS(xw);

    if ((pid_atom = XInternAtom(XtDisplay(xw), "_NET_WM_PID", False)) != None) {
	/* XChangeProperty format 32 really is "long" */
	unsigned long pid_l = (unsigned long) getpid();
	TRACE(("Setting _NET_WM_PID property to %lu\n", pid_l));
	XChangeProperty(XtDisplay(xw), VShellWindow(xw),
			pid_atom, XA_CARDINAL, 32, PropModeReplace,
			(unsigned char *) &pid_l, 1);
    }

    XFlush(XtDisplay(xw));	/* get it out to window manager */

    /* use ForgetGravity instead of SouthWestGravity because translating
       the Expose events for ConfigureNotifys is too hard */
    values->bit_gravity = (GravityIsNorthWest(xw)
			   ? NorthWestGravity
			   : ForgetGravity);
    screen->fullVwin.window = XtWindow(xw) =
	XCreateWindow(XtDisplay(xw), XtWindow(XtParent(xw)),
		      xw->core.x, xw->core.y,
		      xw->core.width, xw->core.height, BorderWidth(xw),
		      (int) xw->core.depth,
		      InputOutput, CopyFromParent,
		      *valuemask | CWBitGravity, values);
    screen->event_mask = values->event_mask;

    set_cursor_gcs(xw);

    /* Reset variables used by ANSI emulation. */

    resetCharsets(screen);

    XDefineCursor(screen->display, VShellWindow(xw), screen->pointer_cursor);

    set_cur_col(screen, 0);
    set_cur_row(screen, 0);
    set_max_col(screen, Width(screen) / screen->fullVwin.f_width - 1);
    set_max_row(screen, Height(screen) / screen->fullVwin.f_height - 1);
    reset_margins(screen);

    memset(screen->sc, 0, sizeof(screen->sc));

    /* Mark screen buffer as unallocated.  We wait until the run loop so
       that the child process does not fork and exec with all the dynamic
       memory it will never use.  If we were to do it here, the
       swap space for new process would be huge for huge savelines. */
    {
	screen->visbuf = NULL;
	screen->saveBuf_index = NULL;
    }

    ResetWrap(screen);
    screen->scrolls = screen->incopy = 0;
    xtermSetCursorBox(screen);

    screen->savedlines = 0;

    for (i = 0; i < 2; ++i) {
	screen->whichBuf = !screen->whichBuf;
	CursorSave(xw);
    }

	xtermLoadIcon(xw);

    /*
     * Do this last, since it may change the layout via a resize.
     */
    if (xw->misc.scrollbar) {
	screen->fullVwin.sb_info.width = 0;
	ScrollBarOn(xw, False);
    }

    return;
}


static void
set_cursor_outline_gc(XtermWidget xw,
		      Bool filled,
		      Pixel fg,
		      Pixel bg,
		      Pixel cc)
{
    TScreen *screen = TScreenOf(xw);
    VTwin *win = WhichVWin(screen);
    CgsEnum cgsId = gcVTcursOutline;

    if (cc == bg)
	cc = fg;

    if (filled) {
	setCgsFore(xw, win, cgsId, bg);
	setCgsBack(xw, win, cgsId, cc);
    } else {
	setCgsFore(xw, win, cgsId, cc);
	setCgsBack(xw, win, cgsId, bg);
    }
}

static Boolean
VTSetValues(Widget cur,
	    Widget request GCC_UNUSED,
	    Widget wnew,
	    ArgList args GCC_UNUSED,
	    Cardinal *num_args GCC_UNUSED)
{
    XtermWidget curvt = (XtermWidget) cur;
    XtermWidget newvt = (XtermWidget) wnew;
    Boolean refresh_needed = False;
    Boolean fonts_redone = False;

    if ((T_COLOR(TScreenOf(curvt), TEXT_BG) !=
	 T_COLOR(TScreenOf(newvt), TEXT_BG)) ||
	(T_COLOR(TScreenOf(curvt), TEXT_FG) !=
	 T_COLOR(TScreenOf(newvt), TEXT_FG)) ||
	(TScreenOf(curvt)->MenuFontName(TScreenOf(curvt)->menu_font_number) !=
	 TScreenOf(newvt)->MenuFontName(TScreenOf(newvt)->menu_font_number)) ||
	(curvt->misc.default_font.f_n != newvt->misc.default_font.f_n)) {
	if (curvt->misc.default_font.f_n != newvt->misc.default_font.f_n)
	    TScreenOf(newvt)->MenuFontName(fontMenu_default) = newvt->misc.default_font.f_n;
	if (xtermLoadFont(newvt,
			  xtermFontName(TScreenOf(newvt)->MenuFontName(TScreenOf(curvt)->menu_font_number)),
			  True, TScreenOf(newvt)->menu_font_number)) {
	    /* resizing does the redisplay, so don't ask for it here */
	    refresh_needed = True;
	    fonts_redone = True;
	} else if (curvt->misc.default_font.f_n != newvt->misc.default_font.f_n)
	    TScreenOf(newvt)->MenuFontName(fontMenu_default) = curvt->misc.default_font.f_n;
    }
    if (!fonts_redone
	&& (T_COLOR(TScreenOf(curvt), TEXT_CURSOR) !=
	    T_COLOR(TScreenOf(newvt), TEXT_CURSOR))) {
	if (set_cursor_gcs(newvt))
	    refresh_needed = True;
    }
    if (curvt->misc.re_verse != newvt->misc.re_verse) {
	newvt->flags ^= REVERSE_VIDEO;
	ReverseVideo(newvt);
	/* ReverseVideo toggles */
	newvt->misc.re_verse = (Boolean) (!newvt->misc.re_verse);
	refresh_needed = True;
    }
    if ((T_COLOR(TScreenOf(curvt), MOUSE_FG) !=
	 T_COLOR(TScreenOf(newvt), MOUSE_FG)) ||
	(T_COLOR(TScreenOf(curvt), MOUSE_BG) !=
	 T_COLOR(TScreenOf(newvt), MOUSE_BG))) {
	recolor_cursor(TScreenOf(newvt),
		       TScreenOf(newvt)->pointer_cursor,
		       T_COLOR(TScreenOf(newvt), MOUSE_FG),
		       T_COLOR(TScreenOf(newvt), MOUSE_BG));
	refresh_needed = True;
    }
    if (curvt->misc.scrollbar != newvt->misc.scrollbar) {
	ToggleScrollBar(newvt);
    }

    return refresh_needed;
}

#define setGC(code) set_at = __LINE__, currentCgs = code

#define OutsideSelection(screen,srow,scol)  \
	 ((srow) > (screen)->endH.row || \
	  ((srow) == (screen)->endH.row && \
	   (scol) >= (screen)->endH.col) || \
	  (srow) < (screen)->startH.row || \
	  ((srow) == (screen)->startH.row && \
	   (scol) < (screen)->startH.col))

/*
 * Shows cursor at new cursor position in screen.
 */
void
ShowCursor(void)
{
    XtermWidget xw = term;
    TScreen *screen = TScreenOf(xw);
    int x, y;
    IChar base;
    unsigned flags;
    CellColor fg_bg = 0;
    GC currentGC;
    GC outlineGC;
    CgsEnum currentCgs = gcMAX;
    VTwin *currentWin = WhichVWin(screen);
    int set_at;
    Bool in_selection;
    Bool reversed;
    Bool filled;
    Pixel fg_pix;
    Pixel bg_pix;
    Pixel tmp;
    int cursor_col;
    CLineData *ld = 0;

    if (screen->cursor_state == BLINKED_OFF)
	return;

    if (screen->eventMode != NORMAL)
	return;

    if (INX2ROW(screen, screen->cur_row) > screen->max_row)
	return;

    screen->cursorp.row = screen->cur_row;
    cursor_col = screen->cursorp.col = screen->cur_col;
    screen->cursor_moved = False;


    ld = getLineData(screen, screen->cur_row);

    base = ld->charData[cursor_col];
    flags = ld->attribs[cursor_col];

    if_OPT_WIDE_CHARS(screen, {
	if (base == HIDDEN_CHAR && cursor_col > 0) {
	    /* if cursor points to non-initial part of wide character,
	     * back it up
	     */
	    --cursor_col;
	    base = ld->charData[cursor_col];
	}
	my_col = cursor_col;
	if (base == 0)
	    base = ' ';
	if (isWide((int) base))
	    my_col += 1;
    });

    if (base == 0) {
	base = ' ';
    }

    /*
     * Compare the current cell to the last set of colors used for the
     * cursor and update the GC's if needed.
     */
    if_OPT_ISO_COLORS(screen, {
	fg_bg = ld->color[cursor_col];
    });

    fg_pix = getXtermForeground(xw, flags, (int) extract_fg(xw, fg_bg, flags));
    bg_pix = getXtermBackground(xw, flags, (int) extract_bg(xw, fg_bg, flags));

    /*
     * If we happen to have the same foreground/background colors, choose
     * a workable foreground color from which we can obtain a visible cursor.
     */
    if (fg_pix == bg_pix) {
	long bg_diff = (long) (bg_pix - T_COLOR(TScreenOf(xw), TEXT_BG));
	long fg_diff = (long) (bg_pix - T_COLOR(TScreenOf(xw), TEXT_FG));
	if (bg_diff < 0)
	    bg_diff = -bg_diff;
	if (fg_diff < 0)
	    fg_diff = -fg_diff;
	if (bg_diff < fg_diff) {
	    fg_pix = T_COLOR(TScreenOf(xw), TEXT_FG);
	} else {
	    fg_pix = T_COLOR(TScreenOf(xw), TEXT_BG);
	}
    }

    if (OutsideSelection(screen, screen->cur_row, screen->cur_col))
	in_selection = False;
    else
	in_selection = True;

    reversed = ReverseOrHilite(screen, flags, in_selection);

    /* This is like updatedXtermGC(), except that we have to worry about
     * whether the window has focus, since in that case we want just an
     * outline for the cursor.
     */
    filled = (screen->select || screen->always_highlight) && isCursorBlock(screen);
    if (filled) {
	if (reversed) {		/* text is reverse video */
	    if (getCgsGC(xw, currentWin, gcVTcursNormal)) {
		setGC(gcVTcursNormal);
	    } else {
		if (flags & BOLDATTR(screen)) {
		    setGC(gcBold);
		} else {
		    setGC(gcNorm);
		}
	    }
	    EXCHANGE(fg_pix, bg_pix, tmp);
	} else {		/* normal video */
	    if (getCgsGC(xw, currentWin, gcVTcursReverse)) {
		setGC(gcVTcursReverse);
	    } else {
		if (flags & BOLDATTR(screen)) {
		    setGC(gcBoldReverse);
		} else {
		    setGC(gcNormReverse);
		}
	    }
	}
	if (T_COLOR(screen, TEXT_CURSOR) == (reversed
					     ? xw->dft_background
					     : xw->dft_foreground)) {
	    setCgsBack(xw, currentWin, currentCgs, fg_pix);
	}
	setCgsFore(xw, currentWin, currentCgs, bg_pix);
    } else {			/* not selected */
	if (reversed) {		/* text is reverse video */
	    EXCHANGE(fg_pix, bg_pix, tmp);
	    setGC(gcNormReverse);
	} else {		/* normal video */
	    setGC(gcNorm);
	}
	setCgsFore(xw, currentWin, currentCgs, fg_pix);
	setCgsBack(xw, currentWin, currentCgs, bg_pix);
    }

    if (screen->cursor_busy == 0
	&& (screen->cursor_state != ON || screen->cursor_GC != set_at)) {

	screen->cursor_GC = set_at;
	TRACE(("ShowCursor calling drawXtermText cur(%d,%d) %s-%s, set_at %d\n",
	       screen->cur_row, screen->cur_col,
	       (filled ? "filled" : "outline"),
	       (isCursorBlock(screen) ? "box" :
		isCursorUnderline(screen) ? "underline" : "bar"),
	       set_at));

	currentGC = getCgsGC(xw, currentWin, currentCgs);
	x = LineCursorX(screen, ld, cursor_col);
	y = CursorY(screen, screen->cur_row);

	if (!isCursorBlock(screen)) {
	    /*
	     * Overriding the combination of filled, reversed, in_selection is
	     * too complicated since the underline or bar and the text-cell use
	     * different rules.  Just redraw the text-cell, and draw the
	     * underline or bar on top of it.
	     */
	    HideCursor();

	    /*
	     * Our current-GC is likely to have been modified in HideCursor().
	     * Set up a new request.
	     */
	    if (filled) {
		if (T_COLOR(screen, TEXT_CURSOR) == (reversed
						     ? xw->dft_background
						     : xw->dft_foreground)) {
		    setCgsBack(xw, currentWin, currentCgs, fg_pix);
		}
		setCgsFore(xw, currentWin, currentCgs, bg_pix);
	    } else {
		setCgsFore(xw, currentWin, currentCgs, fg_pix);
		setCgsBack(xw, currentWin, currentCgs, bg_pix);
	    }
	}

	/*
	 * Update the outline-gc, to keep the cursor color distinct from the
	 * background color.
	 */
	set_cursor_outline_gc(xw,
			      filled,
			      fg_pix,
			      bg_pix,
			      T_COLOR(screen, TEXT_CURSOR));

	outlineGC = getCgsGC(xw, currentWin, gcVTcursOutline);
	if (outlineGC == 0)
	    outlineGC = currentGC;

	if (isCursorUnderline(screen)) {

	    /*
	     * Finally, draw the underline.
	     */
	    screen->box->x = (short) x;
	    screen->box->y = (short) (y + FontHeight(screen) - 2);
	    XDrawLines(screen->display, VDrawable(screen), outlineGC,
		       screen->box, NBOX, CoordModePrevious);
	} else if (isCursorBar(screen)) {

	    /*
	     * Or draw the bar.
	     */
	    screen->box->x = (short) x;
	    screen->box->y = (short) y;
	    XDrawLines(screen->display, VWindow(screen), outlineGC,
		       screen->box, NBOX, CoordModePrevious);
	} else {

	    drawXtermText(xw,
			  flags & DRAWX_MASK,
			  0,
			  currentGC, x, y,
			  LineCharSet(screen, ld),
			  &base, 1, 0);


	    if (!filled) {
		screen->box->x = (short) x;
		screen->box->y = (short) y;
		XDrawLines(screen->display, VDrawable(screen), outlineGC,
			   screen->box, NBOX, CoordModePrevious);
	    }
	}
    }
    screen->cursor_state = ON;

    return;
}

/*
 * hide cursor at previous cursor position in screen.
 */
void
HideCursor(void)
{
    XtermWidget xw = term;
    TScreen *screen = TScreenOf(xw);
    GC currentGC;
    int x, y;
    IChar base;
    unsigned flags;
    CellColor fg_bg = 0;
    Bool in_selection;
    int cursor_col;
    CLineData *ld = 0;

    if (screen->cursor_state == OFF)
	return;
    if (INX2ROW(screen, screen->cursorp.row) > screen->max_row)
	return;

    cursor_col = screen->cursorp.col;


    ld = getLineData(screen, screen->cursorp.row);

    base = ld->charData[cursor_col];
    flags = ld->attribs[cursor_col];

    if_OPT_WIDE_CHARS(screen, {
	if (base == HIDDEN_CHAR && cursor_col > 0) {
	    /* if cursor points to non-initial part of wide character,
	     * back it up
	     */
	    --cursor_col;
	    base = ld->charData[cursor_col];
	}
	my_col = cursor_col;
	if (base == 0)
	    base = ' ';
	if (isWide((int) base))
	    my_col += 1;
    });

    if (base == 0) {
	base = ' ';
    }

    /*
     * Compare the current cell to the last set of colors used for the
     * cursor and update the GC's if needed.
     */
    if_OPT_ISO_COLORS(screen, {
	fg_bg = ld->color[cursor_col];
    });

    if (OutsideSelection(screen, screen->cursorp.row, screen->cursorp.col))
	in_selection = False;
    else
	in_selection = True;


    currentGC = updatedXtermGC(xw, flags, fg_bg, in_selection);

    TRACE(("HideCursor calling drawXtermText cur(%d,%d)\n",
	   screen->cursorp.row, screen->cursorp.col));

    x = LineCursorX(screen, ld, cursor_col);
    y = CursorY(screen, screen->cursorp.row);

    drawXtermText(xw,
		  flags & DRAWX_MASK,
		  0,
		  currentGC, x, y,
		  LineCharSet(screen, ld),
		  &base, 1, 0);

    screen->cursor_state = OFF;

    resetXtermGC(xw, flags, in_selection);

    refresh_displayed_graphics(xw,
			       screen->cursorp.col,
			       screen->cursorp.row,
			       1, 1);

    return;
}


void
RestartBlinking(TScreen *screen GCC_UNUSED)
{
}

/*
 * Implement soft or hard (full) reset of the VTxxx emulation.  There are a
 * couple of differences from real DEC VTxxx terminals (to avoid breaking
 * applications which have come to rely on xterm doing this):
 *
 *	+ autowrap mode should be reset (instead it's reset to the resource
 *	  default).
 *	+ the popup menu offers a choice of resetting the savedLines, or not.
 *	  (but the control sequence does this anyway).
 */
static void
ReallyReset(XtermWidget xw, Bool full, Bool saved)
{

    TScreen *screen = TScreenOf(xw);

    if (!XtIsRealized((Widget) xw) || (CURRENT_EMU() != (Widget) xw)) {
	Bell(xw, XkbBI_MinorError, 0);
	return;
    }

    if (saved) {
	screen->savedlines = 0;
	ScrollBarDrawThumb(screen->scrollWidget);
    }

    /* make cursor visible */
    screen->cursor_set = ON;
    InitCursorShape(screen, screen);
    TRACE(("cursor_shape:%d blinks:%s\n",
	   screen->cursor_shape,
	   BtoS(screen->cursor_blink)));

    /* reset scrolling region */
    reset_margins(screen);

    bitclr(&xw->flags, ORIGIN);

    if_OPT_ISO_COLORS(screen, {
	reset_SGR_Colors(xw);
	if (ResetAnsiColorRequest(xw, empty, 0))
	    xtermRepaint(xw);
    });

    /* Reset character-sets to initial state */
    resetCharsets(screen);


    /* Reset DECSCA */
    bitclr(&xw->flags, PROTECTED);
    screen->protected_mode = OFF_PROTECT;

    reset_displayed_graphics(screen);

    if (full) {			/* RIS */
	if (screen->bellOnReset)
	    Bell(xw, XkbBI_TerminalBell, 0);

	/* reset the mouse mode */
	screen->send_mouse_pos = MOUSE_OFF;
	screen->send_focus_pos = OFF;
	screen->extend_coords = 0;
	screen->waitingForTrackInfo = False;
	screen->eventMode = NORMAL;

	xtermShowPointer(xw, True);

	TabReset(xw->tabs);
	xw->keyboard.flags = MODE_SRM;

	screen->old_fkeys = screen->old_fkeys0;
	initializeKeyboardType(xw);

	    if (TScreenOf(xw)->backarrow_key)
		xw->keyboard.flags |= MODE_DECBKM;
	TRACE(("full reset DECBKM %s\n",
	       BtoS(xw->keyboard.flags & MODE_DECBKM)));

	screen->title_modes = screen->title_modes0;
	screen->pointer_mode = screen->pointer_mode0;

	update_appcursor();
	update_appkeypad();
	update_decbkm();
	update_decsdm();
	show_8bit_control(False);
	reset_decudk(xw);

	FromAlternate(xw);
	ClearScreen(xw);
	screen->cursor_state = OFF;
	if (xw->flags & REVERSE_VIDEO)
	    ReverseVideo(xw);

	xw->flags = xw->initflags;
	update_reversevideo();
	update_autowrap();
	update_reversewrap();
	update_autolinefeed();

	screen->jumpscroll = (Boolean) (!(xw->flags & SMOOTHSCROLL));
	update_jumpscroll();

	if (screen->c132 && (xw->flags & IN132COLUMNS)) {
	    Dimension reqWidth = (Dimension) (80 * FontWidth(screen)
					      + 2 * screen->border
					      + ScrollbarWidth(screen));
	    Dimension reqHeight = (Dimension) (FontHeight(screen)
					       * MaxRows(screen)
					       + 2 * screen->border);
	    Dimension replyWidth;
	    Dimension replyHeight;

	    TRACE(("Making resize-request to restore 80-columns %dx%d\n",
		   reqHeight, reqWidth));
	    REQ_RESIZE((Widget) xw,
		       reqWidth,
		       reqHeight,
		       &replyWidth, &replyHeight);
	    repairSizeHints();
	    XSync(screen->display, False);	/* synchronize */
	    if (xtermAppPending())
		xevents();
	}

	CursorSet(screen, 0, 0, xw->flags);
	CursorSave(xw);
    } else {			/* DECSTR */
	/*
	 * There's a tiny difference, to accommodate usage of xterm.
	 * We reset autowrap to the resource values rather than turning
	 * it off.
	 */
	UIntClr(xw->keyboard.flags, (MODE_DECCKM | MODE_KAM | MODE_DECKPAM));
	bitcpy(&xw->flags, xw->initflags, WRAPAROUND | REVERSEWRAP);
	bitclr(&xw->flags, INSERT | INVERSE | BOLD | BLINK | UNDERLINE | INVISIBLE);
	if_OPT_ISO_COLORS(screen, {
	    reset_SGR_Colors(xw);
	});
	update_appcursor();
	update_autowrap();
	update_reversewrap();

	CursorSave(xw);
	screen->sc[screen->whichBuf].row =
	    screen->sc[screen->whichBuf].col = 0;
    }
}

void
VTReset(XtermWidget xw, Bool full, Bool saved)
{
    ReallyReset(xw, full, saved);
    longjmp(vtjmpbuf, 1);	/* force ground state in parser */
}

/*
 * set_character_class - takes a string of the form
 *
 *   low[-high]:val[,low[-high]:val[...]]
 *
 * and sets the indicated ranges to the indicated values.
 */
static int
set_character_class(char *s)
{
#define FMT "%s in range string \"%s\" (position %d)\n"
    int i;			/* iterator, index into s */
    int len;			/* length of s */
    int acc;			/* accumulator */
    int low, high;		/* bounds of range [0..127] */
    int base;			/* 8, 10, 16 (octal, decimal, hex) */
    int numbers;		/* count of numbers per range */
    int digits;			/* count of digits in a number */

    if (!s || !s[0])
	return -1;

    base = 10;			/* in case we ever add octal, hex */
    low = high = -1;		/* out of range */

    for (i = 0, len = (int) strlen(s), acc = 0, numbers = digits = 0;
	 i < len; i++) {
	Char c = CharOf(s[i]);

	if (isspace(c)) {
	    continue;
	} else if (isdigit(c)) {
	    acc = acc * base + (c - '0');
	    digits++;
	    continue;
	} else if (c == '-') {
	    low = acc;
	    acc = 0;
	    if (digits == 0) {
		xtermWarning(FMT, "missing number", s, i);
		return (-1);
	    }
	    digits = 0;
	    numbers++;
	    continue;
	} else if (c == ':') {
	    if (numbers == 0)
		low = acc;
	    else if (numbers == 1)
		high = acc;
	    else {
		xtermWarning(FMT, "too many numbers", s, i);
		return (-1);
	    }
	    digits = 0;
	    numbers++;
	    acc = 0;
	    continue;
	} else if (c == ',') {
	    /*
	     * now, process it
	     */

	    if (high < 0) {
		high = low;
		numbers++;
	    }
	    if (numbers != 2) {
		xtermWarning(FMT, "bad value number", s, i);
	    } else if (SetCharacterClassRange(low, high, acc) != 0) {
		xtermWarning(FMT, "bad range", s, i);
	    }

	    low = high = -1;
	    acc = 0;
	    digits = 0;
	    numbers = 0;
	    continue;
	} else {
	    xtermWarning(FMT, "bad character", s, i);
	    return (-1);
	}			/* end if else if ... else */

    }

    if (low < 0 && high < 0)
	return (0);

    /*
     * now, process it
     */

    if (high < 0)
	high = low;
    if (numbers < 1 || numbers > 2) {
	xtermWarning(FMT, "bad value number", s, i);
    } else if (SetCharacterClassRange(low, high, acc) != 0) {
	xtermWarning(FMT, "bad range", s, i);
    }

    return (0);
#undef FMT
}

void
getKeymapResources(Widget w,
		   const char *mapName,
		   const char *mapClass,
		   const char *type,
		   void *result,
		   size_t size)
{
    XtResource key_resources[1];
    key_resources[0].resource_name = XtNtranslations;
    key_resources[0].resource_class = XtCTranslations;
    key_resources[0].resource_type = (char *) type;
    key_resources[0].resource_size = (Cardinal) size;
    key_resources[0].resource_offset = 0;
    key_resources[0].default_type = key_resources[0].resource_type;
    key_resources[0].default_addr = 0;
    XtGetSubresources(w, (XtPointer) result, mapName, mapClass,
		      key_resources, (Cardinal) 1, NULL, (Cardinal) 0);
}

/* ARGSUSED */
static void
HandleKeymapChange(Widget w,
		   XEvent *event GCC_UNUSED,
		   String *params,
		   Cardinal *param_count)
{
    static XtTranslations keymap, original;
    char mapName[1000];
    char mapClass[1000];
    char *pmapName;
    char *pmapClass;
    size_t len;

    TRACE(("HandleKeymapChange(%#lx, %s)\n",
	   w,
	   (*param_count
	    ? params[0]
	    : "missing")));

    if (*param_count != 1)
	return;

    if (original == NULL) {
	TRACE(("...saving original keymap-translations\n"));
	original = w->core.tm.translations;
    }

    if (strcmp(params[0], "None") == 0) {
	TRACE(("...restoring original keymap-translations\n"));
	XtOverrideTranslations(w, original);
    } else {

	len = strlen(params[0]) + 7;

	pmapName = (char *) MyStackAlloc(len, mapName);
	pmapClass = (char *) MyStackAlloc(len, mapClass);
	if (pmapName == NULL
	    || pmapClass == NULL) {
	    SysError(ERROR_KMMALLOC1);
	} else {

	    (void) sprintf(pmapName, "%sKeymap", params[0]);
	    (void) strcpy(pmapClass, pmapName);
	    if (islower(CharOf(pmapClass[0])))
		pmapClass[0] = x_toupper(pmapClass[0]);
	    getKeymapResources(w, pmapName, pmapClass, XtRTranslationTable,
			       &keymap, sizeof(keymap));
	    if (keymap != NULL) {
		TRACE(("...applying keymap \"%s\"\n", pmapName));
		XtOverrideTranslations(w, keymap);
	    } else {
		TRACE(("...found no match for keymap \"%s\"\n", pmapName));
	    }

	    MyStackFree(pmapName, mapName);
	    MyStackFree(pmapClass, mapClass);
	}
    }
}

/* ARGSUSED */
static void
HandleBell(Widget w GCC_UNUSED,
	   XEvent *event GCC_UNUSED,
	   String *params,	/* [0] = volume */
	   Cardinal *param_count)	/* 0 or 1 */
{
    int percent = (*param_count) ? atoi(params[0]) : 0;

    Bell(term, XkbBI_TerminalBell, percent);
}

/* ARGSUSED */
static void
HandleVisualBell(Widget w GCC_UNUSED,
		 XEvent *event GCC_UNUSED,
		 String *params GCC_UNUSED,
		 Cardinal *param_count GCC_UNUSED)
{
    VisualBell();
}

/* ARGSUSED */
static void
HandleIgnore(Widget w,
	     XEvent *event,
	     String *params GCC_UNUSED,
	     Cardinal *param_count GCC_UNUSED)
{
    XtermWidget xw;

    TRACE(("Handle ignore for %p %s\n",
	   (void *) w, visibleEventType(event->type)));
    if ((xw = getXtermWidget(w)) != 0) {
	/* do nothing, but check for funny escape sequences */
	(void) SendMousePosition(xw, event);
    }
}

/* ARGSUSED */
static void
DoSetSelectedFont(Widget w,
		  XtPointer client_data GCC_UNUSED,
		  Atom *selection GCC_UNUSED,
		  Atom *type,
		  XtPointer value,
		  unsigned long *length,
		  int *format)
{
    XtermWidget xw = getXtermWidget(w);

    if (xw == 0) {
	xtermWarning("unexpected widget in DoSetSelectedFont\n");
    } else if (*type != XA_STRING || *format != 8) {
	Bell(xw, XkbBI_MinorError, 0);
    } else {
	Boolean failed = False;
	int oldFont = TScreenOf(xw)->menu_font_number;
	String save = TScreenOf(xw)->SelectFontName();
	char *val;
	char *test = 0;
	char *used = 0;
	unsigned len = (unsigned) *length;
	unsigned tst;

	/*
	 * Some versions of X deliver null-terminated selections, some do not.
	 */
	for (tst = 0; tst < len; ++tst) {
	    if (((char *) value)[tst] == '\0') {
		len = tst;
		break;
	    }
	}

	if (len > 0 && (val = TypeMallocN(char, len + 1)) != 0) {
	    memcpy(val, value, (size_t) len);
	    val[len] = '\0';
	    used = x_strtrim(val);
	    TRACE(("DoSetSelectedFont(%s)\n", used));
	    /* Do some sanity checking to avoid sending a long selection
	       back to the server in an OpenFont that is unlikely to succeed.
	       XLFD allows up to 255 characters and no control characters;
	       we are a little more liberal here. */
	    if (len < 1000
		&& used != 0
		&& !strchr(used, '\n')
		&& (test = x_strdup(used)) != 0) {
		TScreenOf(xw)->SelectFontName() = test;
		if (!xtermLoadFont(term,
				   xtermFontName(used),
				   True,
				   fontMenu_fontsel)) {
		    failed = True;
		    free(test);
		    TScreenOf(xw)->SelectFontName() = save;
		}
	    } else {
		failed = True;
	    }
	    if (failed) {
		(void) xtermLoadFont(term,
				     xtermFontName(TScreenOf(xw)->MenuFontName(oldFont)),
				     True,
				     oldFont);
		Bell(xw, XkbBI_MinorError, 0);
	    }
	    free(used);
	    free(val);
	}
    }
}

void
FindFontSelection(XtermWidget xw, const char *atom_name, Bool justprobe)
{
    TScreen *screen = TScreenOf(xw);
    static AtomPtr *atoms;
    static unsigned int atomCount = 0;
    AtomPtr *pAtom;
    unsigned a;
    Atom target;

    if (!atom_name)
	atom_name = ((screen->mappedSelect && atomCount)
		     ? screen->mappedSelect[0]
		     : "PRIMARY");
    TRACE(("FindFontSelection(%s)\n", atom_name));

    for (pAtom = atoms, a = atomCount; a; a--, pAtom++) {
	if (strcmp(atom_name, XmuNameOfAtom(*pAtom)) == 0) {
	    TRACE(("...found atom %d:%s\n", a + 1, atom_name));
	    break;
	}
    }
    if (!a) {
	atoms = TypeXtReallocN(AtomPtr, atoms, atomCount + 1);
	*(pAtom = &atoms[atomCount]) = XmuMakeAtom(atom_name);
	++atomCount;
	TRACE(("...added atom %d:%s\n", atomCount, atom_name));
    }

    target = XmuInternAtom(XtDisplay(xw), *pAtom);
    if (justprobe) {
	screen->SelectFontName() =
	    XGetSelectionOwner(XtDisplay(xw), target) ? _Font_Selected_ : 0;
	TRACE(("...selected fontname '%s'\n",
	       NonNull(screen->SelectFontName())));
    } else {
	XtGetSelectionValue((Widget) xw, target, XA_STRING,
			    DoSetSelectedFont, NULL,
			    XtLastTimestampProcessed(XtDisplay(xw)));
    }
    return;
}

Bool
set_cursor_gcs(XtermWidget xw)
{
    TScreen *screen = TScreenOf(xw);
    VTwin *win = WhichVWin(screen);

    Pixel cc = T_COLOR(screen, TEXT_CURSOR);
    Pixel fg = T_COLOR(screen, TEXT_FG);
    Pixel bg = T_COLOR(screen, TEXT_BG);
    Bool changed = False;

    /*
     * Let's see, there are three things that have "color":
     *
     *     background
     *     text
     *     cursorblock
     *
     * And, there are four situations when drawing a cursor, if we decide
     * that we like have a solid block of cursor color with the letter
     * that it is highlighting shown in the background color to make it
     * stand out:
     *
     *     selected window, normal video - background on cursor
     *     selected window, reverse video - foreground on cursor
     *     unselected window, normal video - foreground on background
     *     unselected window, reverse video - background on foreground
     *
     * Since the last two are really just normalGC and reverseGC, we only
     * need two new GC's.  Under monochrome, we get the same effect as
     * above by setting cursor color to foreground.
     */

    TRACE(("set_cursor_gcs cc=%#lx, fg=%#lx, bg=%#lx\n", cc, fg, bg));
    if (win != 0 && (cc != bg)) {
	/* set the fonts to the current one */
	setCgsFont(xw, win, gcVTcursNormal, 0);
	setCgsFont(xw, win, gcVTcursFilled, 0);
	setCgsFont(xw, win, gcVTcursReverse, 0);
	setCgsFont(xw, win, gcVTcursOutline, 0);

	/* we have a colored cursor */
	setCgsFore(xw, win, gcVTcursNormal, fg);
	setCgsBack(xw, win, gcVTcursNormal, cc);

	setCgsFore(xw, win, gcVTcursFilled, cc);
	setCgsBack(xw, win, gcVTcursFilled, fg);

	if (screen->always_highlight) {
	    /* both GC's use the same color */
	    setCgsFore(xw, win, gcVTcursReverse, bg);
	    setCgsBack(xw, win, gcVTcursReverse, cc);
	} else {
	    setCgsFore(xw, win, gcVTcursReverse, bg);
	    setCgsBack(xw, win, gcVTcursReverse, cc);
	}
	set_cursor_outline_gc(xw, screen->always_highlight, fg, bg, cc);
	changed = True;
    }

    if (changed) {
	TRACE(("...set_cursor_gcs - done\n"));
    }
    return changed;
}

/*
 * Build up the default translations string, allowing the user to suppress
 * some of the features.
 */
void
VTInitTranslations(void)
{
    /* *INDENT-OFF* */
    static struct {
	Boolean wanted;
	const char *name;
	const char *value;
    } table[] = {
	{
	    False,
	    "default",
"\
          Shift <KeyPress> Prior:scroll-back(1,halfpage) \n\
           Shift <KeyPress> Next:scroll-forw(1,halfpage) \n\
         Shift <KeyPress> Select:select-cursor-start() select-cursor-end(SELECT, CUT_BUFFER0) \n\
         Shift <KeyPress> Insert:insert-selection(SELECT, CUT_BUFFER0) \n\
"
	},
	/* PROCURA added "Meta <Btn2Down>:clear-saved-lines()" */
	{
	    False,
	    "default",
"\
                ~Meta <KeyPress>:insert-seven-bit() \n\
                 Meta <KeyPress>:insert-eight-bit() \n\
                !Ctrl <Btn1Down>:popup-menu(mainMenu) \n\
           !Lock Ctrl <Btn1Down>:popup-menu(mainMenu) \n\
 !Lock Ctrl @Num_Lock <Btn1Down>:popup-menu(mainMenu) \n\
     ! @Num_Lock Ctrl <Btn1Down>:popup-menu(mainMenu) \n\
                ~Meta <Btn1Down>:select-start() \n\
              ~Meta <Btn1Motion>:select-extend() \n\
                !Ctrl <Btn2Down>:popup-menu(vtMenu) \n\
           !Lock Ctrl <Btn2Down>:popup-menu(vtMenu) \n\
 !Lock Ctrl @Num_Lock <Btn2Down>:popup-menu(vtMenu) \n\
     ! @Num_Lock Ctrl <Btn2Down>:popup-menu(vtMenu) \n\
          ~Ctrl ~Meta <Btn2Down>:ignore() \n\
                 Meta <Btn2Down>:clear-saved-lines() \n\
            ~Ctrl ~Meta <Btn2Up>:insert-selection(SELECT, CUT_BUFFER0) \n\
                !Ctrl <Btn3Down>:popup-menu(fontMenu) \n\
           !Lock Ctrl <Btn3Down>:popup-menu(fontMenu) \n\
 !Lock Ctrl @Num_Lock <Btn3Down>:popup-menu(fontMenu) \n\
     ! @Num_Lock Ctrl <Btn3Down>:popup-menu(fontMenu) \n\
          ~Ctrl ~Meta <Btn3Down>:start-extend() \n\
              ~Meta <Btn3Motion>:select-extend() \n\
"
	},
	{
	    False,
	    "wheel-mouse",
"\
                 Ctrl <Btn4Down>:scroll-back(1,halfpage,m) \n\
            Lock Ctrl <Btn4Down>:scroll-back(1,halfpage,m) \n\
  Lock @Num_Lock Ctrl <Btn4Down>:scroll-back(1,halfpage,m) \n\
       @Num_Lock Ctrl <Btn4Down>:scroll-back(1,halfpage,m) \n\
                      <Btn4Down>:scroll-back(5,line,m)     \n\
                 Ctrl <Btn5Down>:scroll-forw(1,halfpage,m) \n\
            Lock Ctrl <Btn5Down>:scroll-forw(1,halfpage,m) \n\
  Lock @Num_Lock Ctrl <Btn5Down>:scroll-forw(1,halfpage,m) \n\
       @Num_Lock Ctrl <Btn5Down>:scroll-forw(1,halfpage,m) \n\
                      <Btn5Down>:scroll-forw(5,line,m)     \n\
"
	},
	{
	    False,
	    "default",
"\
                         <BtnUp>:select-end(SELECT, CUT_BUFFER0) \n\
                       <BtnDown>:ignore() \
"
	}
    };
    /* *INDENT-ON* */

    size_t needed = 0;
    char *result = 0;

    int pass;
    Cardinal item;

    TRACE(("VTInitTranslations\n"));
    for (item = 0; item < XtNumber(table); ++item) {
	table[item].wanted = True;
    }
    if (!IsEmpty(resource.omitTranslation)) {
	char *value;
	const char *source = resource.omitTranslation;

	while (*source != '\0' && (value = ParseList(&source)) != 0) {
	    size_t len = strlen(value);

	    TRACE(("parsed:%s\n", value));
	    for (item = 0; item < XtNumber(table); ++item) {
		if (strlen(table[item].name) >= len
		    && x_strncasecmp(table[item].name,
				     value,
				     (unsigned) len) == 0) {
		    table[item].wanted = False;
		    TRACE(("omit(%s):\n%s\n", table[item].name, table[item].value));
		    break;
		}
	    }
	    free(value);
	}
    }

    for (pass = 0; pass < 2; ++pass) {
	needed = 0;
	for (item = 0; item < XtNumber(table); ++item) {
	    if (table[item].wanted) {
		if (pass) {
		    strcat(result, table[item].value);
		} else {
		    needed += strlen(table[item].value) + 1;
		}
	    }
	}
	if (!pass) {
	    result = XtMalloc((Cardinal) needed);
	    *result = '\0';
	}
    }

    TRACE(("result:\n%s\n", result));

    defaultTranslations = result;
    xtermClassRec.core_class.tm_table = result;
}
