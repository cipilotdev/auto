// mwc_fixed.c -- minimal tiling-ish window manager (fixed)
// Build: gcc -Wall -O2 -o mwc_fixed mwc_fixed.c -lX11

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

Client *clients = NULL;
Window focused = 0;

/* --- simple helpers --- */
void die(const char *s) {
    fprintf(stderr, "%s\n", s);
    exit(1);
}

/* find client by window */
Client *find_client(Window w) {
    Client *c = clients;
    while (c) {
        if (c->win == w) return c;
        c = c->next;
    }
    return NULL;
}

/* add client (prepend) */
Client *add_client(Window w) {
    Client *c = calloc(1, sizeof(Client));
    if (!c) die("malloc failed");
    c->win = w;
    /* default geometry will be set by tiling */
    c->x = c->y = 0;
    c->w = 200; c->h = 200;
    c->next = clients;
    clients = c;
    return c;
}

/* remove client by window */
void remove_client(Window w) {
    Client **pc = &clients;
    while (*pc) {
        if ((*pc)->win == w) {
            Client *to_free = *pc;
            *pc = (*pc)->next;
            free(to_free);
            return;
        }
        pc = &(*pc)->next;
    }
}

/* set focus and border color */
void focus_window(Window w) {
    if (focused == w) return;
    if (focused) {
        XSetWindowBorder(dpy, focused, 0x000000); // unfocused black
    }
    focused = w;
    if (focused) {
        XSetWindowBorder(dpy, focused, 0xff0000); // focused red
        XSetInputFocus(dpy, focused, RevertToPointerRoot, CurrentTime);
    } else {
        XSetInputFocus(dpy, PointerRoot, RevertToPointerRoot, CurrentTime);
    }
}

/* configure (move/resize/map) client window */
void configure_window(Client *c) {
    if (!c) return;
    XMoveResizeWindow(dpy, c->win, c->x, c->y, c->w, c->h);
    /* ensure border width and map */
    XSetWindowBorderWidth(dpy, c->win, 2);
    XMapWindow(dpy, c->win);
}

/* simple tiling layout: grid */
void tile_windows() {
    int count = 0;
    Client *c = clients;
    while (c) { count++; c = c->next; }
    if (count == 0) return;

    int cols = 1;
    while (cols * cols < count) cols++;
    int rows = (count + cols - 1) / cols;

    int screen_w = DisplayWidth(dpy, screen);
    int screen_h = DisplayHeight(dpy, screen);

    int win_w = screen_w / cols;
    int win_h = screen_h / rows;

    c = clients;
    for (int r = 0; r < rows; r++) {
        for (int col = 0; col < cols; col++) {
            if (!c) return;
            c->x = col * win_w;
            c->y = r * win_h;
            c->w = win_w;
            c->h = win_h;
            configure_window(c);
            c = c->next;
        }
    }
}

/* --- event handlers --- */
void handle_map_request(XEvent *e) {
    XMapRequestEvent *ev = &e->xmaprequest;
    
    /* Check for override_redirect windows */
    XWindowAttributes wa;
    if (XGetWindowAttributes(dpy, ev->window, &wa)) {
        if (wa.override_redirect) {
            XMapWindow(dpy, ev->window);
            return;
        }
    }

    Client *c = find_client(ev->window);
    if (!c) c = add_client(ev->window);

    /* listen to unmap/destroy/focus events from the client */
    XSelectInput(dpy, ev->window, 
        StructureNotifyMask | FocusChangeMask | PropertyChangeMask);

    XMapWindow(dpy, ev->window); /* allow mapping */
    tile_windows();
    focus_window(ev->window);
}

void handle_unmap_notify(XEvent *e) {
    XUnmapEvent *ev = &e->xunmap;
    if (ev->send_event) return; /* ignore synthetic events */
    
    remove_client(ev->window);
    tile_windows();
    if (focused == ev->window) {
        focus_window(clients ? clients->win : (Window)0);
    }
}

void handle_destroy_notify(XEvent *e) {
    XDestroyWindowEvent *ev = &e->xdestroywindow;
    remove_client(ev->window);
    tile_windows();
    if (focused == ev->window) {
        focus_window(clients ? clients->win : (Window)0);
    }
}

void handle_focus_in(XEvent *e) {
    XFocusChangeEvent *ev = &e->xfocus;
    /* only focus if it's one of our clients */
    if (ev->window != root && find_client(ev->window))
        focus_window(ev->window);
}

void handle_key_press(XEvent *e) {
    XKeyEvent *ev = &e->xkey;
    KeySym ks = XKeycodeToKeysym(dpy, ev->keycode, 0);

    /* Alt+Tab -> focus next client */
    if ((ev->state & Mod1Mask) && ks == XK_Tab) {
        if (clients) {
            Client *c = clients;
            while (c->next && c->win != focused) c = c->next;
            focus_window(c->next ? c->next->win : clients->win);
        }
    }

    /* Alt+Shift+Tab -> focus previous client */
    if ((ev->state & (Mod1Mask|ShiftMask)) && ks == XK_Tab) {
        if (clients) {
            Client *c = clients;
            Client *prev = NULL;
            while (c && c->win != focused) {
                prev = c;
                c = c->next;
            }
            if (!c) focus_window(clients->win);
            else if (!prev) {
                Client *last = clients;
                while (last->next) last = last->next;
                focus_window(last->win);
            } else {
                focus_window(prev->win);
            }
        }
    }

    /* Alt+Return -> spawn xterm */
    if ((ev->state & Mod1Mask) && ks == XK_Return) {
        if (fork() == 0) {
            if (dpy) close(ConnectionNumber(dpy));
            setsid();
            execlp("xterm", "xterm", NULL);
            _exit(0);
        }
    }

    /* Alt+Q -> close focused window */
    if ((ev->state & Mod1Mask) && ks == XK_q) {
        if (focused) {
            XEvent ev;
            ev.xclient.type = ClientMessage;
            ev.xclient.window = focused;
            ev.xclient.message_type = XInternAtom(dpy, "WM_PROTOCOLS", True);
            ev.xclient.format = 32;
            ev.xclient.data.l[0] = XInternAtom(dpy, "WM_DELETE_WINDOW", True);
            ev.xclient.data.l[1] = CurrentTime;
            XSendEvent(dpy, focused, False, NoEventMask, &ev);
        }
    }
}

/* --- setup / run / cleanup --- */
void setup() {
    dpy = XOpenDisplay(NULL);
    if (!dpy) die("Failed to open display");
    screen = DefaultScreen(dpy);
    root = RootWindow(dpy, screen);

    /* claim window manager duties */
    XSelectInput(dpy, root, 
        SubstructureRedirectMask | SubstructureNotifyMask | KeyPressMask);

    /* grab keys */
    XGrabKey(dpy, XKeysymToKeycode(dpy, XK_Tab), Mod1Mask, 
             root, True, GrabModeAsync, GrabModeAsync);
    XGrabKey(dpy, XKeysymToKeycode(dpy, XK_Tab), Mod1Mask|ShiftMask, 
             root, True, GrabModeAsync, GrabModeAsync);
    XGrabKey(dpy, XKeysymToKeycode(dpy, XK_Return), Mod1Mask, 
             root, True, GrabModeAsync, GrabModeAsync);
    XGrabKey(dpy, XKeysymToKeycode(dpy, XK_q), Mod1Mask, 
             root, True, GrabModeAsync, GrabModeAsync);
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

void cleanup() {
    if (dpy) XCloseDisplay(dpy);
}

/* reap children */
void sigchld(int unused) {
    (void)unused;
    while (waitpid(-1, NULL, WNOHANG) > 0) {}
}

int main(int argc, char **argv) {
    if (argc != 1) {
        fprintf(stderr, "Usage: mwc_fixed\n");
        return 1;
    }

    signal(SIGCHLD, sigchld);
    setup();

    /* manage existing viewable windows */
    Window ret_root, ret_parent, *children;
    unsigned int nchildren;
    if (XQueryTree(dpy, root, &ret_root, &ret_parent, &children, &nchildren)) {
        for (unsigned int i = 0; i < nchildren; ++i) {
            XWindowAttributes wa;
            if (!XGetWindowAttributes(dpy, children[i], &wa)) continue;
            
            /* Skip override_redirect windows */
            if (wa.override_redirect) continue;
            
            if (wa.map_state == IsViewable && !find_client(children[i])) {
                add_client(children[i]);
                XSelectInput(dpy, children[i], 
                    StructureNotifyMask | FocusChangeMask | PropertyChangeMask);
            }
        }
        if (children) XFree(children);
        tile_windows();
    }

    run();
    cleanup();
    return 0;
}
