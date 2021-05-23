#include <stdio.h>

#if defined __WIN32__ || defined __MSDOS__ || defined __WATCOMC__
  #include <conio.h>
  #if defined __WIN32__ || defined __WATCOMC__
    #if !defined __WIN32__
      #define __WIN32__ 1
    #endif
    #include <windows.h>
    #if !defined amx_Init && !defined NO_WIN32_CONSOLE && !defined AMX_TERMINAL
      #define WIN32_CONSOLE
    #endif
  #endif
#elif !defined macintosh
  #include "../linux/getch.h"
  #include <fcntl.h>
  #include <termios.h>
  #include <unistd.h>
#endif

#if (defined AMX_TERMINAL || defined amx_Init) && !defined DBG_DUALTERM
  /* required functions are implemented elsewhere */
  int amx_printf(char *,...);
  int amx_putchar(int);
  int amx_fflush(void);
  int amx_getch(void);
  char *amx_gets(char *,int);
  int amx_termctl(int,int);
  void amx_clrscr(void);
  void amx_clreol(void);
  void amx_gotoxy(int x,int y);
  void amx_wherexy(int *x,int *y);
  unsigned int amx_setattr(int foregr,int backgr,int highlight);
  void amx_console(int columns, int lines, int flags);
  void amx_viewsize(int *width,int *height);
  #if defined amx_Init
    #define STR_PROMPT  "dbg\xbb "
    #define CHR_HLINE   '\x97'
  #else
    #define STR_PROMPT  "dbg> "
    #define CHR_HLINE   '-'
  #endif
  #define CHR_VLINE     '|'
#elif defined USE_CURSES || defined HAVE_CURSES_H
  /* Use the "curses" library to implement the console */
  #include <curses.h>
  extern const int _False;     /* to avoid compiler warnings */
  #define amx_printf        printw
  #define amx_putchar(c)    addch(c)
  #define amx_fflush()      (0)
  #define amx_getch()       getch()
  #define amx_gets(s,n)     getnstr(s,n)
  #define amx_clrscr()      (void)(0)
  #define amx_clreol()      (void)(0)
  #define amx_gotoxy(x,y)   (void)(0)
  #define amx_wherexy(x,y)  (*(x)=*(y)=0)
  #define amx_setattr(c,b,h) (_False)
  #define amx_termctl(c,v)  (_False)
  #define amx_console(c,l,f) (void)(0)
  #define STR_PROMPT        "dbg> "
  #define CHR_HLINE         '-'
  #define CHR_VLINE         '|'
#elif defined VT100 || defined __LINUX__ || defined ANSITERM
  /* ANSI/VT100 terminal, or shell emulating "xterm" */
  #if !defined VT100 && !defined ANSITERM && defined __LINUX__
    #define VT100
  #endif
  #define amx_printf      printf
  #define amx_putchar(c)  putchar(c)
  #define amx_fflush()    fflush(stdout)
  #define amx_getch()     getch()
  #define amx_gets(s,n)   fgets(s,n,stdin)
  int amx_termctl(int,int);
  void amx_clrscr(void);
  void amx_clreol(void);
  void amx_gotoxy(int x,int y);
  void amx_wherexy(int *x,int *y);
  unsigned int amx_setattr(int foregr,int backgr,int highlight);
  void amx_console(int columns, int lines, int flags);
  void amx_viewsize(int *width,int *height);
  #define STR_PROMPT    "dbg> "
  #define CHR_HLINE     '-'
  #define CHR_VLINE     '|'
#elif defined WIN32_CONSOLE
  /* Win32 console */
  #define amx_printf      printf
  #define amx_putchar(c)  putchar(c)
  #define amx_fflush()    fflush(stdout)
  int amx_termctl(int,int);
  void amx_clrscr(void);
  void amx_clreol(void);
  void amx_gotoxy(int x,int y);
  void amx_wherexy(int *x,int *y);
  unsigned int amx_setattr(int foregr,int backgr,int highlight);
  void amx_console(int columns, int lines, int flags);
  void amx_viewsize(int *width,int *height);
  #define STR_PROMPT    "dbg> "
  #define CHR_HLINE     '\xc4'
  #define CHR_VLINE     '\xb3'
#else
  /* assume a streaming terminal; limited features (no colour, no cursor
   * control)
   */
  #define amx_printf        printf
  #define amx_putchar(c)    putchar(c)
  #define amx_fflush()      fflush(stdout)
  #define amx_getch()       getch()
  #define amx_gets(s,n)     fgets(s,n,stdin)
  #define amx_clrscr()      (void)(0)
  #define amx_clreol()      (void)(0)
  #define amx_gotoxy(x,y)   ((void)(x),(void)(y),(void)(0))
  #define amx_wherexy(x,y)  (*(x)=*(y)=0)
  #define amx_setattr(c,b,h) ((void)(c),(void)(b),(void)(h),(0))
  #define amx_termctl(c,v)  ((void)(c),(void)(v),(0))
  #define amx_console(c,l,f) ((void)(c),(void)(l),(void)(f),(void)(0))
  #define amx_viewsize      (*(x)=80,*(y)=25)
  #define STR_PROMPT        "dbg> "
  #define CHR_HLINE         '-'
  #define CHR_VLINE         '|'
#endif
#define CHR_CURLINE         '*'
#if defined VT100
  #define CHR_HLINE_VT100   'q' // in alternate font
  #define CHR_VLINE_VT100   'x'
#endif
