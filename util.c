#include <xterm.h>

#include <data.h>
#include <error.h>
#include <menu.h>
#include <fontutils.h>
#include <xstrings.h>

#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <assert.h>



#include <graphics.h>

static int handle_translated_exposure(XtermWidget xw,
				      int rect_x,
				      int rect_y,
				      int rect_width,
				      int rect_height);
static void ClearLeft(XtermWidget xw);
static void CopyWait(XtermWidget xw);
static void horizontal_copy_area(XtermWidget xw,
				 int firstchar,
				 int nchars,
				 int amount);
static void vertical_copy_area(XtermWidget xw,
			       int firstline,
			       int nlines,
			       int amount,
			       int left,
			       int right);

/*
 * These routines are used for the jump scroll feature
 */
void
FlushScroll(XtermWidget xw)
{
    TScreen *screen = TScreenOf(xw);
    int i;
    int shift = INX2ROW(screen, 0);
    int bot = screen->max_row - shift;
    int refreshtop;
    int refreshheight;
    int scrolltop;
    int scrollheight;
    int left = ScrnLeftMargin(xw);
    int right = ScrnRightMargin(xw);
    Boolean full_lines = (Boolean) ((left == 0) && (right == screen->max_col));

    if (screen->cursor_state)
	HideCursor();

    TRACE(("FlushScroll %s-lines scroll:%d refresh %d\n",
	   full_lines ? "full" : "partial",
	   screen->scroll_amt,
	   screen->refresh_amt));

    if (screen->scroll_amt > 0) {
	/*
	 * Lines will be scrolled "up".
	 */
	refreshheight = screen->refresh_amt;
	scrollheight = screen->bot_marg - screen->top_marg - refreshheight + 1;
	refreshtop = screen->bot_marg - refreshheight + 1 + shift;
	i = screen->max_row - screen->scroll_amt + 1;
	if (refreshtop > i) {
	    refreshtop = i;
	}

	/*
	 * If this is the normal (not alternate) screen, and the top margin is
	 * at the top of the screen, then we will shift full lines scrolled out
	 * of the scrolling region into the saved-lines.
	 */
	if (screen->scrollWidget
	    && !screen->whichBuf
	    && full_lines
	    && screen->top_marg == 0) {
	    scrolltop = 0;
	    scrollheight += shift;
	    if (scrollheight > i)
		scrollheight = i;
	    i = screen->bot_marg - bot;
	    if (i > 0) {
		refreshheight -= i;
		if (refreshheight < screen->scroll_amt) {
		    refreshheight = screen->scroll_amt;
		}
	    }
	    i = screen->savedlines;
	    if (i < screen->savelines) {
		i += screen->scroll_amt;
		if (i > screen->savelines) {
		    i = screen->savelines;
		}
		screen->savedlines = i;
		ScrollBarDrawThumb(screen->scrollWidget);
	    }
	} else {
	    scrolltop = screen->top_marg + shift;
	    i = bot - (screen->bot_marg - screen->refresh_amt + screen->scroll_amt);
	    if (i > 0) {
		if (bot < screen->bot_marg) {
		    refreshheight = screen->scroll_amt + i;
		}
	    } else {
		scrollheight += i;
		refreshheight = screen->scroll_amt;
		i = screen->top_marg + screen->scroll_amt - 1 - bot;
		if (i > 0) {
		    refreshtop += i;
		    refreshheight -= i;
		}
	    }
	}
    } else {
	/*
	 * Lines will be scrolled "down".
	 */
	refreshheight = -screen->refresh_amt;
	scrollheight = screen->bot_marg - screen->top_marg - refreshheight + 1;
	refreshtop = screen->top_marg + shift;
	scrolltop = refreshtop + refreshheight;
	i = screen->bot_marg - bot;
	if (i > 0) {
	    scrollheight -= i;
	}
	i = screen->top_marg + refreshheight - 1 - bot;
	if (i > 0) {
	    refreshheight -= i;
	}
    }

    vertical_copy_area(xw,
		       scrolltop + screen->scroll_amt,
		       scrollheight,
		       screen->scroll_amt,
		       left,
		       right);
    ScrollSelection(screen, -(screen->scroll_amt), False);
    screen->scroll_amt = 0;
    screen->refresh_amt = 0;

    if (refreshheight > 0) {
	ClearCurBackground(xw,
			   refreshtop,
			   left,
			   (unsigned) refreshheight,
			   (unsigned) (right + 1 - left),
			   (unsigned) FontWidth(screen));
	ScrnRefresh(xw,
		    refreshtop,
		    0,
		    refreshheight,
		    MaxCols(screen),
		    False);
    }
    return;
}

/*
 * Returns true if there are lines off-screen due to scrolling which should
 * include the current line.  If false, the line is visible and we should
 * paint it now rather than waiting for the line to become visible.
 */
static Bool
AddToRefresh(XtermWidget xw)
{
    TScreen *screen = TScreenOf(xw);
    int amount = screen->refresh_amt;
    int row = screen->cur_row;
    Bool result;

    if (amount == 0) {
	result = False;
    } else if (amount > 0) {
	int bottom;

	if (row == (bottom = screen->bot_marg) - amount) {
	    screen->refresh_amt++;
	    result = True;
	} else {
	    result = (row >= bottom - amount + 1 && row <= bottom);
	}
    } else {
	int top;

	amount = -amount;
	if (row == (top = screen->top_marg) + amount) {
	    screen->refresh_amt--;
	    result = True;
	} else {
	    result = (row <= top + amount - 1 && row >= top);
	}
    }

    /*
     * If this line is visible, and there are scrolled-off lines, flush out
     * those which are now visible.
     */
    if (!result && screen->scroll_amt)
	FlushScroll(xw);

    return result;
}

/*
 * Returns true if the current row is in the visible area (it should be for
 * screen operations) and incidentally flush the scrolled-in lines which
 * have newly become visible.
 */
static Bool
AddToVisible(XtermWidget xw)
{
    TScreen *screen = TScreenOf(xw);
    Bool result = False;

    if (INX2ROW(screen, screen->cur_row) <= screen->max_row) {
	if (!AddToRefresh(xw)) {
	    result = True;
	}
    }
    return result;
}

/*
 * If we're scrolling, leave the selection intact if possible.
 * If it will bump into one of the extremes of the saved-lines, truncate that.
 * If the selection is not entirely contained within the margins and not
 * entirely outside the margins, clear it.
 */
static void
adjustHiliteOnFwdScroll(XtermWidget xw, int amount, Bool all_lines)
{
    TScreen *screen = TScreenOf(xw);
    int lo_row = (all_lines
		  ? (screen->bot_marg - screen->savelines)
		  : screen->top_marg);
    int hi_row = screen->bot_marg;
    int left = ScrnLeftMargin(xw);
    int right = ScrnRightMargin(xw);

    TRACE2(("adjustSelection FWD %s by %d (%s)\n",
	    screen->whichBuf ? "alternate" : "normal",
	    amount,
	    all_lines ? "all" : "visible"));
    TRACE2(("  before highlite %d.%d .. %d.%d\n",
	    screen->startH.row,
	    screen->startH.col,
	    screen->endH.row,
	    screen->endH.col));
    TRACE2(("  margins %d..%d\n", screen->top_marg, screen->bot_marg));
    TRACE2(("  limits  %d..%d\n", lo_row, hi_row));

    if ((left > 0 || right < screen->max_col) &&
	((screen->startH.row >= lo_row &&
	  screen->startH.row - amount <= hi_row) ||
	 (screen->endH.row >= lo_row &&
	  screen->endH.row - amount <= hi_row))) {
	/*
	 * This could be improved slightly by excluding the special case where
	 * the selection is on a single line outside left/right margins.
	 */
	TRACE2(("deselect because selection overlaps with scrolled partial-line\n"));
	ScrnDisownSelection(xw);
    } else if (screen->startH.row >= lo_row
	       && screen->startH.row - amount < lo_row) {
	/* truncate the selection because its start would move out of region */
	if (lo_row + amount <= screen->endH.row) {
	    TRACE2(("truncate selection by changing start %d.%d to %d.%d\n",
		    screen->startH.row,
		    screen->startH.col,
		    lo_row + amount,
		    0));
	    screen->startH.row = lo_row + amount;
	    screen->startH.col = 0;
	} else {
	    TRACE2(("deselect because %d.%d .. %d.%d shifted %d is outside margins %d..%d\n",
		    screen->startH.row,
		    screen->startH.col,
		    screen->endH.row,
		    screen->endH.col,
		    -amount,
		    lo_row,
		    hi_row));
	    ScrnDisownSelection(xw);
	}
    } else if (screen->startH.row <= hi_row && screen->endH.row > hi_row) {
	TRACE2(("deselect because selection straddles top-margin\n"));
	ScrnDisownSelection(xw);
    } else if (screen->startH.row < lo_row && screen->endH.row > lo_row) {
	TRACE2(("deselect because selection straddles bottom-margin\n"));
	ScrnDisownSelection(xw);
    }

    TRACE2(("  after highlite %d.%d .. %d.%d\n",
	    screen->startH.row,
	    screen->startH.col,
	    screen->endH.row,
	    screen->endH.col));
}

/*
 * This is the same as adjustHiliteOnFwdScroll(), but reversed.  In this case,
 * only the visible lines are affected.
 */
static void
adjustHiliteOnBakScroll(XtermWidget xw, int amount)
{
    TScreen *screen = TScreenOf(xw);
    int lo_row = screen->top_marg;
    int hi_row = screen->bot_marg;

    TRACE2(("adjustSelection BAK %s by %d (%s)\n",
	    screen->whichBuf ? "alternate" : "normal",
	    amount,
	    "visible"));
    TRACE2(("  before highlite %d.%d .. %d.%d\n",
	    screen->startH.row,
	    screen->startH.col,
	    screen->endH.row,
	    screen->endH.col));
    TRACE2(("  margins %d..%d\n", screen->top_marg, screen->bot_marg));

    if (screen->endH.row >= hi_row
	&& screen->endH.row + amount > hi_row) {
	/* truncate the selection because its start would move out of region */
	if (hi_row - amount >= screen->startH.row) {
	    TRACE2(("truncate selection by changing start %d.%d to %d.%d\n",
		    screen->startH.row,
		    screen->startH.col,
		    hi_row - amount,
		    0));
	    screen->endH.row = hi_row - amount;
	    screen->endH.col = 0;
	} else {
	    TRACE2(("deselect because %d.%d .. %d.%d shifted %d is outside margins %d..%d\n",
		    screen->startH.row,
		    screen->startH.col,
		    screen->endH.row,
		    screen->endH.col,
		    amount,
		    lo_row,
		    hi_row));
	    ScrnDisownSelection(xw);
	}
    } else if (screen->endH.row >= lo_row && screen->startH.row < lo_row) {
	ScrnDisownSelection(xw);
    } else if (screen->endH.row > hi_row && screen->startH.row > hi_row) {
	ScrnDisownSelection(xw);
    }

    TRACE2(("  after highlite %d.%d .. %d.%d\n",
	    screen->startH.row,
	    screen->startH.col,
	    screen->endH.row,
	    screen->endH.col));
}

/*
 * Move cells in LineData's on the current screen to simulate scrolling by the
 * given amount of lines.
 */
static void
scrollInMargins(XtermWidget xw, int amount, int top)
{
    TScreen *screen = TScreenOf(xw);
    LineData *src;
    LineData *dst;
    int row;
    int left = ScrnLeftMargin(xw);
    int right = ScrnRightMargin(xw);
    int length = right + 1 - left;

    if (amount > 0) {
	for (row = top; row <= screen->bot_marg - amount; ++row) {
	    if ((src = getLineData(screen, row + amount)) != 0
		&& (dst = getLineData(screen, row)) != 0) {
		CopyCells(screen, src, dst, left, length);
	    }
	}
	while (row <= screen->bot_marg) {
	    ClearCells(xw, 0, (unsigned) length, row, left);
	    ++row;
	}
    } else if (amount < 0) {
	for (row = screen->bot_marg; row >= top - amount; --row) {
	    if ((src = getLineData(screen, row + amount)) != 0
		&& (dst = getLineData(screen, row)) != 0) {
		CopyCells(screen, src, dst, left, length);
	    }
	}
	while (row >= top) {
	    ClearCells(xw, 0, (unsigned) length, row, left);
	    --row;
	}
    }
}

/*
 * scrolls the screen by amount lines, erases bottom, doesn't alter
 * cursor position (i.e. cursor moves down amount relative to text).
 * All done within the scrolling region, of course.
 * requires: amount > 0
 */
void
xtermScroll(XtermWidget xw, int amount)
{
    TScreen *screen = TScreenOf(xw);
    int i;
    int shift;
    int bot;
    int refreshtop = 0;
    int refreshheight;
    int scrolltop;
    int scrollheight;
    int left = ScrnLeftMargin(xw);
    int right = ScrnRightMargin(xw);
    Boolean scroll_all_lines = (Boolean) (screen->scrollWidget
					  && !screen->whichBuf
					  && screen->top_marg == 0);

    TRACE(("xtermScroll count=%d\n", amount));

    screen->cursor_busy += 1;
    screen->cursor_moved = True;

    if (screen->cursor_state)
	HideCursor();

    i = screen->bot_marg - screen->top_marg + 1;
    if (amount > i)
	amount = i;

    {
	if (ScrnHaveSelection(screen))
	    adjustHiliteOnFwdScroll(xw, amount, scroll_all_lines);

	if (screen->jumpscroll) {
	    if (screen->scroll_amt > 0) {
		if (!screen->fastscroll) {
		    if (screen->refresh_amt + amount > i)
			FlushScroll(xw);
		}
		screen->scroll_amt += amount;
		screen->refresh_amt += amount;
	    } else {
		if (!screen->fastscroll) {
		    if (screen->scroll_amt < 0)
			FlushScroll(xw);
		}
		screen->scroll_amt = amount;
		screen->refresh_amt = amount;
	    }
	    refreshheight = 0;
	} else {
	    ScrollSelection(screen, -(amount), False);
	    if (amount == i) {
		ClearScreen(xw);
		screen->cursor_busy -= 1;
		return;
	    }

	    shift = INX2ROW(screen, 0);
	    bot = screen->max_row - shift;
	    scrollheight = i - amount;
	    refreshheight = amount;

	    if ((refreshtop = screen->bot_marg - refreshheight + 1 + shift) >
		(i = screen->max_row - refreshheight + 1))
		refreshtop = i;

	    if (scroll_all_lines) {
		scrolltop = 0;
		if ((scrollheight += shift) > i)
		    scrollheight = i;
		if ((i = screen->savedlines) < screen->savelines) {
		    if ((i += amount) > screen->savelines)
			i = screen->savelines;
		    screen->savedlines = i;
		    ScrollBarDrawThumb(screen->scrollWidget);
		}
	    } else {
		scrolltop = screen->top_marg + shift;
		if ((i = screen->bot_marg - bot) > 0) {
		    scrollheight -= i;
		    if ((i = screen->top_marg + amount - 1 - bot) >= 0) {
			refreshtop += i;
			refreshheight -= i;
		    }
		}
	    }

	    if (screen->multiscroll && amount == 1 &&
		screen->topline == 0 && screen->top_marg == 0 &&
		screen->bot_marg == screen->max_row) {
		if (screen->incopy < 0 && screen->scrolls == 0)
		    CopyWait(xw);
		screen->scrolls++;
	    }

	    vertical_copy_area(xw,
			       scrolltop + amount,
			       scrollheight,
			       amount,
			       left,
			       right);

	    if (refreshheight > 0) {
		ClearCurBackground(xw,
				   refreshtop,
				   left,
				   (unsigned) refreshheight,
				   (unsigned) (right + 1 - left),
				   (unsigned) FontWidth(screen));
		if (refreshheight > shift)
		    refreshheight = shift;
	    }
	}
    }

    if (amount > 0) {
	if (left > 0 || right < screen->max_col) {
	    scrollInMargins(xw, amount, screen->top_marg);
	} else if (scroll_all_lines) {
	    ScrnDeleteLine(xw,
			   screen->saveBuf_index,
			   screen->bot_marg + screen->savelines,
			   0,
			   (unsigned) amount);
	} else {
	    ScrnDeleteLine(xw,
			   screen->visbuf,
			   screen->bot_marg,
			   screen->top_marg,
			   (unsigned) amount);
	}
    }

    scroll_displayed_graphics(xw, amount);

    if (refreshheight > 0) {
	ScrnRefresh(xw,
		    refreshtop,
		    left,
		    refreshheight,
		    right + 1 - left,
		    False);
    }

    screen->cursor_busy -= 1;
    return;
}

/*
 * This is from ISO 6429, not found in any of DEC's terminals.
 */
void
xtermScrollLR(XtermWidget xw, int amount, Bool toLeft)
{
    if (amount > 0) {
	xtermColScroll(xw, amount, toLeft, 0);
    }
}

/*
 * Implement DECBI/DECFI (back/forward column index)
 */
void
xtermColIndex(XtermWidget xw, Bool toLeft)
{
    TScreen *screen = TScreenOf(xw);
    int margin;

    if (toLeft) {
	margin = ScrnLeftMargin(xw);
	if (screen->cur_col > margin) {
	    CursorBack(xw, 1);
	} else if (screen->cur_col == margin) {
	    xtermColScroll(xw, 1, False, screen->cur_col);
	}
    } else {
	margin = ScrnRightMargin(xw);
	if (screen->cur_col < margin) {
	    CursorForward(xw, 1);
	} else if (screen->cur_col == margin) {
	    xtermColScroll(xw, 1, True, ScrnLeftMargin(xw));
	}
    }
}

/*
 * Implement DECDC/DECIC (delete/insert column)
 */
void
xtermColScroll(XtermWidget xw, int amount, Bool toLeft, int at_col)
{
    TScreen *screen = TScreenOf(xw);

    if (amount > 0) {
	int min_row;
	int max_row;

	if (ScrnHaveRowMargins(screen)) {
	    min_row = screen->top_marg;
	    max_row = screen->bot_marg;
	} else {
	    min_row = 0;
	    max_row = screen->max_row;
	}

	if (screen->cur_row >= min_row
	    && screen->cur_row <= max_row
	    && screen->cur_col >= screen->lft_marg
	    && screen->cur_col <= screen->rgt_marg) {
	    int save_row = screen->cur_row;
	    int save_col = screen->cur_col;
	    int row;

	    screen->cur_col = at_col;
	    if (toLeft) {
		for (row = min_row; row <= max_row; row++) {
		    screen->cur_row = row;
		    ScrnDeleteChar(xw, (unsigned) amount);
		}
	    } else {
		for (row = min_row; row <= max_row; row++) {
		    screen->cur_row = row;
		    ScrnInsertChar(xw, (unsigned) amount);
		}
	    }
	    screen->cur_row = save_row;
	    screen->cur_col = save_col;
	    xtermRepaint(xw);
	}
    }
}

/*
 * Reverse scrolls the screen by amount lines, erases top, doesn't alter
 * cursor position (i.e. cursor moves up amount relative to text).
 * All done within the scrolling region, of course.
 * Requires: amount > 0
 */
void
RevScroll(XtermWidget xw, int amount)
{
    TScreen *screen = TScreenOf(xw);
    int i = screen->bot_marg - screen->top_marg + 1;
    int shift;
    int bot;
    int refreshtop;
    int refreshheight;
    int scrolltop;
    int scrollheight;
    int left = ScrnLeftMargin(xw);
    int right = ScrnRightMargin(xw);

    TRACE(("RevScroll count=%d\n", amount));

    screen->cursor_busy += 1;
    screen->cursor_moved = True;

    if (screen->cursor_state)
	HideCursor();

    if (amount > i)
	amount = i;

    if (ScrnHaveSelection(screen))
	adjustHiliteOnBakScroll(xw, amount);

    if (screen->jumpscroll) {
	if (screen->scroll_amt < 0) {
	    if (-screen->refresh_amt + amount > i)
		FlushScroll(xw);
	    screen->scroll_amt -= amount;
	    screen->refresh_amt -= amount;
	} else {
	    if (screen->scroll_amt > 0)
		FlushScroll(xw);
	    screen->scroll_amt = -amount;
	    screen->refresh_amt = -amount;
	}
    } else {
	shift = INX2ROW(screen, 0);
	bot = screen->max_row - shift;
	refreshheight = amount;
	scrollheight = screen->bot_marg - screen->top_marg - refreshheight + 1;
	refreshtop = screen->top_marg + shift;
	scrolltop = refreshtop + refreshheight;
	if ((i = screen->bot_marg - bot) > 0)
	    scrollheight -= i;
	if ((i = screen->top_marg + refreshheight - 1 - bot) > 0)
	    refreshheight -= i;

	if (screen->multiscroll && amount == 1 &&
	    screen->topline == 0 && screen->top_marg == 0 &&
	    screen->bot_marg == screen->max_row) {
	    if (screen->incopy < 0 && screen->scrolls == 0)
		CopyWait(xw);
	    screen->scrolls++;
	}

	vertical_copy_area(xw,
			   scrolltop - amount,
			   scrollheight,
			   -amount,
			   left,
			   right);

	if (refreshheight > 0) {
	    ClearCurBackground(xw,
			       refreshtop,
			       left,
			       (unsigned) refreshheight,
			       (unsigned) (right + 1 - left),
			       (unsigned) FontWidth(screen));
	}
    }
    if (amount > 0) {
	if (left > 0 || right < screen->max_col) {
	    scrollInMargins(xw, -amount, screen->top_marg);
	} else {
	    ScrnInsertLine(xw,
			   screen->visbuf,
			   screen->bot_marg,
			   screen->top_marg,
			   (unsigned) amount);
	}
    }
    screen->cursor_busy -= 1;
    return;
}

#define setZIconBeep(xw)	/* nothing */

/*
 * write a string str of length len onto the screen at
 * the current cursor position.  update cursor position.
 */
void
WriteText(XtermWidget xw, IChar *str, Cardinal len)
{
    TScreen *screen = TScreenOf(xw);
    CLineData *ld = 0;
    int fg;
    unsigned test;
    unsigned attr_flags = xw->flags;
    CellColor fg_bg = makeColorPair(xw->cur_foreground, xw->cur_background);
    unsigned cells = visual_width(str, len);
    GC currentGC;

    TRACE(("WriteText %d (%2d,%2d) %3d:%s\n",
	   screen->topline,
	   screen->cur_row,
	   screen->cur_col,
	   len, visibleIChars(str, len)));

    if (cells + (unsigned) screen->cur_col > (unsigned) MaxCols(screen)) {
	cells = (unsigned) (MaxCols(screen) - screen->cur_col);
    }

    if (ScrnHaveSelection(screen)
	&& ScrnIsRowInSelection(screen, INX2ROW(screen, screen->cur_row))) {
	ScrnDisownSelection(xw);
    }

    /* if we are in insert-mode, reserve space for the new cells */
    if (attr_flags & INSERT) {
	InsertChar(xw, cells);
    }

    if (AddToVisible(xw)
	&& ((ld = getLineData(screen, screen->cur_row))) != 0) {
	if (screen->cursor_state)
	    HideCursor();

	/*
	 * If we overwrite part of a multi-column character, fill the rest
	 * of it with blanks.
	 */
	if_OPT_WIDE_CHARS(screen, {
	    int kl;
	    int kr;
	    if (DamagedCurCells(screen, cells, &kl, &kr))
		ClearInLine(xw, screen->cur_row, kl, (unsigned) (kr - kl + 1));
	});

	if (attr_flags & INVISIBLE) {
	    Cardinal n;
	    for (n = 0; n < cells; ++n)
		str[n] = ' ';
	}

	TRACE(("WriteText calling drawXtermText (%d) (%d,%d)\n",
	       LineCharSet(screen, ld),
	       screen->cur_col,
	       screen->cur_row));

	test = attr_flags;

	/* make sure that the correct GC is current */
	currentGC = updatedXtermGC(xw, attr_flags, fg_bg, False);

	drawXtermText(xw,
		      test & DRAWX_MASK,
		      0,
		      currentGC,
		      LineCursorX(screen, ld, screen->cur_col),
		      CursorY(screen, screen->cur_row),
		      LineCharSet(screen, ld),
		      str, len, 0);

	resetXtermGC(xw, attr_flags, False);
    }

    ScrnWriteText(xw, str, attr_flags, fg_bg, len);
    CursorForward(xw, (int) cells);
    setZIconBeep(xw);
    return;
}

/*
 * If cursor not in scrolling region, returns.  Else,
 * inserts n blank lines at the cursor's position.  Lines above the
 * bottom margin are lost.
 */
void
InsertLine(XtermWidget xw, int n)
{
    TScreen *screen = TScreenOf(xw);
    int i;
    int shift;
    int bot;
    int refreshtop;
    int refreshheight;
    int scrolltop;
    int scrollheight;
    int left = ScrnLeftMargin(xw);
    int right = ScrnRightMargin(xw);

    if (!ScrnIsRowInMargins(screen, screen->cur_row)
	|| screen->cur_col < left
	|| screen->cur_col > right)
	return;

    TRACE(("InsertLine count=%d\n", n));

    if (screen->cursor_state)
	HideCursor();

    if (ScrnHaveSelection(screen)
	&& ScrnAreRowsInSelection(screen,
				  INX2ROW(screen, screen->top_marg),
				  INX2ROW(screen, screen->cur_row - 1))
	&& ScrnAreRowsInSelection(screen,
				  INX2ROW(screen, screen->cur_row),
				  INX2ROW(screen, screen->bot_marg))) {
	ScrnDisownSelection(xw);
    }

    ResetWrap(screen);
    if (n > (i = screen->bot_marg - screen->cur_row + 1))
	n = i;
    if (screen->jumpscroll) {
	if (screen->scroll_amt <= 0 &&
	    screen->cur_row <= -screen->refresh_amt) {
	    if (-screen->refresh_amt + n > MaxRows(screen))
		FlushScroll(xw);
	    screen->scroll_amt -= n;
	    screen->refresh_amt -= n;
	} else {
	    if (screen->scroll_amt)
		FlushScroll(xw);
	}
    }
    if (!screen->scroll_amt) {
	shift = INX2ROW(screen, 0);
	bot = screen->max_row - shift;
	refreshheight = n;
	scrollheight = screen->bot_marg - screen->cur_row - refreshheight + 1;
	refreshtop = screen->cur_row + shift;
	scrolltop = refreshtop + refreshheight;
	if ((i = screen->bot_marg - bot) > 0)
	    scrollheight -= i;
	if ((i = screen->cur_row + refreshheight - 1 - bot) > 0)
	    refreshheight -= i;
	vertical_copy_area(xw, scrolltop - n, scrollheight, -n, left, right);
	if (refreshheight > 0) {
	    ClearCurBackground(xw,
			       refreshtop,
			       left,
			       (unsigned) refreshheight,
			       (unsigned) (right + 1 - left),
			       (unsigned) FontWidth(screen));
	}
    }
    if (n > 0) {
	if (left > 0 || right < screen->max_col) {
	    scrollInMargins(xw, -n, screen->cur_row);
	} else {
	    ScrnInsertLine(xw,
			   screen->visbuf,
			   screen->bot_marg,
			   screen->cur_row,
			   (unsigned) n);
	}
    }
}

/*
 * If cursor not in scrolling region, returns.  Else, deletes n lines
 * at the cursor's position, lines added at bottom margin are blank.
 */
void
DeleteLine(XtermWidget xw, int n)
{
    TScreen *screen = TScreenOf(xw);
    int i;
    int shift;
    int bot;
    int refreshtop;
    int refreshheight;
    int scrolltop;
    int scrollheight;
    int left = ScrnLeftMargin(xw);
    int right = ScrnRightMargin(xw);
    Boolean scroll_all_lines = (Boolean) (screen->scrollWidget
					  && !screen->whichBuf
					  && screen->cur_row == 0);

    if (!ScrnIsRowInMargins(screen, screen->cur_row) ||
	!ScrnIsColInMargins(screen, screen->cur_col))
	return;

    TRACE(("DeleteLine count=%d\n", n));

    if (screen->cursor_state)
	HideCursor();

    if (n > (i = screen->bot_marg - screen->cur_row + 1)) {
	n = i;
    }
    if (ScrnHaveSelection(screen)
	&& ScrnAreRowsInSelection(screen,
				  INX2ROW(screen, screen->cur_row),
				  INX2ROW(screen, screen->cur_row + n - 1))) {
	ScrnDisownSelection(xw);
    }

    ResetWrap(screen);
    if (screen->jumpscroll) {
	if (screen->scroll_amt >= 0 && screen->cur_row == screen->top_marg) {
	    if (screen->refresh_amt + n > MaxRows(screen))
		FlushScroll(xw);
	    screen->scroll_amt += n;
	    screen->refresh_amt += n;
	} else {
	    if (screen->scroll_amt)
		FlushScroll(xw);
	}
    }

    /* adjust screen->buf */
    if (n > 0) {
	if (left > 0 || right < screen->max_col) {
	    scrollInMargins(xw, n, screen->cur_row);
	} else if (scroll_all_lines) {
	    ScrnDeleteLine(xw,
			   screen->saveBuf_index,
			   screen->bot_marg + screen->savelines,
			   0,
			   (unsigned) n);
	} else {
	    ScrnDeleteLine(xw,
			   screen->visbuf,
			   screen->bot_marg,
			   screen->cur_row,
			   (unsigned) n);
	}
    }

    /* repaint the screen, as needed */
    if (!screen->scroll_amt) {
	shift = INX2ROW(screen, 0);
	bot = screen->max_row - shift;
	scrollheight = i - n;
	refreshheight = n;
	if ((refreshtop = screen->bot_marg - refreshheight + 1 + shift) >
	    (i = screen->max_row - refreshheight + 1))
	    refreshtop = i;
	if (scroll_all_lines) {
	    scrolltop = 0;
	    if ((scrollheight += shift) > i)
		scrollheight = i;
	    if ((i = screen->savedlines) < screen->savelines) {
		if ((i += n) > screen->savelines)
		    i = screen->savelines;
		screen->savedlines = i;
		ScrollBarDrawThumb(screen->scrollWidget);
	    }
	} else {
	    scrolltop = screen->cur_row + shift;
	    if ((i = screen->bot_marg - bot) > 0) {
		scrollheight -= i;
		if ((i = screen->cur_row + n - 1 - bot) >= 0) {
		    refreshheight -= i;
		}
	    }
	}
	vertical_copy_area(xw, scrolltop + n, scrollheight, n, left, right);
	if (shift > 0 && refreshheight > 0) {
	    int rows = refreshheight;
	    if (rows > shift)
		rows = shift;
	    ScrnUpdate(xw, refreshtop, 0, rows, MaxCols(screen), True);
	    refreshtop += shift;
	    refreshheight -= shift;
	}
	if (refreshheight > 0) {
	    ClearCurBackground(xw,
			       refreshtop,
			       left,
			       (unsigned) refreshheight,
			       (unsigned) (right + 1 - left),
			       (unsigned) FontWidth(screen));
	}
    }
}

/*
 * Insert n blanks at the cursor's position, no wraparound
 */
void
InsertChar(XtermWidget xw, unsigned n)
{
    TScreen *screen = TScreenOf(xw);
    CLineData *ld;
    unsigned limit;
    int row = INX2ROW(screen, screen->cur_row);
    int left = ScrnLeftMargin(xw);
    int right = ScrnRightMargin(xw);

    if (screen->cursor_state)
	HideCursor();

    TRACE(("InsertChar count=%d\n", n));

    if (ScrnHaveSelection(screen)
	&& ScrnIsRowInSelection(screen, row)) {
	ScrnDisownSelection(xw);
    }
    ResetWrap(screen);

    limit = (unsigned) (right + 1 - screen->cur_col);

    if (n > limit)
	n = limit;

    if (screen->cur_col < left || screen->cur_col > right) {
	n = 0;
    } else if (AddToVisible(xw)
	       && (ld = getLineData(screen, screen->cur_row)) != 0) {
	int col = right + 1 - (int) n;

	/*
	 * If we shift part of a multi-column character, fill the rest
	 * of it with blanks.  Do similar repair for the text which will
	 * be shifted into the right-margin.
	 */
	if_OPT_WIDE_CHARS(screen, {
	    int kl;
	    int kr = screen->cur_col;
	    if (DamagedCurCells(screen, n, &kl, (int *) 0) && kr > kl) {
		ClearInLine(xw, screen->cur_row, kl, (unsigned) (kr - kl + 1));
	    }
	    kr = screen->max_col - (int) n + 1;
	    if (DamagedCells(screen, n, &kl, (int *) 0,
			     screen->cur_row,
			     kr) && kr > kl) {
		ClearInLine(xw, screen->cur_row, kl, (unsigned) (kr - kl + 1));
	    }
	});

	/*
	 * prevent InsertChar from shifting the end of a line over
	 * if it is being appended to
	 */
	if (non_blank_line(screen, screen->cur_row,
			   screen->cur_col, MaxCols(screen))) {
	    horizontal_copy_area(xw, screen->cur_col,
				 col - screen->cur_col,
				 (int) n);
	}

	ClearCurBackground(xw,
			   INX2ROW(screen, screen->cur_row),
			   screen->cur_col,
			   1U,
			   n,
			   (unsigned) LineFontWidth(screen, ld));
    }
    if (n != 0) {
	/* adjust screen->buf */
	ScrnInsertChar(xw, n);
    }
}

/*
 * Deletes n chars at the cursor's position, no wraparound.
 */
void
DeleteChar(XtermWidget xw, unsigned n)
{
    TScreen *screen = TScreenOf(xw);
    CLineData *ld;
    unsigned limit;
    int row = INX2ROW(screen, screen->cur_row);
    int right = ScrnRightMargin(xw);

    if (screen->cursor_state)
	HideCursor();

    if (!ScrnIsColInMargins(screen, screen->cur_col))
	return;

    TRACE(("DeleteChar count=%d\n", n));

    if (ScrnHaveSelection(screen)
	&& ScrnIsRowInSelection(screen, row)) {
	ScrnDisownSelection(xw);
    }
    ResetWrap(screen);

    limit = (unsigned) (right + 1 - screen->cur_col);

    if (n > limit)
	n = limit;

    if (AddToVisible(xw)
	&& (ld = getLineData(screen, screen->cur_row)) != 0) {
	int col = right + 1 - (int) n;

	/*
	 * If we delete part of a multi-column character, fill the rest
	 * of it with blanks.
	 */
	if_OPT_WIDE_CHARS(screen, {
	    int kl;
	    int kr;
	    if (DamagedCurCells(screen, n, &kl, &kr))
		ClearInLine(xw, screen->cur_row, kl, (unsigned) (kr - kl + 1));
	});

	horizontal_copy_area(xw,
			     (screen->cur_col + (int) n),
			     col - screen->cur_col,
			     -((int) n));

	ClearCurBackground(xw,
			   INX2ROW(screen, screen->cur_row),
			   col,
			   1U,
			   n,
			   (unsigned) LineFontWidth(screen, ld));
    }
    if (n != 0) {
	/* adjust screen->buf */
	ScrnDeleteChar(xw, n);
    }
}

/*
 * Clear from cursor position to beginning of display, inclusive.
 */
static void
ClearAbove(XtermWidget xw)
{
    TScreen *screen = TScreenOf(xw);

    if (screen->protected_mode != OFF_PROTECT) {
	int row;
	unsigned len = (unsigned) MaxCols(screen);

	assert(screen->max_col >= 0);
	for (row = 0; row < screen->cur_row; row++)
	    ClearInLine(xw, row, 0, len);
	ClearInLine(xw, screen->cur_row, 0, (unsigned) screen->cur_col);
    } else {
	int top, height;

	if (screen->cursor_state)
	    HideCursor();
	if ((top = INX2ROW(screen, 0)) <= screen->max_row) {
	    if (screen->scroll_amt)
		FlushScroll(xw);
	    if ((height = screen->cur_row + top) > screen->max_row)
		height = screen->max_row + 1;
	    if ((height -= top) > 0) {
		chararea_clear_displayed_graphics(screen,
						  0,
						  top,
						  MaxCols(screen),
						  height);

		ClearCurBackground(xw,
				   top,
				   0,
				   (unsigned) height,
				   (unsigned) MaxCols(screen),
				   (unsigned) FontWidth(screen));
	    }
	}
	ClearBufRows(xw, 0, screen->cur_row - 1);
    }

    ClearLeft(xw);
}

/*
 * Clear from cursor position to end of display, inclusive.
 */
static void
ClearBelow(XtermWidget xw)
{
    TScreen *screen = TScreenOf(xw);

    ClearRight(xw, -1);

    if (screen->protected_mode != OFF_PROTECT) {
	int row;
	unsigned len = (unsigned) MaxCols(screen);

	assert(screen->max_col >= 0);
	for (row = screen->cur_row + 1; row <= screen->max_row; row++)
	    ClearInLine(xw, row, 0, len);
    } else {
	int top;

	if ((top = INX2ROW(screen, screen->cur_row)) <= screen->max_row) {
	    if (screen->scroll_amt)
		FlushScroll(xw);
	    if (++top <= screen->max_row) {
		chararea_clear_displayed_graphics(screen,
						  0,
						  top,
						  MaxCols(screen),
						  (screen->max_row - top + 1));
		ClearCurBackground(xw,
				   top,
				   0,
				   (unsigned) (screen->max_row - top + 1),
				   (unsigned) MaxCols(screen),
				   (unsigned) FontWidth(screen));
	    }
	}
	ClearBufRows(xw, screen->cur_row + 1, screen->max_row);
    }
}

/*
 * Clear the given row, for the given range of columns, returning 1 if no
 * protected characters were found, 0 otherwise.
 */
static int
ClearInLine2(XtermWidget xw, int flags, int row, int col, unsigned len)
{
    TScreen *screen = TScreenOf(xw);
    CLineData *ld;
    int rc = 1;

    TRACE(("ClearInLine(row=%d, col=%d, len=%d) vs %d..%d\n",
	   row, col, len,
	   screen->startH.row,
	   screen->startH.col));

    if (ScrnHaveSelection(screen)
	&& ScrnIsRowInSelection(screen, row)) {
	ScrnDisownSelection(xw);
    }

    if (col + (int) len >= MaxCols(screen)) {
	len = (unsigned) (MaxCols(screen) - col);
    }

    /* If we've marked protected text on the screen, we'll have to
     * check each time we do an erase.
     */
    if (screen->protected_mode != OFF_PROTECT) {
	unsigned n;
	IAttr *attrs = getLineData(screen, row)->attribs + col;
	int saved_mode = screen->protected_mode;
	Bool done;

	/* disable this branch during recursion */
	screen->protected_mode = OFF_PROTECT;

	do {
	    done = True;
	    for (n = 0; n < len; n++) {
		if (attrs[n] & PROTECTED) {
		    rc = 0;	/* found a protected segment */
		    if (n != 0) {
			ClearInLine(xw, row, col, n);
		    }
		    while ((n < len)
			   && (attrs[n] & PROTECTED)) {
			n++;
		    }
		    done = False;
		    break;
		}
	    }
	    /* setup for another segment, past the protected text */
	    if (!done) {
		attrs += n;
		col += (int) n;
		len -= n;
	    }
	} while (!done);

	screen->protected_mode = saved_mode;
	if ((int) len <= 0) {
	    return 0;
	}
    }
    /* fall through to the final non-protected segment */

    if (screen->cursor_state)
	HideCursor();
    ResetWrap(screen);

    if (AddToVisible(xw)
	&& (ld = getLineData(screen, row)) != 0) {

	ClearCurBackground(xw,
			   INX2ROW(screen, row),
			   col,
			   1U,
			   len,
			   (unsigned) LineFontWidth(screen, ld));
    }

    if (len != 0) {
	ClearCells(xw, flags, len, row, col);
    }

    return rc;
}

int
ClearInLine(XtermWidget xw, int row, int col, unsigned len)
{
    TScreen *screen = TScreenOf(xw);
    int flags = 0;

    /*
     * If we're clearing to the end of the line, we won't count this as
     * "drawn" characters.  We'll only do cut/paste on "drawn" characters,
     * so this has the effect of suppressing trailing blanks from a
     * selection.
     */
    if (col + (int) len < MaxCols(screen)) {
	flags |= CHARDRAWN;
    }
    return ClearInLine2(xw, flags, row, col, len);
}

/*
 * Clear the next n characters on the cursor's line, including the cursor's
 * position.
 */
void
ClearRight(XtermWidget xw, int n)
{
    TScreen *screen = TScreenOf(xw);
    LineData *ld;
    unsigned len = (unsigned) (MaxCols(screen) - screen->cur_col);

    assert(screen->max_col >= 0);
    assert(screen->max_col >= screen->cur_col);

    if (n < 0)			/* the remainder of the line */
	n = MaxCols(screen);
    if (n == 0)			/* default for 'ECH' */
	n = 1;

    if (len > (unsigned) n)
	len = (unsigned) n;

    ld = getLineData(screen, screen->cur_row);
    if (AddToVisible(xw)) {
	if_OPT_WIDE_CHARS(screen, {
	    int col = screen->cur_col;
	    int row = screen->cur_row;
	    int kl;
	    int kr;
	    int xx;
	    if (DamagedCurCells(screen, len, &kl, &kr) && kr >= kl) {
		xx = col;
		if (kl < xx) {
		    ClearInLine2(xw, 0, row, kl, (unsigned) (xx - kl));
		}
		xx = col + (int) len - 1;
		if (kr > xx) {
		    ClearInLine2(xw, 0, row, xx + 1, (unsigned) (kr - xx));
		}
	    }
	});
	(void) ClearInLine(xw, screen->cur_row, screen->cur_col, len);
    } else {
	ScrnClearCells(xw, screen->cur_row, screen->cur_col, len);
    }

    /* with the right part cleared, we can't be wrapping */
    LineClrWrapped(ld);
    if (screen->show_wrap_marks) {
	ShowWrapMarks(xw, screen->cur_row, ld);
    }
    ResetWrap(screen);
}

/*
 * Clear first part of cursor's line, inclusive.
 */
static void
ClearLeft(XtermWidget xw)
{
    TScreen *screen = TScreenOf(xw);
    unsigned len = (unsigned) screen->cur_col + 1;

    assert(screen->cur_col >= 0);
    if (AddToVisible(xw)) {
	if_OPT_WIDE_CHARS(screen, {
	    int row = screen->cur_row;
	    int kl;
	    int kr;
	    if (DamagedCurCells(screen, 1, &kl, &kr) && kr >= kl) {
		ClearInLine2(xw, 0, row, kl, (unsigned) (kr - kl + 1));
	    }
	});
	(void) ClearInLine(xw, screen->cur_row, 0, len);
    } else {
	ScrnClearCells(xw, screen->cur_row, 0, len);
    }
}

/*
 * Erase the cursor's line.
 */
static void
ClearLine(XtermWidget xw)
{
    TScreen *screen = TScreenOf(xw);
    unsigned len = (unsigned) MaxCols(screen);

    assert(screen->max_col >= 0);
    (void) ClearInLine(xw, screen->cur_row, 0, len);
}

void
ClearScreen(XtermWidget xw)
{
    TScreen *screen = TScreenOf(xw);
    int top;

    TRACE(("ClearScreen\n"));

    if (screen->cursor_state)
	HideCursor();

    ScrnDisownSelection(xw);
    ResetWrap(screen);
    if ((top = INX2ROW(screen, 0)) <= screen->max_row) {
	if (screen->scroll_amt)
	    FlushScroll(xw);
	chararea_clear_displayed_graphics(screen,
					  0,
					  top,
					  MaxCols(screen),
					  (screen->max_row - top + 1));
	ClearCurBackground(xw,
			   top,
			   0,
			   (unsigned) (screen->max_row - top + 1),
			   (unsigned) MaxCols(screen),
			   (unsigned) FontWidth(screen));
    }
    ClearBufRows(xw, 0, screen->max_row);
}

/*
 * If we've written protected text DEC-style, and are issuing a non-DEC
 * erase, temporarily reset the protected_mode flag so that the erase will
 * ignore the protected flags.
 */
void
do_erase_line(XtermWidget xw, int param, int mode)
{
    TScreen *screen = TScreenOf(xw);
    int saved_mode = screen->protected_mode;

    if (saved_mode == DEC_PROTECT
	&& saved_mode != mode) {
	screen->protected_mode = OFF_PROTECT;
    }

    switch (param) {
    case -1:			/* DEFAULT */
    case 0:
	ClearRight(xw, -1);
	break;
    case 1:
	ClearLeft(xw);
	break;
    case 2:
	ClearLine(xw);
	break;
    }
    screen->protected_mode = saved_mode;
}

/*
 * Just like 'do_erase_line()', except that this intercepts ED controls.  If we
 * clear the whole screen, we'll get the return-value from ClearInLine, and
 * find if there were any protected characters left.  If not, reset the
 * protected mode flag in the screen data (it's slower).
 */
void
do_erase_display(XtermWidget xw, int param, int mode)
{
    TScreen *screen = TScreenOf(xw);
    int saved_mode = screen->protected_mode;

    if (saved_mode == DEC_PROTECT
	&& saved_mode != mode)
	screen->protected_mode = OFF_PROTECT;

    switch (param) {
    case -1:			/* DEFAULT */
    case 0:
	if (screen->cur_row == 0
	    && screen->cur_col == 0) {
	    screen->protected_mode = saved_mode;
	    do_erase_display(xw, 2, mode);
	    saved_mode = screen->protected_mode;
	} else
	    ClearBelow(xw);
	break;

    case 1:
	if (screen->cur_row == screen->max_row
	    && screen->cur_col == screen->max_col) {
	    screen->protected_mode = saved_mode;
	    do_erase_display(xw, 2, mode);
	    saved_mode = screen->protected_mode;
	} else
	    ClearAbove(xw);
	break;

    case 2:
	/*
	 * We use 'ClearScreen()' throughout the remainder of the
	 * program for places where we don't care if the characters are
	 * protected or not.  So we modify the logic around this call
	 * on 'ClearScreen()' to handle protected characters.
	 */
	if (screen->protected_mode != OFF_PROTECT) {
	    int row;
	    int rc = 1;
	    unsigned len = (unsigned) MaxCols(screen);

	    assert(screen->max_col >= 0);
	    for (row = 0; row <= screen->max_row; row++)
		rc &= ClearInLine(xw, row, 0, len);
	    if (rc != 0)
		saved_mode = OFF_PROTECT;
	} else {
	    ClearScreen(xw);
	}
	break;

    case 3:
	/* xterm addition - erase saved lines. */
	screen->savedlines = 0;
	ScrollBarDrawThumb(screen->scrollWidget);
	break;
    }
    screen->protected_mode = saved_mode;
}

static Boolean
screen_has_data(XtermWidget xw)
{
    TScreen *screen = TScreenOf(xw);
    Boolean result = False;
    CLineData *ld;
    int row, col;

    for (row = 0; row < screen->max_row; ++row) {
	if ((ld = getLineData(screen, row)) != 0) {
	    for (col = 0; col < screen->max_col; ++col) {
		if (ld->attribs[col] & CHARDRAWN) {
		    result = True;
		    break;
		}
	    }
	}
	if (result)
	    break;
    }
    return result;
}

/*
 * Like tiXtraScroll, perform a scroll up of the page contents.  In this case,
 * it happens for the special case when erasing the whole display starting from
 * the upper-left corner of the screen.
 */
void
do_cd_xtra_scroll(XtermWidget xw)
{
    TScreen *screen = TScreenOf(xw);

    if (xw->misc.cdXtraScroll
	&& screen->cur_col == 0
	&& screen->cur_row == 0
	&& screen_has_data(xw)) {
	xtermScroll(xw, screen->max_row);
    }
}

/*
 * Scroll the page up (saving it).  This is called when doing terminal
 * initialization (ti) or exiting from that (te).
 */
void
do_ti_xtra_scroll(XtermWidget xw)
{
    TScreen *screen = TScreenOf(xw);

    if (xw->misc.tiXtraScroll) {
	xtermScroll(xw, screen->max_row);
    }
}

static void
CopyWait(XtermWidget xw)
{
    TScreen *screen = TScreenOf(xw);
    XEvent reply;
    XEvent *rep = &reply;

    for (;;) {
	XWindowEvent(screen->display, VWindow(screen), ExposureMask, &reply);
	switch (reply.type) {
	case Expose:
	    HandleExposure(xw, &reply);
	    break;
	case NoExpose:
	case GraphicsExpose:
	    if (screen->incopy <= 0) {
		screen->incopy = 1;
		if (screen->scrolls > 0)
		    screen->scrolls--;
	    }
	    if (reply.type == GraphicsExpose)
		HandleExposure(xw, &reply);

	    if ((reply.type == NoExpose) ||
		((XExposeEvent *) rep)->count == 0) {
		if (screen->incopy <= 0 && screen->scrolls > 0)
		    screen->scrolls--;
		if (screen->scrolls == 0) {
		    screen->incopy = 0;
		    return;
		}
		screen->incopy = -1;
	    }
	    break;
	}
    }
}

/*
 * used by vertical_copy_area and and horizontal_copy_area
 */
static void
copy_area(XtermWidget xw,
	  int src_x,
	  int src_y,
	  unsigned width,
	  unsigned height,
	  int dest_x,
	  int dest_y)
{
    TScreen *screen = TScreenOf(xw);

    if (width != 0 && height != 0) {
	/* wait for previous CopyArea to complete unless
	   multiscroll is enabled and active */
	if (screen->incopy && screen->scrolls == 0)
	    CopyWait(xw);
	screen->incopy = -1;

	/* save for translating Expose events */
	screen->copy_src_x = src_x;
	screen->copy_src_y = src_y;
	screen->copy_width = width;
	screen->copy_height = height;
	screen->copy_dest_x = dest_x;
	screen->copy_dest_y = dest_y;

	XCopyArea(screen->display,
		  VDrawable(screen), VDrawable(screen),
		  NormalGC(xw, screen),
		  src_x, src_y, width, height, dest_x, dest_y);
    }
}

/*
 * use when inserting or deleting characters on the current line
 */
static void
horizontal_copy_area(XtermWidget xw,
		     int firstchar,	/* char pos on screen to start copying at */
		     int nchars,
		     int amount)	/* number of characters to move right */
{
    TScreen *screen = TScreenOf(xw);
    CLineData *ld;

    if ((ld = getLineData(screen, screen->cur_row)) != 0) {
	int src_x = LineCursorX(screen, ld, firstchar);
	int src_y = CursorY(screen, screen->cur_row);

	copy_area(xw, src_x, src_y,
		  (unsigned) (nchars * LineFontWidth(screen, ld)),
		  (unsigned) FontHeight(screen),
		  src_x + amount * LineFontWidth(screen, ld), src_y);
    }
}

/*
 * use when inserting or deleting lines from the screen
 */
static void
vertical_copy_area(XtermWidget xw,
		   int firstline,	/* line on screen to start copying at */
		   int nlines,
		   int amount,	/* number of lines to move up (neg=down) */
		   int left,
		   int right)
{
    TScreen *screen = TScreenOf(xw);

    TRACE(("vertical_copy_area - firstline=%d nlines=%d left=%d right=%d amount=%d\n",
	   firstline, nlines, left, right, amount));

    if (nlines > 0) {
	int src_x = CursorX(screen, left);
	int src_y = firstline * FontHeight(screen) + screen->border;
	unsigned int w = (unsigned) ((right + 1 - left) * FontWidth(screen));
	unsigned int h = (unsigned) (nlines * FontHeight(screen));
	int dst_x = src_x;
	int dst_y = src_y - amount * FontHeight(screen);

	copy_area(xw, src_x, src_y, w, h, dst_x, dst_y);

	if (screen->show_wrap_marks) {
	    CLineData *ld;
	    int row;
	    for (row = firstline; row < firstline + nlines; ++row) {
		if ((ld = getLineData(screen, row)) != 0) {
		    ShowWrapMarks(xw, row, ld);
		}
	    }
	}
    }
}

/*
 * use when scrolling the entire screen
 */
void
scrolling_copy_area(XtermWidget xw,
		    int firstline,	/* line on screen to start copying at */
		    int nlines,
		    int amount)	/* number of lines to move up (neg=down) */
{

    if (nlines > 0) {
	vertical_copy_area(xw, firstline, nlines, amount, 0, TScreenOf(xw)->max_col);
    }
}

/*
 * Handler for Expose events on the VT widget.
 * Returns 1 iff the area where the cursor was got refreshed.
 */
int
HandleExposure(XtermWidget xw, XEvent *event)
{
    TScreen *screen = TScreenOf(xw);
    XExposeEvent *reply = (XExposeEvent *) event;


    /* if not doing CopyArea or if this is a GraphicsExpose, don't translate */
    if (!screen->incopy || event->type != Expose)
	return handle_translated_exposure(xw, reply->x, reply->y,
					  reply->width,
					  reply->height);
    else {
	/* compute intersection of area being copied with
	   area being exposed. */
	int both_x1 = Max(screen->copy_src_x, reply->x);
	int both_y1 = Max(screen->copy_src_y, reply->y);
	int both_x2 = Min(screen->copy_src_x + (int) screen->copy_width,
			  (reply->x + (int) reply->width));
	int both_y2 = Min(screen->copy_src_y + (int) screen->copy_height,
			  (reply->y + (int) reply->height));
	int value = 0;

	/* was anything copied affected? */
	if (both_x2 > both_x1 && both_y2 > both_y1) {
	    /* do the copied area */
	    value = handle_translated_exposure
		(xw, reply->x + screen->copy_dest_x - screen->copy_src_x,
		 reply->y + screen->copy_dest_y - screen->copy_src_y,
		 reply->width, reply->height);
	}
	/* was anything not copied affected? */
	if (reply->x < both_x1 || reply->y < both_y1
	    || reply->x + reply->width > both_x2
	    || reply->y + reply->height > both_y2)
	    value = handle_translated_exposure(xw, reply->x, reply->y,
					       reply->width, reply->height);

	return value;
    }
}

static void
set_background(XtermWidget xw, int color GCC_UNUSED)
{
    TScreen *screen = TScreenOf(xw);
    Pixel c = getXtermBackground(xw, xw->flags, color);

    TRACE(("set_background(%d) %#lx\n", color, c));
    XSetWindowBackground(screen->display, VShellWindow(xw), c);
    XSetWindowBackground(screen->display, VWindow(screen), c);
}

/*
 * Called by the ExposeHandler to do the actual repaint after the coordinates
 * have been translated to allow for any CopyArea in progress.
 * The rectangle passed in is pixel coordinates.
 */
static int
handle_translated_exposure(XtermWidget xw,
			   int rect_x,
			   int rect_y,
			   int rect_width,
			   int rect_height)
{
    TScreen *screen = TScreenOf(xw);
    int toprow, leftcol, nrows, ncols;
    int x0, x1;
    int y0, y1;
    int result = 0;

    TRACE(("handle_translated_exposure at %d,%d size %dx%d\n",
	   rect_y, rect_x, rect_height, rect_width));

    x0 = (rect_x - OriginX(screen));
    x1 = (x0 + rect_width);

    y0 = (rect_y - OriginY(screen));
    y1 = (y0 + rect_height);

    if ((x0 < 0 ||
	 y0 < 0 ||
	 x1 > Width(screen) ||
	 y1 > Height(screen))) {
	set_background(xw, -1);
	XClearArea(screen->display, VWindow(screen),
		   rect_x,
		   rect_y,
		   (unsigned) rect_width,
		   (unsigned) rect_height, False);
    }
    toprow = y0 / FontHeight(screen);
    if (toprow < 0)
	toprow = 0;

    leftcol = x0 / FontWidth(screen);
    if (leftcol < 0)
	leftcol = 0;

    nrows = (y1 - 1) / FontHeight(screen) - toprow + 1;
    ncols = (x1 - 1) / FontWidth(screen) - leftcol + 1;
    toprow -= screen->scrolls;
    if (toprow < 0) {
	nrows += toprow;
	toprow = 0;
    }
    if (toprow + nrows > MaxRows(screen))
	nrows = MaxRows(screen) - toprow;
    if (leftcol + ncols > MaxCols(screen))
	ncols = MaxCols(screen) - leftcol;

    if (nrows > 0 && ncols > 0) {
	ScrnRefresh(xw, toprow, leftcol, nrows, ncols, True);
	first_map_occurred();
	if (screen->cur_row >= toprow &&
	    screen->cur_row < toprow + nrows &&
	    screen->cur_col >= leftcol &&
	    screen->cur_col < leftcol + ncols) {
	    result = 1;
	}

    }
    TRACE(("...handle_translated_exposure %d\n", result));
    return (result);
}

/***====================================================================***/

void
GetColors(XtermWidget xw, ScrnColors * pColors)
{
    TScreen *screen = TScreenOf(xw);
    int n;

    pColors->which = 0;
    for (n = 0; n < NCOLORS; ++n) {
	SET_COLOR_VALUE(pColors, n, T_COLOR(screen, n));
    }
}

void
ChangeColors(XtermWidget xw, ScrnColors * pNew)
{
    Bool repaint = False;
    TScreen *screen = TScreenOf(xw);
    VTwin *win = WhichVWin(screen);

    TRACE(("ChangeColors\n"));

    if (COLOR_DEFINED(pNew, TEXT_CURSOR)) {
	T_COLOR(screen, TEXT_CURSOR) = COLOR_VALUE(pNew, TEXT_CURSOR);
	TRACE(("... TEXT_CURSOR: %#lx\n", T_COLOR(screen, TEXT_CURSOR)));
	/* no repaint needed */
    } else if ((T_COLOR(screen, TEXT_CURSOR) == T_COLOR(screen, TEXT_FG)) &&
	       (COLOR_DEFINED(pNew, TEXT_FG))) {
	if (T_COLOR(screen, TEXT_CURSOR) != COLOR_VALUE(pNew, TEXT_FG)) {
	    T_COLOR(screen, TEXT_CURSOR) = COLOR_VALUE(pNew, TEXT_FG);
	    TRACE(("... TEXT_CURSOR: %#lx\n", T_COLOR(screen, TEXT_CURSOR)));
	    if (screen->Vshow)
		repaint = True;
	}
    }

    if (COLOR_DEFINED(pNew, TEXT_FG)) {
	Pixel fg = COLOR_VALUE(pNew, TEXT_FG);
	T_COLOR(screen, TEXT_FG) = fg;
	TRACE(("... TEXT_FG: %#lx\n", T_COLOR(screen, TEXT_FG)));
	if (screen->Vshow) {
	    setCgsFore(xw, win, gcNorm, fg);
	    setCgsBack(xw, win, gcNormReverse, fg);
	    setCgsFore(xw, win, gcBold, fg);
	    setCgsBack(xw, win, gcBoldReverse, fg);
	    repaint = True;
	}
    }

    if (COLOR_DEFINED(pNew, TEXT_BG)) {
	Pixel bg = COLOR_VALUE(pNew, TEXT_BG);
	T_COLOR(screen, TEXT_BG) = bg;
	TRACE(("... TEXT_BG: %#lx\n", T_COLOR(screen, TEXT_BG)));
	if (screen->Vshow) {
	    setCgsBack(xw, win, gcNorm, bg);
	    setCgsFore(xw, win, gcNormReverse, bg);
	    setCgsBack(xw, win, gcBold, bg);
	    setCgsFore(xw, win, gcBoldReverse, bg);
	    set_background(xw, -1);
	    repaint = True;
	}
    }

    if (COLOR_DEFINED(pNew, MOUSE_FG) || (COLOR_DEFINED(pNew, MOUSE_BG))) {
	if (COLOR_DEFINED(pNew, MOUSE_FG)) {
	    T_COLOR(screen, MOUSE_FG) = COLOR_VALUE(pNew, MOUSE_FG);
	    TRACE(("... MOUSE_FG: %#lx\n", T_COLOR(screen, MOUSE_FG)));
	}
	if (COLOR_DEFINED(pNew, MOUSE_BG)) {
	    T_COLOR(screen, MOUSE_BG) = COLOR_VALUE(pNew, MOUSE_BG);
	    TRACE(("... MOUSE_BG: %#lx\n", T_COLOR(screen, MOUSE_BG)));
	}

	if (screen->Vshow) {
	    recolor_cursor(screen,
			   screen->pointer_cursor,
			   T_COLOR(screen, MOUSE_FG),
			   T_COLOR(screen, MOUSE_BG));
	    XDefineCursor(screen->display, VWindow(screen),
			  screen->pointer_cursor);
	}
	/* no repaint needed */
    }

    if (COLOR_DEFINED(pNew, TEXT_FG) ||
	COLOR_DEFINED(pNew, TEXT_BG) ||
	COLOR_DEFINED(pNew, TEXT_CURSOR)) {
	if (set_cursor_gcs(xw) && screen->Vshow) {
	    repaint = True;
	}
    }
    if (repaint)
	xtermRepaint(xw);
}

void
xtermClear(XtermWidget xw)
{
    TScreen *screen = TScreenOf(xw);

    TRACE(("xtermClear\n"));
    XClearWindow(screen->display, VWindow(screen));
}

void
xtermRepaint(XtermWidget xw)
{
    TScreen *screen = TScreenOf(xw);

    TRACE(("xtermRepaint\n"));
    xtermClear(xw);
    ScrnRefresh(xw, 0, 0, MaxRows(screen), MaxCols(screen), True);
}

/***====================================================================***/

Boolean
isDefaultForeground(const char *name)
{
    return (Boolean) !x_strcasecmp(name, XtDefaultForeground);
}

Boolean
isDefaultBackground(const char *name)
{
    return (Boolean) !x_strcasecmp(name, XtDefaultBackground);
}


/***====================================================================***/

typedef struct {
    Pixel fg;
    Pixel bg;
} ToSwap;

#define hc_param		/* nothing */
#define hc_value		/* nothing */

/*
 * Use this to swap the foreground/background color values in the resource
 * data, and to build up a list of the pairs which must be swapped in the
 * GC cache.
 */
static void
swapLocally(ToSwap * list, int *count, ColorRes * fg, ColorRes * bg hc_param)
{
    ColorRes tmp;
    int n;
    Boolean found = False;

    Pixel fg_color = *fg;
    Pixel bg_color = *bg;

    {
	EXCHANGE(*fg, *bg, tmp);
	for (n = 0; n < *count; ++n) {
	    if ((list[n].fg == fg_color && list[n].bg == bg_color)
		|| (list[n].fg == bg_color && list[n].bg == fg_color)) {
		found = True;
		break;
	    }
	}
	if (!found) {
	    list[*count].fg = fg_color;
	    list[*count].bg = bg_color;
	    *count = *count + 1;
	    TRACE(("swapLocally fg %#lx, bg %#lx ->%d\n",
		   fg_color, bg_color, *count));
	}
    }
}

static void
reallySwapColors(XtermWidget xw, ToSwap * list, int count)
{
    int j, k;

    TRACE(("reallySwapColors\n"));
    for (j = 0; j < count; ++j) {
	for_each_text_gc(k) {
	    redoCgs(xw, list[j].fg, list[j].bg, (CgsEnum) k);
	}
    }
}

static void
swapVTwinGCs(XtermWidget xw, VTwin *win)
{
    swapCgs(xw, win, gcNorm, gcNormReverse);
    swapCgs(xw, win, gcBold, gcBoldReverse);
}

void
ReverseVideo(XtermWidget xw)
{
    TScreen *screen = TScreenOf(xw);
    ToSwap listToSwap[5];
    int numToSwap = 0;

    TRACE(("ReverseVideo\n"));

    /*
     * Swap SGR foreground and background colors.  By convention, these are
     * the colors assigned to "black" (SGR #0) and "white" (SGR #7).  Also,
     * SGR #8 and SGR #15 are the bold (or bright) versions of SGR #0 and
     * #7, respectively.
     *
     * We don't swap colors that happen to match the screen's foreground
     * and background because that tends to produce bizarre effects.
     */
#define swapAnyColor(name,a,b) swapLocally(listToSwap, &numToSwap, &(screen->name[a]), &(screen->name[b]) hc_value)
#define swapAColor(a,b) swapAnyColor(Acolors, a, b)
    if_OPT_ISO_COLORS(screen, {
	swapAColor(0, 7);
	swapAColor(8, 15);
    });

    if (T_COLOR(screen, TEXT_CURSOR) == T_COLOR(screen, TEXT_FG))
	T_COLOR(screen, TEXT_CURSOR) = T_COLOR(screen, TEXT_BG);

#define swapTColor(a,b) swapAnyColor(Tcolors, a, b)
    swapTColor(TEXT_FG, TEXT_BG);
    swapTColor(MOUSE_FG, MOUSE_BG);

    reallySwapColors(xw, listToSwap, numToSwap);

    swapVTwinGCs(xw, &(screen->fullVwin));

    xw->misc.re_verse = (Boolean) !xw->misc.re_verse;

    if (XtIsRealized((Widget) xw)) {
	xtermDisplayCursor(xw);
    }

    if (screen->scrollWidget)
	ScrollBarReverseVideo(screen->scrollWidget);

    if (XtIsRealized((Widget) xw)) {
	set_background(xw, -1);
    }
    if (XtIsRealized((Widget) xw)) {
	xtermRepaint(xw);
    }
    ReverseOldColors(xw);
    set_cursor_gcs(xw);
    update_reversevideo();
    TRACE(("...ReverseVideo\n"));
}

void
recolor_cursor(TScreen *screen,
	       Cursor cursor,	/* X cursor ID to set */
	       unsigned long fg,	/* pixel indexes to look up */
	       unsigned long bg)	/* pixel indexes to look up */
{
    Display *dpy = screen->display;
    XColor colordefs[2];	/* 0 is foreground, 1 is background */

    colordefs[0].pixel = fg;
    colordefs[1].pixel = bg;
    XQueryColors(dpy, DefaultColormap(dpy, DefaultScreen(dpy)),
		 colordefs, 2);
    XRecolorCursor(dpy, cursor, colordefs, colordefs + 1);
    cleanup_colored_cursor();
    return;
}

/*
 * Use this when the characters will not fill the cell area properly.  Fill the
 * area where we'll write the characters, otherwise we'll get gaps between
 * them, e.g., in the original background color.
 *
 * The cursor is a special case, because the XFillRectangle call only uses the
 * foreground, while we've set the cursor color in the background.  So we need
 * a special GC for that.
 */
static void
xtermFillCells(XtermWidget xw,
	       unsigned draw_flags,
	       GC gc,
	       int x,
	       int y,
	       Cardinal len)
{
    TScreen *screen = TScreenOf(xw);
    VTwin *currentWin = WhichVWin(screen);

    if (!(draw_flags & NOBACKGROUND)) {
	CgsEnum srcId = getCgsId(xw, currentWin, gc);
	CgsEnum dstId = gcMAX;
	Pixel fg = getCgsFore(xw, currentWin, gc);
	Pixel bg = getCgsBack(xw, currentWin, gc);

	switch (srcId) {
	case gcVTcursNormal:
	case gcVTcursReverse:
	    dstId = gcVTcursOutline;
	    break;
	case gcVTcursFilled:
	case gcVTcursOutline:
	    /* FIXME */
	    break;
	case gcNorm:
	    dstId = gcNormReverse;
	    break;
	case gcNormReverse:
	    dstId = gcNorm;
	    break;
	case gcBold:
	    dstId = gcBoldReverse;
	    break;
	case gcBoldReverse:
	    dstId = gcBold;
	    break;
	case gcMAX:
	    break;
	}

	if (dstId != gcMAX) {
	    setCgsFore(xw, currentWin, dstId, bg);
	    setCgsBack(xw, currentWin, dstId, fg);

	    XFillRectangle(screen->display, VDrawable(screen),
			   getCgsGC(xw, currentWin, dstId),
			   x, y,
			   len * (Cardinal) FontWidth(screen),
			   (unsigned) FontHeight(screen));
	}
    }
}

#define xtermSetClipRectangles(dpy, gc, x, y, rp, nr, order) \
	    XSetClipRectangles(dpy, gc, x, y, rp, (int) nr, order)

#define beginClipping(screen,gc,pwidth,plength)		/* nothing */
#define endClipping(screen,gc)	/* nothing */

#define beginXftClipping(screen,px,py,plength)	/* nothing */
#define endXftClipping(screen)	/* nothing */


#define WhichVFontData(screen,name) \
				(&((screen)->name))

static int
drawUnderline(XtermWidget xw,
	      GC gc,
	      unsigned attr_flags,
	      unsigned underline_len,
	      int font_width,
	      int x,
	      int y,
	      Bool did_ul)
{
    TScreen *screen = TScreenOf(xw);

    if (screen->underline && !did_ul) {
	int repeat = 0;
	int descent = FontDescent(screen);
	int length = x + (int) underline_len * font_width - 1;

	if ((attr_flags & UNDERLINE)) {
	    repeat = 1;
	}
	while (repeat-- > 0) {
	    if (descent-- > 1)
		y++;
	    XDrawLine(screen->display, VDrawable(screen), gc,
		      x, y,
		      length,
		      y);
	}
    }
    return y;
}


/*
 * Draws text with the specified combination of bold/underline.  The return
 * value is the updated x position.
 */
int
drawXtermText(XtermWidget xw,
	      unsigned attr_flags,
	      unsigned draw_flags,
	      GC gc,
	      int start_x,
	      int start_y,
	      int chrset,
	      const IChar *text,
	      Cardinal len,
	      int on_wide)
{
    int x = start_x, y = start_y;
    TScreen *screen = TScreenOf(xw);
    Cardinal real_length = len;
    Cardinal underline_len = 0;
    /* Intended width of the font to draw (as opposed to the actual width of
       the X font, and the width of the default font) */
    int font_width = ((draw_flags & DOUBLEWFONT) ? 2 : 1) * screen->fnt_wide;
    Bool did_ul = False;
    XTermFonts *curFont;

    curFont = ((attr_flags & BOLDATTR(screen))
	       ? WhichVFontData(screen, fnts[fBold])
	       : WhichVFontData(screen, fnts[fNorm]));
    /*
     * If we're asked to display a proportional font, do this with a fixed
     * pitch.  Yes, it's ugly.  But we cannot distinguish the use of xterm
     * as a dumb terminal vs its use as in fullscreen programs such as vi.
     * Hint: do not try to use a proportional font in the icon.
     */
    if (!IsIcon(screen) && !(draw_flags & CHARBYCHAR) && screen->fnt_prop) {
	int adj, width;

	while (len--) {
	    int cells = WideCells(*text);
	    {
		if_WIDE_OR_NARROW(screen, {
		    XChar2b temp[1];
		    temp[0].byte2 = LO_BYTE(*text);
		    temp[0].byte1 = HI_BYTE(*text);
		    width = XTextWidth16(curFont->fs, temp, 1);
		}
		, {
		    char temp[1];
		    temp[0] = (char) LO_BYTE(*text);
		    width = XTextWidth(curFont->fs, temp, 1);
		});
		adj = (FontWidth(screen) - width) / 2;
		if (adj < 0)
		    adj = 0;
	    }
	    xtermFillCells(xw, draw_flags, gc, x, y, (Cardinal) cells);
	    x = drawXtermText(xw,
			      attr_flags,
			      draw_flags | NOBACKGROUND | CHARBYCHAR,
			      gc, x + adj, y, chrset,
			      text++, 1, on_wide) - adj;
	}

	return x;
    }
    /*
     * Behave as if the font has (maybe Unicode-replacements for) drawing
     * characters in the range 1-31 (either we were not asked to ignore them,
     * or the caller made sure that there is none).
     */
#define AttrFlags() (attr_flags & DRAWX_MASK)
#define DrawFlags() (draw_flags & ~DRAWX_MASK)
    TRACE(("drawtext%c[%4d,%4d] {%#x,%#x} (%d) %d:%s\n",
	   screen->cursor_state == OFF ? ' ' : '*',
	   y, x,
	   AttrFlags(),
	   DrawFlags(),
	   chrset, len,
	   visibleIChars(text, len)));
    if (screen->scale_height != 1.0) {
	xtermFillCells(xw, draw_flags, gc, x, y, (Cardinal) len);
    }
    y += FontAscent(screen);

    {
	int length = (int) len;	/* X should have used unsigned */
	char *buffer = (char *) text;


	if (draw_flags & NOBACKGROUND) {
	    XDrawString(screen->display, VDrawable(screen), gc,
			x, y, buffer, length);
	} else {
	    XDrawImageString(screen->display, VDrawable(screen), gc,
			     x, y, buffer, length);
	}

	underline_len = (Cardinal) length;
	if ((attr_flags & BOLDATTR(screen)) && screen->enbolden) {
	    beginClipping(screen, gc, font_width, length);
	    XDrawString(screen->display, VDrawable(screen), gc,
			x + 1, y, buffer, length);
	    endClipping(screen, gc);
	}
    }

    (void) drawUnderline(xw,
			 gc,
			 attr_flags,
			 underline_len,
			 font_width,
			 x,
			 y,
			 did_ul);

    x += ((int) real_length) * FontWidth(screen);
    return x;
}


/* set up size hints for window manager; min 1 char by 1 char */
void
xtermSizeHints(XtermWidget xw, int scrollbarWidth)
{
    TScreen *screen = TScreenOf(xw);

    TRACE(("xtermSizeHints\n"));
    TRACE(("   border    %d\n", xw->core.border_width));
    TRACE(("   scrollbar %d\n", scrollbarWidth));

    xw->hints.base_width = 2 * screen->border + scrollbarWidth;
    xw->hints.base_height = 2 * screen->border;


    xw->hints.width_inc = FontWidth(screen);
    xw->hints.height_inc = FontHeight(screen);
    xw->hints.min_width = xw->hints.base_width + xw->hints.width_inc;
    xw->hints.min_height = xw->hints.base_height + xw->hints.height_inc;

    xw->hints.width = MaxCols(screen) * FontWidth(screen) + xw->hints.min_width;
    xw->hints.height = MaxRows(screen) * FontHeight(screen) + xw->hints.min_height;

    xw->hints.flags |= (PSize | PBaseSize | PMinSize | PResizeInc);

    TRACE_HINTS(&(xw->hints));
}

void
getXtermSizeHints(XtermWidget xw)
{
    TScreen *screen = TScreenOf(xw);
    long supp;

    if (!XGetWMNormalHints(screen->display, VShellWindow(xw),
			   &xw->hints, &supp))
	memset(&xw->hints, 0, sizeof(xw->hints));
    TRACE_HINTS(&(xw->hints));
}

CgsEnum
whichXtermCgs(XtermWidget xw, unsigned attr_flags, Bool hilite)
{
    TScreen *screen = TScreenOf(xw);
    CgsEnum cgsId = gcMAX;

    if (ReverseOrHilite(screen, attr_flags, hilite)) {
	if (attr_flags & BOLDATTR(screen)) {
	    cgsId = gcBoldReverse;
	} else {
	    cgsId = gcNormReverse;
	}
    } else {
	if (attr_flags & BOLDATTR(screen)) {
	    cgsId = gcBold;
	} else {
	    cgsId = gcNorm;
	}
    }
    return cgsId;
}

/*
 * Returns a GC, selected according to the font (reverse/bold/normal) that is
 * required for the current position (implied).  The GC is updated with the
 * current screen foreground and background colors.
 */
GC
updatedXtermGC(XtermWidget xw, unsigned attr_flags, unsigned fg_bg, Bool hilite)
{
    TScreen *screen = TScreenOf(xw);
    VTwin *win = WhichVWin(screen);
    CgsEnum cgsId = whichXtermCgs(xw, attr_flags, hilite);
    unsigned my_fg = extract_fg(xw, fg_bg, attr_flags);
    unsigned my_bg = extract_bg(xw, fg_bg, attr_flags);
    Pixel fg_pix = getXtermForeground(xw, attr_flags, (int) my_fg);
    Pixel bg_pix = getXtermBackground(xw, attr_flags, (int) my_bg);
    Pixel xx_pix;

    (void) fg_bg;
    (void) my_bg;
    (void) my_fg;

    /*
     * Discard video attributes overridden by colorXXXMode's.
     */
    checkVeryBoldColors(attr_flags, my_fg);

    if (ReverseOrHilite(screen, attr_flags, hilite)) {
	EXCHANGE(fg_pix, bg_pix, xx_pix);
    }


    setCgsFore(xw, win, cgsId, fg_pix);
    setCgsBack(xw, win, cgsId, bg_pix);
    return getCgsGC(xw, win, cgsId);
}

/*
 * Resets the foreground/background of the GC returned by 'updatedXtermGC()'
 * to the values that would be set in SGR_Foreground and SGR_Background. This
 * duplicates some logic, but only modifies 1/4 as many GC's.
 */
void
resetXtermGC(XtermWidget xw, unsigned attr_flags, Bool hilite)
{
    TScreen *screen = TScreenOf(xw);
    VTwin *win = WhichVWin(screen);
    CgsEnum cgsId = whichXtermCgs(xw, attr_flags, hilite);
    Pixel fg_pix = getXtermForeground(xw, attr_flags, xw->cur_foreground);
    Pixel bg_pix = getXtermBackground(xw, attr_flags, xw->cur_background);

    checkVeryBoldColors(attr_flags, xw->cur_foreground);

    if (ReverseOrHilite(screen, attr_flags, hilite)) {
	setCgsFore(xw, win, cgsId, bg_pix);
	setCgsBack(xw, win, cgsId, fg_pix);
    } else {
	setCgsFore(xw, win, cgsId, fg_pix);
	setCgsBack(xw, win, cgsId, bg_pix);
    }
}

Pixel
getXtermBackground(XtermWidget xw, unsigned attr_flags, int color)
{
    Pixel result = T_COLOR(TScreenOf(xw), TEXT_BG);
    return result;
}

Pixel
getXtermForeground(XtermWidget xw, unsigned attr_flags, int color)
{
    Pixel result = T_COLOR(TScreenOf(xw), TEXT_FG);
    return result;
}

/*
 * Returns a single base character for the given cell.
 */
unsigned
getXtermCell(TScreen *screen, int row, int col)
{
    CLineData *ld = getLineData(screen, row);

    return ((ld && (col < (int) ld->lineSize))
	    ? ld->charData[col]
	    : (unsigned) ' ');
}

/*
 * Sets a single base character for the given cell.
 */
void
putXtermCell(TScreen *screen, int row, int col, int ch)
{
    LineData *ld = getLineData(screen, row);

    if (ld && (col < (int) ld->lineSize)) {
	ld->charData[col] = (CharData) ch;
	if_OPT_WIDE_CHARS(screen, {
	    size_t off;
	    for_each_combData(off, ld) {
		ld->combData[off][col] = 0;
	    }
	});
    }
}


void
update_keyboard_type(void)
{
    update_delete_del();
    update_tcap_fkeys();
    update_old_fkeys();
    update_hp_fkeys();
    update_sco_fkeys();
    update_sun_fkeys();
    update_sun_kbd();
}

void
set_keyboard_type(XtermWidget xw, xtermKeyboardType type, Bool set)
{
    xtermKeyboardType save = xw->keyboard.type;

    TRACE(("set_keyboard_type(%s, %s) currently %s\n",
	   visibleKeyboardType(type),
	   BtoS(set),
	   visibleKeyboardType(xw->keyboard.type)));
    if (set) {
	xw->keyboard.type = type;
    } else {
	xw->keyboard.type = keyboardIsDefault;
    }

    if (save != xw->keyboard.type) {
	update_keyboard_type();
    }
}

void
toggle_keyboard_type(XtermWidget xw, xtermKeyboardType type)
{
    xtermKeyboardType save = xw->keyboard.type;

    TRACE(("toggle_keyboard_type(%s) currently %s\n",
	   visibleKeyboardType(type),
	   visibleKeyboardType(xw->keyboard.type)));
    if (xw->keyboard.type == type) {
	xw->keyboard.type = keyboardIsDefault;
    } else {
	xw->keyboard.type = type;
    }

    if (save != xw->keyboard.type) {
	update_keyboard_type();
    }
}

void
init_keyboard_type(XtermWidget xw, xtermKeyboardType type, Bool set)
{
    static Bool wasSet = False;

    TRACE(("init_keyboard_type(%s, %s) currently %s\n",
	   visibleKeyboardType(type),
	   BtoS(set),
	   visibleKeyboardType(xw->keyboard.type)));
    if (set) {
	if (wasSet) {
	    xtermWarning("Conflicting keyboard type option (%u/%u)\n",
			 xw->keyboard.type, type);
	}
	xw->keyboard.type = type;
	wasSet = True;
	update_keyboard_type();
    }
}

/*
 * If the keyboardType resource is set, use that, overriding the individual
 * boolean resources for different keyboard types.
 */
void
decode_keyboard_type(XtermWidget xw, XTERM_RESOURCE * rp)
{
#define DATA(n, t, f) { n, t, XtOffsetOf(XTERM_RESOURCE, f) }
#define FLAG(n) *(Boolean *)(((char *)rp) + table[n].offset)
    static struct {
	const char *name;
	xtermKeyboardType type;
	unsigned offset;
    } table[] = {
    };
    Cardinal n;

    TRACE(("decode_keyboard_type(%s)\n", rp->keyboardType));
    if (!x_strcasecmp(rp->keyboardType, "unknown")) {
	/*
	 * Let the individual resources comprise the keyboard-type.
	 */
	for (n = 0; n < XtNumber(table); ++n)
	    init_keyboard_type(xw, table[n].type, FLAG(n));
    } else if (!x_strcasecmp(rp->keyboardType, "default")) {
	/*
	 * Set the keyboard-type to the Sun/PC type, allowing modified
	 * function keys, etc.
	 */
	for (n = 0; n < XtNumber(table); ++n)
	    init_keyboard_type(xw, table[n].type, False);
    } else {
	Bool found = False;

	/*
	 * Choose an individual keyboard type.
	 */
	for (n = 0; n < XtNumber(table); ++n) {
	    if (!x_strcasecmp(rp->keyboardType, table[n].name + 1)) {
		FLAG(n) = True;
		found = True;
	    } else {
		FLAG(n) = False;
	    }
	    init_keyboard_type(xw, table[n].type, FLAG(n));
	}
	if (!found) {
	    xtermWarning("KeyboardType resource \"%s\" not found\n",
			 rp->keyboardType);
	}
    }
#undef DATA
#undef FLAG
}


/*
 * Extend a (normally) boolean resource value by checking for additional values
 * which will be mapped into true/false.
 */
int
extendedBoolean(const char *value, const FlagList * table, Cardinal limit)
{
    int result = -1;
    long check;
    char *next;
    Cardinal n;

    if ((x_strcasecmp(value, "true") == 0)
	|| (x_strcasecmp(value, "yes") == 0)
	|| (x_strcasecmp(value, "on") == 0)) {
	result = True;
    } else if ((x_strcasecmp(value, "false") == 0)
	       || (x_strcasecmp(value, "no") == 0)
	       || (x_strcasecmp(value, "off") == 0)) {
	result = False;
    } else if ((check = strtol(value, &next, 0)) >= 0 && *next == '\0') {
	if (check >= (long) limit)
	    check = True;
	result = (int) check;
    } else {
	for (n = 0; n < limit; ++n) {
	    if (x_strcasecmp(value, table[n].name) == 0) {
		result = table[n].code;
		break;
	    }
	}
    }

    if (result < 0) {
	xtermWarning("Unrecognized keyword: %s\n", value);
	result = False;
    }

    TRACE(("extendedBoolean(%s) = %d\n", value, result));
    return result;
}

/*
 * Something like round() from math library, but round() is less widely-used
 * than xterm.  Also, there are no negative numbers to complicate this.
 */
int
dimRound(double value)
{
    int result = (int) value;
    if (result < value)
	++result;
    return result;
}

/*
 * Find the geometry of the specified Xinerama screen
 */
static void
find_xinerama_screen(Display *display, int screen, struct Xinerama_geometry *ret)
{
    (void) display;
    (void) ret;
    if (screen > 0)
	xtermWarning("Xinerama support not enabled\n");
}

/*
 * Parse the screen code after the @ in a geometry string.
 */
static void
parse_xinerama_screen(Display *display, const char *str, struct Xinerama_geometry *ret)
{
    int screen = -1;
    char *end;

    if (*str == 'g') {
	screen = -1;
	str++;
    } else if (*str == 'c') {
	screen = -2;
	str++;
    } else {
	long s = strtol(str, &end, 0);
	if (end > str && (int) s >= 0) {
	    screen = (int) s;
	    str = end;
	}
    }
    if (*str) {
	xtermWarning("invalid Xinerama specification '%s'\n", str);
	return;
    }
    if (screen == -1)		/* already done */
	return;
    find_xinerama_screen(display, screen, ret);
}

/*
 * Parse a geometry string with extra Xinerama specification:
 * <w>x<h>+<x>+<y>@<screen>.
 */
int
XParseXineramaGeometry(Display *display, char *parsestring, struct Xinerama_geometry *ret)
{
    char *at, buf[128];

    ret->scr_x = 0;
    ret->scr_y = 0;
    ret->scr_w = DisplayWidth(display, DefaultScreen(display));
    ret->scr_h = DisplayHeight(display, DefaultScreen(display));
    at = strchr(parsestring, '@');
    if (at != NULL && (size_t) (at - parsestring) < sizeof(buf) - 1) {
	memcpy(buf, parsestring, (size_t) (at - parsestring));
	buf[at - parsestring] = 0;
	parsestring = buf;
	parse_xinerama_screen(display, at + 1, ret);
    }
    return XParseGeometry(parsestring, &ret->x, &ret->y, &ret->w, &ret->h);
}
