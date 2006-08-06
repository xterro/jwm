/***************************************************************************
 ***************************************************************************/

#include "jwm.h"
#include "taskbar.h"
#include "tray.h"
#include "timing.h"
#include "main.h"
#include "client.h"
#include "color.h"
#include "popup.h"
#include "button.h"
#include "cursor.h"
#include "icon.h"
#include "error.h"
#include "font.h"
#include "winmenu.h"
#include "screen.h"

typedef enum {
	INSERT_LEFT,
	INSERT_RIGHT
} InsertModeType;

typedef struct TaskBarType {

	TrayComponentType *cp;

	int itemHeight;
	LayoutType layout;

	Pixmap buffer;
	GC bufferGC;

	TimeType mouseTime;
	int mousex, mousey;

	unsigned int maxItemWidth;

	struct TaskBarType *next;

} TaskBarType;

typedef struct Node {
	ClientNode *client;
	int y;
	struct Node *next;
	struct Node *prev;
} Node;

static char minimized_bitmap[] = {
	0x01, 0x03,
	0x07, 0x0F
};

static const int TASK_SPACER = 2;

static Pixmap minimizedPixmap;
static InsertModeType insertMode;

static TaskBarType *bars;
static Node *taskBarNodes;
static Node *taskBarNodesTail;

static Node *GetNode(TaskBarType *bar, int x);
static unsigned int GetItemCount();
static int ShouldShowItem(const ClientNode *np);
static int ShouldFocusItem(const ClientNode *np);
static unsigned int GetItemWidth(const TaskBarType *bp,
	unsigned int itemCount);
static void Render(const TaskBarType *bp);
static void ShowTaskWindowMenu(TaskBarType *bar, Node *np);

static void SetSize(TrayComponentType *cp, int width, int height);
static void Create(TrayComponentType *cp);
static void Destroy(TrayComponentType *cp);
static void Resize(TrayComponentType *cp);
static void ProcessTaskButtonEvent(TrayComponentType *cp,
	int x, int y, int mask);
static void ProcessTaskMotionEvent(TrayComponentType *cp,
	int x, int y, int mask);

/***************************************************************************
 ***************************************************************************/
void InitializeTaskBar() {
	bars = NULL;
	taskBarNodes = NULL;
	taskBarNodesTail = NULL;
	insertMode = INSERT_RIGHT;
}

/***************************************************************************
 ***************************************************************************/
void StartupTaskBar() {
	minimizedPixmap = JXCreatePixmapFromBitmapData(display, rootWindow,
		minimized_bitmap, 4, 4, colors[COLOR_TASK_FG],
		colors[COLOR_TASK_BG], rootDepth);
}

/***************************************************************************
 ***************************************************************************/
void ShutdownTaskBar() {

	TaskBarType *bp;

	for(bp = bars; bp; bp = bp->next) {
		JXFreeGC(display, bp->bufferGC);
		JXFreePixmap(display, bp->buffer);
	}

	JXFreePixmap(display, minimizedPixmap);
}

/***************************************************************************
 ***************************************************************************/
void DestroyTaskBar() {

	TaskBarType *bp;

	while(bars) {
		bp = bars->next;
		Release(bars);
		bars = bp;
	}

}

/***************************************************************************
 ***************************************************************************/
TrayComponentType *CreateTaskBar() {

	TrayComponentType *cp;
	TaskBarType *tp;

	tp = Allocate(sizeof(TaskBarType));
	tp->next = bars;
	bars = tp;
	tp->itemHeight = 0;
	tp->layout = LAYOUT_HORIZONTAL;
	tp->mousex = -1;
	tp->mousey = -1;
	tp->mouseTime.seconds = 0;
	tp->mouseTime.ms = 0;
	tp->maxItemWidth = 0;

	cp = CreateTrayComponent();
	cp->object = tp;
	tp->cp = cp;

	cp->SetSize = SetSize;
	cp->Create = Create;
	cp->Destroy = Destroy;
	cp->Resize = Resize;
	cp->ProcessButtonEvent = ProcessTaskButtonEvent;
	cp->ProcessMotionEvent = ProcessTaskMotionEvent;

	return cp;

}

/***************************************************************************
 ***************************************************************************/
void SetSize(TrayComponentType *cp, int width, int height) {

	TaskBarType *tp;

	Assert(cp);

	tp = (TaskBarType*)cp->object;

	Assert(tp);

	if(width == 0) {
		tp->layout = LAYOUT_HORIZONTAL;
	} else if(height == 0) {
		tp->layout = LAYOUT_VERTICAL;
	} else if(width > height) {
		tp->layout = LAYOUT_HORIZONTAL;
	} else {
		tp->layout = LAYOUT_VERTICAL;
	}

}

/***************************************************************************
 ***************************************************************************/
void Create(TrayComponentType *cp) {

	XGCValues gcValues;
	unsigned long gcMask;
	TaskBarType *tp;

	Assert(cp);

	tp = (TaskBarType*)cp->object;

	Assert(tp);

	if(tp->layout == LAYOUT_HORIZONTAL) {
		tp->itemHeight = cp->height - TASK_SPACER;
	} else {
		tp->itemHeight = GetStringHeight(FONT_TASK) + 12;
	}

	Assert(cp->width > 0);
	Assert(cp->height > 0);

	cp->pixmap = JXCreatePixmap(display, rootWindow, cp->width, cp->height,
		rootDepth);
	tp->buffer = cp->pixmap;

	gcMask = GCGraphicsExposures | GCForeground;
	gcValues.graphics_exposures = False;
	gcValues.foreground = colors[COLOR_TASK_BG];
	tp->bufferGC = JXCreateGC(display, cp->pixmap, gcMask, &gcValues);

	JXFillRectangle(display, cp->pixmap, tp->bufferGC,
		0, 0, cp->width, cp->height);

}

/***************************************************************************
 ***************************************************************************/
void Resize(TrayComponentType *cp) {

	XGCValues gcValues;
	unsigned long gcMask;
	TaskBarType *tp;

	Assert(cp);

	tp = (TaskBarType*)cp->object;

	Assert(tp);

	if(tp->bufferGC != None) {
		JXFreeGC(display, tp->bufferGC);
	}
	if(tp->buffer != None) {
		JXFreePixmap(display, tp->buffer);
	}

	if(tp->layout == LAYOUT_HORIZONTAL) {
		tp->itemHeight = cp->height - TASK_SPACER;
	} else {
		tp->itemHeight = GetStringHeight(FONT_TASK) + 12;
	}

	Assert(cp->width > 0);
	Assert(cp->height > 0);

	cp->pixmap = JXCreatePixmap(display, rootWindow, cp->width, cp->height,
		rootDepth);
	tp->buffer = cp->pixmap;

	gcMask = GCGraphicsExposures | GCForeground;
	gcValues.graphics_exposures = False;
	gcValues.foreground = colors[COLOR_TASK_BG];
	tp->bufferGC = JXCreateGC(display, cp->pixmap, gcMask, &gcValues);

	JXFillRectangle(display, cp->pixmap, tp->bufferGC,
		0, 0, cp->width, cp->height);
}

/***************************************************************************
 ***************************************************************************/
void Destroy(TrayComponentType *cp) {

}

/***************************************************************************
 ***************************************************************************/
void ProcessTaskButtonEvent(TrayComponentType *cp, int x, int y, int mask) {

	TaskBarType *bar = (TaskBarType*)cp->object;
	Node *np;

	Assert(bar);

	if(bar->layout == LAYOUT_HORIZONTAL) {
		np = GetNode(bar, x);
	} else {
		np = GetNode(bar, y);
	}

	if(np) {
		switch(mask) {
		case Button1:
			if(np->client->state.status & STAT_ACTIVE
				&& np->client == nodes[np->client->state.layer]) {
				MinimizeClient(np->client);
			} else {
				RestoreClient(np->client, 1);
				FocusClient(np->client);
			}
			break;
		case Button3:
			ShowTaskWindowMenu(bar, np);
			break;
		case Button4:
			FocusPrevious();
			break;
		case Button5:
			FocusNext();
			break;
		default:
			break;
		}
	}

}

/***************************************************************************
 ***************************************************************************/
void ProcessTaskMotionEvent(TrayComponentType *cp, int x, int y, int mask) {

	TaskBarType *bp = (TaskBarType*)cp->object;

	bp->mousex = cp->screenx + x;
	bp->mousey = cp->screeny + y;
	GetCurrentTime(&bp->mouseTime);

}

/***************************************************************************
 ***************************************************************************/
void ShowTaskWindowMenu(TaskBarType *bar, Node *np) {

	int x, y;
	int mwidth, mheight;
	const ScreenType *sp;

	GetWindowMenuSize(np->client, &mwidth, &mheight);

	sp = GetCurrentScreen(x, y);

	if(bar->layout == LAYOUT_HORIZONTAL) {
		GetMousePosition(&x, &y);
		if(bar->cp->screeny + bar->cp->height / 2 < sp->y + sp->height / 2) {
			y = bar->cp->screeny + bar->cp->height;
		} else {
			y = bar->cp->screeny - mheight;
		}
		x -= mwidth / 2;
	} else {
		if(bar->cp->screenx + bar->cp->width / 2 < sp->x + sp->width / 2) {
			x = bar->cp->screenx + bar->cp->width;
		} else {
			x = bar->cp->screenx - mwidth;
		}
		y = bar->cp->screeny + np->y;
	}

	ShowWindowMenu(np->client, x, y);

}

/***************************************************************************
 ***************************************************************************/
void AddClientToTaskBar(ClientNode *np) {

	Node *tp;

	Assert(np);

	tp = Allocate(sizeof(Node));
	tp->client = np;

	if(insertMode == INSERT_RIGHT) {
		tp->next = NULL;
		tp->prev = taskBarNodesTail;
		if(taskBarNodesTail) {
			taskBarNodesTail->next = tp;
		} else {
			taskBarNodes = tp;
		}
		taskBarNodesTail = tp;
	} else {
		tp->prev = NULL;
		tp->next = taskBarNodes;
		if(taskBarNodes) {
			taskBarNodes->prev = tp;
		}
		taskBarNodes = tp;
		if(!taskBarNodesTail) {
			taskBarNodesTail = tp;
		}
	}

	UpdateTaskBar();

	UpdateNetClientList();

}

/***************************************************************************
 ***************************************************************************/
void RemoveClientFromTaskBar(ClientNode *np) {

	Node *tp;

	Assert(np);

	for(tp = taskBarNodes; tp; tp = tp->next) {
		if(tp->client == np) {
			if(tp->prev) {
				tp->prev->next = tp->next;
			} else {
				taskBarNodes = tp->next;
			}
			if(tp->next) {
				tp->next->prev = tp->prev;
			} else {
				taskBarNodesTail = tp->prev;
			}
			Release(tp);
			break;
		}
	}

	UpdateTaskBar();

	UpdateNetClientList();

}

/***************************************************************************
 ***************************************************************************/
void UpdateTaskBar() {

	TaskBarType *bp;
	unsigned int count;
	int lastHeight;

	if(shouldExit) {
		return;
	}

	for(bp = bars; bp; bp = bp->next) {

		if(bp->layout == LAYOUT_VERTICAL) {
			lastHeight = bp->cp->requestedHeight;
			count = GetItemCount();
			bp->cp->requestedHeight = GetStringHeight(FONT_TASK) + 12;
			bp->cp->requestedHeight *= count;
			bp->cp->requestedHeight += 2;
			if(lastHeight != bp->cp->requestedHeight) {
				ResizeTray(bp->cp->tray);
			}
		}

		Render(bp);
	}

}

/***************************************************************************
 ***************************************************************************/
void SignalTaskbar(const TimeType *now, int x, int y) {

	TaskBarType *bp;
	Node *np;

	for(bp = bars; bp; bp = bp->next) {
		if(abs(bp->mousex - x) < POPUP_DELTA
			&& abs(bp->mousey - y) < POPUP_DELTA) {
			if(GetTimeDifference(now, &bp->mouseTime) >= popupDelay) {
				if(bp->layout == LAYOUT_HORIZONTAL) {
					np = GetNode(bp, x - bp->cp->screenx);
				} else {
					np = GetNode(bp, y - bp->cp->screeny);
				}
				if(np) {
					ShowPopup(x, y, np->client->name);
				}
			}
		}
	}

}

/***************************************************************************
 ***************************************************************************/
void Render(const TaskBarType *bp) {

	Node *tp;
	ButtonNode button;
	int x, y;
	int remainder;
	int itemWidth, itemCount;
	int width, height;
	int iconSize;
	Pixmap buffer;
	GC gc;
	char *minimizedName;

	if(shouldExit) {
		return;
	}

	Assert(bp);
	Assert(bp->cp);

	width = bp->cp->width;
	height = bp->cp->height;
	buffer = bp->cp->pixmap;
	gc = bp->bufferGC;

	Assert(buffer != None);
	Assert(gc != None);

	x = TASK_SPACER;
	width -= x;
	y = 1;

	JXSetForeground(display, gc, colors[COLOR_TASK_BG]);
	JXFillRectangle(display, buffer, gc, 0, 0, width, height);

	itemCount = GetItemCount();
	if(!itemCount) {
		UpdateSpecificTray(bp->cp->tray, bp->cp);
		return;
	}
	if(bp->layout == LAYOUT_HORIZONTAL) {
		itemWidth = GetItemWidth(bp, itemCount);
		remainder = width - itemWidth * itemCount;
	} else {
		itemWidth = width;
		remainder = 0;
	}

	iconSize = bp->itemHeight - 2 * TASK_SPACER - 4;

	ResetButton(&button, buffer, gc);
	button.font = FONT_TASK;

	for(tp = taskBarNodes; tp; tp = tp->next) {
		if(ShouldShowItem(tp->client)) {

			tp->y = y;

			if(tp->client->state.status & STAT_ACTIVE) {
				button.type = BUTTON_TASK_ACTIVE;
			} else {
				button.type = BUTTON_TASK;
			}

			if(remainder) {
				button.width = itemWidth - TASK_SPACER;
			} else {
				button.width = itemWidth - TASK_SPACER - 1;
			}
			button.height = bp->itemHeight - 1;
			button.x = x;
			button.y = y;
			button.icon = tp->client->icon;

			if(tp->client->state.status & STAT_MINIMIZED) {
				minimizedName = AllocateStack(strlen(tp->client->name) + 3);
				sprintf(minimizedName, "[%s]", tp->client->name);
				button.text = minimizedName;
				DrawButton(&button);
				ReleaseStack(minimizedName);
			} else {
				button.text = tp->client->name;
				DrawButton(&button);
			}

			if(tp->client->state.status & STAT_MINIMIZED) {
				JXCopyArea(display, minimizedPixmap, buffer, gc,
					0, 0, 4, 4, x + 3, y + bp->itemHeight - 7);
			}

			if(bp->layout == LAYOUT_HORIZONTAL) {
				x += itemWidth;
				if(remainder) {
					++x;
					--remainder;
				}
			} else {
				y += bp->itemHeight;
				if(remainder) {
					++y;
					--remainder;
				}
			}

		}
	}

	UpdateSpecificTray(bp->cp->tray, bp->cp);

}

/***************************************************************************
 ***************************************************************************/
void FocusNext() {

	Node *tp;

	for(tp = taskBarNodes; tp; tp = tp->next) {
		if(ShouldFocusItem(tp->client)) {
			if(tp->client->state.status & STAT_ACTIVE) {
				tp = tp->next;
				break;
			}
		}
	}

	if(!tp) {
		tp = taskBarNodes;
	}

	while(tp && !ShouldFocusItem(tp->client)) {
		tp = tp->next;
	}

	if(!tp) {
		tp = taskBarNodes;
		while(tp && !ShouldFocusItem(tp->client)) {
			tp = tp->next;
		}
	}

	if(tp) {
		RestoreClient(tp->client, 1);
		FocusClient(tp->client);
	}

}

/***************************************************************************
 ***************************************************************************/
void FocusPrevious() {

	Node *tp;

	for(tp = taskBarNodesTail; tp; tp = tp->prev) {
		if(ShouldFocusItem(tp->client)) {
			if(tp->client->state.status & STAT_ACTIVE) {
				tp = tp->prev;
				break;
			}
		}
	}

	if(!tp) {
		tp = taskBarNodesTail;
	}

	while(tp && !ShouldFocusItem(tp->client)) {
		tp = tp->prev;
	}

	if(!tp) {
		tp = taskBarNodesTail;
		while(tp && !ShouldFocusItem(tp->client)) {
			tp = tp->prev;
		}
	}

	if(tp) {
		RestoreClient(tp->client, 1);
		FocusClient(tp->client);
	}

}

/***************************************************************************
 ***************************************************************************/
void FocusNextStackedCircular() {

	ClientNode *ac;
	ClientNode *np;
	int x;

	ac = GetActiveClient();
	np = NULL;

	/* Check for a valid client below this client in the same layer. */
	if(ac) {
		for(np = ac->next; np; np = np->next) {
			if(ShouldFocusItem(np)) {
				break;
			}
		}
	}

	/* Check for a valid client in lower layers. */
	if(ac && !np) {
		for(x = ac->state.layer - 1; x >= LAYER_BOTTOM; x--) {
			for(np = nodes[x]; np; np = np->next) {
				if(ShouldFocusItem(np)) {
					break;
				}
			}
			if(np) {
				break;
			}
		}
	}

	/* Revert to the top-most valid client. */
	if(!np) {
		for(x = LAYER_TOP; x >= LAYER_BOTTOM; x--) {
			for(np = nodes[x]; np; np = np->next) {
				if(ShouldFocusItem(np)) {
					break;
				}
			}
			if(np) {
				break;
			}
		}
	}

	if(np) {
		FocusClient(np);
	}

}

/***************************************************************************
 ***************************************************************************/
Node *GetNode(TaskBarType *bar, int x) {

	Node *tp;
	int remainder;
	int itemCount;
	int itemWidth;
	int index, stop;
	int width;

	index = TASK_SPACER;

	itemCount = GetItemCount();

	if(bar->layout == LAYOUT_HORIZONTAL) {

		width = bar->cp->width - index; 
		itemWidth = GetItemWidth(bar, itemCount);
		remainder = width - itemWidth * itemCount;

		for(tp = taskBarNodes; tp; tp = tp->next) {
			if(ShouldShowItem(tp->client)) {
				if(remainder) {
					stop = index + itemWidth + 1;
					--remainder;
				} else {
					stop = index + itemWidth;
				}
				if(x >= index && x < stop) {
					return tp;
				}
				index = stop;
			}
		}

	} else {

		for(tp = taskBarNodes; tp; tp = tp->next) {
			if(ShouldShowItem(tp->client)) {
				stop = index + bar->itemHeight;
				if(x >= index && x < stop) {
					return tp;
				}
				index = stop;
			}
		}

	}

	return NULL;

}

/***************************************************************************
 ***************************************************************************/
unsigned int GetItemCount() {

	Node *tp;
	unsigned int count;

	count = 0;
	for(tp = taskBarNodes; tp; tp = tp->next) {
		if(ShouldShowItem(tp->client)) {
			++count;
		}
	}

	return count;

}

/***************************************************************************
 ***************************************************************************/
int ShouldShowItem(const ClientNode *np) {

	if(np->state.desktop != currentDesktop
		&& !(np->state.status & STAT_STICKY)) {
		return 0;
	}

	if(np->state.status & STAT_NOLIST) {
		return 0;
	}

	if(np->owner != None) {
		return 0;
	}

	if(!(np->state.status & STAT_MAPPED)
		&& !(np->state.status & (STAT_MINIMIZED | STAT_SHADED))) {
		return 0;
	}

	return 1;

}

/***************************************************************************
 ***************************************************************************/
int ShouldFocusItem(const ClientNode *np) {

	if(np->state.desktop != currentDesktop
		&& !(np->state.status & STAT_STICKY)) {
		return 0;
	}

	if(np->state.status & STAT_NOLIST) {
		return 0;
	}

	if(!(np->state.status & STAT_MAPPED)) {
		return 0;
	}

	if(np->owner != None) {
		return 0;
	}

	return 1;

}

/***************************************************************************
 ***************************************************************************/
unsigned int GetItemWidth(const TaskBarType *bp, unsigned int itemCount) {

	unsigned int itemWidth;

	itemWidth = bp->cp->width - TASK_SPACER;

	if(!itemCount) {
		return itemWidth;
	}

	itemWidth /= itemCount;
	if(!itemWidth) {
		itemWidth = 1;
	}

	if(bp->maxItemWidth > 0 && itemWidth > bp->maxItemWidth) {
		itemWidth = bp->maxItemWidth;
	}

	return itemWidth;

}

/***************************************************************************
 ***************************************************************************/
void SetMaxTaskBarItemWidth(TrayComponentType *cp, const char *value) {

	int temp;
	TaskBarType *bp;

	Assert(cp);

	if(value) {
		temp = atoi(value);
		if(temp < 0) {
			Warning("invalid maxwidth for TaskList: %s", value);
			return;
		}
		bp = (TaskBarType*)cp->object;
		bp->maxItemWidth = temp;
	}

}

/***************************************************************************
 ***************************************************************************/
void SetTaskBarInsertMode(const char *mode) {

	if(!mode) {
		insertMode = INSERT_RIGHT;
		return;
	}

	if(!strcmp(mode, "right")) {
		insertMode = INSERT_RIGHT;
	} else if(!strcmp(mode, "left")) {
		insertMode = INSERT_LEFT;
	} else {
		Warning("invalid insert mode: \"%s\"", mode);
		insertMode = INSERT_RIGHT;
	}

}

/***************************************************************************
 * Maintain the _NET_CLIENT_LIST[_STACKING] properties on the root window.
 ***************************************************************************/
void UpdateNetClientList() {

	Node *np;
	ClientNode *client;
	Window *windows;
	int count;
	int layer;

	count = 0;
	for(np = taskBarNodes; np; np = np->next) {
		++count;
	}

	if(count == 0) {
		windows = NULL;
	} else {
		windows = AllocateStack(count * sizeof(Window));
	}

	/* Set _NET_CLIENT_LIST */
	count = 0;
	for(np = taskBarNodes; np; np = np->next) {
		windows[count++] = np->client->window;
	}
	JXChangeProperty(display, rootWindow, atoms[ATOM_NET_CLIENT_LIST],
		XA_WINDOW, 32, PropModeReplace, (unsigned char*)windows, count);

	/* Set _NET_CLIENT_LIST_STACKING */
	count = 0;
	for(layer = LAYER_BOTTOM; layer <= LAYER_TOP; layer++) {
		for(client = nodes[layer]; client; client = client->next) {
			windows[count++] = client->window;
		}
	}
	JXChangeProperty(display, rootWindow, atoms[ATOM_NET_CLIENT_LIST_STACKING],
		XA_WINDOW, 32, PropModeReplace, (unsigned char*)windows, count);

	if(windows != NULL) {
		ReleaseStack(windows);
	}
	
}

