// mwc_tiling.c -- minimal master-stack tiling WM
// Build: gcc -Wall -O2 -o mwc_tiling mwc_tiling.c -lX11

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>

Display *dpy;
int screen;
Window root;

typedef struct Client {
    Window win;
    int x, y, w, h;
    struct Client *next;
} Client;

Client *clients = NULL;   /* head is the master */
Window focused = 0;

/* master area factor (0.1 .. 0.9) */
double master_factor = 0.6;

/* --- helpers --- */
void die(const char *s) { fprintf(stderr, "%s\n", s); exit(1); }

int count_clients() {
    int n = 0;
    for (Client *c = clients; c; c = c->next) n++;
    return n;
}

Client *find_client(Window w) {
    for (Client *c = clients; c; c = c->next)
        if (c->win == w) return c;
    return NULL;
}

/* add client at head (becomes master) */
Client *add_client(Window w) {
    Client *c = calloc(1, sizeof(Client));
    if (!c) die("malloc failed");
    c->win = w;
    c->x = c->y = 0;
    c->w = 200; c->h = 200;
    c->next = clients;
    clients = c;
    return c;
}

void remove_client(Window w) {
    Client **pc = &clients;
    while (*pc) {
        if ((*pc)->win == w) {
            Client *tmp = *pc;
            *pc = (*pc)->next;
            free(tmp);
            return;
        }
        pc = &(*pc)->next;
    }
}

void focus_window(Window w) {
    if (focused == w) return;
    if (focused) XSetWindowBorder(dpy, focused, 0x000000); /* black */
    focused = w;
    if (focused) {
        XSetWindowBorder(dpy, focused, 0xff0000); /* red */
        XSetInputFocus(dpy, focused, RevertToPointerRoot, CurrentTime);
    } else {
        XSetInputFocus(dpy, PointerRoot, RevertToPointerRoot, CurrentTime);
    }
}

void configure_window(Client *c) {
    if (!c) return;
    XMoveResizeWindow(dpy, c->win, c->x, c->y, c->w, c->h);
    XSetWindowBorderWidth(dpy, c->win, 2);
    XMapWindow(dpy, c->win);
}

/* master-stack tiling:
   - head of linked list is master
   - others are stacked vertically in stack column */
void tile_windows() {
    int n = count_clients();
    if (n == 0) return;

    int sw = DisplayWidth(dpy, screen);
    int sh = DisplayHeight(dpy, screen);

    if (n == 1) {
        Client *c = clients;
        c->x = 0; c->y = 0; c->w = sw; c->h = sh;
        configure_window(c);
        return;
    }

    int master_w = (int)(sw * master_factor);
    if (master_w < 1) master_w = 1;
    int stack_w = sw - master_w;
    int stack_count = n - 1;
    int win_h = sh / stack_count;

    /* master */
    Client *c = clients;
    if (c) {
        c->x = 0; c->y = 0; c->w = master_w; c->h = sh;
        configure_window(c);
        c = c->next;
    }

    /* stack */
    int i = 0;
    while (c) {
        c->x = master_w;
        c->y = i * win_h;
        /* last stack window takes remaining height to avoid gap due to integer division */
        c->w = stack_w;
        c->h = (i == stack_count - 1) ? (sh - i * win_h) : win_h;
        configure_window(c);
        c = c->next;
        i++;
    }
}

/* move client c to head (make master) */
void promote_to_master(Client *c) {
    if (!c || clients == c) return;
    Client **pc = &clients;
    while (*pc && *pc != c) pc = &(*pc)->next;
    if (!*pc) return;
    *pc = c->next;
    c->next = clients;
    clients = c;
}

/* rotate clients: head -> tail (used for Alt+Tab cycling) */
void rotate_clients() {
    if (!clients || !clients->next) return;
    Client *first = clients;
    Client *last = clients;
    while (last->next) last = last->next;
    clients = first->next;
    first->next = NULL;
    last->next = first;
}

/* find client by Window and return pointer to it */
Client *client_by_window(Window w) {
    return find_client(w);
}

/* swap focused client with master (make focused the head) */
void swap_focused_with_master() {
    Client *c = client_by_window(focused);
    if (!c || clients == c) return;
    promote_to_master(c);
    tile_windows();
    focus_window(c->win);
}

/* adjust master_factor by delta (positive or negative) */
void adjust_master_factor(double delta) {
    master_factor += delta;
    if (master_factor < 0.1) master_factor = 0.1;
    if (master_factor > 0.9) master_factor = 0.9;
    tile_windows();
}

/* --- event handlers --- */
void handle_map_request(XEvent *e) {
    XMapRequestEvent *ev = &e->xmaprequest;
    Client *c = find_client(ev->window);
    if (!c) c = add_client(ev->window);

    XSelectInput(dpy, ev->window, StructureNotifyMask | FocusChangeMask | PropertyChangeMask);
    XMapWindow(dpy, ev->window);
    tile_windows();
    focus_window(ev->window);
}

void handle_unmap_notify(XEvent *e) {
    XUnmapEvent *ev = &e->xunmap;
    remove_client(ev->window);
    tile_windows();
    if (focused == ev->window) focus_window(clients ? clients->win : (Window)0);
}

void handle_destroy_notify(XEvent *e) {
    XDestroyWindowEvent *ev = &e->xdestroywindow;
    remove_client(ev->window);
    tile_windows();
    if (focused == ev->window) focus_window(clients ? clients->win : (Window)0);
}

void handle_focus_in(XEvent *e) {
    XFocusChangeEvent *ev = &e->xfocus;
    if (ev->window != root)
        focus_window(ev->window);
}

void handle_key_press(XEvent *e) {
    XKeyEvent *ev = &e->xkey;
    KeySym ks = XLookupKeysym(ev, 0);

    /* Alt+Tab -> rotate clients and focus new head */
    if ((ev->state & Mod1Mask) && ks == XK_Tab) {
        rotate_clients();
        tile_windows();
        if (clients) focus_window(clients->win);
        return;
    }

    /* Alt+Return -> swap focused with master (promote focused) */
    if ((ev->state & Mod1Mask) && ks == XK_Return) {
        swap_focused_with_master();
        return;
    }

    /* Alt+h / Alt+l -> shrink/enlarge master area */
    if ((ev->state & Mod1Mask) && ks == XK_h) {
        adjust_master_factor(-0.05);
        return;
    }
    if ((ev->state & Mod1Mask) && ks == XK_l) {
        adjust_master_factor(0.05);
        return;
    }

    /* Alt+Shift+q -> kill focused client */
    if ((ev->state & (Mod1Mask | ShiftMask)) == (Mod1Mask | ShiftMask) && ks == XK_q) {
        if (focused) XKillClient(dpy, focused);
        return;
    }

    /* Alt+Return when alone (no focused) example: spawn xterm if Alt+Shift+Return */
    if ((ev->state & (Mod1Mask | ShiftMask)) == (Mod1Mask | ShiftMask) && ks == XK_Return) {
        if (fork() == 0) {
            if (dpy) close(ConnectionNumber(dpy));
            setsid();
            execlp("xterm", "xterm", NULL);
            _exit(0);
        }
    }
}

/* --- setup / run / cleanup --- */
void setup() {
    dpy = XOpenDisplay(NULL);
    if (!dpy) die("Failed to open display");
    screen = DefaultScreen(dpy);
    root = RootWindow(dpy, screen);

    /* become WM */
    XSelectInput(dpy, root, SubstructureRedirectMask | SubstructureNotifyMask | KeyPressMask);

    /* grab keys we use */
    XGrabKey(dpy, XKeysymToKeycode(dpy, XK_Tab), Mod1Mask, root, True, GrabModeAsync, GrabModeAsync);
    XGrabKey(dpy, XKeysymToKeycode(dpy, XK_Return), Mod1Mask, root, True, GrabModeAsync, GrabModeAsync);
    XGrabKey(dpy, XKeysymToKeycode(dpy, XK_h), Mod1Mask, root, True, GrabModeAsync, GrabModeAsync);
    XGrabKey(dpy, XKeysymToKeycode(dpy, XK_l), Mod1Mask, root, True, GrabModeAsync, GrabModeAsync);
    XGrabKey(dpy, XKeysymToKeycode(dpy, XK_q), Mod1Mask | ShiftMask, root, True, GrabModeAsync, GrabModeAsync);
    XGrabKey(dpy, XKeysymToKeycode(dpy, XK_Return), Mod1Mask | ShiftMask, root, True, GrabModeAsync, GrabModeAsync);
}

void run() {
    XEvent ev;
    while (1) {
        XNextEvent(dpy, &ev);
        switch (ev.type) {
            case MapRequest:      handle_map_request(&ev); break;
            case UnmapNotify:     handle_unmap_notify(&ev); break;
            case DestroyNotify:   handle_destroy_notify(&ev); break;
            case FocusIn:         handle_focus_in(&ev); break;
            case KeyPress:        handle_key_press(&ev); break;
            default:              break;
        }
    }
}

void cleanup() { if (dpy) XCloseDisplay(dpy); }

void sigchld(int unused) { (void)unused; while (waitpid(-1, NULL, WNOHANG) > 0) {} }

int main(int argc, char **argv) {
    if (argc != 1) {
        fprintf(stderr, "Usage: mwc_tiling\n");
        return 1;
    }
    signal(SIGCHLD, sigchld);
    setup();

    /* manage existing windows */
    Window ret_root, ret_parent, *children;
    unsigned int nchildren;
    if (XQueryTree(dpy, root, &ret_root, &ret_parent, &children, &nchildren)) {
        for (unsigned int i = 0; i < nchildren; ++i) {
            XWindowAttributes wa;
            if (!XGetWindowAttributes(dpy, children[i], &wa)) continue;
            if (wa.map_state == IsViewable && !find_client(children[i])) {
                add_client(children[i]);
                XSelectInput(dpy, children[i], StructureNotifyMask | FocusChangeMask | PropertyChangeMask);
            }
        }
        if (children) XFree(children);
        tile_windows();
    }

    run();
    cleanup();
    return 0;
}

