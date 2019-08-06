#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <termios.h>
#include <unistd.h>

struct termios orig_termios;

void DisableRawModel()
{
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios);
}

void EnableRawModel()
{
    tcgetattr(STDIN_FILENO, &orig_termios);
    atexit(DisableRawModel);
    struct termios raw = orig_termios;
    raw.c_lflag &= ~(ECHO | ICANON);
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
}

int main()
{
    EnableRawModel();
    char c;
    while (read(STDIN_FILENO, &c, 1) && c != 'q')
    {
        if (iscntrl(c))
        {
            printf("%d\n", c);
        }
        else
        {
            printf("%d ('%c')\n", c, c);
        }
    }
    return 0;
}
