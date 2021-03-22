#include "dbgterm.h"

#if defined WIN32_CONSOLE
  /* Win32 console */
  static int localecho=1;

  int amx_getch(void)  /* this implementation works with redirected input */
  {
    char ch;
    DWORD count,mode;
    HANDLE hConsole=GetStdHandle(STD_INPUT_HANDLE);
    GetConsoleMode(hConsole,&mode);
    SetConsoleMode(hConsole,mode & ~(ENABLE_LINE_INPUT|ENABLE_ECHO_INPUT));
    while (ReadFile(hConsole,&ch,1,&count,NULL) && count==0)
      /* nothing */;
    SetConsoleMode(hConsole,mode);
    if (count>0)
      return ch;
    return EOF;
  }

  void amx_gets(char *s,int n) /* this implementation works with redirected input */
  {
    if (n>0) {
      int c,chars=0;
      c=amx_getch();
      while (c!=EOF && c!='\r' && chars<n-1) {
        if (c=='\b') {
          if (chars>0) {
            chars--;
            if (localecho) {
              amx_putchar((char)c);
              amx_putchar(' ');
            }
          }
        } else {
          s[chars++]=(char)c;
        }
        if (localecho) {
          amx_putchar((char)c);
          amx_fflush();
        }
        if (chars<n-1)
          c=amx_getch();
      } /* while */
      if (c=='\r' && localecho) {
        amx_putchar('\n');
        amx_fflush();
      } /* if */
      assert(chars<n);
      s[chars]='\0';
    }
  }
#endif // defined WIN32_CONSOLE
