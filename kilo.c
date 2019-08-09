#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <termios.h>
#include <unistd.h>
#include <errno.h>

#define CTRL_KEY(k) ((k) & 0x1f)

struct EditorConfig
{
    struct termios orig_termios;
};

struct EditorConfig E;

void Die(const char* s)
{
    write(STDOUT_FILENO, "\x1b[2J", 4);
    write(STDOUT_FILENO, "\x1b[H", 3);

    perror(s);
    exit(1);
}

void DisableRawModel()
{
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &E.orig_termios) == -1)
    {
        Die("tcsetattr");
    }
}

void EnableRawModel()
{
    if (tcgetattr(STDIN_FILENO, &E.orig_termios) == -1)
    {
        Die("tcsgetattr");
    }
    atexit(DisableRawModel);
    struct termios raw = E.orig_termios;
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
            write(STDOUT_FILENO, "\x1b[2J", 4);
            write(STDOUT_FILENO, "\x1b[H", 3);
            exit(0);
            break;
    }
}

void EditorDrawRows()
{
    int y;

    for (y = 0; y < 24; ++y)
    {
        write(STDOUT_FILENO, "~\r\n", 3);
    }
}

void EditorRefreshScreen()
{
    write(STDOUT_FILENO, "\x1b[2J", 4);
    write(STDOUT_FILENO, "\x1b[H", 3);

    EditorDrawRows();
    write(STDOUT_FILENO, "\x1b[H", 3);
}

int main()
{
    EnableRawModel();
    while (1)
    {
        EditorRefreshScreen();
        EditorProcessKey();
    }
    return 0;
}
