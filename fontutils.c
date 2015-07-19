#define RES_OFFSET(field)	XtOffsetOf(SubResourceRec, field)

#include <fontutils.h>
#include <X11/Xmu/Drawing.h>
#include <X11/Xmu/CharSet.h>

#include <main.h>
#include <data.h>
#include <menu.h>
#include <xstrings.h>
#include <xterm.h>

#include <stdio.h>
#include <ctype.h>

#define SetFontWidth(screen,dst,src)  (dst)->f_width = (src)
#define SetFontHeight(screen,dst,src) (dst)->f_height = dimRound((screen)->scale_height * (float) (src))

/* from X11/Xlibint.h - not all vendors install this file */
#define CI_NONEXISTCHAR(cs) (((cs)->width == 0) && \
			     (((cs)->rbearing|(cs)->lbearing| \
			       (cs)->ascent|(cs)->descent) == 0))

#define CI_GET_CHAR_INFO_1D(fs,col,cs) \
{ \
    cs = 0; \
    if (col >= fs->min_char_or_byte2 && col <= fs->max_char_or_byte2) { \
	if (fs->per_char == NULL) { \
	    cs = &fs->min_bounds; \
	} else { \
	    cs = &fs->per_char[(col - fs->min_char_or_byte2)]; \
	} \
	if (CI_NONEXISTCHAR(cs)) cs = 0; \
    } \
}

#define CI_GET_CHAR_INFO_2D(fs,row,col,cs) \
{ \
    cs = 0; \
    if (row >= fs->min_byte1 && row <= fs->max_byte1 && \
	col >= fs->min_char_or_byte2 && col <= fs->max_char_or_byte2) { \
	if (fs->per_char == NULL) { \
	    cs = &fs->min_bounds; \
	} else { \
	    cs = &fs->per_char[((row - fs->min_byte1) * \
				(fs->max_char_or_byte2 - \
				 fs->min_char_or_byte2 + 1)) + \
			       (col - fs->min_char_or_byte2)]; \
	} \
	if (CI_NONEXISTCHAR(cs)) cs = 0; \
    } \
}

#define FREE_FNAME(field) \
	    if (fonts == 0 || myfonts.field != fonts->field) { \
		FREE_STRING(myfonts.field); \
		myfonts.field = 0; \
	    }

/*
 * A structure to hold the relevant properties from a font
 * we need to make a well formed font name for it.
 */
typedef struct {
    /* registry, foundry, family */
    const char *beginning;
    /* weight */
    const char *weight;
    /* slant */
    const char *slant;
    /* wideness */
    const char *wideness;
    /* add style */
    const char *add_style;
    int pixel_size;
    const char *point_size;
    int res_x;
    int res_y;
    const char *spacing;
    int average_width;
    /* charset registry, charset encoding */
    char *end;
} FontNameProperties;

/*
 * Returns the fields from start to stop in a dash- separated string.  This
 * function will modify the source, putting '\0's in the appropriate place and
 * moving the beginning forward to after the '\0'
 *
 * This will NOT work for the last field (but we won't need it).
 */
static char *
n_fields(char **source, int start, int stop)
{
    int i;
    char *str, *str1;

    /*
     * find the start-1th dash
     */
    for (i = start - 1, str = *source; i; i--, str++) {
	if ((str = strchr(str, '-')) == 0)
	    return 0;
    }

    /*
     * find the stopth dash
     */
    for (i = stop - start + 1, str1 = str; i; i--, str1++) {
	if ((str1 = strchr(str1, '-')) == 0)
	    return 0;
    }

    /*
     * put a \0 at the end of the fields
     */
    *(str1 - 1) = '\0';

    /*
     * move source forward
     */
    *source = str1;

    return str;
}

static Boolean
check_fontname(const char *name)
{
    Boolean result = True;

    if (IsEmpty(name)) {
	TRACE(("fontname missing\n"));
	result = False;
    }
    return result;
}

/*
 * Gets the font properties from a given font structure.  We use the FONT name
 * to find them out, since that seems easier.
 *
 * Returns a pointer to a static FontNameProperties structure
 * or NULL on error.
 */
static FontNameProperties *
get_font_name_props(Display *dpy, XFontStruct *fs, char **result)
{
    static FontNameProperties props;
    static char *last_name;

    XFontProp *fp;
    int i;
    Atom fontatom = XInternAtom(dpy, "FONT", False);
    char *name = 0;
    char *str;

    /*
     * first get the full font name
     */
    if (fontatom != 0) {
	for (i = 0, fp = fs->properties; i < fs->n_properties; i++, fp++) {
	    if (fp->name == fontatom) {
		name = XGetAtomName(dpy, fp->card32);
		break;
	    }
	}
    }

    if (name == 0)
	return 0;

    /*
     * XGetAtomName allocates memory - don't leak
     */
    if (last_name != 0)
	XFree(last_name);
    last_name = name;

    if (result != 0) {
	if (!check_fontname(name))
	    return 0;
	if (*result != 0)
	    free(*result);
	*result = x_strdup(name);
    }

    /*
     * Now split it up into parts and put them in
     * their places. Since we are using parts of
     * the original string, we must not free the Atom Name
     */

    /* registry, foundry, family */
    if ((props.beginning = n_fields(&name, 1, 3)) == 0)
	return 0;

    /* weight is the next */
    if ((props.weight = n_fields(&name, 1, 1)) == 0)
	return 0;

    /* slant */
    if ((props.slant = n_fields(&name, 1, 1)) == 0)
	return 0;

    /* width */
    if ((props.wideness = n_fields(&name, 1, 1)) == 0)
	return 0;

    /* add style */
    if ((props.add_style = n_fields(&name, 1, 1)) == 0)
	return 0;

    /* pixel size */
    if ((str = n_fields(&name, 1, 1)) == 0)
	return 0;
    if ((props.pixel_size = atoi(str)) == 0)
	return 0;

    /* point size */
    if ((props.point_size = n_fields(&name, 1, 1)) == 0)
	return 0;

    /* res_x */
    if ((str = n_fields(&name, 1, 1)) == 0)
	return 0;
    if ((props.res_x = atoi(str)) == 0)
	return 0;

    /* res_y */
    if ((str = n_fields(&name, 1, 1)) == 0)
	return 0;
    if ((props.res_y = atoi(str)) == 0)
	return 0;

    /* spacing */
    if ((props.spacing = n_fields(&name, 1, 1)) == 0)
	return 0;

    /* average width */
    if ((str = n_fields(&name, 1, 1)) == 0)
	return 0;
    if ((props.average_width = atoi(str)) == 0)
	return 0;

    /* the rest: charset registry and charset encoding */
    props.end = name;

    return &props;
}

#define ALLOCHUNK(n) ((n | 127) + 1)

static void
alloca_fontname(char **result, size_t next)
{
    size_t last = (*result != 0) ? strlen(*result) : 0;
    size_t have = (*result != 0) ? ALLOCHUNK(last) : 0;
    size_t want = last + next + 2;

    if (want >= have) {
	want = ALLOCHUNK(want);
	if (last != 0) {
	    char *save = *result;
	    *result = TypeRealloc(char, want, *result);
	    if (*result == 0)
		free(save);
	} else {
	    if ((*result = TypeMallocN(char, want)) != 0)
		**result = '\0';
	}
    }
}

static void
append_fontname_str(char **result, const char *value)
{
    if (value == 0)
	value = "*";
    alloca_fontname(result, strlen(value));
    if (*result != 0) {
	if (**result != '\0')
	    strcat(*result, "-");
	strcat(*result, value);
    }
}

static void
append_fontname_num(char **result, int value)
{
    if (value < 0) {
	append_fontname_str(result, "*");
    } else {
	char temp[100];
	sprintf(temp, "%d", value);
	append_fontname_str(result, temp);
    }
}

/*
 * Take the given font props and try to make a well formed font name specifying
 * the same base font and size and everything, but with different weight/width
 * according to the parameters.  The return value is allocated, should be freed
 * by the caller.
 */
static char *
derive_font_name(FontNameProperties *props,
		 const char *use_weight,
		 int use_average_width,
		 const char *use_encoding)
{
    char *result = 0;

    append_fontname_str(&result, props->beginning);
    append_fontname_str(&result, use_weight);
    append_fontname_str(&result, props->slant);
    append_fontname_str(&result, 0);
    append_fontname_str(&result, 0);
    append_fontname_num(&result, props->pixel_size);
    append_fontname_str(&result, props->point_size);
    append_fontname_num(&result, props->res_x);
    append_fontname_num(&result, props->res_y);
    append_fontname_str(&result, props->spacing);
    append_fontname_num(&result, use_average_width);
    append_fontname_str(&result, use_encoding);

    return result;
}

static char *
bold_font_name(FontNameProperties *props, int use_average_width)
{
    return derive_font_name(props, "bold", use_average_width, props->end);
}




/*
 * Case-independent comparison for font-names, including wildcards.
 * XLFD allows '?' as a wildcard, but we do not handle that (no one seems
 * to use it).
 */
static Bool
same_font_name(const char *pattern, const char *match)
{
    Bool result = False;

    if (pattern && match) {
	while (*pattern && *match) {
	    if (*pattern == *match) {
		pattern++;
		match++;
	    } else if (*pattern == '*' || *match == '*') {
		if (same_font_name(pattern + 1, match)) {
		    return True;
		} else if (same_font_name(pattern, match + 1)) {
		    return True;
		} else {
		    return False;
		}
	    } else {
		int p = x_toupper(*pattern++);
		int m = x_toupper(*match++);
		if (p != m)
		    return False;
	    }
	}
	result = (*pattern == *match);	/* both should be NUL */
    }
    return result;
}

/*
 * Double-check the fontname that we asked for versus what the font server
 * actually gave us.  The larger fixed fonts do not always have a matching bold
 * font, and the font server may try to scale another font or otherwise
 * substitute a mismatched font.
 *
 * If we cannot get what we requested, we will fallback to the original
 * behavior, which simulates bold by overstriking each character at one pixel
 * offset.
 */
static int
got_bold_font(Display *dpy, XFontStruct *fs, String requested)
{
    char *actual = 0;
    int got;

    if (get_font_name_props(dpy, fs, &actual) == 0)
	got = 0;
    else
	got = same_font_name(requested, actual);
    free(actual);
    return got;
}

/*
 * Check normal/bold (or wide/wide-bold) font pairs to see if we will be able
 * to check for missing glyphs in a comparable manner.
 */
static int
comparable_metrics(XFontStruct *normal, XFontStruct *bold)
{
#define DATA "comparable_metrics: "
    int result = 0;

    if (normal->all_chars_exist) {
	if (bold->all_chars_exist) {
	    result = 1;
	} else {
	    TRACE((DATA "all chars exist in normal font, but not in bold\n"));
	}
    } else if (normal->per_char != 0) {
	if (bold->per_char != 0) {
	    result = 1;
	} else {
	    TRACE((DATA "normal font has per-char metrics, but not bold\n"));
	}
    } else {
	TRACE((DATA "normal font is not very good!\n"));
	result = 1;		/* give in (we're not going in reverse) */
    }
    return result;
#undef DATA
}

/*
 * If the font server tries to adjust another font, it may not adjust it
 * properly.  Check that the bounding boxes are compatible.  Otherwise we'll
 * leave trash on the display when we mix normal and bold fonts.
 */
static int
same_font_size(XtermWidget xw, XFontStruct *nfs, XFontStruct *bfs)
{
    TScreen *screen = TScreenOf(xw);
    TRACE(("same_font_size height %d/%d, min %d/%d max %d/%d\n",
	   nfs->ascent + nfs->descent,
	   bfs->ascent + bfs->descent,
	   nfs->min_bounds.width, bfs->min_bounds.width,
	   nfs->max_bounds.width, bfs->max_bounds.width));
    return screen->free_bold_box
	|| ((nfs->ascent + nfs->descent) == (bfs->ascent + bfs->descent)
	    && (nfs->min_bounds.width == bfs->min_bounds.width
		|| nfs->min_bounds.width == bfs->min_bounds.width + 1)
	    && (nfs->max_bounds.width == bfs->max_bounds.width
		|| nfs->max_bounds.width == bfs->max_bounds.width + 1));
}

/*
 * Check if the font looks like it has fixed width
 */
static int
is_fixed_font(XFontStruct *fs)
{
    if (fs)
	return (fs->min_bounds.width == fs->max_bounds.width);
    return 1;
}

/*
 * Check if the font looks like a double width font (i.e. contains
 * characters of width X and 2X
 */
#define is_double_width_font(fs) 0

#define EmptyFont(fs) (fs != 0 \
		   && ((fs)->ascent + (fs)->descent == 0 \
		    || (fs)->max_bounds.width == 0))

#define FontSize(fs) (((fs)->ascent + (fs)->descent) \
		    *  (fs)->max_bounds.width)

const VTFontNames *
xtermFontName(const char *normal)
{
    static VTFontNames data;
    FREE_STRING(data.f_n);
    memset(&data, 0, sizeof(data));
    data.f_n = x_strdup(normal);
    return &data;
}

static void
cache_menu_font_name(TScreen *screen, int fontnum, int which, const char *name)
{
    if (name != 0) {
	String last = screen->menu_font_names[fontnum][which];
	if (last != 0) {
	    if (strcmp(last, name)) {
		FREE_STRING(last);
		TRACE(("caching menu fontname %d.%d %s\n", fontnum, which, name));
		screen->menu_font_names[fontnum][which] = x_strdup(name);
	    }
	} else {
	    TRACE(("caching menu fontname %d.%d %s\n", fontnum, which, name));
	    screen->menu_font_names[fontnum][which] = x_strdup(name);
	}
    }
}

/*
 * Open the given font and verify that it is non-empty.  Return a null on
 * failure.
 */
Bool
xtermOpenFont(XtermWidget xw,
	      const char *name,
	      XTermFonts * result,
	      fontWarningTypes warn,
	      Bool force)
{
    Bool code = False;
    TScreen *screen = TScreenOf(xw);

    if (!IsEmpty(name)) {
	if ((result->fs = XLoadQueryFont(screen->display, name)) != 0) {
	    code = True;
	    if (EmptyFont(result->fs)) {
		(void) xtermCloseFont(xw, result);
		code = False;
	    } else {
		result->fn = x_strdup(name);
	    }
	} else if (XmuCompareISOLatin1(name, DEFFONT) != 0) {
	    if (warn <= xw->misc.fontWarnings
		) {
		TRACE(("OOPS: cannot load font %s\n", name));
		xtermWarning("cannot load font '%s'\n", name);
	    } else {
		TRACE(("xtermOpenFont: cannot load font '%s'\n", name));
	    }
	    if (force) {
		code = xtermOpenFont(xw, DEFFONT, result, fwAlways, True);
	    }
	}
    }
    return code;
}

/*
 * Close the font and free the font info.
 */
XTermFonts *
xtermCloseFont(XtermWidget xw, XTermFonts * fnt)
{
    if (fnt != 0 && fnt->fs != 0) {
	TScreen *screen = TScreenOf(xw);

	clrCgsFonts(xw, WhichVWin(screen), fnt);
	XFreeFont(screen->display, fnt->fs);
	xtermFreeFontInfo(fnt);
    }
    return 0;
}

/*
 * Close the listed fonts, noting that some may use copies of the pointer.
 */
void
xtermCloseFonts(XtermWidget xw, XTermFonts * fnts)
{
    int j, k;

    for (j = 0; j < fMAX; ++j) {
	/*
	 * Need to save the pointer since xtermCloseFont zeroes it
	 */
	XFontStruct *thisFont = fnts[j].fs;
	if (thisFont != 0) {
	    xtermCloseFont(xw, &fnts[j]);
	    for (k = j + 1; k < fMAX; ++k) {
		if (thisFont == fnts[k].fs)
		    xtermFreeFontInfo(&fnts[k]);
	    }
	}
    }
}

/*
 * Make a copy of the source, assuming the XFontStruct's to be unique, but
 * ensuring that the names are reallocated to simplify freeing.
 */
void
xtermCopyFontInfo(XTermFonts * target, XTermFonts * source)
{
    xtermFreeFontInfo(target);
    target->chrset = source->chrset;
    target->flags = source->flags;
    target->fn = x_strdup(source->fn);
    target->fs = source->fs;
}

void
xtermFreeFontInfo(XTermFonts * target)
{
    target->chrset = 0;
    target->flags = 0;
    if (target->fn != 0) {
	free(target->fn);
	target->fn = 0;
    }
    target->fs = 0;
}


void
xtermUpdateFontGCs(XtermWidget xw, XTermFonts * fnts)
{
    TScreen *screen = TScreenOf(xw);
    VTwin *win = WhichVWin(screen);
    Pixel new_normal = getXtermForeground(xw, xw->flags, xw->cur_foreground);
    Pixel new_revers = getXtermBackground(xw, xw->flags, xw->cur_background);

    setCgsFore(xw, win, gcNorm, new_normal);
    setCgsBack(xw, win, gcNorm, new_revers);
    setCgsFont(xw, win, gcNorm, &(fnts[fNorm]));

    copyCgs(xw, win, gcBold, gcNorm);
    setCgsFont(xw, win, gcBold, &(fnts[fBold]));

    setCgsFore(xw, win, gcNormReverse, new_revers);
    setCgsBack(xw, win, gcNormReverse, new_normal);
    setCgsFont(xw, win, gcNormReverse, &(fnts[fNorm]));

    copyCgs(xw, win, gcBoldReverse, gcNormReverse);
    setCgsFont(xw, win, gcBoldReverse, &(fnts[fBold]));

    if_OPT_WIDE_CHARS(screen, {
	if (fnts[fWide].fs != 0
	    && fnts[fWBold].fs != 0) {
	    setCgsFore(xw, win, gcWide, new_normal);
	    setCgsBack(xw, win, gcWide, new_revers);
	    setCgsFont(xw, win, gcWide, &(fnts[fWide]));

	    copyCgs(xw, win, gcWBold, gcWide);
	    setCgsFont(xw, win, gcWBold, &(fnts[fWBold]));

	    setCgsFore(xw, win, gcWideReverse, new_revers);
	    setCgsBack(xw, win, gcWideReverse, new_normal);
	    setCgsFont(xw, win, gcWideReverse, &(fnts[fWide]));

	    copyCgs(xw, win, gcWBoldReverse, gcWideReverse);
	    setCgsFont(xw, win, gcWBoldReverse, &(fnts[fWBold]));
	}
    });
}


int
xtermLoadFont(XtermWidget xw,
	      const VTFontNames * fonts,
	      Bool doresize,
	      int fontnum)
{
    TScreen *screen = TScreenOf(xw);
    VTwin *win = WhichVWin(screen);

    VTFontNames myfonts;
    FontNameProperties *fp;
    XTermFonts fnts[fMAX];
    char *tmpname = NULL;
    char *normal = NULL;
    Boolean proportional = False;
    fontWarningTypes warn[fMAX];
    int j;

    memset(&myfonts, 0, sizeof(myfonts));
    memset(fnts, 0, sizeof(fnts));

    if (fonts != 0)
	myfonts = *fonts;
    if (!check_fontname(myfonts.f_n))
	return 0;

    /*
     * Check the font names against the resource values, to see which were
     * derived in a previous call.  If so, we'll only warn about those if
     * the warning level is set to "always".
     */
    for (j = 0; j < fMAX; ++j) {
	warn[j] = fwAlways;
    }
#define CmpResource(field, index) \
    if (same_font_name(screen->menu_font_names[fontnum][index], myfonts.field)) \
	warn[index] = fwResource

    CmpResource(f_n, fNorm);
    if (fontnum == fontMenu_default) {
	CmpResource(f_b, fBold);
    }

    if (fontnum == fontMenu_fontescape
	&& myfonts.f_n != screen->MenuFontName(fontnum)) {
	if ((tmpname = x_strdup(myfonts.f_n)) == 0)
	    return 0;
    }

    TRACE(("Begin Cgs - xtermLoadFont(%s)\n", myfonts.f_n));
    releaseWindowGCs(xw, win);

#define DbgResource(name, field, index) \
    TRACE(("xtermLoadFont #%d "name" %s%s\n", \
    	   fontnum, \
	   (warn[index] == fwResource) ? "*" : " ", \
	   NonNull(myfonts.field)))
    DbgResource("normal", f_n, fNorm);
    DbgResource("bold  ", f_b, fBold);

    /*
     * If we are opening the default font, and it happens to be missing, force
     * that to the compiled-in default font, e.g., "fixed".  If we cannot open
     * the font, disable it from the menu.
     */
    if (!xtermOpenFont(xw,
		       myfonts.f_n,
		       &fnts[fNorm],
		       warn[fNorm],
		       (fontnum == fontMenu_default))) {
	if (fontnum != fontMenu_fontsel) {
	    SetItemSensitivity(fontMenuEntries[fontnum].widget, False);
	}
	goto bad;
    }

    normal = x_strdup(myfonts.f_n);
    if (!check_fontname(myfonts.f_b)) {
	warn[fBold] = fwAlways;
	fp = get_font_name_props(screen->display, fnts[fNorm].fs, &normal);
	if (fp != 0) {
	    FREE_FNAME(f_b);
	    myfonts.f_b = bold_font_name(fp, fp->average_width);
	    if (!xtermOpenFont(xw, myfonts.f_b, &fnts[fBold], fwAlways, False)) {
		FREE_FNAME(f_b);
		myfonts.f_b = bold_font_name(fp, -1);
		xtermOpenFont(xw, myfonts.f_b, &fnts[fBold], fwAlways, False);
	    }
	    TRACE(("...derived bold '%s'\n", NonNull(myfonts.f_b)));
	}
	if (fp == 0 || fnts[fBold].fs == 0) {
	    xtermCopyFontInfo(&fnts[fBold], &fnts[fNorm]);
	    TRACE(("...cannot load a matching bold font\n"));
	} else if (comparable_metrics(fnts[fNorm].fs, fnts[fBold].fs)
		   && same_font_size(xw, fnts[fNorm].fs, fnts[fBold].fs)
		   && got_bold_font(screen->display, fnts[fBold].fs, myfonts.f_b)) {
	    TRACE(("...got a matching bold font\n"));
	    cache_menu_font_name(screen, fontnum, fBold, myfonts.f_b);
	} else {
	    xtermCloseFont(xw, &fnts[fBold]);
	    fnts[fBold] = fnts[fNorm];
	    TRACE(("...did not get a matching bold font\n"));
	}
    } else if (!xtermOpenFont(xw, myfonts.f_b, &fnts[fBold], warn[fBold], False)) {
	xtermCopyFontInfo(&fnts[fBold], &fnts[fNorm]);
	warn[fBold] = fwAlways;
	TRACE(("...cannot load bold font '%s'\n", NonNull(myfonts.f_b)));
    } else {
	cache_menu_font_name(screen, fontnum, fBold, myfonts.f_b);
    }

    /*
     * If there is no widefont specified, fake it by doubling AVERAGE_WIDTH
     * of normal fonts XLFD, and asking for it.  This plucks out 18x18ja
     * and 12x13ja as the corresponding fonts for 9x18 and 6x13.
     */
    if_OPT_WIDE_CHARS(screen, {
	Boolean derived;
	char *bold = NULL;

	if (check_fontname(myfonts.f_w)) {
	    cache_menu_font_name(screen, fontnum, fWide, myfonts.f_w);
	} else if (screen->utf8_fonts && !is_double_width_font(fnts[fNorm].fs)) {
	    FREE_FNAME(f_w);
	    fp = get_font_name_props(screen->display, fnts[fNorm].fs, &normal);
	    if (fp != 0) {
		myfonts.f_w = wide_font_name(fp);
		warn[fWide] = fwAlways;
		TRACE(("...derived wide %s\n", NonNull(myfonts.f_w)));
		cache_menu_font_name(screen, fontnum, fWide, myfonts.f_w);
	    }
	}

	if (check_fontname(myfonts.f_w)) {
	    (void) xtermOpenFont(xw, myfonts.f_w, &fnts[fWide], warn[fWide], False);
	} else {
	    xtermCopyFontInfo(&fnts[fWide], &fnts[fNorm]);
	    warn[fWide] = fwAlways;
	}

	derived = False;
	if (!check_fontname(myfonts.f_wb)) {
	    fp = get_font_name_props(screen->display, fnts[fBold].fs, &bold);
	    if (fp != 0) {
		myfonts.f_wb = widebold_font_name(fp);
		warn[fWBold] = fwAlways;
		derived = True;
	    }
	}

	if (check_fontname(myfonts.f_wb)) {

	    xtermOpenFont(xw,
			  myfonts.f_wb,
			  &fnts[fWBold],
			  (screen->utf8_fonts
			   ? warn[fWBold]
			   : (fontWarningTypes) (xw->misc.fontWarnings + 1)),
			  False);

	    if (derived
		&& !compatibleWideCounts(fnts[fWide].fs, fnts[fWBold].fs)) {
		xtermCloseFont(xw, &fnts[fWBold]);
	    }
	    if (fnts[fWBold].fs == 0) {
		FREE_FNAME(f_wb);
		if (IsEmpty(myfonts.f_w)) {
		    myfonts.f_wb = x_strdup(myfonts.f_b);
		    warn[fWBold] = fwAlways;
		    xtermCopyFontInfo(&fnts[fWBold], &fnts[fBold]);
		    TRACE(("...cannot load wide-bold, use bold %s\n",
			   NonNull(myfonts.f_b)));
		} else {
		    myfonts.f_wb = x_strdup(myfonts.f_w);
		    warn[fWBold] = fwAlways;
		    xtermCopyFontInfo(&fnts[fWBold], &fnts[fWide]);
		    TRACE(("...cannot load wide-bold, use wide %s\n",
			   NonNull(myfonts.f_w)));
		}
	    } else {
		TRACE(("...%s wide/bold %s\n",
		       derived ? "derived" : "given",
		       NonNull(myfonts.f_wb)));
		cache_menu_font_name(screen, fontnum, fWBold, myfonts.f_wb);
	    }
	} else if (is_double_width_font(fnts[fBold].fs)) {
	    xtermCopyFontInfo(&fnts[fWBold], &fnts[fBold]);
	    warn[fWBold] = fwAlways;
	    TRACE(("...bold font is double-width, use it %s\n", NonNull(myfonts.f_b)));
	} else {
	    xtermCopyFontInfo(&fnts[fWBold], &fnts[fWide]);
	    warn[fWBold] = fwAlways;
	    TRACE(("...cannot load wide bold font, use wide %s\n", NonNull(myfonts.f_w)));
	}

	free(bold);

	if (EmptyFont(fnts[fWBold].fs))
	    goto bad;		/* can't use a 0-sized font */
    });

    /*
     * Most of the time this call to load the font will succeed, even if
     * there is no wide font :  the X server doubles the width of the
     * normal font, or similar.
     *
     * But if it did fail for some reason, then nevermind.
     */
    if (EmptyFont(fnts[fBold].fs))
	goto bad;		/* can't use a 0-sized font */

    if (!same_font_size(xw, fnts[fNorm].fs, fnts[fBold].fs)
	&& (is_fixed_font(fnts[fNorm].fs) && is_fixed_font(fnts[fBold].fs))) {
	TRACE(("...ignoring mismatched normal/bold fonts\n"));
	xtermCloseFont(xw, &fnts[fBold]);
	xtermCopyFontInfo(&fnts[fBold], &fnts[fNorm]);
    }

    if_OPT_WIDE_CHARS(screen, {
	if (fnts[fWide].fs != 0
	    && fnts[fWBold].fs != 0
	    && (!comparable_metrics(fnts[fWide].fs, fnts[fWBold].fs)
		|| (!same_font_size(xw, fnts[fWide].fs, fnts[fWBold].fs)
		    && is_fixed_font(fnts[fWide].fs)
		    && is_fixed_font(fnts[fWBold].fs)))) {
	    TRACE(("...ignoring mismatched normal/bold wide fonts\n"));
	    xtermCloseFont(xw, &fnts[fWBold]);
	    xtermCopyFontInfo(&fnts[fWBold], &fnts[fWide]);
	}
    });

    /*
     * Normal/bold fonts should be the same width.  Also, the min/max
     * values should be the same.
     */
    if (!is_fixed_font(fnts[fNorm].fs)
	|| !is_fixed_font(fnts[fBold].fs)
	|| fnts[fNorm].fs->max_bounds.width != fnts[fBold].fs->max_bounds.width) {
	TRACE(("Proportional font! normal %d/%d, bold %d/%d\n",
	       fnts[fNorm].fs->min_bounds.width,
	       fnts[fNorm].fs->max_bounds.width,
	       fnts[fBold].fs->min_bounds.width,
	       fnts[fBold].fs->max_bounds.width));
	proportional = True;
    }

    if_OPT_WIDE_CHARS(screen, {
	if (fnts[fWide].fs != 0
	    && fnts[fWBold].fs != 0
	    && (!is_fixed_font(fnts[fWide].fs)
		|| !is_fixed_font(fnts[fWBold].fs)
		|| fnts[fWide].fs->max_bounds.width != fnts[fWBold].fs->max_bounds.width)) {
	    TRACE(("Proportional font! wide %d/%d, wide bold %d/%d\n",
		   fnts[fWide].fs->min_bounds.width,
		   fnts[fWide].fs->max_bounds.width,
		   fnts[fWBold].fs->min_bounds.width,
		   fnts[fWBold].fs->max_bounds.width));
	    proportional = True;
	}
    });

    /* TODO : enforce that the width of the wide font is 2* the width
       of the narrow font */

    /*
     * If we're switching fonts, free the old ones.  Otherwise we'll leak
     * the memory that is associated with the old fonts.  The
     * XLoadQueryFont call allocates a new XFontStruct.
     */
    xtermCloseFonts(xw, screen->fnts);

    xtermCopyFontInfo(&(screen->fnts[fNorm]), &fnts[fNorm]);
    xtermCopyFontInfo(&(screen->fnts[fBold]), &fnts[fBold]);

    xtermUpdateFontGCs(xw, screen->fnts);

    screen->fnt_prop = (Boolean) (proportional && !(screen->force_packed));
    screen->fnt_boxes = True;


    if (screen->always_bold_mode) {
	screen->enbolden = screen->bold_mode;
    } else {
	screen->enbolden = screen->bold_mode
	    && ((fnts[fNorm].fs == fnts[fBold].fs)
		|| same_font_name(normal, myfonts.f_b));
    }
    TRACE(("Will %suse 1-pixel offset/overstrike to simulate bold\n",
	   screen->enbolden ? "" : "not "));

    set_menu_font(False);
    screen->menu_font_number = fontnum;
    set_menu_font(True);
    if (tmpname) {		/* if setting escape or sel */
	if (screen->MenuFontName(fontnum))
	    FREE_STRING(screen->MenuFontName(fontnum));
	screen->MenuFontName(fontnum) = tmpname;
	if (fontnum == fontMenu_fontescape) {
	    update_font_escape();
	}
    }
    if (normal)
	free(normal);
    set_cursor_gcs(xw);
    xtermUpdateFontInfo(xw, doresize);
    TRACE(("Success Cgs - xtermLoadFont\n"));
    FREE_FNAME(f_n);
    FREE_FNAME(f_b);
    if (fnts[fNorm].fn == fnts[fBold].fn) {
	free(fnts[fNorm].fn);
    } else {
	free(fnts[fNorm].fn);
	free(fnts[fBold].fn);
    }
    return 1;

  bad:
    if (normal)
	free(normal);
    if (tmpname)
	free(tmpname);


    releaseWindowGCs(xw, win);

    xtermCloseFonts(xw, fnts);
    TRACE(("Fail Cgs - xtermLoadFont\n"));
    return 0;
}




/*
 * Set the limits for the box that outlines the cursor.
 */
void
xtermSetCursorBox(TScreen *screen)
{
    static XPoint VTbox[NBOX];
    XPoint *vp;
    int fw = FontWidth(screen) - 1;
    int fh = FontHeight(screen) - 1;
    int ww = isCursorBar(screen) ? 1 : fw;
    int hh = isCursorUnderline(screen) ? 1 : fh;

    vp = &VTbox[1];
    (vp++)->x = (short) ww;
    (vp++)->y = (short) hh;
    (vp++)->x = (short) -ww;
    vp->y = (short) -hh;

    screen->box = VTbox;
}

#define CACHE_XFT(dst,src) if (src != 0) {\
	    checkXft(xw, &(dst[fontnum]), src);\
	    TRACE(("Xft metrics %s[%d] = %d (%d,%d)%s advance %d, actual %d%s\n",\
		#dst,\
	    	fontnum,\
		src->height,\
		src->ascent,\
		src->descent,\
		((src->ascent + src->descent) > src->height ? "*" : ""),\
		src->max_advance_width,\
		dst[fontnum].map.min_width,\
		dst[fontnum].map.mixed ? " mixed" : ""));\
	}



static void
checkFontInfo(int value, const char *tag)
{
    if (value == 0) {
	xtermWarning("Selected font has no non-zero %s for ISO-8859-1 encoding\n", tag);
	exit(1);
    }
}


/*
 * Compute useful values for the font/window sizes
 */
void
xtermComputeFontInfo(XtermWidget xw,
		     VTwin *win,
		     XFontStruct *font,
		     int sbwidth)
{
    TScreen *screen = TScreenOf(xw);

    int i, j, width, height;

    {
	if (is_double_width_font(font) && !(screen->fnt_prop)) {
	    SetFontWidth(screen, win, font->min_bounds.width);
	} else {
	    SetFontWidth(screen, win, font->max_bounds.width);
	}
	SetFontHeight(screen, win, font->ascent + font->descent);
	win->f_ascent = font->ascent;
	win->f_descent = font->descent;
    }
    i = 2 * screen->border + sbwidth;
    j = 2 * screen->border;
    width = MaxCols(screen) * win->f_width + i;
    height = MaxRows(screen) * win->f_height + j;
    win->fullwidth = (Dimension) width;
    win->fullheight = (Dimension) height;
    win->width = width - i;
    win->height = height - j;

    TRACE(("xtermComputeFontInfo window %dx%d (full %dx%d), fontsize %dx%d (asc %d, dsc %d)\n",
	   win->height,
	   win->width,
	   win->fullheight,
	   win->fullwidth,
	   win->f_height,
	   win->f_width,
	   win->f_ascent,
	   win->f_descent));

    checkFontInfo(win->f_height, "height");
    checkFontInfo(win->f_width, "width");
}

/* save this information as a side-effect for double-sized characters */
void
xtermSaveFontInfo(TScreen *screen, XFontStruct *font)
{
    screen->fnt_wide = (Dimension) (font->max_bounds.width);
    screen->fnt_high = (Dimension) (font->ascent + font->descent);
    TRACE(("xtermSaveFontInfo %dx%d\n", screen->fnt_high, screen->fnt_wide));
}

/*
 * After loading a new font, update the structures that use its size.
 */
void
xtermUpdateFontInfo(XtermWidget xw, Bool doresize)
{
    TScreen *screen = TScreenOf(xw);

    int scrollbar_width;
    VTwin *win = &(screen->fullVwin);

    scrollbar_width = (xw->misc.scrollbar
		       ? (screen->scrollWidget->core.width +
			  BorderWidth(screen->scrollWidget))
		       : 0);
    xtermComputeFontInfo(xw, win, screen->fnts[fNorm].fs, scrollbar_width);
    xtermSaveFontInfo(screen, screen->fnts[fNorm].fs);

    if (doresize) {
	if (VWindow(screen)) {
	    xtermClear(xw);
	}
	TRACE(("xtermUpdateFontInfo {{\n"));
	DoResizeScreen(xw);	/* set to the new natural size */
	ResizeScrollBar(xw);
	Redraw();
	TRACE(("... }} xtermUpdateFontInfo\n"));
    }
    xtermSetCursorBox(screen);
}

int
xtermGetFont(const char *param)
{
    int fontnum;

    switch (param[0]) {
    case 'd':
    case 'D':
    case '0':
	fontnum = fontMenu_default;
	break;
    case '1':
	fontnum = fontMenu_font1;
	break;
    case '2':
	fontnum = fontMenu_font2;
	break;
    case '3':
	fontnum = fontMenu_font3;
	break;
    case '4':
	fontnum = fontMenu_font4;
	break;
    case '5':
	fontnum = fontMenu_font5;
	break;
    case '6':
	fontnum = fontMenu_font6;
	break;
    case 'e':
    case 'E':
	fontnum = fontMenu_fontescape;
	break;
    case 's':
    case 'S':
	fontnum = fontMenu_fontsel;
	break;
    default:
	fontnum = -1;
	break;
    }
    return fontnum;
}

/* ARGSUSED */
void
HandleSetFont(Widget w GCC_UNUSED,
	      XEvent *event GCC_UNUSED,
	      String *params,
	      Cardinal *param_count)
{
    XtermWidget xw;

    if ((xw = getXtermWidget(w)) != 0) {
	int fontnum;
	VTFontNames fonts;

	memset(&fonts, 0, sizeof(fonts));

	if (*param_count == 0) {
	    fontnum = fontMenu_default;
	} else {
	    Cardinal maxparams = 1;	/* total number of params allowed */
	    int result = xtermGetFont(params[0]);

	    switch (result) {
	    case fontMenu_default:	/* FALLTHRU */
	    case fontMenu_font1:	/* FALLTHRU */
	    case fontMenu_font2:	/* FALLTHRU */
	    case fontMenu_font3:	/* FALLTHRU */
	    case fontMenu_font4:	/* FALLTHRU */
	    case fontMenu_font5:	/* FALLTHRU */
	    case fontMenu_font6:	/* FALLTHRU */
		break;
	    case fontMenu_fontescape:
		maxparams = 3;
		break;
	    case fontMenu_fontsel:
		maxparams = 2;
		break;
	    default:
		Bell(xw, XkbBI_MinorError, 0);
		return;
	    }
	    fontnum = result;

	    if (*param_count > maxparams) {	/* see if extra args given */
		Bell(xw, XkbBI_MinorError, 0);
		return;
	    }
	    switch (*param_count) {	/* assign 'em */
	    case 3:
		fonts.f_b = params[2];
		/* FALLTHRU */
	    case 2:
		fonts.f_n = params[1];
		break;
	    }
	}

	SetVTFont(xw, fontnum, True, &fonts);
    }
}

void
SetVTFont(XtermWidget xw,
	  int which,
	  Bool doresize,
	  const VTFontNames * fonts)
{
    TScreen *screen = TScreenOf(xw);

    TRACE(("SetVTFont(which=%d, f_n=%s, f_b=%s)\n", which,
	   (fonts && fonts->f_n) ? fonts->f_n : "<null>",
	   (fonts && fonts->f_b) ? fonts->f_b : "<null>"));

    if (IsIcon(screen)) {
	Bell(xw, XkbBI_MinorError, 0);
    } else if (which >= 0 && which < NMENUFONTS) {
	VTFontNames myfonts;

	memset(&myfonts, 0, sizeof(myfonts));
	if (fonts != 0)
	    myfonts = *fonts;

	if (which == fontMenu_fontsel) {	/* go get the selection */
	    FindFontSelection(xw, myfonts.f_n, False);
	} else {
	    int oldFont = screen->menu_font_number;

#define USE_CACHED(field, name) \
	    if (myfonts.field == 0) { \
		myfonts.field = x_strdup(screen->menu_font_names[which][name]); \
		TRACE(("set myfonts." #field " from menu_font_names[%d][" #name "] %s\n", \
		       which, NonNull(myfonts.field))); \
	    } else { \
		TRACE(("set myfonts." #field " reused\n")); \
	    }
#define SAVE_FNAME(field, name) \
	    if (myfonts.field != 0) { \
		if (screen->menu_font_names[which][name] == 0 \
		 || strcmp(screen->menu_font_names[which][name], myfonts.field)) { \
		    TRACE(("updating menu_font_names[%d][" #name "] to %s\n", \
			   which, myfonts.field)); \
		    FREE_STRING(screen->menu_font_names[which][name]); \
		    screen->menu_font_names[which][name] = x_strdup(myfonts.field); \
		} \
	    }

	    USE_CACHED(f_n, fNorm);
	    USE_CACHED(f_b, fBold);
	    if (xtermLoadFont(xw,
			      &myfonts,
			      doresize, which)) {
		/*
		 * If successful, save the data so that a subsequent query via
		 * OSC-50 will return the expected values.
		 */
		SAVE_FNAME(f_n, fNorm);
		SAVE_FNAME(f_b, fBold);
	    } else {
		(void) xtermLoadFont(xw,
				     xtermFontName(screen->MenuFontName(oldFont)),
				     doresize, oldFont);
		Bell(xw, XkbBI_MinorError, 0);
	    }
	    FREE_FNAME(f_n);
	    FREE_FNAME(f_b);
	}
    } else {
	Bell(xw, XkbBI_MinorError, 0);
    }
    return;
}
