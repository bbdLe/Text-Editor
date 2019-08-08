#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <termios.h>
#include <unistd.h>
#include <errno.h>

#define CTRL_KEY(k) ((k) & 0x1f)

struct termios orig_termios;

void Die(const char* s)
{
    perror(s);
    exit(1);
}

void DisableRawModel()
{
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios) == -1)
    {
        Die("tcsetattr");
    }
}

void EnableRawModel()
{
    if (tcgetattr(STDIN_FILENO, &orig_termios) == -1)
    {
        Die("tcsgetattr");
    }
    atexit(DisableRawModel);
    struct termios raw = orig_termios;
    raw.c_iflag &= ~(BRKINT | INPCK | ISTRIP | IXON | ICRNL);
    raw.c_oflag &= ~(OPOST);
    raw.c_cflag &= (CS8);
    raw.c_lflag &= ~(ECHO | ICANON | ISIG | IEXTEN);
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 1;
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1)
    {
        Die("tcsetattr");
    }
}

char EditorReadKey()
{
    int nread;
    char c;
    while ((nread = read(STDIN_FILENO, &c, 1)) != 1)
    {
        if (nread == -1 && errno != EAGAIN)
            Die("read");
    }

    return c;
}

void EditorProcessKey()
{
    char c = EditorReadKey();

    switch (c)
    {
        case CTRL_KEY('q'):
            exit(0);
            break;
    }
}

int main()
{
    EnableRawModel();
    while (1)
    {
        EditorProcessKey();
    }
    return 0;
}
